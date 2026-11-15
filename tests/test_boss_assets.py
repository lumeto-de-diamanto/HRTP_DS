#!/usr/bin/env python3
"""Check generated SinGyoku BOS/OAM assets and required route glyphs."""
from pathlib import Path
import re
import tempfile
import unittest

from bos_to_h import decode_bos, pack_frame_4bpp, pad_image
from halve_image import shrink_image
from orb_arrow_to_h import convert, load_arrow


class BossAssetTests(unittest.TestCase):
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
