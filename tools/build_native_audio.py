#!/usr/bin/env python3
"""Build native MDT sequence/PCM assets for any soundtrack entry.

Timing/control and instrument usage come directly from decoded MDT events.
Host-rendered OPNA zones and user-provided rhythm WAVs produce native PCM
directly, with no intermediate tracker modules or soundbank converter.
"""
import argparse
from collections import Counter
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys

from audio_mix import load_mix_profile

NOTE, VOLUME, LFO, PAN, PATCH, MIXER, NOISE, DETUNE, OFF = range(1, 10)
TEMPO, PITCH, RHYTHM_LEVEL, REL_VOLUME = 10, 11, 12, 13
NO_LOOP = 0xffffffff
NO_GATE = 0xffff
DRUM_KEYS = (36, 38, 46, 42, 48, 37)
PERIODS = (3816, 3602, 3400, 3209, 3029, 2859, 2698, 2547, 2404, 2269, 2142, 2022)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def clocks(length):
    if length.startswith("%"):
        value = Fraction(int(length[1:]))
    else:
        dotted = length.endswith(".")
        value = Fraction(192, int(length.rstrip("."))) * (Fraction(3, 2) if dotted else 1)
    if value.denominator != 1 or value <= 0:
        raise ValueError(f"non-native note length: {length}")
    return int(value)


def lfo(event):
    command = event[0]
    if command == "S":
        kind, p = 1, event[1:]
    elif command == "SP":
        kind, p = 2, [event[1], event[2], event[4], event[5]]
    elif command == "SA" and event[2] == 0:
        kind, p = 3, [event[1], event[3], event[4], event[5]]
    elif command == "SA" and event[3] == 0:
        kind, p = 4, [event[1], event[2], event[4], event[5]]
    else:
        raise ValueError(f"unsupported native LFO: {event}")
    if len(p) != 4 or any(not 0 <= v <= 255 for v in p):
        raise ValueError(f"invalid native LFO bytes: {event}")
    return kind, sum(v << (8 * i) for i, v in enumerate(p))


def fm_fnum(key):
    value = 144 * (440 * 2 ** ((key - 69) / 12)) * 2 ** 20 / 7987200 / 8
    block = 4
    while value < 512 and block > 0:
        block -= 1
        value *= 2
    while value > 2047 and block < 7:
        block += 1
        value /= 2
    return max(1, min(2047, math.floor(value + 0.5)))


def expand_macros(events, macros, active=()):
    """Inline decoded calls with shared driver state; reject recursion/loop escapes."""
    result = []
    for event in events:
        if event[0] != "U":
            result.append(event)
            continue
        number = event[1]
        if number not in macros or number in active or len(active) >= 16:
            raise ValueError(f"missing/recursive native macro {number}")
        body = expand_macros(macros[number].events, macros, (*active, number))
        depth = 0
        for e in body:
            depth += (e[0] in ("[", "[:", "|:")) - (e[0] in ("]", ":]", ":|"))
            if depth < 0 or e[0] == "\\":
                raise ValueError("macro cannot escape its structural loop/call")
        if depth:
            raise ValueError("unclosed macro loop")
        result.extend(body)
        if len(result) > 1000000:
            raise ValueError("native macro expansion budget exceeded")
    return result


def compile_channel(channel, *, allow_portamento=False, diagnostics=None, original_rhythm=False, macros=None):
    """Expand only structural loops; retain native controls and integer ticks.

    Unknown commands and invalid macros fail. Portamento is opt-in, explicitly diagnosed
    chip-domain linear interpolation, not a claim of original-driver slopes.
    """
    source = channel.id
    if source not in (*range(0x80, 0x86), *range(0x40, 0x43), 0x10):
        raise ValueError(f"unsupported native channel {source:#x}")
    source_events = expand_macros(channel.events, macros or {})
    rhythm, ssg = source == 0x10, bool(source & 0x40)
    slots = list(range(9, 15)) if rhythm else [6 + (source & 15) if ssg else source & 15]
    streams = {slot: [] for slot in slots}
    state = dict(octave=0, transpose=0, volume=127 if not ssg else 15,
                 patch=0, mixer=1, noise=0, detune=0, pan=3, q=8,
                 rhythm_mask=63, master=63, levels=[31] * 6)
    tick, marker, marker_indices, pending, tied, stack, i = 0, None, {}, {}, False, [], 0

    def emit(op, a=0, b=0, c=0, targets=None):
        for slot in slots if targets is None else targets:
            streams[slot].append(dict(tick=tick, op=op, slot=slot, a=a, b=b, c=c))

    def rhythm_volume():
        for drum, slot in enumerate(slots):
            if original_rhythm:
                emit(RHYTHM_LEVEL, state["master"], state["levels"][drum], targets=[slot])
            else:
                emit(VOLUME, state["master"] * state["levels"][drum], targets=[slot])

    while i < len(source_events):
        e = source_events[i]
        command = e[0]
        glide = re.fullmatch(r"\((\d)([a-g][+-]?),([0-9])([a-g][+-]?)\)(%\d+|\d+\.?)", command)
        if glide and not allow_portamento:
            raise ValueError("portamento requires explicit --approximate-portamento")
        parsed_command = glide[2] + glide[5] if glide else command
        match = re.fullmatch(r"([<>]?)([a-g])([+-]?)(%\d+|\d+\.?)", parsed_command)
        if match:
            shift, name, accidental, length = match.groups()
            state["octave"] += (shift == ">") - (shift == "<")
            key = (state["octave"] + 1) * 12 + dict(c=0, d=2, e=4, f=5, g=7, a=9, b=11)[name]
            key += (accidental == "+") - (accidental == "-") + state["transpose"]
            target = None
            if glide:
                def explicit_key(octave, name):
                    return (int(octave) + 1) * 12 + dict(c=0, d=2, e=4, f=5, g=7, a=9, b=11)[name[0]] + ("+" in name) - ("-" in name) + state["transpose"]
                key = explicit_key(glide[1], glide[2])
                target = explicit_key(glide[3], glide[4])
                if tied:
                    raise ValueError("portamento tied from a previous note is not implemented")
                if diagnostics is not None:
                    diagnostics.append(dict(source=source, slot=slots[0], tick=tick,
                        start=key, end=target, command=command,
                        limitation="approximate chip-domain linear portamento; original slope/rounding unverified"))
            duration = clocks(length)
            # Gate counters are integer source clocks, not fractional MIDI
            # durations. Retain Q0's disabled key-off; other gates truncate.
            gate = duration if glide else max(1, duration * state["q"] // 8) if state["q"] else NO_GATE
            if rhythm and original_rhythm:
                gate = NO_GATE  # rhythm triggers decay naturally, not as pitched gated notes
            if tied:
                if not pending:
                    raise ValueError("tie without prior note")
                for note in pending.values():
                    if note["gate"] != NO_GATE:
                        note["gate"] += gate
                tied = False
            else:
                pending = {}
                for slot in slots:
                    if rhythm and not state["rhythm_mask"] & (1 << (slot - 9)):
                        continue
                    note = dict(tick=tick, op=NOTE, slot=slot, key=DRUM_KEYS[slot - 9] if rhythm else key,
                                gate=gate, patch=state["patch"], mixer=state["mixer"],
                                noise=state["noise"], detune=state["detune"])
                    if not 0 <= note["key"] < 120:
                        raise ValueError(f"native note outside range: {note}")
                    streams[slot].append(note)
                    pending[slot] = note
                    if glide:
                        if rhythm or not 0 <= target < 120:
                            raise ValueError("invalid portamento endpoint")
                        note.update(glide_target=target, glide_ticks=duration)
            tick += duration
        elif command.startswith("r"):
            tick += clocks(command[1:])
            tied = False
        elif command == "&":
            tied = True
        elif command == "O":
            state["octave"] = e[1] + int(ssg)
        elif command in ("[", "[:", "|:"):
            if e[1] < 1:
                raise ValueError("invalid loop count")
            stack.append([e[1] - 1, i + 1, -1, state["octave"]])
        elif command in (":", "|"):
            if not stack:
                raise ValueError("loop skip without loop")
            if stack[-1][0] == 0 and stack[-1][2] >= 0:
                i = stack[-1][2]
                continue
        elif command in ("]", ":]", ":|"):
            if not stack:
                raise ValueError("loop end without loop")
            if stack[-1][0]:
                stack[-1][0] -= 1
                stack[-1][2] = i
                state["octave"] = stack[-1][3]
                i = stack[-1][1]
                continue
            stack.pop()
        elif command == "\\":
            if marker is not None or stack:
                raise ValueError("ambiguous infinite-loop marker")
            marker = tick
            marker_indices = {slot: len(events) for slot, events in streams.items()}
        elif command == "Q":
            if not 0 <= e[1] <= 8:
                raise ValueError(f"unsupported gate: {e}")
            state["q"] = e[1]
        elif command == "@^":
            state["transpose"] = e[1]
        elif command == "^":
            state["detune"] = e[1]
            if not -32768 <= e[1] <= 32767:
                raise ValueError("detune outside signed 16-bit range")
            emit(DETUNE, e[1] & 0xffff)
        elif command == "@":
            if rhythm:
                state["rhythm_mask"] = e[1]
            else:
                state["patch"] = e[1]
                emit(PATCH, e[1])
        elif command in ("@V", "V"):
            if rhythm:
                if command == "V":
                    state["master"], state["levels"] = e[1], list(e[2:])
                    if len(state["levels"]) != 6:
                        raise ValueError("invalid rhythm levels")
                else:
                    state["levels"][e[1]] = e[2]
                rhythm_volume()
            else:
                state["volume"] = e[1] if ssg or command == "@V" else round(e[1] * 127 / 15)
                emit(VOLUME, state["volume"])
        elif command in ("@V+", "@V-"):
            if rhythm:
                raise ValueError("relative rhythm volume requires documented instrument semantics")
            state["volume"] = max(0, min(15 if ssg else 127,
                state["volume"] + (e[1] if command == "@V+" else -e[1])))
            emit(REL_VOLUME, (e[1] if command == "@V+" else -e[1]) & 0xffff)
        elif command == "P":
            if rhythm:
                raise ValueError("per-drum pan not yet supported")
            state["pan"] = e[1]
            emit(PAN, e[1])
        elif command in ("S", "SP", "SA"):
            kind, params = lfo(e)
            if ssg and kind <= 2 and params:
                raise ValueError("SSG pitch LFO is not implemented in this first prototype")
            emit(LFO, kind, params)
        elif command == "N":
            state["mixer"] = e[1]
            emit(MIXER, e[1])
        elif command == "W" and ssg:
            state["noise"] = e[1]
            emit(NOISE, e[1])
        elif command == "t":
            if not 16 <= e[1] <= 127:
                raise ValueError("tempo must be 16..127")
            # Rhythm is split into six slots; a global command executes once.
            emit(TEMPO, e[1], targets=[slots[0]])
        elif command == "/":
            emit(OFF)
        else:
            raise ValueError(f"unsupported native command on {source:#x} at tick {tick}: {e}")
        i += 1
    if stack or tied:
        raise ValueError("unclosed loop or dangling tie")
    return [dict(source=source, slot=slot, events=events, loopTick=marker,
                 loopIndex=marker_indices.get(slot, NO_LOOP), endTick=tick)
            for slot, events in streams.items()]


def detune_ratio(key, detune, ssg):
    base = PERIODS[key % 12] if ssg else fm_fnum(key)
    changed = max(1, min(4095 if ssg else 2047, base - detune if ssg else base + detune))
    return base / changed if ssg else changed / base


def pack_song(song, samples, zones, rhythm, *, tempo=76, intro=960, period=4704,
              comparison=5664, allow_portamento=False, diagnostics=None, original_rhythm=False):
    tracks = []
    for channel in song.channels:
        tracks.extend(compile_channel(channel, allow_portamento=allow_portamento,
                                      diagnostics=diagnostics, original_rhythm=original_rhythm,
                                      macros={m.macro_id: m for m in song.macros.values()}))
    if len({t["slot"] for t in tracks}) != len(tracks):
        raise ValueError("duplicate native slot")
    by_slot = {t["slot"]: t for t in tracks}
    tracks = [by_slot.get(slot, dict(source=0x80 + slot, slot=slot, events=[],
                                    loopTick=None, loopIndex=NO_LOOP, endTick=0))
              for slot in range(15)]
    events, counts = [], Counter()
    for track in tracks:
        track["first"] = len(events)
        expanded = []
        new_loop_index = NO_LOOP
        for index, event in enumerate(track["events"]):
            if index == track["loopIndex"]:
                new_loop_index = len(expanded)
            e = dict(event)
            if e["op"] == NOTE:
                key, slot = e["key"], e["slot"]
                if slot >= 9:
                    sample = rhythm[key]
                else:
                    matches = [z for z in zones if z["kind"] == (slot >= 6)
                               and z["program"] == e["patch"] and z["low"] <= key <= z["high"]
                               and (slot < 6 or (z["mixer"] == e["mixer"] and z["noise"] == (e["noise"] if e["mixer"] & 2 else 0)))]
                    if len(matches) != 1:
                        raise ValueError(f"missing/ambiguous PCM zone: {e}")
                    sample = matches[0]["sample"]
                ratio = detune_ratio(key, e["detune"], slot >= 6) if slot < 9 else 1
                rate = samples[sample]["c5"] * 2 ** ((key - 60) / 12) * ratio
                ds_period = math.floor(16756991 / rate + 0.5)
                if not 1 <= ds_period <= 65535 or not 0 < e["gate"] <= NO_GATE:
                    raise ValueError(f"note cannot fit native event: {e}")
                base = max(1, min(2047, fm_fnum(key) + e["detune"])) if slot < 6 else 0
                e.update(a=key, b=sample | (ds_period << 16), c=e["gate"] | (base << 16))
                counts[slot] += 1
            expanded.append(e)
            if e["op"] == NOTE and "glide_target" in e:
                end = e["glide_target"]
                end_ratio = 2 ** ((end - key) / 12) * detune_ratio(end, e["detune"], slot >= 6) / ratio
                for elapsed in range(1, e["glide_ticks"] + 1):
                    progress = elapsed / e["glide_ticks"]
                    # FM frequency/FNUM interpolation; SSG period interpolation.
                    factor = 1 / (1 + (1 / end_ratio - 1) * progress) if slot >= 6 else 1 + (end_ratio - 1) * progress
                    changed = math.floor(16756991 / (rate * factor) + 0.5)
                    expanded.append(dict(tick=e["tick"] + elapsed, op=PITCH, slot=slot, a=changed, b=0, c=0))
        track["events"] = sorted(expanded, key=lambda e: e["tick"])
        # No supported command inserts events across the infinite-loop marker.
        track["loopIndex"] = new_loop_index
        events.extend(track["events"])
    header_size, sample_offset = 64, 64
    track_offset = sample_offset + 64 * len(samples)
    event_offset = track_offset + 32 * len(tracks)
    gain_offset = event_offset + 16 * len(events)
    pcm_offset = gain_offset + 288
    pcm, sample_table, pcm_offsets = bytearray(), bytearray(), {}
    for sample in samples:
        payload = sample["pcm"]
        # Native pitch lives in the event timer; store identical PCM only once.
        if payload not in pcm_offsets:
            pcm_offsets[payload] = pcm_offset + len(pcm)
            pcm.extend(payload)
        offset = pcm_offsets[payload]
        sample_table.extend(struct.pack("<6I32s6BH", offset, len(sample["pcm"]),
            sample["loopStart"], sample["loopEnd"], sample["c5"], int(bool(sample["loopEnd"])),
            sample["name"].encode("ascii")[:31], *sample["envelope"], 0))
    header = struct.pack("<8s14I", b"HRNSQ01\0", 1, pcm_offset + len(pcm), len(samples),
        len(tracks), len(events), sample_offset, track_offset, event_offset, pcm_offset,
        tempo, intro, period, comparison, gain_offset)
    assert len(header) == header_size
    descriptors = b"".join(struct.pack("<8I", t["source"], t["slot"], t["first"],
        len(t["events"]), t["loopIndex"], t["loopTick"] or 0, t["endTick"], 0) for t in tracks)
    packed_events = b"".join(struct.pack("<IBBHII", e["tick"], e["op"], e["slot"],
        e.get("a", 0), e.get("b", 0), e.get("c", 0)) for e in events)
    gains = [min(65535, round(65535 * 10 ** (-0.75 * (127 - v) / 20))) for v in range(128)]
    dac = (0, 141, 222, 306, 441, 585, 836, 1112, 1595, 2146, 3081, 4135, 6006, 8155, 11976, 16382)
    ssg_mix = load_mix_profile()["ssg_global_volume"]
    gains += [round(65535 * value / dac[-1] * ssg_mix / 64) for value in dac]
    blob = header + sample_table + descriptors + packed_events + struct.pack("<144H", *gains) + pcm
    assert len(blob) == pcm_offset + len(pcm)
    return blob, tracks, counts


def build_track(args):
    from native_instruments import host_renderer, instrument_assets
    source = Path(f"assets/hdi_dump/{args.track.upper()}.MDT")
    args.out.mkdir(parents=True, exist_ok=True)
    nitro = args.out / "nitrofs" / args.track
    nitro.mkdir(parents=True, exist_ok=True)
    mix = load_mix_profile()
    sys.path.insert(0, str(args.tools_dir.resolve()))
    from mdt_decomp_rip import parse_mdt
    song = parse_mdt(str(source))
    if song.chip != 1:
        raise ValueError("native playback requires OPNA")
    tempos = [e[1] for c in song.channels for e in c.events if e[0] == "t"]
    if not tempos:
        raise ValueError("missing initial tempo")
    diagnostics = []
    compiled = [track for channel in song.channels for track in compile_channel(channel,
        allow_portamento=args.approximate_portamento, original_rhythm=True,
        macros={m.macro_id: m for m in song.macros.values()})]
    looped = [t for t in compiled if t['loopTick'] is not None]
    if not looped or any(t['endTick'] <= t['loopTick'] for t in looped):
        raise ValueError('playable soundtrack entry requires nonempty loops')
    intro = max(t['loopTick'] for t in looped)
    period = math.lcm(*(t['endTick'] - t['loopTick'] for t in looped))
    comparison = intro + period
    renderer = host_renderer(args.ymfm, args.cxx)
    samples, zones, rhythm, rhythm_report = instrument_assets(song, compiled, args.out / 'instruments',
        renderer, args.rhythm_dir, source)
    blob, tracks, counts = pack_song(song, samples, zones, rhythm, tempo=tempos[0],
        intro=intro, period=period, comparison=comparison,
        allow_portamento=args.approximate_portamento, diagnostics=diagnostics,
        original_rhythm=True)
    (nitro / "song.nsq").write_bytes(blob)
    (args.out / "native_track.h").write_text(
        f'#pragma once\n#define NS_TRACK_NAME "{args.track.upper()}"\n'
        f'#define NS_TRACK_ID "{args.track}"\n'
        f'#define NS_INITIAL_TRACK NATIVE_{args.track.upper()}\n'
        f'#define NS_TRACK_SLIDES {len(diagnostics)}\n#define NS_ORIGINAL_RHYTHM {int(bool(args.rhythm_dir))}\n')
    for item in diagnostics:
        print(f"WARNING {args.track.upper()} {item['source']:#x} tick {item['tick']}: {item['limitation']} {item['command']}")
    inputs = [Path(__file__), Path('tools/native_instruments.py'), Path('tools/render_opna_fm.cpp'),
        Path('tools/import_opna_rhythm.py'), Path('audio/opna_mix.json'),
        *sorted(args.tools_dir.glob('*.py')), *sorted((args.ymfm / 'src').glob('ymfm*.*'))]
    report = dict(version=1, track=args.track, source=str(source),
        sequence_path=str(nitro / "song.nsq"), rhythm=rhythm_report, mix=mix,
        source_sha256=sha(source.read_bytes()),
        sequence_sha256=sha(blob), parser_sha256=sha((args.tools_dir / "mdt_decomp_rip.py").read_bytes()),
        build_inputs={str(p.relative_to(Path.cwd()) if p.is_absolute() and p.is_relative_to(Path.cwd()) else p): sha(p.read_bytes()) for p in inputs},
        native_ticks_per_beat=48, tempo=tempos[0], approximations=diagnostics,
        notes_by_slot=dict(sorted(counts.items())),
        tracks=[{k: v for k, v in t.items() if k != "events"} | {"events": len(t["events"])} for t in tracks],
        samples=[{k: v for k, v in s.items() if k != "pcm"} | {"pcm_sha256": sha(s["pcm"]), "bytes": len(s["pcm"])} for s in samples],
        limitations=["Fixed FM/SSG instrument PCM, not runtime synthesis; rhythm provenance recorded separately",
            "Tone+noise PCM retains existing pitch-coupled noise limitation",
            "Native 48-tick clock uses exported tempos, not raw OPNA timer-B emulation",
            "Portamento, if explicitly enabled, is unverified chip-domain linear interpolation",
            "Integer gate counters truncate fractional Q durations; original rounding not runtime-verified",
            "No hardware LFO or nonzero SSG pitch LFO"])
    (args.out / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("build/native_audio"))
    parser.add_argument("--tools-dir", type=Path, default=Path("tools/deps/mdt-tools"))
    parser.add_argument("--ymfm", type=Path, default=Path("tools/deps/ymfm"))
    parser.add_argument("--cxx", default="g++")
    tracks = json.loads(Path("audio/tracks.json").read_text())["tracks"]
    parser.add_argument("--track", choices=[t["id"] for t in tracks if t.get("playable", True)], default="kami2")
    parser.add_argument("--approximate-portamento", action="store_true",
                        help="explicitly allow diagnosed, unverified chip-domain linear slides")
    parser.add_argument("--rhythm-dir", type=Path,
                        default=Path("assets/drums") if Path("assets/drums/provenance.json").is_file() else None)
    build_track(parser.parse_args())


if __name__ == "__main__":
    main()
