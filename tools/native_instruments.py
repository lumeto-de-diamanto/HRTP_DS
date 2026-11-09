"""Render native instrument PCM directly from decoded MDT usage and local WAVs."""
from collections import defaultdict
import hashlib
from pathlib import Path
import struct
import subprocess

from audio_mix import load_mix_profile
from import_opna_rhythm import DRUMS, load_rhythm


def host_renderer(ymfm, cxx='g++'):
    out = Path('build/native_tools')
    out.mkdir(parents=True, exist_ok=True)
    sources = [Path('tools/render_opna_fm.cpp'),
               *(ymfm / 'src' / name for name in ('ymfm_opn.cpp', 'ymfm_adpcm.cpp', 'ymfm_ssg.cpp'))]
    inputs = [Path('tools/native_instruments.py'), Path('tests/test_opna_patch.cpp'),
              Path('tests/test_opna_tuning.cpp'), *sources, *sorted((ymfm / 'src').glob('ymfm*.*'))]
    identity = hashlib.sha256((cxx + ''.join(hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs)).encode()).hexdigest()
    exe, stamp = out / 'render_opna_fm', out / 'renderer.sha256'
    if not exe.exists() or not stamp.exists() or stamp.read_text() != identity:
        flags = [cxx, '-O2', '-std=c++17', '-Wno-unused-parameter', f'-I{ymfm / "src"}']
        subprocess.run([*flags, *map(str, sources), '-o', str(exe)], check=True)
        for name in ('test_opna_patch', 'test_opna_tuning'):
            test = out / name
            subprocess.run([*flags, f'tests/{name}.cpp', *map(str, sources[1:]), "-Itools", '-o', str(test)], check=True)
            if name == 'test_opna_tuning':
                subprocess.run([str(test)], check=True)
        stamp.write_text(identity)
    return exe


def instrument_assets(song, tracks, work, renderer, rhythm_dir, source):
    from mdt_decomp_rip import write_opm_header
    fm, ssg = defaultdict(list), defaultdict(list)
    for track in tracks:
        for e in track['events']:
            if e['op'] != 1 or e['slot'] >= 9:
                continue
            keys = [e['key']] + ([e['glide_target']] if 'glide_target' in e else [])
            if e['slot'] < 6:
                fm[e['patch']].extend(keys)
            else:
                ssg[e['patch'], e['mixer'], e['noise'] if e['mixer'] & 2 else 0].extend(keys)
    work.mkdir(parents=True, exist_ok=True)
    opm, fm_path, ssg_path, bank = (work / name for name in ('patches.opm', 'fm_usage.txt', 'ssg_usage.txt', 'instruments.bank'))
    with opm.open('w') as f:
        write_opm_header(f)
        for i, patch in enumerate(song.fm):
            f.write(patch.opm_str(i))
    fm_path.write_text(''.join(f'{p} {min(keys)} {max(keys)}\n' for p, keys in sorted(fm.items())))
    ssg_path.write_text(''.join(' '.join(map(str, (p, min(keys), max(keys), mixer, noise,
                                                *song.ssg[p].params))) + '\n'
                                  for (p, mixer, noise), keys in sorted(ssg.items())))
    subprocess.run([str(renderer.parent / 'test_opna_patch'), str(source), str(opm), str(fm_path)], check=True)
    subprocess.run([str(renderer), str(opm), str(fm_path), str(ssg_path), str(bank)], check=True)
    raw = bank.read_bytes()
    if raw[:8] != b'OPNABK3\0':
        raise ValueError('invalid rendered instrument bank')
    samples, zones, pos = [], [], 10
    for i in range(struct.unpack_from('<H', raw, 8)[0]):
        kind, program, root, low, high, mixer, noise, *envelope = raw[pos:pos + 13]
        c5, size, begin, end = struct.unpack_from('<4I', raw, pos + 13)
        pos += 29
        pcm = raw[pos:pos + size]; pos += size
        if len(pcm) != size or not any(pcm) or (end and (begin % 4 or end % 4 or not begin < end <= size)):
            raise ValueError('invalid/silent/unaligned rendered PCM')
        # Preserve the approved hardware tuning grid and trailing DMA padding.
        # They are now explicit native-format policies, not a packaging side effect.
        pcm += bytes((-len(pcm)) & 3)
        pcm += bytes(max(0, 16 - len(pcm)))
        samples.append(dict(name=f'{"SSG" if kind else "OPNA"} @{program} key {root}',
            c5=(c5 // 4) * 4, loopStart=begin if end else 0, loopEnd=end,
            pcm=pcm, envelope=envelope,
            global_volume=load_mix_profile()['ssg_global_volume'] if kind else 64))
        zones.append(dict(kind=kind, program=program, root=root, low=low, high=high,
                          mixer=mixer, noise=noise, sample=i))
    if pos != len(raw):
        raise ValueError('trailing instrument-bank data')
    if not rhythm_dir:
        raise ValueError('Native soundtrack requires local YM2608 WAVs and provenance.json; no placeholder fallback')
    drums = load_rhythm(rhythm_dir)
    rhythm = {}
    used = {e['key'] for t in tracks for e in t['events'] if t['slot'] >= 9 and e['op'] == 1}
    for key, _ in DRUMS:
        if key not in used:
            continue
        drum = drums[key]
        rhythm[key] = len(samples)
        samples.append(dict(name=f'YM2608 {drum["name"]}', c5=(round(drum['rate'] * 2 ** ((60 - key) / 12)) // 4) * 4,
            loopStart=0, loopEnd=0, pcm=drum['pcm'], envelope=[0] * 6, global_volume=64))
    report = dict(original=True, kind='caller-supplied-YM2608-WAV', directory=str(rhythm_dir),
        samples=[{k: v for k, v in sample.items() if k != 'pcm'} | {'midi_key': key, 'bytes': len(sample['pcm'])}
                 for key, sample in drums.items()])
    return samples, zones, rhythm, report
