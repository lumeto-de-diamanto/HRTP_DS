#!/usr/bin/env python3
"""Real clean/incremental/parallel graphics builds in disposable workspaces.

Only original inputs and tools are shared read-only via symlinks. Never clean
or delete the production assets tree to exercise dependency recovery.
"""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AssetBuildTests(unittest.TestCase):
    def make_assets(self, root, jobs):
        result = subprocess.run(['make', f'-j{jobs}', '-f', 'assets/Makefile', 'assets'],
                                cwd=root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def snapshot(self, root):
        return {str(p.relative_to(root)): (p.stat().st_mtime_ns,
                                          hashlib.sha256(p.read_bytes()).hexdigest())
                for directory in ('assets/processed', 'nitrofs')
                for p in (root / directory).rglob('*') if p.is_file()}

    def test_clean_builds_and_missing_output_recovery(self):
        for jobs in (1, 8):
            with self.subTest(jobs=jobs), tempfile.TemporaryDirectory(
                    prefix='hrtp-assets-', dir='/tmp/opencode') as directory:
                root = Path(directory)
                (root / 'assets').mkdir()
                (root / 'tools').symlink_to(ROOT / 'tools', target_is_directory=True)
                for name in ('Makefile', 'th01.hdi', 'font.bmp', 'sprites'):
                    (root / 'assets' / name).symlink_to(ROOT / 'assets' / name)
                output = self.make_assets(root, jobs)
                self.assertEqual(output.count('ASSETS  extracting assets/hdi_dump/'), 1)
                self.assertEqual(output.count('file(s) extracted to assets/hdi_dump'), 1)
                before = self.snapshot(root)
                for scene in range(8):
                    self.assertIn(f'assets/processed/stagedat/stagedat{scene}.h', before)
                self.assertIn('nitrofs/konngara_intro.bin', before)
                self.assertIn('nitrofs/konngara_art.bin', before)
                self.assertIn('nitrofs/konngara_combat.bin', before)
                self.assertIn('nitrofs/konngara_slash.bin', before)
                self.assertIn('nitrofs/konngara_defeat.bin', before)
                self.assertIn('assets/processed/sprites/BombKuji.h', before)
                self.assertIn('assets/processed/backgrounds/HudPattern.h', before)
                self.assertIn('assets/processed/backgrounds/RoundResults.h', before)
                for scene in range(7):
                    self.assertEqual((root / f'nitrofs/st{scene}.bin').stat().st_size, 61440)
                self.assertIn('FONT16X16_GLYPH_COUNT 272',
                              (root / 'assets/processed/fonts/font16x16.h').read_text())
                self.make_assets(root, jobs)
                self.assertEqual(self.snapshot(root), before, 'second build must be a no-op')

                (root / 'nitrofs/konngara_combat.bin').unlink()
                self.make_assets(root, jobs)
                recovered = self.snapshot(root)
                self.assertEqual(recovered['nitrofs/konngara_combat.bin'][1], before['nitrofs/konngara_combat.bin'][1])
                self.assertEqual({p: v for p, v in recovered.items() if p != 'nitrofs/konngara_combat.bin'},
                                 {p: v for p, v in before.items() if p != 'nitrofs/konngara_combat.bin'})

                # Recover each half of a converter's paired output, as well as
                # missing extraction members with their old stamps still present.
                for path in ('nitrofs/st0.bin', 'assets/processed/backgrounds/boss1.h',
                             'assets/packfile/STAGE0.DAT', 'assets/hdi_dump/BOSS8_D2.GRP',
                             'nitrofs/konngara_art.bin', 'nitrofs/konngara_combat.bin', 'nitrofs/konngara_slash.bin', 'assets/packfile/BOSS8_E1.BOS',
                              'assets/hdi_dump/BOSS8.GRZ', 'nitrofs/konngara_defeat.bin',
                               'assets/packfile/KUZI1.GRC', 'assets/processed/sprites/BombKuji.h',
                               'assets/hdi_dump/CLEAR3.GRP', 'assets/processed/backgrounds/HudPattern.h',
                               'assets/processed/backgrounds/RoundResults.h'):
                    (root / path).unlink()
                self.make_assets(root, jobs)
                after = self.snapshot(root)
                self.assertEqual({p: v[1] for p, v in before.items()},
                                 {p: v[1] for p, v in after.items()})
                self.make_assets(root, jobs)
                self.assertEqual(self.snapshot(root), after)


if __name__ == '__main__':
    unittest.main()
