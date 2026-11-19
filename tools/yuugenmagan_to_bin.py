#!/usr/bin/env python3
"""YuugenMagan: original eye cels and recolorable full-width 50% scenery.

Retain the actual 2x2 source-index mixtures, not a fixed RGB quantization.
Every palette animation then recolors antialiased kanji edges as well as the
solid ink. No original assets, other bosses, or player palettes are modified.
"""
import argparse
from pathlib import Path
import struct

from bos_to_h import decode_bos, pack_frame_4bpp
from grp_to_bin import decode_grp
from halve_image import shrink_image

INDICES = 80
SIGNATURES = INDICES + 320 * 192


def build(eyes: Path, background: Path) -> bytes:
    frames, size = decode_bos(eyes.read_bytes())
    image, palette, dimensions = decode_grp(background.read_bytes())
    if size != (64, 48) or len(frames) != 7 or dimensions != (400, 640):
        raise ValueError('expected seven64x48 BOSS2.BOS cels and640x400 BOSS2.GRP')
    inventory = {}
    indices = bytearray()
    for y in range(16, 400, 2):
        for x in range(0, 640, 2):
            pixels = sorted(image[y+dy][x+dx] for dy in (0, 1) for dx in (0, 1))
            signature = sum(p << (i*4) for i, p in enumerate(pixels))
            if signature not in inventory:
                inventory[signature] = len(inventory)
            indices.append(16 + inventory[signature])
    if not 0 < len(inventory) <= 192:
        raise ValueError('background exceeds reserved208-color palette budget')
    sprites = bytearray()
    for frame in frames:
        half = shrink_image(frame, 'dither') + [[0]*32 for _ in range(8)]
        sprites.extend(struct.pack('<128I', *pack_frame_4bpp(half)))
    sprite_offset = SIGNATURES + len(inventory)*2
    header = struct.pack('<4s6H4I', b'YMG1', 64, 48, 7, 512, 5, len(inventory),
                         INDICES, SIGNATURES, sprite_offset, sprite_offset + len(sprites))
    colors = bytes(c//17 for rgb in palette for c in rgb)
    return header + colors + indices + struct.pack('<'+'H'*len(inventory), *inventory) + sprites


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('eyes', type=Path)
    parser.add_argument('background', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = build(args.eyes, args.background)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)


if __name__ == '__main__':
    main()
