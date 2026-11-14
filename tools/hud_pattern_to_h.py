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
             '#pragma once', 'alignas(4) inline constexpr unsigned char HudPattern_pixels[128*128] = {']
    lines += ['    ' + ','.join(map(str, pixels[row:row+128])) + ','
              for row in range(0, len(pixels), 128)]
    return '\n'.join(lines + ['};', ''])


def results_crop(data):
    image, palette, size = decode_grp(data)
    if size != (400, 640):
        raise ValueError('expected the original 640x400 CLEAR3 artwork')
    return bytes(pixel for row in image[64:256] for pixel in row[384:640]), palette


def build_results(data):
    pixels, palette = results_crop(data)
    colors = [(r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) for r, g, b in palette]
    lines = ['// Generated: unscaled CLEAR3.GRP crop(384,64,256,192), original palette.',
             '#pragma once', 'alignas(4) inline constexpr unsigned char RoundResults_pixels[256*192] = {']
    lines += ['    ' + ','.join(map(str, pixels[row:row+256])) + ','
              for row in range(0, len(pixels), 256)]
    lines += ['};', 'inline constexpr unsigned short RoundResults_palette[16] = {',
              '    ' + ','.join(map(str, colors)), '};', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--results', action='store_true', help='emit the unscaled Reimu results crop')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text((build_results if args.results else build)(args.input.read_bytes()), encoding='ascii')
