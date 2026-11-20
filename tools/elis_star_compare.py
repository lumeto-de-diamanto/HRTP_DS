#!/usr/bin/env python3
"""Render a review sheet from the actual Elis assets, without changing them."""
import argparse
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont

from halve_image import shrink_image


ROOT = Path(__file__).resolve().parent.parent
YELLOW = (221, 204, 68)


def build():
    shapes = Image.open(ROOT / 'assets/sprites/shape8x8.bmp')
    native = [[int(bool(shapes.getpixel((8+x, y)))) for x in range(8)] for y in range(8)]
    binary = Image.new('L', (8, 8))
    binary.putdata([p*255 for row in native for p in row])
    candidates = [(method, shrink_image(native, method, [0, 0x233B]))
                  for method in ('rare', 'topleft', 'topleft2', 'stagger', 'dither', 'salient')]
    for method, resample in [('6px nearest', Image.Resampling.NEAREST), ('6px area >=50%', Image.Resampling.BOX)]:
        small = binary.resize((6, 6), resample)
        candidates.append((method, [[int(small.getpixel((x, y)) >= 128) for x in range(6)] for y in range(6)]))
    candidates.append(('8px original, unscaled', native))

    # Reproduce the port's palette-mixture background from the actual ELS4 asset.
    art = (ROOT / 'nitrofs/elis_art.bin').read_bytes()
    magic, _, _, _, _, _, count, indices, signatures, body, masks = struct.unpack_from('<4s6H4I', art)
    if magic != b'ELS4':
        raise ValueError('expected the current ELS4 asset')
    current = art[masks+5*256:masks+5*256+16]
    palette = [(0, 0, 0)] + [tuple(c*17 for c in art[32+i*3:35+i*3]) for i in range(1, 16)]
    mixtures = []
    for i in range(count):
        signature = struct.unpack_from('<H', art, signatures+i*2)[0]
        mixtures.append(tuple(sum(palette[(signature >> (p*4)) & 15][c] for p in range(4))//4 for c in range(3)))
    arena = Image.new('RGB', (320, 192))
    arena.putdata([mixtures[i-16] for i in art[indices:indices+320*192]])
    # Still girl at source (256,96), projected into the background's y=-8 view.
    for y in range(48):
        for x in range(64):
            index = art[body+y*64+x]
            if index:
                arena.putpixel((128+x, 40+y), palette[index])
    context = arena.crop((64, 16, 256, 128))

    sheet = Image.new('RGB', (1056, 914), '#151922')
    draw = ImageDraw.Draw(sheet)
    title = ImageFont.truetype('DejaVuSans.ttf', 23)
    label = ImageFont.truetype('DejaVuSans.ttf', 17)
    small_font = ImageFont.truetype('DejaVuSans.ttf', 12)
    draw.text((20, 15), 'Elis teleport stars: exact pixel comparisons', font=title, fill='white')
    draw.text((20, 49), 'Left: 16x pixel grid. Right: actual-size DS arena composite, with the same five star positions.', font=small_font, fill='#ccd0d9')
    draw.text((20, 70), 'Top two rows: existing 4x4 methods. Bottom row: larger-size alternatives (not strict half scale).', font=small_font, fill='#ccd0d9')
    for i, (name, rows) in enumerate(candidates):
        x, y = 16+(i % 3)*344, 103+(i//3)*258
        w, h = len(rows[0]), len(rows)
        draw.rounded_rectangle((x, y, x+335, y+247), radius=6, fill='#242a36')
        suffix = ' [current]' if (w, h) == (4, 4) and bytes(p for row in rows for p in row) == current else ' [4px pick]' if name == 'dither' else ''
        draw.text((x+10, y+10), name+suffix, font=label, fill=YELLOW if suffix else 'white')
        draw.text((x+10, y+36), f'{w}x{h}, {sum(map(sum, rows))} yellow pixels', font=small_font, fill='#b8c0d0')
        for py, row in enumerate(rows):
            for px, p in enumerate(row):
                xx, yy = x+10+px*16, y+62+py*16
                draw.rectangle((xx, yy, xx+15, yy+15), fill=YELLOW if p else '#090b10', outline='#414858')
        preview = context.copy()
        for cx, cy in ((49, 20), (118, 24), (42, 65), (125, 77), (89, 95)):
            for py, row in enumerate(rows):
                for px, p in enumerate(row):
                    if p:
                        preview.putpixel((cx-w//2+px, cy-h//2+py), YELLOW)
        sheet.paste(preview, (x+140, y+62))
        draw.text((x+143, y+179), '192x112 crop, 1:1 DS pixels', font=small_font, fill='#b8c0d0')
        note = ' / '.join(''.join('#' if p else '.' for p in row) for row in rows) if w == 4 else 'Larger than half scale; renderer change needed.'
        draw.text((x+10, y+219), note, font=small_font, fill='#ccd0d9')
    draw.text((20, 886), 'Illustrative composite, not an emulator capture. Original yellow #DDCC44. Game assets are unchanged.', font=small_font, fill='#ccd0d9')
    return sheet


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    build().save(args.output)
