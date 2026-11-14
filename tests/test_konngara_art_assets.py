#!/usr/bin/env python3
"""Independent source-plane/VRAM oracle for Konngara's visual-only slice."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest

from grp_to_bin import decode_grp
from grz_decode import decode_grz, decode_stream
from konngara_art_to_bin import build, bos_patches
from konngara_combat_to_bin import build as build_combat
from konngara_slash_to_bin import build_pair
from konngara_defeat_to_bin import build as build_defeat

ROOT = Path(__file__).resolve().parents[1]


class SourcePage:
    def __init__(self):
        def grp(name):
            return decode_grp((ROOT / f'assets/hdi_dump/{name}.GRP').read_bytes())
        scroll, _, _ = grp('BOSS7_D1')
        arena, palette, _ = grp('BOSS8_A1')
        self.palette = [[c // 17 for c in color] for color in palette]
        self.palette[9] = [0, 9, 10]
        self.base = bytearray(p for row in scroll[:16] + arena[16:] for p in row)
        for n in range(1, 5):
            overlay, _, _ = grp(f'BOSS8_D{n}')
            for y in range(400):
                for x in range(640):
                    if overlay[y][x] != 15:
                        self.base[y * 640 + x] = overlay[y][x]
        self.reset()

    def reset(self):
        self.page = self.base[:]
        self.direction, self.expression = 2, 0
        self.flashing = False
        self.siddham_white = 0

    def bos(self, filename, cel, top):
        data = (ROOT / f'assets/packfile/{filename}.BOS').read_bytes()
        width, height = data[4], data[6]
        area = width * height
        start = 64 + cel * area * 5
        for y in range(height):
            for x in range(width * 8):
                p, bit = y * width + x // 8, 7 - x % 8
                if not data[start + p] >> bit & 1:
                    color = sum(((data[start + p + area * (plane + 1)] >> bit) & 1) << plane
                                for plane in range(4))
                    self.page[(top + y) * 640 + 280 + x] = color

    def face(self):
        if self.expression == 3:
            self.bos('BOSS8_E2', self.direction, 128)
        elif self.expression:
            self.bos('BOSS8_E1', (self.expression - 1) * 3 + self.direction, 128)

    def command(self, op, arg):
        if op == 0:
            self.reset()
        elif op == 4:
            self.flashing = bool(arg)
        elif op == 5:
            self.siddham_white = arg
        elif op == 1 and self.direction != arg:
            self.direction = arg
            self.bos('BOSS8_1', arg, 80)
            self.face()
        elif op == 2 and self.expression != arg:
            self.expression = arg
            if arg:
                self.face()
            else:
                self.bos('BOSS8_1', self.direction, 80)
        elif op == 3:
            # Independent span-based interpretation of the source RLE. Do not
            # call the production GRZ decoder to build the reference image.
            data = (ROOT / 'assets/hdi_dump/BOSS8.GRZ').read_bytes()
            start = struct.unpack_from('<I', data, 8 + arg * 4)[0]
            rle_size, _ = struct.unpack_from('<HH', data, start + 8)
            rle, planar, offset = start + 64, start + 64 + rle_size, 0
            while offset < 32000:
                mode, length = data[rle], 1
                rle += 1
                if mode == 1:
                    runs = data[rle]
                    rle += 1
                    if runs:
                        mode, length = data[rle], runs + 1
                        rle += 1
                if mode == 0:
                    for pos in range(offset, offset + length):
                        for x in range(8):
                            color = sum(((data[planar + p] >> (7 - x)) & 1) << p for p in range(4))
                            self.page[pos * 8 + x] = color
                        planar += 4
                offset += length
            assert offset == 32000

    def frame(self, palette=None, line=0):
        out = bytearray()
        colors = [color[:] for color in (self.palette if palette is None else palette)]
        if palette is None:
            t = self.siddham_white
            colors[9] = [t, max(t, 9), max(t, 10)]
        components = [[(15 if palette is None and self.flashing and i in (3, 4, 5) else v) * 17 // 4
                       for v in color] for i, color in enumerate(colors)]
        for y in range(16, 400, 2):
            top,below=(y+line)%400,(y+line+1)%400
            for x in range(0, 640, 2):
                pixels = [self.page[top * 640 + x], self.page[top * 640 + x + 1],
                          self.page[below * 640 + x], self.page[below * 640 + x + 1]]
                rgb = [sum(components[p][c] for p in pixels) >> 3 for c in range(3)]
                out += struct.pack('<H', 0x8000 | rgb[0] | rgb[1] << 5 | rgb[2] << 10)
        return out


def skip(count):
    out = bytearray()
    while count:
        n = min(count, 256)
        out += bytes((1, n - 1, 2)) if n > 1 else bytes((2,))
        count -= n
    return out


class KonngaraArtAssets(unittest.TestCase):
    def test_defeat_cache(self):
        data=(ROOT/'nitrofs/konngara_defeat.bin').read_bytes()
        self.assertEqual(data,build_defeat(ROOT/'assets/hdi_dump'))
        self.assertEqual(struct.unpack_from('<4sHHHHI',data),(b'KGD1',320,200,10,0,len(data)))
    def test_hybrid_cache(self):
        art = (ROOT / 'nitrofs/konngara_art.bin').read_bytes()
        data = (ROOT / 'nitrofs/konngara_slash.bin').read_bytes()
        arena, slash = build_pair(art)
        self.assertEqual(data, slash)
        self.assertEqual((ROOT / 'nitrofs/konngara_combat.bin').read_bytes(), arena)
        self.assertEqual(struct.unpack_from('<4sIIIIIII',data),
                         (b'KHS2',len(data),704,287920,288624,671,32,28))
    def test_grx_run_semantics_and_bounds(self):
        self.assertEqual(decode_stream(bytes((1, 2, 0)) + skip(31997), bytes(range(12))),
                         [(0, bytes(range(4))), (1, bytes(range(4, 8))), (2, bytes(range(8, 12)))])
        self.assertEqual(decode_stream(bytes((1, 0, 0)) + skip(31998), b'ABCD'), [(1, b'ABCD')])
        self.assertEqual(decode_stream(skip(32000), b''), [])
        for rle, planar in [(b'\1', b''), (b'\0', b''), (skip(32001), b''),
                            (skip(32000) + b'\2', b''), (skip(32000), b'1234')]:
            with self.assertRaises(ValueError):
                decode_stream(rle, planar)

    def test_all_grz_streams_and_seven_source_slash_cels(self):
        data = (ROOT / 'assets/hdi_dump/BOSS8.GRZ').read_bytes()
        for image in range(16):
            patches = decode_grz(data, image)
            self.assertTrue(patches)
            self.assertEqual([p[0] for p in patches], sorted(set(p[0] for p in patches)))
        # grx_put must use stream0 of images0..6, NOT all16 archive frames.
        for image, count in enumerate((112, 272, 1672, 2180, 2718, 3358, 1348)):
            self.assertEqual(len(decode_grz(data, image)), count)
        for bad in (b'', data[:95], b'xxxx' + data[4:], data[:-1]):
            with self.assertRaises(ValueError):
                decode_grz(bad, 0)
        bad = bytearray(data)
        bad[4] = 17
        with self.assertRaises(ValueError):
            decode_grz(bad, 0)
        bad = bytearray(data)
        struct.pack_into('<I', bad, 8, len(data))
        with self.assertRaises(ValueError):
            decode_grz(bad, 0)

    def test_bos_alpha_does_not_reserve_color15(self):
        # Synthetic white pixel proves alpha is independent of the color nibble.
        area = 10 * 32
        data = bytearray(64 + area * 5)
        data[:4] = b'BOSS'
        data[4], data[6], data[8] = 10, 32, 1
        data[64:64 + area] = b'\xff' * area
        data[64] = 127
        for plane in range(1, 5):
            data[64 + plane * area] = 128
        records = bos_patches(data, 280, 128, 32, 1)
        self.assertEqual(records, [struct.pack('<HBBI', 128 * 80 + 35, 128, 0, 15)])

    def test_packaged_asset_and_source_provenance(self):
        data = (ROOT / 'nitrofs/konngara_art.bin').read_bytes()
        self.assertEqual(data, build(ROOT / 'assets/hdi_dump', ROOT / 'assets/packfile'))
        self.assertEqual(struct.unpack_from('<4sHHHHI', data), (b'KGA1', 640, 400, 19, 0, len(data)))
        source = (ROOT / 'source/main.cpp').read_text()
        self.assertNotIn('konngara_art_preview()',source)
        self.assertNotIn('konngara_combat_review(',source)
        self.assertIn('stage_has_konngara', (ROOT / 'source/engine/main/progression.hpp').read_text())

    def test_phase1_cache_and_mask_guards(self):
        art = (ROOT / 'nitrofs/konngara_art.bin').read_bytes()
        cache = (ROOT / 'nitrofs/konngara_combat.bin').read_bytes()
        self.assertEqual(struct.unpack_from('<4sHHHHI', cache), (b'KGC2', 320, 192, 16, 191, 287920))
        oracle = SourcePage()
        self.assertEqual(struct.unpack_from('<16H', cache, 16),
                         tuple(sum((v * 17 >> 3) << (c * 5) for c, v in enumerate(color)) for color in oracle.palette))
        # Changed head or face masks must fail instead of producing an
        # independent-pose cache that differs from chronological source VRAM.
        for index in (1, 4):
            bad = bytearray(art)
            offset = struct.unpack_from('<I', bad, 64 + index * 8)[0]
            bad[offset + 2] ^= 1
            with self.assertRaises(ValueError):
                build_combat(bad)


def run_renderer(binary):
    runtime = ROOT / 'build/konngara/art-tests/runtime'
    destination = runtime / 'nitro:'
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'nitrofs/konngara_art.bin', destination / 'konngara_art.bin')
    shutil.copyfile(ROOT / 'nitrofs/konngara_combat.bin', destination / 'konngara_combat.bin')
    shutil.copyfile(ROOT / 'nitrofs/konngara_slash.bin', destination / 'konngara_slash.bin')
    shutil.copyfile(ROOT / 'nitrofs/konngara_defeat.bin', destination / 'konngara_defeat.bin')
    oracle = SourcePage()
    commands = [(0, 0)]
    for direction in range(3):
        commands += [(1, direction)] + [(2, expression) for expression in range(4)]
    commands += [(0, 0), (2, 1)] + [(3, cel) for cel in range(7)]
    commands += [(2, 3), (1, 1), (2, 0), (3, 2), (2, 1), (3, 6)]
    commands += [(0, 0), (4, 1), (4, 0)]
    with (runtime / 'commands.bin').open('wb') as out:
        out.write(struct.pack('<I', len(commands)))
        for op, arg in commands:
            oracle.command(op, arg)
            out.write(struct.pack('<II', op, arg))
            out.write(oracle.frame())
    oracle.reset()
    with (runtime / 'preview.bin').open('wb') as out:
        for direction in range(3):
            oracle.command(1, direction)
            for expression in range(4):
                oracle.command(2, expression)
                out.write(oracle.frame())
        oracle.reset()
        out.write(oracle.frame())
        oracle.command(2, 1)
        out.write(oracle.frame())
        for cel in range(7):
            oracle.command(3, cel)
            out.write(oracle.frame())
    # A second independent chronological oracle for the optimized hardware
    # composition, including all four initial-GRP-head expressions and both
    # palettes. Same BOS decoder as gallery, never the production GPU builder.
    combat = [(0, 0)]
    for expression in (1, 2, 3, 0):
        combat += [(2, expression), (4, 1), (4, 0)]
    for direction in range(3):
        combat += [(1, direction)]
        for expression in range(4):
            combat += [(2, expression), (4, 1), (4, 0)]
    combat += [(0, 0), (2, 3), (1, 0), (2, 1), (1, 2), (2, 0)]
    for white in range(15, -1, -1):
        combat += [(5, white), (4, 1), (4, 0)]
    oracle.reset()
    with (runtime / 'combat.bin').open('wb') as out:
        out.write(struct.pack('<I', len(combat)))
        for op, arg in combat:
            oracle.command(op, arg)
            out.write(struct.pack('<II', op, arg))
            out.write(oracle.frame())
    # First and repeat slash passes have different persistent arena pixels.
    # Cover both expressions on both backgrounds; do not regenerate reference
    # images from the production indexed/RLE cache.
    sequence = []
    for first_expression in (1,3):
        sequence += [(0,0),(1,0),(1,2)]
        for expression in (first_expression,4-first_expression):
            sequence += [(2,expression)]
            for cel in range(7): sequence += [(3,cel),(4,1),(4,0)]
            sequence += [(2,0)]
            for direction in (0,1,2): sequence += [(1,direction),(2,3),(2,0)]
    oracle.reset()
    with (runtime / 'slash.bin').open('wb') as out:
        out.write(struct.pack('<I',len(sequence)))
        for op,arg in sequence:
            oracle.command(op,arg)
            out.write(struct.pack('<II',op,arg)); out.write(oracle.frame())
    # Sparse full-pixel checkpoints for the entire421-frame source defeat.
    # GRZ spans are decoded independently by SourcePage, not the KGD1 builder.
    combat=[c[:] for c in oracle.palette]
    unflashed=[c[:] for c in combat]
    for i in (3,4,5): combat[i]=[15,15,15]
    records=[]
    for page in range(9):
        oracle.page=bytearray(640*400);oracle.command(3,page+7)
        colors=[c[:] for c in combat];colors[0]=[15 if page%2 else 0]*3
        records.append((page*10,oracle.frame(colors)))
        if page==0:
            with (runtime/'defeat_normal.bin').open('wb') as out:
                out.write(struct.pack('<II',1,0));out.write(oracle.frame(unflashed))
    for frame in (1,4,59,60,64,65):
        colors=[[max(0,c-frame//4) for c in rgb] for rgb in combat];colors[0]=[0,0,0]
        records.append((90+frame-1,oracle.frame(colors,(408+(frame%2)*16)%400)))
    rows,palette,_=decode_grp((ROOT/'assets/hdi_dump/BOSS7_D1.GRP').read_bytes())
    oracle.page=bytearray(p for row in rows for p in row)
    target=[[c//17 for c in rgb] for rgb in palette]
    for frame in (0,8,112,144,152,192,216,256,264):
        ramp=min(15,frame//8+1);fade=0 if frame<152 else (frame-152)//8+1
        colors=[[max(0,min(c,ramp)-fade) for c in rgb] for rgb in target]
        records.append((155+frame,oracle.frame(colors,(frame*32)%400)))
    records.append((420,oracle.frame([[0]*3 for _ in range(16)],0)))
    with (runtime/'defeat.bin').open('wb') as out:
        out.write(struct.pack('<I',len(records)))
        for tick,pixels in records: out.write(struct.pack('<I',tick));out.write(pixels)
    if binary is not None:
        subprocess.run([str(Path(binary).resolve())], cwd=runtime, check=True)


if __name__ == '__main__':
    binary = None
    if '--run' in sys.argv:
        index = sys.argv.index('--run')
        binary = sys.argv[index + 1]
        del sys.argv[index:index + 2]
    result = unittest.main(exit=False)
    if not result.result.wasSuccessful():
        sys.exit(1)
    if binary:
        run_renderer(binary)
