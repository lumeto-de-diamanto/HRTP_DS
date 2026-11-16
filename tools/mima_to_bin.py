#!/usr/bin/env python3
"""Build Mima's source-derived split-OBJ art/entrance review asset.

Keep every 128x160 source row. Reconstruct spread-in BEFORE halving, including
the original missing center row and extra bottom row. Each 64x96 tiled frame
is a 64x64 + 64x32 OBJ pair; no cropping or additional scaling.
Animation blocks identical to the base reuse its shrunken pixel, preventing
error diffusion from making unchanged parts of the body shimmer.
"""
import argparse
from pathlib import Path
import struct

from bos_to_h import decode_bos, pack_frame_4bpp
from grp_to_bin import decode_grp
from halve_image import shrink_image

FRAME_BYTES = 3072
FRAME_COUNT = 47


def shrink_animation(base, animation, base_half):
    """Dither normally, then restore each spatially identical2x2 base block.

    Compare the composited source indices, including transparent zero, BEFORE
    shrinking. Changed blocks keep their ordinary animation-dither result.
    The caller caches base_half once; neither source image is modified.
    """
    half = shrink_image(animation, 'dither')
    for y, row in enumerate(half):
        sy = y * 2
        for x in range(len(row)):
            sx = x * 2
            if (animation[sy][sx:sx+2] == base[sy][sx:sx+2] and
                    animation[sy+1][sx:sx+2] == base[sy+1][sx:sx+2]):
                row[x] = base_half[y][x]
    return half


def build(packfile: Path, background: Path) -> bytes:
    still, size = decode_bos((packfile / 'BOSS3_1.BOS').read_bytes())
    anim, anim_size = decode_bos((packfile / 'BOSS3_2.BOS').read_bytes())
    if size != (128, 160) or len(still) != 1 or anim_size != (128, 64) or len(anim) != 5:
        raise ValueError('expected original Mima 128x160 body and five 128x64 animation cels')
    _, colors, dimensions = decode_grp(background.read_bytes())
    if dimensions != (400, 640):
        raise ValueError('expected original 640x400 BOSS3.GRP')
    palette = [0] + [(r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
                     for r, g, b in colors[:15]]
    frames = []
    for step in range(41):
        page = [[0] * 128 for _ in range(192)]
        extent = step * 2
        for row in range(extent):
            page[80 - extent + row] = still[0][row][:]
            page[80 + extent - row] = still[0][159 - row][:]
        frames.append(page)
    frames.append([row[:] for row in still[0]] + [[0] * 128 for _ in range(32)])
    for cel in anim:
        page = [row[:] for row in frames[41]]
        for y, row in enumerate(cel):
            for x, pixel in enumerate(row):
                if pixel:
                    page[48 + y][x] = pixel
        frames.append(page)
    payload = bytearray()
    base = frames[41]
    base_half = shrink_image(base, 'dither')
    for index, page in enumerate(frames):
        if index >= 42:
            half = shrink_animation(base, page, base_half)
        else:
            half = base_half if index == 41 else shrink_image(page, 'dither')
        words = pack_frame_4bpp(half)
        if len(words) * 4 != FRAME_BYTES:
            raise ValueError('invalid split-OBJ frame size')
        payload.extend(struct.pack('<768I', *words))
    size = 48 + FRAME_COUNT * FRAME_BYTES
    return struct.pack('<4sHHHHI16H', b'MIM1', 64, 96, FRAME_COUNT, FRAME_BYTES, size,
                       *palette) + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packfile', type=Path)
    parser.add_argument('background', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = build(args.packfile, args.background)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)


if __name__ == '__main__':
    main()
