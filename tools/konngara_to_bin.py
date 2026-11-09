#!/usr/bin/env python3
"""Compose Konngara's entrance pages before halving; retain palette provenance.

KGI1 stores RGB4 colors, a dictionary of sorted four-source-pixel signatures,
and three 320x400 u16 ID maps (one map row per *source* scanline). Keeping odd
rows is essential: the decelerating scroll must not lose its 1px source steps.
Colors are averaged only after runtime RGB4 palette changes, not baked into a
static quantized palette. All 640 columns/400 rows survive conversion.
"""
from array import array
from pathlib import Path
import argparse
import struct
import sys

from grp_to_bin import decode_grp

WIDTH, HEIGHT, PAGES = 320, 400, 3
HEADER = struct.Struct('<4sHHHHI')


def source_pages(directory):
    def load(name):
        image, palette, size = decode_grp((directory / name).read_bytes())
        if size != (400, 640):
            raise ValueError(f'{name}: expected 640x400, got {size}')
        return image, palette

    scroll, _ = load('BOSS7_D1.GRP')
    arena, palette = load('BOSS8_A1.GRP')
    # The pan stops at y16; untouched y0..15 remains the scroll page.
    final = [row[:] for row in scroll[:16] + arena[16:]]
    for i in range(1, 5):
        overlay, _ = load(f'BOSS8_D{i}.GRP')
        for y, row in enumerate(overlay):
            for x, pixel in enumerate(row):
                if pixel != 15:  # GPF_COLORKEY, not color zero!
                    final[y][x] = pixel
    # GPF_PALETTE_SHOW on A1 is the most recent palette load before black-in;
    # the four color-keyed GRPs keep that hardware palette.
    rgb4 = bytes(c // 17 for color in palette for c in color)
    return (scroll, arena, final), rgb4


def build(directory):
    pages, palette = source_pages(directory)
    signatures, lookup, maps = [], {}, array('H')
    for image in pages:
        for y, row in enumerate(image):
            below = image[(y + 1) % HEIGHT]
            for x in range(0, 640, 2):
                pixels = sorted((row[x], row[x + 1], below[x], below[x + 1]))
                signature = sum(p << (i * 4) for i, p in enumerate(pixels))
                if signature not in lookup:
                    lookup[signature] = len(signatures)
                    signatures.append(signature)
                maps.append(lookup[signature])
    if len(signatures) > 3876:  # C(16+4-1,4), all sorted four-color multisets
        raise ValueError('invalid signature dictionary')
    dictionary = struct.pack('<' + 'H' * len(signatures), *signatures)
    if sys.byteorder != 'little':
        maps.byteswap()
    size = HEADER.size + len(palette) + len(dictionary) + len(maps) * 2
    return HEADER.pack(b'KGI1', WIDTH, HEIGHT, PAGES, len(signatures), size) + palette + dictionary + maps.tobytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = build(args.assets)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f'Konngara intro: {len(data)} bytes, {struct.unpack_from("<H", data, 10)[0]} palette signatures')


if __name__ == '__main__':
    main()
