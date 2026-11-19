"""Source-pixel oracle for Kikuri; no production KIK1 builder used as oracle."""
from pathlib import Path
import shutil
import struct
import unittest

from grp_to_bin import decode_grp
from bos_to_h import decode_bos
from ptn_to_h import decode_ptn
from halve_image import shrink_image


def source_pixels():
    image,palette,size=decode_grp(Path('assets/hdi_dump/BOSS4.GRP').read_bytes())
    assert size==(400,640)
    return image,palette


def source_sprites():
    soul,_=decode_bos(Path('assets/packfile/TAMASII.BOS').read_bytes())
    tear,_=decode_bos(Path('assets/packfile/TAMASII2.BOS').read_bytes())
    _,ripple=decode_ptn(Path('assets/packfile/TAMAYEN.PTN').read_bytes())
    quarters=[[row[x:x+16] for row in ripple[0][y:y+16]] for y in (0,16) for x in (0,16)]
    return b''.join(bytes(p for row in shrink_image(cel,'dither') for p in row)
                    for cel in soul+tear+quarters)


class KikuriAssets(unittest.TestCase):
    def test_original_pixels_signatures_and_cels(self):
        art=Path('nitrofs/kikuri_art.bin').read_bytes()
        magic,w,h,souls,tears,ripples,count,source,indices,signatures,sprites=struct.unpack_from('<4s6H4I',art)
        self.assertEqual((magic,w,h,souls,tears,ripples),(b'KIK1',640,400,3,3,4))
        self.assertTrue(0<count<=192)
        self.assertEqual((source,indices,signatures,sprites),(80,128080,189520,189520+count*2))
        self.assertEqual(len(art),sprites+1216)
        image,palette=source_pixels()
        self.assertEqual(art[32:80],bytes(c//17 for rgb in palette for c in rgb))
        self.assertEqual(art[source:indices],bytes(row[x]+16*row[x+1] for row in image for x in range(0,640,2)))
        inventory=struct.unpack_from('<'+'H'*count,art,signatures)
        for y in range(192):
            for x in range(320):
                raw=sorted(image[16+y*2+dy][x*2+dx] for dy in (0,1) for dx in (0,1))
                index=art[indices+y*320+x]
                self.assertTrue(16<=index<16+count)
                self.assertEqual(inventory[index-16],sum(p<<(4*i) for i,p in enumerate(raw)))
        self.assertEqual(art[sprites:],source_sprites())

    def test_stage15_and_no_developer_shortcuts(self):
        progression=Path('source/engine/main/progression.hpp').read_text()
        self.assertIn('stage_has_kikuri',progression)
        self.assertIn('scene == 4 && stage == 4',progression)
        source=Path('source/main.cpp').read_text()
        self.assertNotIn('kikuri_art_review',source)
        self.assertNotIn('kikuri_phase1_review',source)
        self.assertIn('stage_navigate(keys_held,keys_down)',source)
        self.assertFalse(Path('source/platform/kikuri_review.cpp').exists())
        self.assertFalse(Path('source/platform/kikuri_combat_review.cpp').exists())


def make_fixtures(runtime):
    destination=runtime/'nitro:';destination.mkdir(parents=True,exist_ok=True)
    shutil.copyfile('nitrofs/kikuri_art.bin',destination/'kikuri_art.bin')
    image,palette=source_pixels()
    (runtime/'kikuri-source.bin').write_bytes(bytes(c//17 for rgb in palette for c in rgb)+bytes(p for row in image for p in row))
    (runtime/'kikuri-sprites.bin').write_bytes(source_sprites())
