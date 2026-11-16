#!/usr/bin/env python3
"""Halve the original two32x8 pillar masks into16x4 canvas stamps."""
import argparse
from pathlib import Path

from mono_bmp_to_h import load_bmp
from halve_image import shrink_image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bmp', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    image, width, height = load_bmp(args.bmp)
    if (width, height) != (64, 8):
        parser.error('expected the original64x8 two-mask pillar sheet')
    cels = [shrink_image([row[x:x+32] for row in image], 'rare') for x in (0, 32)]
    lines = ['// Generated from pillar.bmp; original body/cap masks,16x4 each.',
             '#pragma once', 'static const unsigned char Pillar_cels[2][64] = {']
    for cel in cels:
        lines.append('    {' + ', '.join(str(p) for row in cel for p in row) + '},')
    lines += ['};', '']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines), encoding='ascii')


if __name__ == '__main__':
    main()
