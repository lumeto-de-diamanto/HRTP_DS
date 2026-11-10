#!/usr/bin/env python3
"""MDE static-interpretation and rendered-bank regressions, not PC-98 execution."""
import hashlib
import json
from pathlib import Path
import struct
import unittest

import numpy as np

from decode_mde import Bank, Envelope, PERIODS
from audio_mix import load_mix_profile


class DecoderTests(unittest.TestCase):
    def setUp(self):
        self.raw = Path('assets/hdi_dump/ZIGOKU.MDE').read_bytes()
        self.bank = Bank(self.raw)

    def test_original_bank_identity_and_all_entry_stream_counts(self):
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(),
            'f859ba43f53e17bc917ceb7d860b9455bad471fd5bd4b9f73511c08f832e4027')
        self.assertEqual(self.bank.count, 17)
        self.assertEqual([len(p) for p in self.bank.entries], [1] * 8 + [2] + [1] * 8)

    def test_all_entries_terminate_with_expected_attacks(self):
        expected = [1, 3, 1, 3, 3, 3, 2, 4, 4, 1, 1, 56, 64, 1, 15, 5, 10]
        for number, attacks in enumerate(expected, 1):
            with self.subTest(effect=number):
                decoded = self.bank.render_events(number)
                self.assertEqual(len(decoded['notes']), attacks)
                self.assertLess(decoded['ticks'], 256)
                final = {reg: value for _, reg, value in decoded['events'] if 8 <= reg <= 10}
                self.assertTrue(final and all(value == 0 for value in final.values()))

    def test_slide_uses_raw_signed_increment_before_octave_shift(self):
        d = self.bank.render_events(1)
        self.assertEqual(d['notes'][0]['slope'], 69)
        self.assertEqual(d['notes'][0]['base_period'], 2022)
        low = {t: v for t, r, v in d['events'] if r == 4}
        self.assertEqual(low[1], 2022 >> 4)
        self.assertEqual(low[2], (2022 + 69) >> 4)
        self.assertEqual(low[12], ((2022 + 69 * 11) >> 4) & 255)
        self.assertEqual(self.bank.render_events(17)['notes'][0]['slope'], -299)

    def test_periods_match_shipped_driver_table(self):
        driver = Path('assets/hdi_dump/MDRV98.COM').read_bytes()
        self.assertEqual(struct.unpack_from('<12H', driver, 0x14c - 0x100), PERIODS)

    def test_noise_raw_bytes_reach_chip_without_pitch_coupling(self):
        d = self.bank.render_events(14)
        self.assertIn((1, 6, 50), d['events'])
        d = self.bank.render_events(17)
        self.assertIn((0, 6, 100), d['events'])

    def test_two_stream_effect_retains_both_voices_in_register_render(self):
        d = self.bank.render_events(9)
        self.assertEqual({n['channel'] for n in d['notes']}, {1, 2})
        self.assertEqual([e for e in d['events'] if e[0] == 0 and e[1] == 7][-1][2], 0x0f)

    def test_zero_release_holds_until_end_or_retrigger_as_driver_does(self):
        e = Envelope((255, 255, 0, 255, 0, 0))
        e.keyon(); e.step(); e.step(); e.keyoff()
        for _ in range(100): e.step()
        self.assertEqual(e.level, 255)
        self.assertTrue(e.active)

    def test_all_six_envelope_bytes_keep_distinct_roles(self):
        e = Envelope((20, 100, 10, 200, 3, 7))
        e.keyon()
        levels = []
        for _ in range(4): e.step(); levels.append(e.level)
        self.assertEqual(levels, [120, 220, 255, 245])
        e.keyoff(); e.step()
        self.assertEqual(e.level, 238)

    def test_unknown_opcode_or_nonzero_lfo_rejected(self):
        for offset, value in ((0x56, 0xee), (0x59, 1)):
            raw = bytearray(self.raw); raw[offset] = value
            with self.assertRaises(ValueError): Bank(raw).render_events(1)

    def test_invalid_ranges_loops_and_truncation_rejected(self):
        mutations = []
        for pos, value in ((4, 0), (18, 0), (0x1a7, 0)):
            raw = bytearray(self.raw); raw[pos] = value; mutations.append(raw)
        mutations.append(self.raw[:-1])
        for raw in mutations:
            with self.subTest(raw_size=len(raw)), self.assertRaises(ValueError):
                bank = Bank(raw)
                for i in range(1, bank.count + 1): bank.render_events(i)


class RenderedBankTests(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads(Path('build/native_sfx/manifest.json').read_text())
        self.blob = Path('nitrofs/audio/sfx.bin').read_bytes()

    def pcm(self, number):
        return np.fromfile(f'build/native_sfx/sfx_{number:02}.pcm', dtype='<i2').astype(float)

    def test_resident_bank_layout_identity_and_mix_once(self):
        magic, version, size, count, table, period, gain = struct.unpack_from('<8s6I', self.blob)
        self.assertEqual((magic, version, size, count, table), (b'HRNSFX1\0', 1, len(self.blob), 17, 32))
        self.assertEqual(period, 760)
        self.assertEqual(gain, round(65535 * load_mix_profile()['ssg_global_volume'] / 64))
        self.assertEqual(hashlib.sha256(self.blob).hexdigest(), self.manifest['bank_sha256'])
        self.assertLess(len(self.blob), 1024 * 1024)
        for effect in self.manifest['effects']:
            offset, length = struct.unpack_from('<II', self.blob, table + (effect['id'] - 1) * 8)
            self.assertEqual(offset % 4, 0)
            self.assertEqual(length % 4, 0)
            pcm = self.blob[offset:offset + length]
            self.assertEqual(hashlib.sha256(pcm).hexdigest(), effect['pcm_sha256'])
            self.assertEqual(pcm, Path(f'build/native_sfx/sfx_{effect["id"]:02}.pcm').read_bytes())
            self.assertGreater(effect['peak'], 0)
            self.assertLess(effect['peak'], 32767)

    def test_common_gain_preserves_noise_volume_ratio(self):
        full, quiet = self.pcm(10), self.pcm(11)
        end = min(len(full), len(quiet), 10000)
        a, b = full[1000:end], quiet[1000:end]
        ratio = np.sqrt(np.mean(b * b) / np.mean(a * a))
        self.assertAlmostEqual(ratio, 6006 / 16382, delta=.003)

    def test_two_layer_effect_is_mixed_not_individually_normalized(self):
        one, two = self.pcm(7), self.pcm(9)
        self.assertEqual(len(one), len(two))
        np.testing.assert_allclose(two, 2 * one, atol=5)


if __name__ == '__main__':
    unittest.main()
