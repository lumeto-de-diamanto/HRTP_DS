#!/usr/bin/env python3
"""Exercise every native track through the portable production sequencer."""
import json
from pathlib import Path
import subprocess

from test_native_audio import check_trace


def check(binaries):
    out = Path('build/native_soundtrack/tests')
    out.mkdir(parents=True, exist_ok=True)
    exe = binaries['engine']
    if 'music' in binaries:
        subprocess.run([str(binaries['music'])], check=True)
    manifest = json.loads(Path('build/native_soundtrack/manifest.json').read_text())
    for track in manifest['tracks']:
        prefix = out / track['id']
        subprocess.run([str(exe), track['sequence_path'], str(prefix)], check=True)
        check_trace(str(prefix) + '_trace.csv', track)
        for model in ('service', 'diagnostic'):
            if model in binaries:
                subprocess.run([str(binaries[model]), track['sequence_path']], check=True)
        print(track['id'], 'passed', flush=True)
    print('All 16 native tracks passed engine, gain, tempo, loop and trace checks')


if __name__ == '__main__':
    raise SystemExit('Use make test TEST_SUITE=audio (or make sanitize TEST_SUITE=audio)')
