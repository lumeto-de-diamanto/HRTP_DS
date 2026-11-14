#!/usr/bin/env python3
"""Synthetic-only native PCM import tests; no Yamaha waveform is included."""
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
import wave

from import_opna_rhythm import DRUMS, load_rhythm


def fixture(directory, *, silent=False):
    for i, (_, name) in enumerate(DRUMS):
        amplitude = 0 if silent else 1000 * (i + 1)
        with wave.open(str(directory / f'2608_{name}.WAV'), 'wb') as f:
            f.setparams((1, 2, 22050, 0, 'NONE', 'not compressed'))
            f.writeframes(struct.pack('<128h', *([amplitude, -amplitude] * 64)))


class RhythmTests(unittest.TestCase):
    def test_common_gain_preserves_drum_level_differences(self):
        with tempfile.TemporaryDirectory() as d:
            directory = Path(d); fixture(directory)
            before = {p.name: p.read_bytes() for p in directory.iterdir()}
            samples = load_rhythm(directory)
            self.assertEqual(len(samples), 6)
            self.assertEqual(samples[36]['pcm'][0], 4)
            self.assertEqual(samples[37]['pcm'][0], 23)
            self.assertEqual(samples[36]['rate'], 22050)
            self.assertEqual(before, {p.name: p.read_bytes() for p in directory.iterdir()})
            for sample in samples.values():
                self.assertEqual(len(sample['pcm']) % 4, 0)
                self.assertEqual(sample['source_sha256'],
                                 hashlib.sha256(before[sample['source_file']]).hexdigest())
                self.assertEqual(sample['pcm_sha256'], hashlib.sha256(sample['pcm']).hexdigest())

    def test_lowercase_user_filenames_are_accepted_without_renaming(self):
        with tempfile.TemporaryDirectory() as d:
            directory = Path(d); fixture(directory)
            for p in directory.glob('*.WAV'):
                p.rename(p.with_name(p.name.lower()))
            samples = load_rhythm(directory)
            self.assertEqual(samples[36]['source_file'], '2608_bd.wav')
            self.assertTrue((directory / '2608_bd.wav').is_file())

    def test_silent_input_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            directory = Path(d); fixture(directory, silent=True)
            with self.assertRaisesRegex(ValueError, 'silent'):
                load_rhythm(directory)


if __name__ == '__main__':
    unittest.main()
