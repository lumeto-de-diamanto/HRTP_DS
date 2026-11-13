#!/usr/bin/env python3
"""Konngara review/fight artwork: raw indexed page + masked eight-pixel patches.

KGA1 retains original 640x400 coordinates, BOS alpha (including opaque color15)
and chronological GRZ overwrites. Composition precedes 50% sampling at runtime.
The last arena GRP palette wins; BOS/GRX palettes do not replace it.
"""
import argparse
from pathlib import Path
import struct

from grz_decode import decode_grz
from konngara_to_bin import source_pages

HEADER = struct.Struct('<4sHHHHI')
ENTRY = struct.Struct('<II')
PATCH = struct.Struct('<HBBI')
PATCH_COUNT = 19  # head3, closed/glare6, aim3, slash7
BASE_SIZE = 640 * 400 // 2


def planar_word(planes):
    return sum(sum(((plane >> (7 - x)) & 1) << bit for bit, plane in enumerate(planes)) << (x * 4)
               for x in range(8))


def bos_patches(data: bytes, left: int, top: int, expected_height: int, expected_count: int):
    if len(data) < 64 or data[:4] != b'BOSS':
        raise ValueError('invalid BOS header')
    width, height, count = data[4], data[6], data[8]
    if (width, height, count) != (10, expected_height, expected_count):
        raise ValueError('unexpected Konngara BOS dimensions/count')
    plane_size = width * height
    if len(data) != 64 + count * plane_size * 5:
        raise ValueError('invalid BOS extent')
    frames = []
    for cel in range(count):
        start = 64 + cel * plane_size * 5
        records = bytearray()
        for p in range(plane_size):
            mask = data[start + p] ^ 255
            if mask:
                offset = (top + p // width) * 80 + left // 8 + p % width
                planes = [data[start + p + plane_size * plane] for plane in range(1, 5)]
                records += PATCH.pack(offset, mask, 0, planar_word(planes))
        frames.append(bytes(records))
    return frames


def build(hdi: Path, packfile: Path):
    pages, palette = source_pages(hdi)
    # Entrance final palette: the syllables end at RGB4(0,9,10).
    palette = bytearray(palette)
    palette[9 * 3:9 * 3 + 3] = bytes((0, 9, 10))
    page = pages[2]
    base = bytes(row[x] | row[x + 1] << 4 for row in page for x in range(0, 640, 2))
    frames = bos_patches((packfile / 'BOSS8_1.BOS').read_bytes(), 280, 80, 80, 3)
    frames += bos_patches((packfile / 'BOSS8_E1.BOS').read_bytes(), 280, 128, 32, 6)
    frames += bos_patches((packfile / 'BOSS8_E2.BOS').read_bytes(), 280, 128, 32, 3)
    grz = (hdi / 'BOSS8.GRZ').read_bytes()
    for image in range(7):
        frames.append(b''.join(PATCH.pack(offset, 255, 0, planar_word(planes))
                               for offset, planes in decode_grz(grz, image)))
    offset = HEADER.size + len(palette) + PATCH_COUNT * ENTRY.size + BASE_SIZE
    entries = bytearray()
    for frame in frames:
        entries += ENTRY.pack(offset, len(frame) // PATCH.size)
        offset += len(frame)
    return HEADER.pack(b'KGA1', 640, 400, PATCH_COUNT, 0, offset) + palette + entries + base + b''.join(frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('hdi', type=Path)
    parser.add_argument('packfile', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = build(args.hdi, args.packfile)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f'Konngara artwork: {len(data)} bytes, 19 original-coordinate patches')


if __name__ == '__main__':
    main()
