#!/usr/bin/env python3
"""Full-width asset/source checks and host renderer fixtures."""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import unittest

from grp_to_bin import decode_grp, halve_background, pack_palette
from ptn_to_h import decode_ptn
from hud_pattern_to_h import pattern, results_crop
from mono_bmp_to_h import load_bmp
from halve_image import shrink_image


def layout_bytes(scene):
    text = Path(f'assets/processed/stagedat/stagedat{scene}.h').read_text().split('= {', 1)[1]
    text = re.sub(r'//[^\n]*', '', text)
    return bytes(int(n) for n in re.findall(r'\b\d+\b', text))


def results_font_strips(path=Path('assets/fonts/results_serif.bdf')):
    # Independent BDF oracle: do not consume the generated renderer header or
    # call the production BDF converter to decide what its pixels should be.
    glyphs = {}
    for block in path.read_text().split('STARTCHAR ')[1:]:
        metrics, bitmap = block.split('BITMAP\n', 1)
        char = chr(int(re.search(r'^ENCODING (\d+)$', metrics, re.M)[1]))
        width = int(re.search(r'^BBX (\d+) ', metrics, re.M)[1])
        rows = bitmap.split('\nENDCHAR', 1)[0].splitlines()
        glyphs[char] = [[int(row[x:x+4], 16) for row in rows] for x in range(0, width // 4, 4)]
    return [glyphs[char] for char in '0123456789' + ''.join(map(chr, range(0xE000, 0xE008)))]


def results_font_bytes(path=Path('assets/fonts/results_serif.bdf')):
    rows = [row for strip in results_font_strips(path) for cell in strip for row in cell]
    return struct.pack('<' + 'H' * len(rows), *rows)


class ViewportAssets(unittest.TestCase):
    def test_stage_center_snap_precedes_scene_and_banner_display(self):
        source = Path('source/main.cpp').read_text()
        load = source.split('static void stage_load(', 1)[1].split('static void start_new_game(', 1)[0]
        self.assertLess(load.index('camera.snap_center()'), load.index('load_boss_background()'))
        self.assertLess(load.index('stage_entrance_prepare()'), load.index('load_boss_background()'))
        self.assertLess(load.index('player_initialize()'), load.index('stage_entrance_animate('))
        presentations = list(re.finditer(r'(?<!\w)render_frame\(\);', load))
        self.assertEqual(len(presentations), 3)  # Konngara, Mima, ordinary entrances
        self.assertLess(load.index('konngara_intro_play()'), load.index('konngara_view.begin()'))
        self.assertLess(load.index('konngara_view.begin()'), presentations[0].start())
        self.assertLess(load.index('stage_entrance_animate('), presentations[1].start())
        self.assertLess(load.index('load_mima_background()'), load.index('stage_entrance_animate('))
        self.assertLess(load.index('stage_entrance_animate('), load.index('mima_view.begin(true)'))
        self.assertLess(load.index('mima_view.begin(true)'), load.index('music_play_track(NATIVE_LEGEND)'))
        self.assertLess(load.index('music_play_track(NATIVE_LEGEND)'), load.index('mima_entrance_play('))
        self.assertLess(load.index('mima_entrance_play('), load.index('mima.reset(true)'))
        self.assertLess(load.index('mima.reset(true)'), presentations[1].start())
        self.assertLess(presentations[2].start(), load.index('music_play_track(NATIVE_POSITIVE)'))
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
        for name in [f'ST{i}' for i in range(7)] + ['BOSS1', 'BOSS3']:
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
    runtime.mkdir(parents=True, exist_ok=True)
    (runtime / 'results-font.bin').write_bytes(results_font_bytes())
    strips = results_font_strips()
    sizes = [(sum(map(len, strips[:i])), len(strip)) for i, strip in enumerate(strips)]
    (runtime / 'results-labels.bin').write_bytes(bytes(v for pair in sizes for v in pair))
    pillar,_,_=load_bmp(Path('assets/sprites/pillar.bmp'))
    (runtime / 'mima-pillar.bin').write_bytes(bytes(p for x in (0,32)
        for row in shrink_image([row[x:x+32] for row in pillar],'rare') for p in row))
    image,raw,_=decode_grp(Path('assets/hdi_dump/BOSS3.GRP').read_bytes())
    half,normal=halve_background(image,raw);samples={}
    for y,row in enumerate(half):
        for x,index in enumerate(row):
            samples[index]=[image[y*2+a][x*2+b] for a in range(2) for b in range(2)]
    states=[]
    for fading in (False,True):
        for step in range(16):
            colors=[tuple((max(c//17,15-step) if fading else min(15,c//17+step))*17 for c in rgb) for rgb in raw]
            state=[]
            for i in range(len(normal)):
                rgb=colors[i] if i<16 else tuple(sum(colors[p][c]//4 for p in samples[i]) for c in range(3)) if i in samples else normal[i]
                state+=pack_palette([rgb])
            states+=state
    (runtime / 'mima-defeat-palettes.bin').write_bytes(struct.pack('<'+'H'*len(states),*states))
    destination = runtime / 'nitro:'
    destination.mkdir(parents=True, exist_ok=True)
    for name in [f'st{i}' for i in range(7)] + ['boss1', 'boss3', 'mima_art']:
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
