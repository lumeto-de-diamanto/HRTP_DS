#!/usr/bin/env python3
"""Source RGB4 effects without changing Mima's approved arena indices/art."""
import argparse
from pathlib import Path
from grp_to_bin import decode_grp, halve_background, pack_palette


def build(source: Path) -> str:
    image, palette, _ = decode_grp(source.read_bytes())
    half, colors = halve_background(image, palette)
    signatures = {}
    for y, row in enumerate(half):
        for x, index in enumerate(row):
            signature = tuple(sorted(image[y*2+a][x*2+b] for a in range(2) for b in range(2)))
            if index in signatures and signatures[index] != signature:
                raise ValueError('ambiguous source signature in Mima arena palette')
            signatures[index] = signature
    states = []
    for darkness in range(0, 17, 4):
        pair = []
        # mode0=normal;1..5=flashed colors at RGB4 levels15/11/7/3/0.
        # A hop can darken an existing flash BETWEEN its two-frame updates.
        for flash in range(6):
            raw = [tuple(max(0,15-(flash-1)*4)*17 if flash and i in (3, 9) else max(0, c//17-darkness)*17 for c in rgb)
                   for i, rgb in enumerate(palette)]
            state = []
            for index in range(len(colors)):
                # Counter.subtract() in the approved converter also retains
                # unused duplicate/negative-count palette entries. They have
                # no image pixels; keep those inert values byte-identical.
                rgb = raw[index] if index < 16 else colors[index] if index not in signatures else tuple(sum(raw[p][c]//4 for p in signatures[index]) for c in range(3))
                state.append(pack_palette([rgb])[0])
            pair.append(state)
        states.append(pair)
    if states[0][0] != pack_palette(colors):
        raise ValueError('normal palette differs from approved arena')
    lines = ['// Generated from BOSS3.GRP; source RGB4 darken and colors3/9 hit flash.',
             '#pragma once', f'constexpr int MIMA_BG_COLORS={len(colors)};',
             f'static const unsigned short Mima_palette_fx[5][6][{len(colors)}] = {{']
    for pair in states:
        lines.append('    {')
        for state in pair:
            lines.append('        {' + ','.join(f'0x{v:04X}' for v in state) + '},')
        lines.append('    },')
    return '\n'.join(lines+['};', ''])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    args = parser.parse_args()
    data = build(args.source)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(data,encoding='ascii')
