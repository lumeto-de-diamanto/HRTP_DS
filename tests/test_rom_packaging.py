#!/usr/bin/env python3
"""DS-only header/integrity checks and exact generated NitroFS/font payloads."""
from pathlib import Path
import re
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from nds_files import nitro_files


def crc16(data):
    value = 0xffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xa001 if value & 1 else 0)
    return value


class RomPackagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = Path('hrtp_ds.nds').read_bytes()

    def test_ds_only_header_and_checksums(self):
        self.assertEqual(self.rom[0x12], 0, 'ROM must identify as DS-only, not DS+DSi')
        self.assertEqual(struct.unpack_from('<I', self.rom, 0x84)[0], 0x200)
        for start, end, checksum in ((0, 0x15e, 0x15e), (0xc0, 0x15c, 0x15c)):
            self.assertEqual(crc16(self.rom[start:end]),
                             struct.unpack_from('<H', self.rom, checksum)[0])

    def test_executables_fit_original_ds_main_ram(self):
        regions = []
        for cpu, offset in ((9, 0x20), (7, 0x30)):
            position, entry, address, size = struct.unpack_from('<4I', self.rom, offset)
            self.assertGreater(size, 0)
            self.assertGreaterEqual(position, 0x200)
            self.assertLessEqual(position + size, len(self.rom))
            self.assertGreaterEqual(address, 0x02000000)
            self.assertLessEqual(address + size, 0x02400000)
            self.assertTrue(address <= entry < address + size, f'ARM{cpu} entry')
            regions.append((address, address + size))
        self.assertLessEqual(regions[0][1], regions[1][0])

    def test_every_staged_nitrofs_file_is_exact(self):
        expected = {str(p.relative_to('nitrofs')): p.read_bytes()
                    for p in Path('nitrofs').rglob('*') if p.is_file()}
        self.assertTrue(expected)
        self.assertEqual(nitro_files(self.rom), expected)

    def test_complete_font_is_linked_into_arm9(self):
        text = Path('assets/processed/fonts/font16x16.h').read_text()
        self.assertIn('FONT16X16_GLYPH_COUNT 272', text)
        codes = [int(v, 16) for v in re.findall(r'0x([0-9A-F]{4}), //', text)]
        rows = text.split('font16x16_rows', 1)[1]
        pixels = [int(v, 16) for v in re.findall(r'0x([0-9A-F]{4})', re.sub(r'//[^\n]*', '', rows))]
        position, _, _, size = struct.unpack_from('<4I', self.rom, 0x20)
        arm9 = self.rom[position:position + size]
        for values in (codes, pixels):
            self.assertTrue(values)
            self.assertIn(struct.pack('<' + 'H' * len(values), *values), arm9)


if __name__ == '__main__':
    unittest.main()
