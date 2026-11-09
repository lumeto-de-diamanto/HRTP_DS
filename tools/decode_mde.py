"""Bounded ZIGOKU.MDE SSG decoder, based on static MDRV98.COM evidence.

This is a transcription of the shipped SSG path, not upstream's guessed
portamento endpoint export. No original DOS code runs here. See docs/native-sfx.md.
"""
from dataclasses import dataclass, field
import struct

PERIODS = (3816, 3602, 3400, 3209, 3029, 2859, 2698, 2547, 2404, 2269, 2142, 2022)
MAX_TICKS = 4096


@dataclass
class Envelope:
    definition: tuple = (255, 255, 255, 255, 1, 1)
    level: int = 0
    stage: int = 0
    active: bool = False

    def keyon(self):
        self.level = self.definition[0]
        self.stage = 0
        self.active = True

    def keyoff(self):
        # MDRV's zero release step holds the level, rather than synthesizing
        # a release speed or replacing it with an immediate zero.
        self.stage = 3

    def step(self):
        if not self.active:
            return
        d = self.definition
        delta, target = ((d[1], 255), (d[2], d[3]), (d[4], 0), (d[5], 0))[self.stage]
        value = self.level + delta if self.stage == 0 else self.level - delta
        reached = value >= target if self.stage == 0 else value <= target
        if reached:
            self.level = target
            if not target or self.stage >= 2:
                self.level = 0
                self.active = False
            else:
                self.stage += 1
        else:
            self.level = value


@dataclass
class Stream:
    position: int
    channel: int
    volume: int = 15  # explicit standalone policy for inherited-volume ID 6
    q: int = 8
    detune: int = 0
    transpose: int = 0
    period: int = 3816
    octave: int = 0
    slide: int = 0
    gate: int = 0
    wait: int = 1
    ended: bool = False
    mixer: int = 0xc0
    env: Envelope = field(default_factory=Envelope)
    loops: list = field(default_factory=list)
    commands: list = field(default_factory=list)


class Bank:
    def __init__(self, data):
        self.data = bytes(data)
        if len(data) < 20:
            raise ValueError('truncated MDE header')
        kind, self.count, self.envelopes_offset = struct.unpack_from('<3H', data)
        if kind != 1 or not 1 <= self.count <= 256 or any(data[6:16]):
            raise ValueError('unsupported MDE header')
        end_table = 16 + 4 * self.count
        if not end_table <= self.envelopes_offset < len(data) or (len(data) - self.envelopes_offset) % 6:
            raise ValueError('invalid MDE table/envelope ranges')
        self.envelopes = [tuple(data[p:p + 6]) for p in range(self.envelopes_offset, len(data), 6)]
        self.entries = []
        for i in range(self.count):
            description, table = struct.unpack_from('<HH', data, 16 + 4 * i)
            channels, kind = description & 255, description >> 8
            if kind != 0x40 or not 1 <= channels <= 3 or not end_table <= table <= self.envelopes_offset - 2 * channels:
                raise ValueError(f'invalid MDE entry {i + 1}')
            pointers = struct.unpack_from('<' + 'H' * channels, data, table)
            if any(not table + channels * 2 <= p < self.envelopes_offset for p in pointers):
                raise ValueError('MDE stream pointer outside command data')
            self.entries.append(pointers)

    def render_events(self, number):
        if not 1 <= number <= self.count:
            raise ValueError('invalid SFX ID')
        # Entry order is C, B, A. IRQ execution order is A, B, C.
        streams = sorted([Stream(p, 2 - i) for i, p in enumerate(self.entries[number - 1])], key=lambda s: s.channel)
        events, trace, mixer = [], [], 0x3f

        def write(tick, reg, value):
            events.append((tick, reg, value & 255))

        def amplitude(s, tick):
            write(tick, 8 + s.channel, s.env.level * (s.volume + 1) // 256)

        def frequency(s, tick):
            shifted = (s.period & 65535) >> s.octave
            write(tick, 2 * s.channel + 1, shifted >> 8)
            write(tick, 2 * s.channel, shifted)

        def take(s, count):
            if s.position + count > self.envelopes_offset:
                raise ValueError('MDE command crosses envelope table')
            value = self.data[s.position:s.position + count]
            s.position += count
            return value

        for tick in range(MAX_TICKS):
            for s in streams:
                if s.ended:
                    continue
                if s.slide and s.gate:
                    s.period = (s.period + s.slide) & 65535
                    frequency(s, tick)
                s.env.step()
                amplitude(s, tick)
                if s.gate:
                    s.gate -= 1
                    if not s.gate:
                        s.env.keyoff()
                    continue
                s.wait -= 1
                if s.wait:
                    continue
                for _ in range(1024):
                    offset = s.position
                    command = take(s, 1)[0]
                    s.commands.append((tick, offset, command))
                    if command < 0x80 or command == 0xf2:
                        if command == 0xf2:
                            note, duration, lo, hi = take(s, 4)
                            slope = struct.unpack('<h', bytes([lo, hi]))[0]
                            gate = duration
                        else:
                            note, duration, slope = command, take(s, 1)[0], 0
                            gate = duration * s.q // 8 if s.q else 0
                        pitch = (note & 15) + s.transpose
                        octave, pitch = divmod((note >> 4) * 12 + pitch, 12)
                        if not 0 <= octave <= 15 or not duration or (note & 15) >= 12:
                            raise ValueError('invalid MDE pitch/duration')
                        s.period = (PERIODS[pitch] - s.detune) & 65535
                        s.octave, s.slide = octave, slope
                        s.gate, s.wait = gate, duration - gate + 1
                        s.env.keyon()
                        frequency(s, tick)
                        s.env.step()
                        amplitude(s, tick)
                        trace.append(dict(tick=tick, channel=s.channel, offset=offset,
                            note=note, duration=duration, gate=gate, slope=slope,
                            base_period=s.period, octave=octave, volume=s.volume))
                        if s.gate:
                            s.gate -= 1
                            if not s.gate:
                                s.env.keyoff()
                        else:
                            s.wait -= 1
                        break
                    if command == 0x90:
                        duration = take(s, 1)[0]
                        if not duration:
                            raise ValueError('zero MDE rest')
                        s.slide, s.wait = 0, duration
                        s.env.step()
                        amplitude(s, tick)
                        break
                    if command == 0xea:
                        s.q = take(s, 1)[0]
                        if s.q > 8:
                            raise ValueError('unsupported MDE articulation')
                    elif command == 0xed:
                        if any(take(s, 4)):
                            raise ValueError('nonzero MDE pitch LFO requires explicit implementation')
                    elif command in (0xe6, 0xe7):
                        value = struct.unpack('<b', take(s, 1))[0]
                        if command == 0xe6:
                            s.detune = value
                        else:
                            s.transpose = value
                    elif command == 0xec:
                        s.volume = take(s, 1)[0]
                        if s.volume > 15:
                            raise ValueError('invalid MDE SSG volume')
                    elif command == 0xeb:
                        value = take(s, 1)[0]
                        index = value & 63
                        if index >= len(self.envelopes):
                            raise ValueError('invalid MDE envelope')
                        s.env.definition = self.envelopes[index]
                        s.mixer = value & 0xc0
                        mask = (1 << s.channel) | (8 << s.channel)
                        mixer = (mixer & ~mask) | ((bool(value & 64) << s.channel) | (bool(value & 128) << (s.channel + 3)))
                        write(tick, 7, mixer)
                    elif command == 0xef:
                        write(tick, 6, take(s, 1)[0])  # chip masks the raw byte to five bits
                    elif command == 0xe4:
                        count = take(s, 1)[0]
                        if not count:
                            raise ValueError('infinite MDE loop is not a one-shot')
                        s.loops.append([count, s.position])
                    elif command == 0xe5:
                        if not s.loops:
                            raise ValueError('unmatched MDE loop end')
                        s.loops[-1][0] -= 1
                        if s.loops[-1][0]:
                            s.position = s.loops[-1][1]
                        else:
                            s.loops.pop()
                    elif command == 0xff:
                        if s.loops:
                            raise ValueError('unclosed MDE loop')
                        s.ended = True
                        write(tick, 8 + s.channel, 0)
                        break
                    else:
                        raise ValueError(f'unsupported MDE opcode {command:#x} at {offset:#x}')
                else:
                    raise ValueError('MDE zero-time command budget exceeded')
            if all(s.ended for s in streams):
                return dict(id=number, ticks=tick + 1, streams=len(streams), events=events,
                    notes=trace, commands=[c for s in streams for c in s.commands])
        raise ValueError('MDE one-shot clock budget exceeded')
