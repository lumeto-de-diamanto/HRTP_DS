#!/usr/bin/env python3
"""Decode the two original monochrome GRC files into nine halved BG2 glyphs."""
import argparse
from pathlib import Path
import struct

from halve_image import shrink_image


def decode(path, expected_count):
    data = Path(path).read_bytes()
    if len(data) < 64:
        raise ValueError(f'{path}: truncated GRC header')
    magic, row_bytes, height, count = struct.unpack_from('<4shhB', data)
    if (magic, row_bytes, height, count) != (b'GRCG', 4, 32, expected_count):
        raise ValueError(f'{path}: expected {expected_count} monochrome 32x32 GRC images')
    if len(data) != 64 + count * row_bytes * height:
        raise ValueError(f'{path}: truncated or trailing image data')
    images = []
    for cel in range(count):
        start = 64 + cel * 128
        pixels = [[(data[start + y * 4 + x // 8] >> (7 - x % 8)) & 1
                   for x in range(32)] for y in range(32)]
        images.append(shrink_image(pixels, 'rare'))
    return images


def build(first, second):
    images = decode(first, 8) + decode(second, 1)
    lines = ['// Generated from KUZI1/2.GRC; 16x16 monochrome BG2 cels.',
             '#pragma once', 'static const unsigned char BombKuji_cels[9][256] = {']
    for image in images:
        lines.append('    {' + ','.join(str(pixel) for row in image for pixel in row) + '},')
    return '\n'.join(lines + ['};', ''])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('first', type=Path)
    parser.add_argument('second', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(build(args.first, args.second), encoding='ascii')
