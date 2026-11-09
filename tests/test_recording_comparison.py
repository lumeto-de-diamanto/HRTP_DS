#!/usr/bin/env python3
"""Host-analysis regressions, using synthetic matrices, not captured music."""
import unittest

import numpy as np

from compare_audio_recordings import CENTERS, HOP, RATE, align, nnls, dominant_band_comparison


class RecordingAnalysisTests(unittest.TestCase):
    def test_nonnegative_fit_recovers_known_power_gains(self):
        rng = np.random.default_rng(274)
        matrix = rng.random((400, 7))
        expected = np.array([.1, 2, 0, 4, .5, .75, 1])
        actual, _ = nnls(matrix, matrix @ expected)
        np.testing.assert_allclose(actual, expected, atol=1e-6)

    def test_alignment_is_invariant_to_constant_capture_gain(self):
        rng = np.random.default_rng(890)
        template = np.exp(rng.normal(size=(160, len(CENTERS))))
        offset = 18
        query = template[offset:offset + 100] * 12.5
        result = align(template, query)
        self.assertAlmostEqual(result["time_scale"], 1, places=6)
        self.assertAlmostEqual(result["stem_shift_seconds"], offset * HOP / RATE, places=5)
        self.assertGreater(result["cosine_score"], .999)

    def test_band_ratios_remove_capture_gain_but_keep_relative_ssg_error(self):
        basis = np.zeros((150, 7, len(CENTERS)))
        for group in range(7):
            basis[:, group, 10 + group * 10] = 1
        original = basis.sum(axis=1)
        changed = basis.copy()
        changed[:, 4:6] *= 2  # +3.0103 dB power in both SSG channels
        ds = changed.sum(axis=1) * 6  # unrelated common capture gain
        identity = dict(stem_shift_seconds=0, time_scale=1)
        report = dominant_band_comparison(basis, {"original": original, "ds": ds},
            {"original": identity, "ds": identity}, 1.5)
        for group in ("S1", "S2"):
            self.assertAlmostEqual(report["all"][group]["relative_to_fm_db"], 10 * np.log10(2), places=6)


if __name__ == "__main__":
    unittest.main()
