"""Independent source-pixel oracle; never use the YMG1 builder as expected data."""
from pathlib import Path
import shutil
import struct
import unittest

from bos_to_h import decode_bos
from grp_to_bin import decode_grp
from halve_image import shrink_image


def source():
    image, palette, size = decode_grp(Path('assets/hdi_dump/BOSS2.GRP').read_bytes())
    eyes, dimensions = decode_bos(Path('assets/packfile/BOSS2.BOS').read_bytes())
    assert size == (400, 640) and dimensions == (64, 48) and len(eyes) == 7
    return image, palette, [shrink_image(cel, 'dither') for cel in eyes]


class YuugenMaganAssets(unittest.TestCase):
    def test_original_background_mixtures_and_padded_eye_pixels(self):
        image, palette, eyes = source()
        art = Path('nitrofs/yuugenmagan_art.bin').read_bytes()
        magic,w,h,cels,stride,actors,count,indices,signatures,sprites,size = struct.unpack_from('<4s6H4I', art)
        self.assertEqual((magic,w,h,cels,stride,actors), (b'YMG1',64,48,7,512,5))
        self.assertTrue(0 < count <= 192)
        self.assertEqual((indices,signatures,sprites,size),(80,61520,61520+count*2,len(art)))
        self.assertEqual(size,sprites+7*512)
        self.assertEqual(art[32:80],bytes(c//17 for rgb in palette for c in rgb))
        inventory = struct.unpack_from('<'+'H'*count,art,signatures)
        for y in range(192):
            for x in range(320):
                raw = sorted(image[16+y*2+dy][x*2+dx] for dy in (0,1) for dx in (0,1))
                index = art[indices+y*320+x]
                self.assertTrue(16 <= index < 16+count)
                self.assertEqual(inventory[index-16],sum(p<<(4*i) for i,p in enumerate(raw)))
        for cel, pixels in enumerate(eyes):
            for y in range(32):
                for x in range(32):
                    offset=sprites+cel*512+((y//8)*4+x//8)*32+(y%8)*4+(x%8)//2
                    actual=(art[offset]>>((x%2)*4))&15
                    self.assertEqual(actual,pixels[y][x] if y<24 else 0)

    def test_story_stage10_and_no_developer_shortcuts(self):
        progression=Path('source/engine/main/progression.hpp').read_text()
        self.assertIn('stage_has_yuugenmagan',progression)
        self.assertIn('scene == 1 && stage == 4',progression)
        main=Path('source/main.cpp').read_text()
        navigator=main.split('static bool stage_navigate(',1)[1].split('#ifndef HRTP_GAME_FLOW_TEST',1)[0]
        self.assertNotIn('KEY_Y',navigator)
        self.assertNotIn('yuugenmagan_art_review',main)
        self.assertNotIn('yuugenmagan_phase1_review',main)
        self.assertIn('yuugenmagan.reset(true,true,true,true,true)',main)
        self.assertIn('yuugenmagan_view.defeat(',main)
        self.assertIn('stage_load(3,0,false,true)',main)


def make_fixtures(runtime):
    destination=runtime/'nitro:';destination.mkdir(parents=True,exist_ok=True)
    shutil.copyfile('nitrofs/yuugenmagan_art.bin',destination/'yuugenmagan_art.bin')
    image,palette,eyes=source()
    (runtime/'yuugenmagan-source.bin').write_bytes(
        bytes(c//17 for rgb in palette for c in rgb)+bytes(p for row in image for p in row))
    (runtime/'yuugenmagan-eyes.bin').write_bytes(bytes(p for cel in eyes for row in cel for p in row))
