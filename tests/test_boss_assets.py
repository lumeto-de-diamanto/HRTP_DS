#!/usr/bin/env python3
"""Check generated SinGyoku BOS/OAM assets and required route glyphs."""
from pathlib import Path
import re
import tempfile
import struct
import hashlib
import unittest

from PIL import Image

from bos_to_h import decode_bos, pack_frame_4bpp, pad_image
from halve_image import shrink_image
from orb_arrow_to_h import convert, load_arrow
from grp_to_bin import decode_grp, halve_background, pack_palette
from ptn_to_h import decode_ptn
from mono_bmp_to_h import load_bmp


class BossAssetTests(unittest.TestCase):
    def test_mima_stable_dither_exact_block_reuse(self):
        from mima_to_bin import shrink_animation
        base = [[(x+2*y) % 7+1 for x in range(8)] for y in range(8)]
        frame = [row[:] for row in base]
        # Change EACH possible position in four different2x2 source blocks.
        for block, (dx, dy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
            frame[dy][block*2+dx] = 9
        # Same histogram, different positions must NOT reuse the base pixel.
        frame[2][0], frame[3][1] = frame[3][1], frame[2][0]
        # Transparent zeros are compared too, not treated as "don't care".
        for y in (2, 3):
            frame[y][2:4] = [0, 0]
        for y in (4, 5):
            base[y][0:2] = frame[y][0:2] = [0, 0]
        before = [[row[:] for row in image] for image in (base, frame)]
        # Distinct cached values make accidental reuse on any changed block
        # observable, even when both normal dithers happen to pick one color.
        cached = [[15]*4 for _ in range(4)]
        ordinary = shrink_image(frame, 'dither')
        stable = shrink_animation(base, frame, cached)
        for y in range(4):
            for x in range(4):
                same = all(frame[y*2+dy][x*2+dx] == base[y*2+dy][x*2+dx]
                           for dy in (0, 1) for dx in (0, 1))
                self.assertEqual(stable[y][x], 15 if same else ordinary[y][x], (x, y))
        self.assertEqual(stable[2][0], 15)  # fully transparent, identical block
        self.assertEqual(shrink_animation(base, base, cached), cached)
        self.assertEqual([base, frame], before)
        self.assertEqual(cached, [[15]*4 for _ in range(4)])

    def test_mima_meteor_comparison_preserves_current_art(self):
        from mima_shrink_compare import METHODS, export_comparison, load_comparison
        packfile, background = Path('assets/packfile'), Path('assets/hdi_dump/BOSS3.GRP')
        paths = [packfile / 'BOSS3_1.BOS', packfile / 'BOSS3_2.BOS', background,
                 Path('nitrofs/mima_art.bin'), Path('assets/font.bmp'),
                 Path('assets/sprites/orb_arrow.xpm'), Path('assets/sprites/boss_arrow.xpm')]
        before = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        pages, methods, palette, _, _ = load_comparison(packfile, background)
        self.assertEqual(set(methods), {'dither', 'stable_dither', 'topleft', 'topleft2', 'stagger', 'salient', 'rare'})
        data = Path('nitrofs/mima_art.bin').read_bytes()
        body, _ = decode_bos((packfile / 'BOSS3_1.BOS').read_bytes())
        base = body[0] + [[0]*128 for _ in range(32)]
        base_half = shrink_image(base, 'dither')
        for method, frames in methods.items():
            self.assertEqual(len(frames), 4)
            for cel, frame in enumerate(frames):
                self.assertEqual((len(frame[0]), len(frame)), (64, 96))
                self.assertTrue(all(0 <= pixel < 16 for row in frame for pixel in row))
                if method == 'stable_dither':
                    ordinary = shrink_image(pages[cel], 'dither')
                    for y in range(96):
                        for x in range(64):
                            same = all(pages[cel][y*2+dy][x*2+dx] == base[y*2+dy][x*2+dx]
                                       for dy in (0, 1) for dx in (0, 1))
                            self.assertEqual(frame[y][x], base_half[y][x] if same else ordinary[y][x])
                    self.assertEqual(tuple(pack_frame_4bpp(frame)), struct.unpack_from('<768I', data, 48+(43+cel)*3072))
                else:
                    self.assertEqual(frame, shrink_image(pages[cel], method, palette))
        with tempfile.TemporaryDirectory(prefix='mima-compare-', dir='/tmp/opencode') as directory:
            output = Path(directory)
            report = export_comparison(packfile, background, output)
            self.assertEqual(report['asset_frames'], [43, 44, 45, 46])
            self.assertEqual(report['source_frames_per_cel'], 8)
            self.assertEqual(set(report['methods']), set(METHODS))
            self.assertEqual(report['current_method'], 'stable_dither')
            for method in ('stable_dither', 'topleft', 'topleft2', 'stagger', 'salient'):
                self.assertEqual(report['methods'][method]['unchanged_body_pixel_changes'], [0]*4)
            html = (output / 'index.html').read_text()
            self.assertIn('requestAnimationFrame(tick)', html)
            self.assertIn('8*1000/60', html)
            self.assertIn('data:image/png;base64,', html)
            self.assertNotIn('https://', html)  # self-contained offline viewer
            with Image.open(output / 'first-frame.png') as sheet, Image.open(output / 'comparison.gif') as gif:
                self.assertEqual(gif.n_frames, 4)
                self.assertEqual(gif.info['loop'], 0)
                self.assertEqual(gif.size, sheet.size)
                self.assertEqual(gif.convert('RGB').tobytes(), sheet.convert('RGB').tobytes())
                self.assertEqual([gif.seek(i) or gif.info['duration'] for i in range(4)], [130, 140, 130, 130])
        self.assertEqual(before, {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths})

    def test_mima_defeat_original_rgb4_and_pixel_signatures(self):
        image,raw,_=decode_grp(Path('assets/hdi_dump/BOSS3.GRP').read_bytes())
        indices,colors=halve_background(image,raw)
        text=Path('assets/processed/backgrounds/MimaDefeatPalette.h').read_text()
        rgb=text.split('= {',1)[1].split('};',1)[0]
        self.assertEqual(list(map(int,re.findall(r'\b\d+\b',rgb))),[c//17 for entry in raw for c in entry])
        signatures=[int(n,16) for n in re.findall(r'0x[0-9A-F]+',text)]
        self.assertEqual(len(signatures),len(colors))
        for i in range(16): self.assertEqual(signatures[i],i*0x1111)
        used=set()
        for y,row in enumerate(indices):
            for x,index in enumerate(row):
                values=sorted(image[y*2+a][x*2+b] for a in range(2) for b in range(2))
                if index>=16:self.assertEqual(signatures[index],sum(v<<(p*4) for p,v in enumerate(values)))
                used.add(index)
        for i in range(16,len(colors)):
            if i not in used:self.assertEqual(signatures[i],0x10000)

    def test_mima_original_pillar_masks(self):
        path=Path('assets/sprites/pillar.bmp')
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                         'f1308d9e2264743755121cf7beeedfd983763f1cb9fcd25b17d10f74629e4915')
        page,w,h=load_bmp(path);self.assertEqual((w,h),(64,8))
        expected=[p for x in (0,32) for row in shrink_image([row[x:x+32] for row in page],'rare') for p in row]
        text=Path('assets/processed/sprites/Pillar.h').read_text().split('= {',1)[1].split('};',1)[0]
        self.assertEqual(list(map(int,re.findall(r'\b\d+\b',text))),expected)

    def test_mima_missiles_and_original_cloud_art(self):
        _, pages=decode_ptn(Path('assets/packfile/BOSS3_M.PTN').read_bytes())
        expected=[]
        for page in pages:
            for y,x in ((0,0),(0,16),(16,0),(16,16)):
                cel=shrink_image([row[x:x+16] for row in page[y:y+16]],'dither')
                expected.extend(p for row in cel for p in row)
        text=Path('assets/processed/sprites/Missile.h').read_text().split('= {',1)[1].split('};',1)[0]
        self.assertEqual(list(map(int,re.findall(r'\b\d+\b',text))),expected)
        self.assertEqual(len(expected),20*64)
        path=Path('assets/sprites/pellet_cloud.bmp')
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                         '76d936fb7227ea41e7085962d16b694fec8284dd7bfe30fa051ecbf76d07380a')
        page,w,h=load_bmp(path);self.assertEqual((w,h),(32,16))
        expected=[p for x in (0,16) for row in shrink_image([row[x:x+16] for row in page],'rare') for p in row]
        text=Path('assets/processed/sprites/PelletCloud.h').read_text().split('= {',1)[1].split('};',1)[0]
        self.assertEqual(list(map(int,re.findall(r'\b\d+\b',text))),expected)

    def test_mima_exact_local_source_palette_effects(self):
        image,raw,_=decode_grp(Path('assets/hdi_dump/BOSS3.GRP').read_bytes())
        indices,normal=halve_background(image,raw)
        text=Path('assets/processed/backgrounds/MimaPaletteFx.h').read_text()
        colors=[int(v,16) for v in re.findall(r'0x[0-9A-F]+',text)]
        n=len(normal);self.assertEqual(len(colors),30*n)
        signatures={}
        for y,row in enumerate(indices):
            for x,index in enumerate(row):
                signature=tuple(sorted(image[y*2+a][x*2+b] for a in range(2) for b in range(2)))
                if index in signatures:self.assertEqual(signatures[index],signature)
                signatures[index]=signature
        for darkness in range(0,17,4):
            for flash in range(6):
                state=colors[(darkness//4*6+flash)*n:(darkness//4*6+flash+1)*n]
                for index in range(n):
                    if index>=16 and index not in signatures:
                        self.assertEqual(state[index],pack_palette([normal[index]])[0]);continue
                    pixels=[index] if index<16 else signatures[index]
                    rgb=[]
                    for c in range(3):
                        values=[max(0,15-(flash-1)*4)*17 if flash and p in (3,9) else max(0,raw[p][c]//17-darkness)*17 for p in pixels]
                        rgb.append(values[0] if index<16 else sum(v//4 for v in values))
                    self.assertEqual(state[index],pack_palette([rgb])[0])
        self.assertEqual(colors[:n],pack_palette(normal))

    def test_mima_all_source_rows_spreadin_and_animation_cels(self):
        data = Path('nitrofs/mima_art.bin').read_bytes()
        self.assertEqual(struct.unpack_from('<4sHHHHI', data),
                         (b'MIM1', 64, 96, 47, 3072, len(data)))
        self.assertEqual(len(data), 48 + 47 * 3072)
        _, colors, _ = decode_grp(Path('assets/hdi_dump/BOSS3.GRP').read_bytes())
        expected_palette = [0] + [(r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
                                  for r, g, b in colors[:15]]
        self.assertEqual(list(struct.unpack_from('<16H', data, 16)), expected_palette)
        body, size = decode_bos(Path('assets/packfile/BOSS3_1.BOS').read_bytes())
        cels, cel_size = decode_bos(Path('assets/packfile/BOSS3_2.BOS').read_bytes())
        self.assertEqual((size, len(body), cel_size, len(cels)), ((128, 160), 1, (128, 64), 5))
        base = body[0] + [[0]*128 for _ in range(32)]
        base_half = shrink_image(base, 'dither')
        restored = 0
        for frame in range(47):
            # Independent destination-row oracle for b10j.cpp:294..309.
            page = [[0] * 128 for _ in range(192)]
            for y in range(192):
                if frame <= 40:
                    n = frame * 2
                    row = y - (80 - n) if 80-n <= y < 80 else 79-n+y if 80 < y <= 80+n else None
                else:
                    row = y if y < 160 else None
                if row is not None:
                    page[y] = body[0][row][:]
            if frame >= 42:
                for y in range(64):
                    page[y+48] = [cels[frame-42][y][x] or page[y+48][x] for x in range(128)]
            half = shrink_image(page, 'dither')
            if frame >= 42:
                # Independent per-position oracle, not the converter helper:
                # unchanged four source indices reuse the cached base pixel;
                # changed blocks retain their normal per-frame dither result.
                for y in range(96):
                    for x in range(64):
                        same = all(page[y*2+dy][x*2+dx] == base[y*2+dy][x*2+dx]
                                   for dy in (0, 1) for dx in (0, 1))
                        if same:
                            restored += half[y][x] != base_half[y][x]
                            half[y][x] = base_half[y][x]
            expected = pack_frame_4bpp(half)
            self.assertEqual(struct.unpack_from('<768I', data, 48+frame*3072), tuple(expected), frame)
            if frame >= 41:
                self.assertTrue(any(p for row in half[64:80] for p in row), 'body below row64 must survive')
                self.assertFalse(any(p for row in half[80:] for p in row), 'lower OBJ padding must be transparent')
        self.assertGreater(restored, 0, 'fixture must actually exercise shimmer repair')

    def test_independent_boss_arrow_pixels_and_converter(self):
        rows = ('00000000', '00000000', '00000000', '33330000',
                '11113000', '01111300', '00111130', '00011113',
                '00011113', '00111130', '01111300', '11113000',
                '33330000', '00000000', '00000000', '00000000')
        source = Path('assets/sprites/boss_arrow.xpm')
        pixels = load_arrow(source, boss=True)
        self.assertEqual(pixels, [[int(p) for p in row] for row in rows])
        self.assertGreater(sum(row.count(1) for row in pixels), sum(row.count(3) for row in pixels))
        self.assertNotEqual(pixels, load_arrow(Path('assets/sprites/orb_arrow.xpm')))
        with self.assertRaises(ValueError):
            load_arrow(source)  # orb mode cannot silently remap purple to white
        with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
            output = Path(directory) / 'BossArrow.h'
            convert(source, output, boss=True)
            self.assertEqual(output.read_bytes(), Path('assets/processed/sprites/BossArrow.h').read_bytes())
            expected = [sum(int(p) << (x*4) for x, p in enumerate(row)) for row in rows]
            self.assertEqual([int(v, 16) for v in re.findall(r'0x[0-9A-F]+', output.read_text())], expected)
            invalid = Path(directory) / 'invalid.xpm'
            for text in (source.read_text().replace('#A020F0', '#FFFFFF'),
                         source.read_text().replace('"........",', '"Z.......",', 1),
                         source.read_text().replace('8 16 3 1', '8 15 3 1')):
                invalid.write_text(text)
                self.assertNotEqual(text, source.read_text(), 'invalid fixture must actually change the XPM')
                with self.assertRaises(ValueError):
                    convert(invalid, output, boss=True)

    def test_original_bos_frames_match_generated_64px_oam_payloads(self):
        for original, generated, count in (
                ('BOSS1.BOS', 'SingyokuSphere', 8),
                ('BOSS1_2.BOS', 'SingyokuFlash', 3),
                ('BOSS1_3.BOS', 'SingyokuPerson', 5)):
            with self.subTest(sheet=original):
                frames, size = decode_bos((Path('assets/packfile') / original).read_bytes())
                self.assertEqual(size, (96, 96))
                self.assertEqual(len(frames), count)
                expected = []
                for frame in frames:
                    half = shrink_image(frame, 'dither')
                    self.assertEqual((len(half[0]), len(half)), (48, 48))
                    padded = pad_image(half, (64, 64))
                    self.assertTrue(all(v == 0 for row in padded[:16] for v in row))
                    self.assertTrue(all(v == 0 for row in padded for v in row[:8] + row[56:]))
                    words = pack_frame_4bpp(padded)
                    self.assertEqual(len(words) * 4, 2048)
                    self.assertTrue(any(words))
                    expected.extend(words)
                text = (Path('assets/processed/sprites') / f'{generated}.h').read_text()
                actual = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', text)]
                self.assertEqual(actual, expected)

    def test_arena_background_and_route_glyphs_are_available(self):
        self.assertEqual(Path('nitrofs/boss1.bin').stat().st_size, 320 * 192)
        font = Path('assets/processed/fonts/font16x16.h').read_text().lower()
        for text in ('魔界へ', '地獄へ'):
            for char in text:
                code = int.from_bytes(char.encode('euc_jp'), 'big') - 0x8080
                self.assertIn(f'0x{code:04x}', font)


if __name__ == '__main__':
    unittest.main()
