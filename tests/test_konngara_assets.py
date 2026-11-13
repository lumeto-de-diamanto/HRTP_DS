#!/usr/bin/env python3
"""Original-source composition, independent entrance trace and ROM checks."""
from array import array
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest

from grp_to_bin import decode_grp
from konngara_to_bin import build, HEADER
from nds_files import nitro_files


def reference_trace():
    # Literal b20j.cpp control flow, independent of the C++ state machine.
    trace = []
    line, remaining, quarter, frame, ramp = 0, 32, 0, 0, 0
    while True:
        displayed = line
        line += remaining
        if quarter == 0 and line > 100:
            quarter += 1
            remaining -= 1
        if quarter == 1 and line > 200:
            quarter += 1
            remaining -= 1
        if quarter == 2 and line > 300:
            quarter += 1
            remaining -= 1
        if quarter == 3 and line > 400:
            quarter = 0
            remaining -= 1
            line -= 400
        if remaining <= 0:
            trace.append((0, displayed, 400, ramp, -1, 0))
            break
        if frame % 8 == 0:
            ramp = min(15, ramp + 1)
        trace.append((0, displayed, 400, ramp, -1, 0))
        frame += 1
    trace += [(1, 0, 400, ramp, -1, 0)] * 40
    trace += [(2, 416 - f % 2 * 32, 400, ramp, -1, int(f % 8 == 0)) for f in range(32)]
    trace += [(3, y, y, ramp, -1, 0) for y in range(400, -1, -32)]
    trace += [(4, 416 - f % 2 * 16, 16, ramp, -1, 0) for f in range(32)]
    trace += [(5, 0, 16, ramp, -1, 0)] * 30
    trace += [(6, 0, 16, ramp, step, 0) for step in range(16) for _ in range(10)]
    return trace


class KonngaraAssets(unittest.TestCase):
    def test_palette_provenance_and_every_source_pixel_signature(self):
        data = Path('nitrofs/konngara_intro.bin').read_bytes()
        magic, width, height, count, colors, size = HEADER.unpack_from(data)
        self.assertEqual((magic, width, height, count, size), (b'KGI1', 320, 400, 3, len(data)))
        self.assertEqual(data, build(Path('assets/hdi_dump')))
        dictionaries = struct.unpack_from('<' + 'H' * colors, data, 64)
        ids = array('H')
        ids.frombytes(data[64 + colors * 2:])
        if sys.byteorder != 'little':
            ids.byteswap()
        self.assertEqual(len(ids), 320 * 400 * 3)
        def load(name):
            return decode_grp(Path(f'assets/hdi_dump/{name}.GRP').read_bytes())
        scroll, _, _ = load('BOSS7_D1')
        arena, palette, _ = load('BOSS8_A1')
        self.assertEqual(data[16:64], bytes(c // 17 for rgb in palette for c in rgb))
        final = [r[:] for r in scroll[:16] + arena[16:]]
        for i in range(1, 5):
            overlay, _, _ = load(f'BOSS8_D{i}')
            self.assertEqual({p for r in overlay for p in r}, {9, 15})
            for y in range(400):
                for x in range(640):
                    if overlay[y][x] != 15:
                        final[y][x] = overlay[y][x]
        for page, image in enumerate((scroll, arena, final)):
            for y in range(400):
                below = image[(y + 1) % 400]
                for x in range(320):
                    index = ids[(page * 400 + y) * 320 + x]
                    self.assertLess(index, colors)
                    actual = sorted((dictionaries[index] >> (i * 4)) & 15 for i in range(4))
                    self.assertEqual(actual, sorted((image[y][x * 2], image[y][x * 2 + 1],
                                                     below[x * 2], below[x * 2 + 1])))

    def test_source_timeline_and_real_stage_entry(self):
        trace = reference_trace()
        self.assertEqual(len(trace), 711)
        self.assertEqual([sum(f[0] == p for f in trace) for p in range(7)], [404, 40, 32, 13, 32, 30, 160])
        self.assertEqual([i for i, f in enumerate(trace) if f[-1]], [444, 452, 460, 468])
        self.assertEqual(trace[476:489], [(3, y, y, 15, -1, 0) for y in range(400, -1, -32)])
        source = Path('source/main.cpp').read_text()
        navigation=source.split('if (keys_held & KEY_SELECT)',1)[1].split('// START = Esc',1)[0]
        for key in ('R','L','A','B','X','Y'):
            self.assertNotRegex(navigation,rf'\bKEY_{key}\b')
        for shortcut in ('konngara_intro_preview()','konngara_art_preview()','konngara_combat_review('):
            self.assertNotIn(shortcut,source)
        self.assertIn('KEY_LEFT',navigation)
        self.assertIn('KEY_DOWN',navigation)
        # Actual Stage20 keeps its production intro after shortcuts are removed.
        self.assertIn('konngara_intro_play()',source)
        self.assertIn('stage_has_konngara',Path('source/engine/main/progression.hpp').read_text())


def run_renderer(binary):
    runtime = Path('build/konngara/tests/runtime')
    destination = runtime / 'nitro:'
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile('nitrofs/konngara_intro.bin', destination / 'konngara_intro.bin')
    trace = reference_trace()
    (runtime / 'trace.bin').write_bytes(b''.join(struct.pack('<6i', *frame) for frame in trace))
    subprocess.run([str(Path(binary).resolve())], cwd=runtime, check=True)


if __name__ == '__main__':
    rom_check = '--rom' in sys.argv
    if rom_check:
        sys.argv.remove('--rom')
    binary = None
    if '--run' in sys.argv:
        index = sys.argv.index('--run')
        binary = sys.argv[index + 1]
        del sys.argv[index:index + 2]
    result = unittest.main(exit=False)
    if not result.result.wasSuccessful():
        sys.exit(1)
    if rom_check:
        actual = nitro_files(Path('hrtp_ds.nds').read_bytes())
        assert actual.get('konngara_intro.bin') == Path('nitrofs/konngara_intro.bin').read_bytes()
        print('ROM: exact original-source Konngara intro composition verified')
    if binary:
        run_renderer(binary)
