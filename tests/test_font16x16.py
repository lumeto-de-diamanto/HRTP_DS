#!/usr/bin/env python3
"""The production font must be complete without any reference checkout."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from font16x16 import glyph_rows, load_bitmap, required_chars, sjis_to_jis
from font16x16_chars import PRODUCTION_CHARS

ROOT = Path(__file__).resolve().parents[1]


class FontTests(unittest.TestCase):
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
