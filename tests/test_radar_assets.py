#!/usr/bin/env python3
"""Editable PNG inputs, exact native masks/colors and safe incremental builds."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

from radar_icons_to_h import ICONS, build
from test_viewport_assets import radar_reference_icons

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/sprites/radar'
FONT = ROOT / 'assets/font.bmp'


class RadarAssetTests(unittest.TestCase):
    def test_png_and_current_font_pixels_colors_dimensions_match_generated_header(self):
        expected_names = ('player','orb','card','obstacle','portal','item','turret',
                          'barrier_horizontal','barrier_vertical')
        self.assertEqual(tuple(icon[1] for icon in ICONS), expected_names)
        self.assertEqual(tuple((icon[2],icon[3]) for icon in ICONS),
                         ((7,7),(7,7),(7,7),(7,7),(7,7),(6,6),(7,7),(8,2),(2,8)))
        self.assertFalse((ART / 'boss.png').exists())
        header = build(ART, FONT)
        self.assertEqual(header, (ROOT / 'assets/processed/sprites/RadarIcons.h').read_text())
        masks, colors, widths, heights, offsets = [0], [0], [0], [0], [0]
        for width,height,color,pixels in radar_reference_icons(ART,FONT):
            colors.append(color)
            widths.append(width);heights.append(height)
            offsets.append(len(masks))
            masks += pixels
        for name, expected in (('width',widths),('height',heights),('colors',colors),('offsets',offsets)):
            body = header.split('RadarIcons_'+name+'[] = {',1)[1].split('}',1)[0]
            self.assertEqual(list(map(int,re.findall(r'\d+',body))),expected)
        body = header.split('RadarIcons_pixels',1)[1].split('= {',1)[1].split('};',1)[0]
        self.assertEqual(list(map(int,re.findall(r'\b\d+\b',body))),masks)
        self.assertNotIn('shrink_image', (ROOT / 'tools/radar_icons_to_h.py').read_text())

    def test_invalid_pngs_never_replace_previous_output(self):
        with tempfile.TemporaryDirectory(prefix='radar-invalid-',dir='/tmp/opencode') as directory:
            root = Path(directory);art=root/'radar';shutil.copytree(ART,art)
            font=root/'font.bmp';shutil.copyfile(FONT,font)
            output=root/'RadarIcons.h';command=[sys.executable,'-B',str(ROOT/'tools/radar_icons_to_h.py'),str(art),str(output),'--font',str(font)]
            subprocess.run(command,check=True,capture_output=True)
            before=output.read_bytes();source=(art/'player.png').read_bytes()
            for kind in ('size','alpha','colors','empty','missing','wrong-format'):
                path=art/'player.png';path.write_bytes(source)
                with Image.open(path) as image: image=image.convert('RGBA')
                if kind=='size': image=image.resize((8,8),Image.Resampling.NEAREST)
                elif kind=='alpha': image.putpixel((3,0),(255,0,0,128))
                elif kind=='colors': image.putpixel((3,0),(0,255,0,255))
                elif kind=='empty': image=Image.new('RGBA',image.size)
                if kind=='missing': path.unlink()
                else: image.save(path,format='BMP' if kind=='wrong-format' else 'PNG')
                result=subprocess.run(command,capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0,kind)
                self.assertIn('error:',result.stderr)
                self.assertEqual(output.read_bytes(),before)
            path.write_bytes(source)
            original=font.read_bytes()
            for invalid in (b'',b'not a bitmap',original[:62],original[:-1]):
                font.write_bytes(invalid)
                result=subprocess.run(command,capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0)
                self.assertIn('error:',result.stderr)
                self.assertEqual(output.read_bytes(),before)
            font.unlink()
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(output.read_bytes(),before)

    def test_mask_and_color_edits_rebuild_only_the_generated_asset(self):
        with tempfile.TemporaryDirectory(prefix='radar-build-',dir='/tmp/opencode') as directory:
            root=Path(directory);art=root/'assets/sprites/radar';art.parent.mkdir(parents=True)
            shutil.copytree(ART,art);(root/'tools').symlink_to(ROOT/'tools',target_is_directory=True)
            font=root/'assets/font.bmp';shutil.copyfile(FONT,font)
            output=root/'assets/processed/sprites/RadarIcons.h'
            command=['make','-f',str(ROOT/'assets/Makefile'),'assets/processed/sprites/RadarIcons.h']
            subprocess.run(command,cwd=root,check=True,capture_output=True)
            before=output.read_bytes()
            path=art/'obstacle.png'
            with Image.open(path) as image: image=image.convert('RGBA')
            image.putpixel((0,0),(255,136,0,255));image.save(path)
            subprocess.run(command,cwd=root,check=True,capture_output=True)
            changed=output.read_bytes();self.assertNotEqual(changed,before)
            for y in range(image.height):
                for x in range(image.width):
                    if image.getpixel((x,y))[3]: image.putpixel((x,y),(0,255,255,255))
            image.save(path)
            subprocess.run(command,cwd=root,check=True,capture_output=True)
            self.assertNotEqual(output.read_bytes(),changed)
            stamp=output.stat().st_mtime_ns
            subprocess.run(command,cwd=root,check=True,capture_output=True)
            self.assertEqual(output.stat().st_mtime_ns,stamp)
            # Replace only the disposable font's enemy glyph, not any PNG.
            # The Make dependency must propagate it into the boss mask while
            # preserving all other icons, palette entries and metadata.
            previous=output.read_text()
            original=font.read_bytes();alternate=bytearray(original)
            offset=int.from_bytes(alternate[10:14],'little')
            ku,ten=(b-128 for b in '敵'.encode('euc_jp'))
            x,y=(ku-32)*16+7,ten*16+7
            alternate[offset+(2047-y)*256+x//8] ^= 1 << (7-x%8)
            font.write_bytes(alternate)
            subprocess.run(command,cwd=root,check=True,capture_output=True)
            variant=output.read_text()
            self.assertNotEqual(variant,previous)
            self.assertEqual(variant,build(art,font))
            self.assertEqual(variant.split('RadarIcons_pixels',1)[0],previous.split('RadarIcons_pixels',1)[0])
            old_pixels=list(map(int,re.findall(r'\b\d+\b',previous.split('= {\n',1)[1])))
            new_pixels=list(map(int,re.findall(r'\b\d+\b',variant.split('= {\n',1)[1])))
            self.assertEqual(old_pixels[:-256],new_pixels[:-256])
            self.assertEqual(sum(a!=b for a,b in zip(old_pixels,new_pixels)),1)
            self.assertFalse((art/'boss.png').exists())
            stamp=output.stat().st_mtime_ns
            subprocess.run(command,cwd=root,check=True,capture_output=True)
            self.assertEqual(output.stat().st_mtime_ns,stamp)
            font.write_bytes(original)
            subprocess.run(command,cwd=root,check=True,capture_output=True)
            self.assertEqual(output.read_text(),previous)
