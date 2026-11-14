#!/usr/bin/env python3
"""Full-width asset/source checks and host renderer fixtures."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

from grp_to_bin import decode_grp, halve_background
from ptn_to_h import decode_ptn
from hud_pattern_to_h import pattern, results_crop


def layout_bytes(scene):
    text = Path(f'assets/processed/stagedat/stagedat{scene}.h').read_text().split('= {', 1)[1]
    text = re.sub(r'//[^\n]*', '', text)
    return bytes(int(n) for n in re.findall(r'\b\d+\b', text))


class ViewportAssets(unittest.TestCase):
    def test_stage_center_snap_precedes_scene_and_banner_display(self):
        source = Path('source/main.cpp').read_text()
        load = source.split('static void stage_load(', 1)[1].split('static void start_new_game(', 1)[0]
        self.assertLess(load.index('camera.snap_center()'), load.index('load_boss_background()'))
        self.assertLess(load.index('stage_entrance_prepare()'), load.index('load_boss_background()'))
        self.assertLess(load.index('player_initialize()'), load.index('stage_entrance_animate('))
        presentations = list(re.finditer(r'(?<!\w)render_frame\(\);', load))
        self.assertEqual(len(presentations), 2)  # distinct real Konngara/ordinary entrances
        self.assertLess(load.index('konngara_intro_play()'), load.index('konngara_view.begin()'))
        self.assertLess(load.index('konngara_view.begin()'), presentations[0].start())
        self.assertLess(load.index('stage_entrance_animate('), presentations[1].start())
        self.assertLess(presentations[1].start(), load.index('music_play_track('))
        for path in Path('source/engine').rglob('*'):
            if path.suffix in ('.cpp', '.hpp', '.h'):
                self.assertNotRegex(path.read_text(), r'#include\s*[<\"][^>\"]*camera\.')

    def test_every_original_stage_cell_is_preserved(self):
        for scene in range(8):
            raw = Path(f'assets/packfile/STAGE{scene}.DAT').read_bytes()
            converted = layout_bytes(scene)
            self.assertEqual(len(converted), 5 * 401)
            for stage in range(5):
                data = raw[22 + stage * 250:22 + (stage + 1) * 250]
                expected_cards = bytes((data[y * 5 + x // 4] >> (3 - x % 4)) & 1
                                       for y in range(10) for x in range(20))
                self.assertEqual(converted[stage * 401:(stage + 1) * 401],
                                 data[50:] + expected_cards + bytes([int(stage != 4)]))

    def test_backgrounds_keep_all_source_columns(self):
        for name in [f'ST{i}' for i in range(7)] + ['BOSS1']:
            with self.subTest(background=name):
                image, palette, size = decode_grp(Path(f'assets/hdi_dump/{name}.GRP').read_bytes())
                self.assertEqual(size, (400, 640))
                half, _ = halve_background(image, palette)
                expected = bytes(pixel for row in half[8:] for pixel in row)
                actual = Path(f'nitrofs/{name.lower()}.bin').read_bytes()
                self.assertEqual(len(actual), 320 * 192)
                self.assertEqual(actual, expected)
                # A centered view is no longer the only data packaged.
                for offset in (0, 1, 32, 63, 64):
                    self.assertEqual(len(b''.join(actual[y * 320 + offset:y * 320 + offset + 256]
                                                  for y in range(192))), 256 * 192)

    def test_original_hud_quarters_retain_all_sixteen_pixels(self):
        palette, images = decode_ptn(Path('assets/packfile/STG.PTN').read_bytes())
        image = images[0]
        quarters = [[row[:16] for row in image[:16]], [row[16:] for row in image[:16]],
                    [row[:16] for row in image[16:]], [row[16:] for row in image[16:]]]
        expected = bytes(p for q in quarters for row in q for p in row)
        text = Path('assets/processed/sprites/HudIcons.h').read_text()
        body = text.split('= {', 1)[1].split('};', 1)[0]
        actual = bytes(int(n) for n in re.findall(r'\b\d+\b', body))
        self.assertEqual(len(actual), 4 * 16 * 16)
        self.assertEqual(actual, expected)

    def test_bottom_pattern_is_clean_original_results_crop(self):
        data = Path('assets/hdi_dump/CLEAR3.GRP').read_bytes()
        image, _, size = decode_grp(data)
        self.assertEqual(size, (400, 640))
        crop = [pixel for row in image[240:368] for pixel in row[:128]]
        self.assertEqual(set(crop), {4, 15})
        expected = bytes(5 if pixel == 15 else 4 for pixel in crop)
        self.assertEqual(pattern(data), expected)
        text = Path('assets/processed/backgrounds/HudPattern.h').read_text()
        actual = bytes(int(n) for n in re.findall(r'\b\d+\b', text.split('= {', 1)[1]))
        self.assertEqual(len(actual), 128 * 128)
        self.assertEqual(actual, expected)


    def test_round_results_crop_and_palette_are_unscaled_source(self):
        data = Path('assets/hdi_dump/CLEAR3.GRP').read_bytes()
        image, palette, _ = decode_grp(data)
        expected = bytes(pixel for row in image[64:256] for pixel in row[384:640])
        self.assertEqual(results_crop(data), (expected, palette))
        text = Path('assets/processed/backgrounds/RoundResults.h').read_text()
        body = text.split('= {', 1)[1].split('};', 1)[0]
        actual = bytes(int(n) for n in re.findall(r'\b\d+\b', body))
        self.assertEqual(actual, expected)
        colors = [int(n) for n in re.findall(r'\b\d+\b', text.split('= {', 2)[2])]
        self.assertEqual(colors, [(r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) for r, g, b in palette])


def run_renderer(binary):
    # Test fixtures are generated by Make under build/, never preplaced inputs.
    runtime = Path('build/viewport/tests/runtime')
    destination = runtime / 'nitro:'
    destination.mkdir(parents=True, exist_ok=True)
    for name in [f'st{i}' for i in range(7)] + ['boss1']:
        shutil.copyfile(f'nitrofs/{name}.bin', destination / f'{name}.bin')
    shutil.copyfile('nitrofs/konngara_art.bin', destination / 'konngara_art.bin')
    shutil.copyfile('nitrofs/konngara_combat.bin', destination / 'konngara_combat.bin')
    shutil.copyfile('nitrofs/konngara_slash.bin', destination / 'konngara_slash.bin')
    shutil.copyfile('nitrofs/konngara_defeat.bin', destination / 'konngara_defeat.bin')
    shutil.copyfile('nitrofs/konngara_intro.bin', destination / 'konngara_intro.bin')
    if binary is not None:
        subprocess.run([str(Path(binary).resolve())], cwd=runtime, check=True)


if __name__ == '__main__':
    binary = None
    if '--run' in sys.argv:
        index = sys.argv.index('--run')
        binary = sys.argv[index + 1]
        del sys.argv[index:index + 2]
    result = unittest.main(exit=False)
    if not result.result.wasSuccessful():
        sys.exit(1)
    if binary:
        run_renderer(binary)
