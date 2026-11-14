#!/usr/bin/env python3
"""Extract a clean 128x128 CLEAR3 background tile, recoloured for the sub HUD."""
import argparse
from pathlib import Path

from grp_to_bin import decode_grp


def pattern(data):
    image, palette, size = decode_grp(data)
    if size != (400, 640):
        raise ValueError('expected the original 640x400 CLEAR3 artwork')
    # Below all labels and left of Reimu: no source text/character pixels.
    tile = [row[:128] for row in image[240:368]]
    colors = {pixel for row in tile for pixel in row}
    if len(colors) != 2:
        raise ValueError('expected the clean two-colour CLEAR3 background motif')
    white = max(colors, key=lambda index: sum(palette[index]))
    return bytes(5 if pixel == white else 4 for row in tile for pixel in row)


def build(data):
    pixels = pattern(data)
    lines = ['// Generated from CLEAR3.GRP crop(0,240),128x128; palette slots4/5.',
             '#pragma once', 'alignas(4) static const unsigned char HudPattern_pixels[128*128] = {']
    lines += ['    ' + ','.join(map(str, pixels[row:row+128])) + ','
              for row in range(0, len(pixels), 128)]
    return '\n'.join(lines + ['};', ''])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(build(args.input.read_bytes()), encoding='ascii')
