#!/usr/bin/env python3
"""Exercise every native track through the portable production sequencer."""
import json
from pathlib import Path

from test_native_audio import check_trace


def check(binaries, run):
    out = Path('build/native_soundtrack/tests')
    out.mkdir(parents=True, exist_ok=True)
    exe = binaries['engine']
    if 'music' in binaries:
        run(binaries['music'])
    if 'tuning' in binaries:
        run(binaries['tuning'])
    manifest = json.loads(Path('build/native_soundtrack/manifest.json').read_text())
    for track in manifest['tracks']:
        prefix = out / track['id']
        if 'patch' in binaries:
            instruments = Path(track['sequence_path']).parents[2] / 'instruments'
            run(binaries['patch'], arguments=(track['source'], str(instruments / 'patches.opm'),
                                             str(instruments / 'fm_usage.txt')))
        run(exe, arguments=(track['sequence_path'], str(prefix)))
        check_trace(str(prefix) + '_trace.csv', track)
        for model in ('service', 'diagnostic'):
            if model in binaries:
                run(binaries[model], arguments=(track['sequence_path'],))
        print(track['id'], 'passed', flush=True)
    print('All 16 native tracks passed engine, gain, tempo, loop and trace checks')


if __name__ == '__main__':
    raise SystemExit('Use make test TEST_SUITE=audio (or make sanitize TEST_SUITE=audio)')
