#!/usr/bin/env python3
"""Exact phases1..3 GPU cache: indexed arena, 16 heads, flash/fade palettes.

No quantization: each index represents all 32 hit-flash/Siddham-fade colors.
The initial GRP head is distinct from the three subsequently painted BOS heads.
The hybrid converter reuses this builder for original/post heads, then assigns
the common BG palette; slash sprite keyframes and the post page live in KHS2.
"""
import argparse
from pathlib import Path
import struct

HEADER = struct.Struct('<4sHHHHI')
ENTRY = struct.Struct('<HHI')
BG_COLORS, OBJ_COLORS, OBJ_FIRST, LEVELS = 208, 144, 112, 16
COLORS = LEVELS * 2
POSE_BYTES = OBJ_COLORS * 2 * COLORS + 4096
BACKDROP = HEADER.size + 32 + BG_COLORS * 2 * COLORS + 16 * ENTRY.size
POSES = BACKDROP + 320 * 192
SIZE = POSES + 16 * POSE_BYTES


def unpack_art(data):
    if HEADER.unpack_from(data) != (b'KGA1', 640, 400, 19, 0, len(data)):
        raise ValueError('expected validated KGA1 artwork')
    palette = data[16:64]
    packed = data[216:128216]
    base = bytearray(p for b in packed for p in (b & 15, b >> 4))
    patches = []
    for i in range(19):
        offset, count = struct.unpack_from('<II', data, 64 + i * 8)
        patches.append([struct.unpack_from('<HBBI', data, offset + j * 8) for j in range(count)])
    return palette, base, patches


def apply_patch(page, patch):
    for offset, mask, _, word in patch:
        for x in range(8):
            if mask & (128 >> x):
                page[offset * 8 + x] = (word >> (x * 4)) & 15


def build(data):
    palette, base, patches = unpack_art(data)
    masks = [{offset: mask for offset, mask, _, _ in patch} for patch in patches]
    # These source invariants make chronological face overwrites equivalent to
    # independent cached poses. Fail rather than silently accepting changed art.
    if any(mask != masks[0] for mask in masks[1:3]) or any(mask != masks[3] for mask in masks[4:12]):
        raise ValueError('head/face alpha masks differ between poses')
    if any(mask & ~masks[0].get(offset, 0) for offset, mask in masks[3].items()):
        raise ValueError('face extends outside head alpha')

    def apply(page, index):
        apply_patch(page, patches[index])

    colors = {}

    def sample(page, x, y):
        signature = tuple(sorted((page[y * 640 + x], page[y * 640 + x + 1],
                                  page[(y + 1) * 640 + x], page[(y + 1) * 640 + x + 1])))
        if signature not in colors:
            colors[signature] = tuple(sum((sum(((white, max(9, white), max(10, white))[c] if p == 9 else
                                               15 if flash and p in (3, 4, 5) else palette[p * 3 + c])
                                                * 17 // 4 for p in signature) >> 3) << (c * 5)
                                          for c in range(3)) for white in range(LEVELS) for flash in (False, True))
        return colors[signature]

    def indexed(pairs, capacity, first):
        unique = sorted(set(pairs))
        if len(unique) > capacity:
            raise ValueError(f'exact palette needs {len(unique)} colors; only {capacity} available')
        lookup = {pair: first + i for i, pair in enumerate(unique)}
        entries = ([(0,) * COLORS] if first == 1 else []) + unique + [(0,) * COLORS] * (capacity - len(unique))
        palettes = b''.join(struct.pack('<H', pair[color]) for color in range(COLORS) for pair in entries)
        return len(unique), palettes, bytes(lookup[pair] for pair in pairs)

    bg_pairs = [sample(base, x, y) for y in range(16, 400, 2) for x in range(0, 640, 2)]
    bg_count, bg_palettes, background = indexed(bg_pairs, BG_COLORS - 1, 1)
    # Bitmap index0 is transparent on DS. Keep every arena pixel opaque,
    # including black, and leave text/stage banks208..255 untouched.
    entries, payload = bytearray(), bytearray()
    for pose in range(16):
        direction, expression = (pose // 4, pose % 4) if pose < 12 else (2, pose - 12)
        page = base[:]
        if pose < 12:
            apply(page, direction)
        if expression:
            apply(page, 9 + direction if expression == 3 else 3 + (expression - 1) * 3 + direction)
        pairs = [sample(page, x, y) for y in range(80, 160, 2) for x in range(280, 360, 2)]
        count, palettes, pixels = indexed(pairs, OBJ_COLORS, OBJ_FIRST)
        texture = bytearray(4096)
        for y in range(40):
            for x in range(40):
                texture[((y // 8) * 8 + x // 8) * 64 + (y % 8) * 8 + x % 8] = pixels[y * 40 + x]
        entries += ENTRY.pack(count, 0, POSES + len(payload))
        payload += palettes + texture
    raw_palette = b''.join(struct.pack('<H', sum((palette[i * 3 + c] * 17 >> 3) << (c * 5)
                                                for c in range(3))) for i in range(16))
    return HEADER.pack(b'KGC2', 320, 192, 16, bg_count, SIZE) + raw_palette + bg_palettes + entries + background + payload


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artwork', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = build(args.artwork.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f'Konngara phases1..3 GPU cache: {len(data)} bytes, 32 exact palette states/16 head poses')
