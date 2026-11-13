#!/usr/bin/env python3
"""Feasibility regressions only: does not replace/rebuild the game renderer."""
from pathlib import Path
import unittest

from analyze_konngara_hybrid import prototype, compose, verify, SHAPES
from tests.test_konngara_art_assets import SourcePage

ROOT = Path(__file__).resolve().parents[1]


class HybridInvestigation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model = prototype((ROOT/'nitrofs/konngara_art.bin').read_bytes())

    def test_legal_shapes_palettes_and_real_buffer_packing(self):
        self.assertLessEqual(len(self.model['bg_colors']),207)
        for frame in self.model['frames']:
            self.assertLessEqual(len(frame['colors']),144)
            self.assertLessEqual(len(frame['objects']),32)
            self.assertLessEqual(frame['bytes'],8192)
            used = [set(),set()]
            for obj in frame['objects']:
                self.assertIn((obj['w'],obj['h']),SHAPES)
                self.assertEqual(len(obj['texture']),obj['w']*obj['h'])
                self.assertEqual(obj['offset']%64,0)
                self.assertLessEqual(obj['offset']+len(obj['texture']),4096)
                region = set(range(obj['offset'],obj['offset']+len(obj['texture'])))
                self.assertFalse(region & used[obj['bank']])
                used[obj['bank']].update(region)
                self.assertTrue(all(p==0 or 112<=p<112+len(frame['colors']) for p in obj['texture']))
            self.assertLessEqual(75+len(frame['objects']),128) # unused high OAM slots; HUD ends74

    def test_first_background_patch_and_complete_composition(self):
        self.assertEqual(verify(self.model),28*2*320*192)

    def test_independent_source_bos_grz_pixels_first_and_repeat(self):
        frames = {(f['repeat'],f['expression'],f['cel']):f for f in self.model['frames']}
        source = SourcePage()
        for expression in (1,3):
            source.command(0,0); source.command(1,0); source.command(1,2)
            for repeat in (0,1):
                source.command(2,expression)
                for cel in range(7):
                    source.command(3,cel)
                    for flash in (0,1):
                        source.command(4,flash)
                        actual = compose(self.model,frames[repeat,expression,cel],flash)
                        self.assertEqual(actual,source.frame(),(repeat,expression,cel,flash))
                source.command(4,0); source.command(2,0)


if __name__=='__main__':
    unittest.main()
