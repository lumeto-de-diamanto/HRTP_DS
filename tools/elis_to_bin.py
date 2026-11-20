#!/usr/bin/env python3
"""Elis encounter: original girl/attack/bat cels, arena, masks and stars."""
import argparse
from pathlib import Path
import struct
from PIL import Image

from bos_to_h import decode_bos
from grp_to_bin import decode_grp
from halve_image import shrink_image


def build(packfile, background):
    image, palette, size = decode_grp(background.read_bytes())
    girls, dimensions = decode_bos((packfile / 'BOSS5.BOS').read_bytes())
    attacks, attack_size = decode_bos((packfile / 'BOSS5_2.BOS').read_bytes())
    bats, bat_size = decode_bos((packfile / 'BOSS5_3.BOS').read_bytes())
    grc = (packfile / 'BOSS5_GR.GRC').read_bytes()
    if size != (400, 640) or dimensions != (128, 96) or len(girls) != 6:
        raise ValueError('expected BOSS5.GRP and six128x96 BOSS5.BOS cels')
    if attack_size != (128, 96) or len(attacks) != 3:
        raise ValueError('expected three128x96 BOSS5_2.BOS attack cels')
    if bat_size != (48, 32) or len(bats) != 3:
        raise ValueError('expected three48x32 BOSS5_3.BOS bat cels')
    if struct.unpack_from('<4shhB', grc) != (b'GRCG', 4, 32, 6) or len(grc) != 64+6*128:
        raise ValueError('expected six32x32 BOSS5_GR.GRC masks')
    inventory = {}
    indices = bytearray()
    for y in range(16, 400, 2):
        for x in range(0, 640, 2):
            pixels = sorted(image[y+dy][x+dx] for dy in (0, 1) for dx in (0, 1))
            signature = sum(p << (i*4) for i, p in enumerate(pixels))
            inventory.setdefault(signature, len(inventory))
            indices.append(16+inventory[signature])
    if not 0 < len(inventory) <= 192:
        raise ValueError('Elis background exceeds the208-color BG budget')
    body = bytes(p for girl in girls[:5]+attacks for row in shrink_image(girl, 'dither') for p in row)
    spheres = bytearray()
    for cel in (1, 2, 3, 4, 5):
        pixels = [[(grc[64+cel*128+y*4+x//8] >> (7-x%8)) & 1 for x in range(32)] for y in range(32)]
        spheres.extend(p for row in shrink_image(pixels, 'dither' if cel<=3 else 'rare') for p in row)
    body_offset = 61520+len(inventory)*2
    shapes = Image.open(Path(__file__).resolve().parents[1] / 'assets/sprites/shape8x8.bmp')
    if shapes.size != (56, 8):
        raise ValueError('expected seven original8x8 shapes')
    star = bytes(p for row in shrink_image([[int(bool(shapes.getpixel((8+x,y)))) for x in range(8)] for y in range(8)], 'dither') for p in row)
    bat_pixels = bytes(p for bat in bats for row in shrink_image(bat, 'dither') for p in row)
    header = struct.pack('<4s6H4I', b'ELS4', 128, 96, 8, 5, 256, len(inventory),
                         80, 61520, body_offset, body_offset+len(body))
    return (header+bytes(c//17 for rgb in palette for c in rgb)+indices+
            struct.pack('<'+'H'*len(inventory), *inventory)+body+spheres+star+bat_pixels)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packfile', type=Path);parser.add_argument('background', type=Path)
    parser.add_argument('output', type=Path);args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(build(args.packfile, args.background))
