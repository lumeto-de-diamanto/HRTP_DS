#!/usr/bin/env python3
"""Check generated SinGyoku BOS/OAM assets and required route glyphs."""
from pathlib import Path
import re
import struct
import sys
import unittest

from bos_to_h import decode_bos, pack_frame_4bpp, pad_image
from halve_image import shrink_image
from nds_files import nitro_files


class BossAssetTests(unittest.TestCase):
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
    if '--rom' in sys.argv:
        sys.argv.remove('--rom')
        rom = Path('hrtp_ds.nds').read_bytes()
        assert nitro_files(rom).get('boss1.bin') == Path('nitrofs/boss1.bin').read_bytes()
        offset = struct.unpack_from('<I', rom, 0x20)[0]
        size = struct.unpack_from('<I', rom, 0x2c)[0]
        arm9 = rom[offset:offset + size]
        for name in ('SingyokuSphere', 'SingyokuFlash', 'SingyokuPerson'):
            text = (Path('assets/processed/sprites') / f'{name}.h').read_text()
            words = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', text)]
            assert struct.pack('<' + 'I' * len(words), *words) in arm9, f'missing/stale {name} in ARM9'
        print('Game ROM: exact SinGyoku background and all three BOS sheets verified')
    unittest.main()
