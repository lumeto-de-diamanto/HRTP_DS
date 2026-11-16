#!/usr/bin/env python3
"""Keep original RGB4 channels/signatures for exact Mima defeat white fades."""
import argparse
from pathlib import Path
from grp_to_bin import decode_grp, halve_background


def build(source):
    image, palette, _ = decode_grp(source.read_bytes())
    half, colors = halve_background(image, palette)
    signatures = {}
    for y, row in enumerate(half):
        for x, index in enumerate(row):
            signature = tuple(sorted(image[y*2+a][x*2+b] for a in range(2) for b in range(2)))
            if index in signatures and signatures[index] != signature:
                raise ValueError('ambiguous Mima background signature')
            signatures[index] = signature
    lines = ['// Generated from BOSS3.GRP; RGB4 components and four-source-pixel signatures.',
             '#pragma once', 'static const unsigned char Mima_defeat_rgb[16][3] = {']
    lines += ['    {' + ','.join(str(c//17) for c in rgb) + '},' for rgb in palette]
    lines += ['};', f'static const unsigned int Mima_defeat_signatures[{len(colors)}] = {{']
    for i in range(len(colors)):
        pixels = (i,)*4 if i < 16 else signatures.get(i)
        # 0x10000 marks unused duplicate palette entries, not image pixels.
        value = sum(p << (j*4) for j, p in enumerate(pixels)) if pixels else 0x10000
        lines.append(f'    0x{value:05X},')
    return '\n'.join(lines + ['};', ''])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(build(args.source), encoding='ascii')
