"""Original-source oracle for Elis's entrance and first combat block."""
from pathlib import Path
import json
import shutil
import struct
import unittest
from PIL import Image
from bos_to_h import decode_bos
from grp_to_bin import decode_grp
from halve_image import shrink_image


def source():
    image, palette, _ = decode_grp(Path('assets/hdi_dump/BOSS5.GRP').read_bytes())
    girls, _ = decode_bos(Path('assets/packfile/BOSS5.BOS').read_bytes())
    attacks, _ = decode_bos(Path('assets/packfile/BOSS5_2.BOS').read_bytes())
    bats, _ = decode_bos(Path('assets/packfile/BOSS5_3.BOS').read_bytes())
    grc = Path('assets/packfile/BOSS5_GR.GRC').read_bytes()
    masks = []
    for cel in (1, 2, 3, 4, 5):
        native = [[int(bool(grc[64+cel*128+y*4+x//8] & (0x80 >> (x%8)))) for x in range(32)] for y in range(32)]
        masks.extend(p for row in shrink_image(native, 'dither' if cel<=3 else 'rare') for p in row)
    shapes = Image.open('assets/sprites/shape8x8.bmp')
    star = bytes(p for row in shrink_image([[int(bool(shapes.getpixel((x+8,y)))) for x in range(8)] for y in range(8)], 'dither') for p in row)
    bat_pixels = bytes(p for bat in bats for row in shrink_image(bat, 'dither') for p in row)
    return image, bytes(c//17 for rgb in palette for c in rgb), bytes(p for girl in girls[:5]+attacks for row in shrink_image(girl, 'dither') for p in row), bytes(masks)+star+bat_pixels


class ElisAssets(unittest.TestCase):
    def test_source_pixels_and_music_contract(self):
        image, palette, girl, spheres = source()
        data = Path('nitrofs/elis_art.bin').read_bytes()
        magic,w,h,cels,masks,stride,count,indices,signatures,body,orbs = struct.unpack_from('<4s6H4I', data)
        self.assertEqual((magic,w,h,cels,masks,stride), (b'ELS4',128,96,8,5,256))
        self.assertEqual((indices,signatures,body,orbs,len(data)), (80,61520,61520+2*count,61520+2*count+24576,61520+2*count+27024))
        self.assertEqual(data[32:80], palette)
        # Active BOSS5.GRP palette, NOT the skipped BOS/GRC header palettes.
        self.assertEqual(tuple(palette[2*3:2*3+3]), (13, 12, 4))  # yellow stars
        self.assertEqual(tuple(palette[4*3:4*3+3]), (7, 9, 11))  # gray-blue rifts/circles
        self.assertEqual(tuple(palette[7*3:7*3+3]), (15, 15, 15))  # V_WHITE rays
        self.assertEqual(data[body:orbs], girl)
        self.assertEqual(data[orbs:], spheres)
        # Approved4x4 dither silhouette: symmetric top and separate lower tips.
        self.assertEqual(data[orbs+1280:orbs+1296], bytes(int(c=='#') for c in '.##.'+'####'+'.##.'+'#..#'))
        inventory = struct.unpack_from('<'+'H'*count, data, signatures)
        for y in range(192):
            for x in range(320):
                expected = sorted(image[16+y*2+dy][x*2+dx] for dy in (0,1) for dx in (0,1))
                self.assertEqual(inventory[data[80+y*320+x]-16], sum(p<<(4*i) for i,p in enumerate(expected)))
        track = next(t for t in json.loads(Path('build/native_soundtrack/manifest.json').read_text())['tracks'] if t['id']=='kami2')
        self.assertEqual((track['tempo'],track['native_ticks_per_beat']), (90,48))
        self.assertEqual(track['tracks'][0]['loopTick'],17*48)

    def test_production_encounter_replaces_shortcuts(self):
        progression = Path('source/engine/main/progression.hpp').read_text()
        self.assertIn('stage_has_elis', progression)
        main = Path('source/main.cpp').read_text()
        self.assertIn('elis_entrance_play(elis_view)', main)
        self.assertIn('elis.update(bomb_damage)', main)
        self.assertNotIn('elis_entrance_review(', main)
        self.assertNotIn('elis_phase1_review(', main)
        self.assertNotIn('yuugenmagan_phase1_review(',main)


def make_fixtures(runtime):
    destination = runtime/'nitro:';destination.mkdir(parents=True,exist_ok=True)
    shutil.copyfile('nitrofs/elis_art.bin',destination/'elis_art.bin')
    _,palette,girl,spheres = source()
    (runtime/'elis-expected.bin').write_bytes(palette+girl[:3072]+spheres[:768])
    (runtime/'elis-combat-expected.bin').write_bytes(palette+girl+spheres)
