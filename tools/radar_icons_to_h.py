#!/usr/bin/env python3
"""Pack radar PNGs and the selected font's native16x16 enemy glyph."""
import argparse
from pathlib import Path
import sys
import struct

from PIL import Image
from font16x16 import glyph_rows, load_bitmap

ICONS = (
    ('Player', 'player', 7, 7), ('Orb', 'orb', 7, 7),
    ('Card', 'card', 7, 7), ('Obstacle', 'obstacle', 7, 7),
    ('Portal', 'portal', 7, 7), ('Item', 'item', 6, 6),
    ('Turret', 'turret', 7, 7), ('BarH', 'barrier_horizontal', 8, 2),
    ('BarV', 'barrier_vertical', 2, 8),
)


def load_icons(directory, font):
    result = []
    for symbol, name, width, height in ICONS:
        path = directory / (name + '.png')
        with Image.open(path) as image:
            if image.format != 'PNG' or image.size != (width, height):
                raise ValueError(f'{path}: expected a{width}x{height} PNG')
            pixels = list(image.convert('RGBA').getdata())
        if any(a not in (0, 255) for _, _, _, a in pixels):
            raise ValueError(f'{path}: use binary transparency, no antialiasing')
        colors = {pixel[:3] for pixel in pixels if pixel[3]}
        if len(colors) != 1:
            raise ValueError(f'{path}: expected one opaque ink color and transparent background')
        r, g, b = colors.pop()
        color = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
        result.append((symbol, width, height, color, [int(bool(pixel[3])) for pixel in pixels]))
    # Extract from the replaceable input on every asset rebuild. Never write a
    # boss PNG or depend on a copied glyph/particular font's pixel signature.
    data, offset = load_bitmap(font)
    if offset < 62 or len(data) < offset + 2048 * 256:
        raise ValueError(f'{font}: truncated font bitmap')
    rows = glyph_rows(data, offset, 0x4528)  # 敵, JIS X0208
    purple = (160 >> 3) | ((32 >> 3) << 5) | ((240 >> 3) << 10)
    result.append(('Boss', 16, 16, purple,
                   [int(bool(row & (0x8000 >> x))) for row in rows for x in range(16)]))
    return result


def build(directory, font):
    icons = [('None', 0, 0, 0, [0])] + load_icons(directory, font)
    lines = [
        '// Generated from radar PNGs and assets/font.bmp (boss: JIS0x4528); do not edit.',
        '// Native pixels:0=transparent,1=ink. No shrinking, dithering or runtime scaling.',
        '#pragma once',
        'enum RadarIcon { ' + ', '.join(f'RadarIcon_{icon[0]}={i}' for i, icon in enumerate(icons)) + ' };',
    ]
    for name, index in (('width', 1), ('height', 2), ('colors', 3)):
        kind = 'short' if name == 'colors' else 'char'
        lines.append(f'inline constexpr unsigned {kind} RadarIcons_{name}[] = {{' +
                     ','.join(str(icon[index]) for icon in icons) + '};')
    offsets, offset = [], 0
    for _, _, _, _, pixels in icons:
        offsets.append(offset)
        offset += len(pixels)
    lines.append('inline constexpr unsigned short RadarIcons_offsets[] = {' +
                 ','.join(map(str, offsets)) + '};')
    lines.append(f'inline constexpr unsigned char RadarIcons_pixels[{offset}] = {{')
    for symbol, _, _, _, pixels in icons:
        lines.append('    ' + ','.join(map(str, pixels)) + ', // ' + symbol)
    return '\n'.join(lines + ['};', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--font', type=Path, required=True, help='Replaceable PC-98 font.bmp input')
    args = parser.parse_args()
    try:
        header = build(args.directory, args.font)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(header, encoding='ascii')
    except (OSError, ValueError, struct.error) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
