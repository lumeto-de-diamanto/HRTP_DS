#!/usr/bin/env python3
"""Exercise every native track through the portable production sequencer."""
import argparse
import json
from pathlib import Path
import subprocess

from test_native_audio import check_trace


def check(args):
    out = Path('build/native_soundtrack/tests')
    out.mkdir(parents=True, exist_ok=True)
    exe = out / ('engine_sanitized' if args.sanitize else 'engine')
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else ['-O2']
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', *flags, '-Itools',
        'tests/test_native_audio.cpp', '-o', str(exe)], check=True)
    if not args.sanitize:
        # Fixed-address RAM model conflicts with sanitizer shadow layouts on
        # some hosts; the portable engine above is the sanitizer target.
        service = out / 'service'
        subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra',
            '-Inative_audio/service_stubs', '-Inative_audio', '-Itools',
            'tests/test_native_service.cpp', '-o', str(service)], check=True)
        diagnostic = out / 'diagnostic'
        subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra',
            '-Inative_audio/service_stubs', '-Inative_audio', '-Itools',
            'tests/test_native_diagnostic.cpp', '-o', str(diagnostic)], check=True)
        music = out / 'music'
        subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra',
            '-Inative_audio/test_stubs', '-Inative_audio', '-Ibuild/native_soundtrack',
            '-Isource/platform', 'tests/test_native_music.cpp', 'source/platform/music.cpp',
            '-o', str(music)], check=True)
        subprocess.run([str(music)], check=True)
    manifest = json.loads(Path('build/native_soundtrack/manifest.json').read_text())
    for track in manifest['tracks']:
        prefix = out / track['id']
        subprocess.run([str(exe), track['sequence_path'], str(prefix)], check=True)
        check_trace(str(prefix) + '_trace.csv', track)
        if not args.sanitize:
            subprocess.run([str(service), track['sequence_path']], check=True)
            subprocess.run([str(diagnostic), track['sequence_path']], check=True)
        print(track['id'], 'passed', flush=True)
    print('All 16 native tracks passed engine, gain, tempo, loop and trace checks')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    check(parser.parse_args())
