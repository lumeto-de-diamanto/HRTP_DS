#!/usr/bin/env python3
"""The production font must be complete without any reference checkout."""
from pathlib import Path
import re
import subprocess
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from font16x16 import glyph_rows, load_bitmap, required_chars, sjis_to_jis
from font16x16_chars import PRODUCTION_CHARS
from results_serif_to_h import ALPHABET, build as build_serif, glyphs as serif_glyphs
from test_viewport_assets import results_font_bytes, results_font_strips

ROOT = Path(__file__).resolve().parents[1]


class FontTests(unittest.TestCase):
    def test_results_serif_bitmap_inventory_and_exact_rows(self):
        text = (ROOT / 'assets/fonts/results_serif.bdf').read_text()
        strips = serif_glyphs(text)
        self.assertEqual(ALPHABET, '0123456789')
        self.assertEqual(len(strips), 18)
        self.assertEqual(strips, results_font_strips(ROOT / 'assets/fonts/results_serif.bdf'))
        self.assertTrue(all(len(cell) == 16 for strip in strips for cell in strip))
        digits = [strip[0] for strip in strips[:10]]
        self.assertEqual(len({tuple(glyph) for glyph in digits}), 10)
        # Prevent the previous 9x11 numeral regression: full 16-row height,
        # naturally narrower 1, and a separating column between wide digits.
        self.assertTrue(all(all(glyph) for glyph in digits))
        widths = []
        for glyph in digits:
            bits = 0
            for row in glyph:
                bits |= row
            widths.append(bits.bit_length() - (bits & -bits).bit_length() + 1)
        self.assertEqual(widths, [15, 11, 13, 13, 15, 13, 13, 13, 13, 13])
        self.assertTrue(all(not row & 1 for glyph in digits for row in glyph))
        for label in ('Time', 'Max Combo', 'Bomb/Life', 'Clear', 'Total', 'A or B'):
            self.assertIn('COMMENT ' + label + '\n', text)
        header = build_serif(text)
        body = header.split('ResultsSerif_rows', 1)[1]
        values = [int(v, 16) for v in re.findall(r'0x[0-9A-F]{4}', body)]
        self.assertEqual(struct.pack('<' + 'H' * len(values), *values),
                         results_font_bytes(ROOT / 'assets/fonts/results_serif.bdf'))
        self.assertIn('SIL OFL 1.1', header)
        for bad in [text.replace('CHARS 18', 'CHARS 19'),
                    text.replace('FONT_ASCENT 16', 'FONT_ASCENT 12'),
                    text.replace('ENCODING 48', 'ENCODING 49'),
                    text.replace('BBX 16 16 0 0', 'BBX 17 16 0 0', 1),
                    text.replace('DWIDTH 16 0', 'DWIDTH 32 0', 1),
                    text.replace('ENCODING 57344', 'ENCODING 57444'),
                    text.replace('ENDFONT', ''),
                    text.replace('BITMAP\n', 'BITMAP\nFFFF\n', 1)]:
            with self.assertRaises(ValueError):
                build_serif(bad)

    def test_results_serif_cli_has_no_system_font_or_freetype_dependency(self):
        with tempfile.TemporaryDirectory(prefix='hrtp-serif-', dir='/tmp/opencode') as directory:
            out = Path(directory) / 'ResultsSerif.h'
            command = [sys.executable, '-B', str(ROOT / 'tools/results_serif_to_h.py'),
                       str(ROOT / 'assets/fonts/results_serif.bdf'), str(out)]
            for _ in range(2):
                result = subprocess.run(command, cwd=directory, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(out.read_text(), build_serif((ROOT / 'assets/fonts/results_serif.bdf').read_text()))
            expected = out.read_bytes()
            command[-2] = str(Path(directory) / 'missing.bdf')
            result = subprocess.run(command, cwd=directory, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('error:', result.stderr)
            self.assertEqual(out.read_bytes(), expected)

    def test_explicit_inventory_includes_current_and_future_screens(self):
        chars = required_chars()
        self.assertEqual(len(chars), 272)
        self.assertEqual(len(PRODUCTION_CHARS), len(chars))
        for text in ('東方★靈異伝', '魔界へ', '地獄へ', '陰陽', '霊靈',
                     '０１２３４５６７８９', 'ＡＢＣＸＹＺａｂｃｘｙｚ'):
            self.assertTrue(set(text) <= chars, text)
        codes = [sjis_to_jis(c.encode('cp932')) for c in chars]
        self.assertEqual(len(set(codes)), 272)
        for code in codes:
            self.assertTrue(0x21 <= code >> 8 <= 0x7e)
            self.assertTrue(0x21 <= code & 255 <= 0x7e)

    def test_jis_mapping_and_essential_artwork(self):
        data, offset = load_bitmap(ROOT / 'assets/font.bmp')
        for char in '東方★靈異伝魔界へ地獄０９ＡＺａｚ':
            # Independent standard codec oracle, not the converter's SJIS math.
            code = int.from_bytes(char.encode('euc_jp'), 'big') - 0x8080
            self.assertEqual(sjis_to_jis(char.encode('cp932')), code)
            self.assertTrue(any(glyph_rows(data, offset, code)), char)
        self.assertEqual(glyph_rows(data, offset, 0x2121), [0] * 16)

    def test_cli_needs_only_sheet_and_output_directory(self):
        with tempfile.TemporaryDirectory(prefix='hrtp-font-', dir='/tmp/opencode') as directory:
            out = Path(directory)
            command = [sys.executable, '-B', str(ROOT / 'tools/font16x16.py'),
                       str(ROOT / 'assets/font.bmp'), str(out)]
            result = subprocess.run(command, cwd=out, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            generated = (out / 'font16x16.h').read_bytes()
            self.assertIn(b'#define FONT16X16_GLYPH_COUNT 272', generated)
            codes = re.findall(rb'0x([0-9A-F]{4}), //', generated)
            self.assertEqual(len(codes), 272)
            self.assertEqual(codes, sorted(set(codes)))
            subprocess.run(command, cwd=out, check=True, capture_output=True)
            self.assertEqual(generated, (out / 'font16x16.h').read_bytes())
            command[-2] = str(out / 'missing.bmp')
            result = subprocess.run(command, cwd=out, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('error:', result.stderr)
            self.assertEqual(generated, (out / 'font16x16.h').read_bytes())


if __name__ == '__main__':
    unittest.main()
