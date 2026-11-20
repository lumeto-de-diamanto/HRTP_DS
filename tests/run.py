#!/usr/bin/env python3
"""Shared host, sanitizer and packaged-release entry points (run via Make)."""
import argparse
from concurrent.futures import ThreadPoolExecutor
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
import time
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
        jobs = min(4, len(sources), os.cpu_count() or 1)
        print(f'  build host: {name} ({jobs} compile jobs)', flush=True)
        started = time.monotonic()
        with tempfile.TemporaryDirectory(prefix=name + '-', dir=self.directory) as directory:
            temporary = Path(directory) / name
            deadline = started + 120
            objects = [str(Path(directory) / f'{i}.o') for i in range(len(sources))]

            def compile_source(item):
                i, source = item
                subprocess.run([*self.cxx, *flags, '-c', source, '-o', objects[i]],
                               check=True, timeout=max(1, deadline-time.monotonic()))

            # Compilation is independent; fixture mutation and execution remain
            # serialized. This makes a cold run bounded without hiding tests or
            # lowering sanitizer/optimization coverage to meet the time budget.
            with ThreadPoolExecutor(max_workers=jobs) as pool:
                list(pool.map(compile_source, enumerate(sources)))
            subprocess.run([*self.cxx, *flags, *objects, *link, '-o', str(temporary)],
                           check=True, timeout=max(1, deadline-time.monotonic()))
            temporary.replace(binary)
        save_stamp(stamp, dict(signature=signature, output=digest(binary)))
        print(f'  built: {name} in {time.monotonic()-started:.2f}s', flush=True)
        return binary


def fixtures(module_name, runtime):
    module = importlib.import_module(module_name)
    # Independent pixel oracles consume original GRP/BOS/GRZ data and staged
    # graphics, not just renderer inputs. Hash content rather than timestamps.
    inputs = [Path(__file__), Path('assets/font.bmp')]
    for directory, pattern in [('tests', '*.py'), ('tools', '*.py'),
                                ('assets/hdi_dump', '*'), ('assets/packfile', '*'),
                                ('assets/fonts', '*.bdf'),
                                ('assets/sprites/radar', '*.png'),
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
    started = time.monotonic()
    stamp.unlink(missing_ok=True)
    module.run_renderer(None)
    save_stamp(stamp, dict(inputs=signature,
                          outputs=snapshot(p for p in runtime.rglob('*') if p.is_file())))
    print(f'  prepared: {module_name} in {time.monotonic()-started:.2f}s', flush=True)


def python_tests(names):
    for name in names:
        started = time.monotonic()
        print(f'  run python: {name}', flush=True)
        suite = unittest.defaultTestLoader.loadTestsFromName(name)
        if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
            raise SystemExit(1)
        print(f'  completed: {name} in {time.monotonic()-started:.2f}s', flush=True)


def run_binary(binary, *, cwd=None, arguments=(), sanitize=False):
    budget = 120 if sanitize else 60
    started = time.monotonic()
    print(f'  run host: {Path(binary).name} (limit {budget}s)', flush=True)
    try:
        subprocess.run([str(binary), *arguments], cwd=cwd, check=True, timeout=budget)
    except subprocess.TimeoutExpired:
        raise SystemExit(f'{Path(binary).name}: exceeded {budget}s; last running case identifies the stall')
    print(f'  completed: {Path(binary).name} in {time.monotonic()-started:.2f}s', flush=True)


def host_tests(args):
    selected = lambda suite: args.suite in ('all', suite)
    if args.mode == 'test':
        names = []
        if selected('graphics'):
            names += ['test_font16x16', 'test_boss_assets',
                      'test_radar_assets', 'test_vram_budget',
                      'test_viewport_assets', 'test_konngara_assets', 'test_konngara_art_assets',
                      'test_kikuri_assets', 'test_yuugenmagan_assets', 'test_elis_assets']
        if selected('audio'):
            names += ['test_native_build_cache', 'test_native_audio', 'test_native_sfx', 'test_opna_rhythm',
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
        run_binary(build(name, sources, includes), arguments=arguments, sanitize=args.mode=='sanitize')

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
        run('mima_phase1', ['tests/test_mima_phase1.cpp',
                            *eng('main/boss/mima_phase1','main/bullet/missile','main/bullet/shootout_laser','main/boss/boss',
                                  'main/player/shot','main/math/vector','main/math/trig')])
        run('kikuri_phase1', ['tests/test_kikuri_phase1.cpp',
                              *eng('main/boss/kikuri_phase1','main/boss/boss','main/player/shot','main/math/trig','main/bullet/shootout_laser')])
        run('kikuri_phase5', ['tests/test_kikuri_phase5.cpp',
                              *eng('main/boss/kikuri_phase1','main/boss/boss','main/player/shot','main/math/trig','main/bullet/shootout_laser')])
        run('kikuri_phase6', ['tests/test_kikuri_phase6.cpp',
                              *eng('main/boss/kikuri_phase1','main/boss/boss','main/player/shot','main/math/trig','main/bullet/shootout_laser')])
        for phase in (3, 5):
            run(f'phase{phase}', [f'tests/test_konngara_phase{phase}.cpp', *combat,
                                  *eng('main/boss/konngara', 'main/bullet/shootout_laser')])

    if selected('graphics'):
        viewport = ['tests/test_viewport.cpp', 'tests/heap_probe.cpp',
                    'tests/test_stage_entrance.cpp', 'tests/test_konngara_review.cpp',
                    'tests/test_mima_review.cpp','tests/test_mima_phase1_review.cpp',
                    'tests/test_kikuri_art.cpp','tests/test_kikuri_render.cpp','tests/test_kikuri_defeat.cpp',
                    'tests/test_yuugenmagan.cpp','tests/test_yuugenmagan_phase1.cpp','tests/test_yuugenmagan_combat.cpp',
                    'tests/test_yuugenmagan_patterns.cpp','tests/test_yuugenmagan_pentagram.cpp',
                    'tests/test_yuugenmagan_phase13.cpp','tests/test_yuugenmagan_defeat.cpp','tests/test_elis_entrance.cpp','tests/test_elis_phase1.cpp','tests/test_elis_phase3.cpp','tests/test_elis_phase5.cpp','tests/test_elis_defeat.cpp']
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
            'konngara_intro_render menu_render bomb_render round_results mima_render mima_review '
            'mima_combat_render mima_phase1_review mima_defeat_render kikuri_render kikuri_entrance kikuri_defeat_render '
            'yuugenmagan_render yuugenmagan_combat_render yuugenmagan_defeat_render elis_render elis_entrance elis_defeat_render').split()]
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
            run_binary(binary,cwd=runtime,sanitize=args.mode=='sanitize')

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
            binaries['patch'] = Path('build/native_tools/test_opna_patch')
            binaries['tuning'] = Path('build/native_tools/test_opna_tuning')
        importlib.import_module('test_native_soundtrack').check(
            binaries, lambda binary, **kwargs: run_binary(binary, sanitize=args.mode=='sanitize', **kwargs))


def main():
    started = time.monotonic()
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
            # Disposable clean-build/recovery is a release audit, not something
            # to rebuild from scratch on every C++ edit/test cycle.
            python_tests(['test_asset_build', 'test_rom_packaging'])
            importlib.import_module('check_audio_release').check_release()
        else:
            host_tests(args)
    print(f'{args.mode} ({args.suite}): passed in {time.monotonic()-started:.2f}s', flush=True)


if __name__ == '__main__':
    main()
