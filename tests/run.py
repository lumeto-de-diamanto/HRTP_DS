#!/usr/bin/env python3
"""Shared host, sanitizer and packaged-release entry points (run via Make)."""
import argparse
import fcntl
import hashlib
import importlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tools'), str(ROOT)]
CACHE = ROOT / 'build/tests'


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def snapshot(paths):
    return {str(p): digest(p) for p in sorted(set(map(Path, paths)))}


def load_stamp(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return None


def save_stamp(path, value):
    # Publish only complete builds/fixtures, never a partially written stamp.
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as out:
        temporary = Path(out.name)
        try:
            json.dump(value, out, sort_keys=True)
            out.close()
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)


class HostBuilder:
    def __init__(self, cxx, sanitize, directory=CACHE):
        self.cxx = shlex.split(cxx)
        self.directory = directory / ('sanitize' if sanitize else 'host')
        self.directory.mkdir(parents=True, exist_ok=True)
        self.flags = ['-std=c++17', '-Wall', '-Wextra'] + (
            ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-fno-pie', '-no-pie'] if sanitize else ['-O2'])
        self.identity = {
            'command': self.cxx,
            'executable': digest(shutil.which(self.cxx[0]) or self.cxx[0]),
            'version': subprocess.check_output([*self.cxx, '--version'], text=True),
            'machine': subprocess.check_output([*self.cxx, '-dumpmachine'], text=True),
        }

    def build(self, name, sources, includes, link=()):
        flags = [*self.flags, *('-I' + str(p) for p in includes)]
        # Scan every translation unit, including .cpp files included by harnesses.
        # A fresh scan also detects new headers shadowing an old include path.
        dependencies = subprocess.check_output(
            [*self.cxx, *flags, '-MM', '-MT', 'target', *sources], text=True)
        paths = []
        for line in dependencies.replace('\\\n', '').splitlines():
            paths.extend(shlex.split(line.split(': ', 1)[1]))
        signature = dict(compiler=self.identity, flags=flags, sources=sources,
                         link=list(link), inputs=snapshot(paths))
        binary = self.directory / name
        stamp = binary.with_suffix('.json')
        previous = load_stamp(stamp)
        if (binary.is_file() and previous and previous.get('signature') == signature
                and previous.get('output') == digest(binary)):
            print(f'  cached host: {name}', flush=True)
            return binary
        print(f'  build host: {name}', flush=True)
        with tempfile.TemporaryDirectory(prefix=name + '-', dir=self.directory) as directory:
            temporary = Path(directory) / name
            subprocess.run([*self.cxx, *flags, *sources, *link, '-o', str(temporary)], check=True)
            temporary.replace(binary)
        save_stamp(stamp, dict(signature=signature, output=digest(binary)))
        return binary


def fixtures(module_name, runtime):
    module = importlib.import_module(module_name)
    # Independent pixel oracles consume original GRP/BOS/GRZ data and staged
    # graphics, not just renderer inputs. Hash content rather than timestamps.
    inputs = [Path(__file__)]
    for directory, pattern in [('tests', '*.py'), ('tools', '*.py'),
                               ('assets/hdi_dump', '*'), ('assets/packfile', '*'),
                               ('nitrofs', '*.bin')]:
        inputs += [p for p in Path(directory).glob(pattern) if p.is_file()]
    signature = snapshot(inputs)
    stamp = CACHE / (module_name + '.json')
    previous = load_stamp(stamp)
    outputs = [p for p in runtime.rglob('*') if p.is_file()]
    if (previous and previous.get('inputs') == signature and outputs
            and previous.get('outputs') == snapshot(outputs)):
        print(f'  cached fixtures: {module_name}', flush=True)
        return
    print(f'  prepare fixtures: {module_name}', flush=True)
    stamp.unlink(missing_ok=True)
    module.run_renderer(None)
    save_stamp(stamp, dict(inputs=signature,
                          outputs=snapshot(p for p in runtime.rglob('*') if p.is_file())))


def python_tests(names):
    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromName(name) for name in names)
    if not unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful():
        raise SystemExit(1)


def host_tests(args):
    selected = lambda suite: args.suite in ('all', suite)
    if args.mode == 'test':
        names = []
        if selected('graphics'):
            names += ['test_asset_build', 'test_font16x16', 'test_boss_assets',
                      'test_viewport_assets', 'test_konngara_assets', 'test_konngara_art_assets']
        if selected('audio'):
            names += ['test_native_audio', 'test_native_sfx', 'test_opna_rhythm',
                      'test_recording_comparison']
        if names:
            python_tests(names)

    builder = HostBuilder(args.cxx, args.mode == 'sanitize')
    engine = 'source/engine/'
    platform = 'source/platform/'
    base = [Path(args.blocksds) / 'libs/libnds/include', 'source/engine', 'source/platform']
    graphics = ['tools/viewport_stubs', *base, 'assets/processed', 'build/native_soundtrack']
    heap = ['-Wl,--wrap=malloc,--wrap=free']

    def build(name, sources, includes=base, link=()):
        return builder.build(name, sources, includes, link)

    def run(name, sources, includes=base, arguments=()):
        subprocess.run([str(build(name, sources, includes)), *arguments], check=True)

    def eng(*paths):
        return [engine + p + '.cpp' for p in paths]

    if selected('gameplay'):
        run('timer', ['tests/test_timer.cpp', *eng('main/stage/timer')])
        projectiles = eng('main/player/orb', 'main/player/shot', 'main/bullet/pellet',
                          'main/math/vector', 'main/math/trig')
        run('projectiles', ['tests/test_projectiles_portals.cpp',
                            *eng('main/stage/stageobj'), *projectiles])
        run('boss', ['tests/test_boss.cpp', *eng('main/boss/boss', 'main/boss/singyoku',
                                               'main/random'), *projectiles])
        combat = eng('main/boss/konngara_phase1', 'main/boss/boss', 'main/player/shot',
                     'main/math/vector', 'main/math/trig')
        run('phase1', ['tests/test_konngara_phase1.cpp', *combat])
        for phase in (3, 5):
            run(f'phase{phase}', [f'tests/test_konngara_phase{phase}.cpp', *combat,
                                  *eng('main/boss/konngara', 'main/bullet/shootout_laser')])

    if selected('graphics'):
        viewport = ['tests/test_viewport.cpp', 'tests/heap_probe.cpp',
                    'tests/test_stage_entrance.cpp', 'tests/test_konngara_review.cpp']
        for pattern in ('main/player/*.cpp', 'main/boss/*.cpp', 'main/bullet/*.cpp',
                        'main/math/*.cpp'):
            viewport += sorted(str(p) for p in Path('source/engine').glob(pattern))
        viewport += eng('main/stage/stageobj', 'main/stage/card', 'main/stage/item',
                        'main/stage/timer', 'main/random', 'main/bonus')
        viewport += [platform + p + '.cpp' for p in (
            'camera canvas video tiles stage_render background upper_hud hud_render font16x16 '
            'player_render orb_render shot_render item_render pellet_render popup boss_render '
            'palette_fx text_layer stage_entrance boss_gfx konngara_art_render '
            'konngara_combat_render konngara_combat_review konngara_defeat_render '
            'konngara_intro_render menu_render bomb_render round_results').split()]
        intro = ['tests/test_konngara.cpp', 'tests/heap_probe.cpp',
                 *eng('main/boss/konngara_intro', 'main/math/trig')]
        intro += [platform + p + '.cpp' for p in
                  'konngara_intro_render camera canvas video font16x16'.split()]
        art = ['tests/test_konngara_art.cpp', 'tests/heap_probe.cpp',
               *eng('main/math/trig', 'main/bullet/shootout_laser')]
        art += [platform + p + '.cpp' for p in (
            'konngara_art_render konngara_combat_render boss_gfx camera canvas video '
            'font16x16 konngara_defeat_render text_layer').split()]
        for name, sources, module, runtime in (
                ('viewport', viewport, 'test_viewport_assets', 'build/viewport/tests/runtime'),
                ('intro', intro, 'test_konngara_assets', 'build/konngara/tests/runtime'),
                ('art', art, 'test_konngara_art_assets', 'build/konngara/art-tests/runtime')):
            binary = build(name, sources, graphics, heap)
            fixtures(module, Path(runtime))
            subprocess.run([str(binary)], cwd=runtime, check=True)

    if selected('audio'):
        run('lfo', ['tests/test_audio_lfo.cpp'], ['tools'])
        run('sfx', ['tests/test_native_sfx.cpp'], ['.', 'native_audio/service_stubs', 'tools'],
            ['nitrofs/audio/sfx.bin'])
        run('sfx_hooks', ['tests/test_sfx_gameplay_hooks.cpp',
                         *eng('main/player/shot', 'main/stage/card', 'main/stage/item', 'main/random')])
        binaries = {'engine': build('engine', ['tests/test_native_audio.cpp'], ['.', 'tools'])}
        if args.mode != 'sanitize':
            # These service/diagnostic/music RAM models map fixed DS addresses
            # that conflict with sanitizer shadow memory on some hosts.
            for name in ('service', 'diagnostic'):
                binaries[name] = build(name, [f'tests/test_native_{name}.cpp'],
                                       ['native_audio/service_stubs', 'native_audio', 'tools'])
            binaries['music'] = build('music', ['tests/test_native_music.cpp', platform + 'music.cpp'],
                                      ['native_audio/test_stubs', 'native_audio',
                                       'build/native_soundtrack', 'source/platform'])
        importlib.import_module('test_native_soundtrack').check(binaries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('test', 'sanitize', 'release'))
    parser.add_argument('--suite', choices=('all', 'gameplay', 'graphics', 'audio'), default='all')
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--blocksds', default=('/opt/blocksds/core' if Path('/opt/blocksds/core').exists()
                                             else '/opt/wonderful/thirdparty/blocksds/core'))
    args = parser.parse_args()
    if args.mode == 'release' and args.suite != 'all':
        parser.error('release always checks the complete packaged ROMs')
    os.chdir(ROOT)
    os.environ.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
    CACHE.mkdir(parents=True, exist_ok=True)
    # Serialize publication and runtime use across normal/sanitized runners.
    with (CACHE / 'runner.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.mode == 'release':
            python_tests(['test_rom_packaging'])
            importlib.import_module('check_audio_release').check_release()
        else:
            host_tests(args)
    print(f'{args.mode} ({args.suite}): passed', flush=True)


if __name__ == '__main__':
    main()
