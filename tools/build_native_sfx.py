#!/usr/bin/env python3
"""Render all original MDE effects to single mono PCM16 samples and a resident bank."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import wave

from audio_mix import load_mix_profile
from decode_mde import Bank

DRIVER_SHA = 'e14f26007f1a71d747f50deeb0f338065466e87ad142268416a1bcbb14b8ae7d'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def changed(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def build(args):
    if digest(args.driver) != DRIVER_SHA:
        raise ValueError('MDRV98 differs from the driver used to verify the SFX decoder')
    if not 17 <= args.tempo <= 127:
        raise ValueError('SFX reference tempo must be 17..127')
    args.out.mkdir(parents=True, exist_ok=True)
    sources = [Path('tools/decode_mde.py'), Path(__file__), Path('tools/render_opna_sfx.cpp'),
        *sorted((args.ymfm / 'src').glob('ymfm*.*'))]
    identity = dict(source_sha256=digest(args.source), driver_sha256=digest(args.driver),
        renderer_sources={str(p): digest(p) for p in sources}, mix=load_mix_profile(), reference_tempo=args.tempo)
    manifest_path = args.out / 'manifest.json'
    if manifest_path.exists():
        previous = json.loads(manifest_path.read_text())
        bank = args.game_dir / 'sfx.bin'
        if previous.get('identity') == identity and bank.exists() and digest(bank) == previous.get('bank_sha256'):
            print('Native SFX: current 17-effect bank reused')
            return
    renderer = args.out / 'render_opna_sfx'
    subprocess.run([args.cxx, '-O2', '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-parameter', f'-I{args.ymfm / "src"}',
        'tools/render_opna_sfx.cpp', *(str(args.ymfm / 'src' / name) for name in
        ('ymfm_opn.cpp', 'ymfm_adpcm.cpp', 'ymfm_ssg.cpp')), '-o', str(renderer)], check=True)
    source = Bank(args.source.read_bytes())
    if source.count != 17:
        raise ValueError('expected the 17-effect TH01 bank')
    tick_clocks = 2304 * (4340 // args.tempo)  # actual OPNA timer-B divisor in MDRV98
    period = round(16756991 / 22050)
    gain = round(65535 * identity['mix']['ssg_global_volume'] / 64)
    header = struct.pack('<8s6I', b'HRNSFX1\0', 1, 0, source.count, 32, period, gain)
    entries, pcm_bank, reports = bytearray(), bytearray(), []
    for number in range(1, source.count + 1):
        decoded = source.render_events(number)
        event_blob = struct.pack('<4I', 0x31584653, tick_clocks, decoded['ticks'], len(decoded['events']))
        event_blob += b''.join(struct.pack('<II', tick, reg | value << 8) for tick, reg, value in decoded['events'])
        registers = args.out / f'sfx_{number:02}.registers'
        registers.write_bytes(event_blob)
        pcm_path = args.out / f'sfx_{number:02}.pcm'
        subprocess.run([str(renderer), str(registers), str(pcm_path)], check=True)
        pcm = pcm_path.read_bytes()
        if len(pcm) < 16 or len(pcm) % 4:
            raise ValueError('SFX PCM is not hardware aligned')
        values = struct.unpack('<' + 'h' * (len(pcm) // 2), pcm)
        wav = args.out / f'sfx_{number:02}.wav'
        with wave.open(str(wav), 'wb') as output:
            output.setnchannels(1); output.setsampwidth(2); output.setframerate(22050)
            output.writeframes(pcm)
        entries.extend(struct.pack('<II', len(header) + source.count * 8 + len(pcm_bank), len(pcm)))
        pcm_bank.extend(pcm)
        reports.append(dict(id=number, bytes=len(pcm), samples=len(pcm) // 2,
            seconds=len(pcm) / 2 / 22050, source_ticks=decoded['ticks'], source_streams=decoded['streams'],
            attacks=len(decoded['notes']), notes=decoded['notes'], pcm_sha256=hashlib.sha256(pcm).hexdigest(),
            wav=str(wav), peak=max(abs(v) for v in values), register_events=len(decoded['events']),
            registers_sha256=hashlib.sha256(event_blob).hexdigest()))
    bank = bytearray(header + entries + pcm_bank)
    struct.pack_into('<I', bank, 12, len(bank))
    manifest = dict(version=1, identity=identity, source=str(args.source), complete=True,
        format='One mono PCM16 sample per effect, including mixed two-stream effect 9',
        rate=22050, hardware_period=period, native_gain=gain, tick_clocks=tick_clocks,
        reference_clock_hz=7987200 / tick_clocks, pcm16_full_tone_peak=12544,
        provenance='User-provided original ZIGOKU.MDE and MDRV98.COM; local conversion requested. No redistribution license is asserted.',
        bank_sha256=hashlib.sha256(bank).hexdigest(), bank_bytes=len(bank), effects=reports,
        limitations=['Static driver transcription, not observed PC-98 execution',
            'Fixed reference timer-B tempo, not dynamic music-tempo-following SFX',
            'SSG reset phase and standalone inherited volume 15 are deterministic policies',
            'Effect 9 layers share one sample and cannot be interrupted separately',
            'Two-voice independent SFX playback keeps music audible rather than stealing its SSG voices',
            'Shared DAC-level centering and 10-Hz DC filter with 100-ms tail; no per-effect normalization'])
    changed(args.game_dir / 'sfx.bin', bank)
    changed(manifest_path, (json.dumps(manifest, indent=2) + '\n').encode())
    changed(args.game_dir / 'sfx-manifest.json', (json.dumps(manifest, indent=2) + '\n').encode())


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=Path('assets/hdi_dump/ZIGOKU.MDE'))
    p.add_argument('--driver', type=Path, default=Path('assets/hdi_dump/MDRV98.COM'))
    p.add_argument('--out', type=Path, default=Path('build/native_sfx'))
    p.add_argument('--game-dir', type=Path, default=Path('nitrofs/audio'))
    p.add_argument('--ymfm', type=Path, default=Path('tools/deps/ymfm'))
    p.add_argument('--tempo', type=int, default=90)
    p.add_argument('--cxx', default='g++')
    build(p.parse_args())
