#!/usr/bin/env python3
"""Recreate the static DS icon from the externally supplied Yuji Syuku TTF.

Uses only Pillow, not an installed system font or a network download. The normal
ROM build consumes the committed icon.gif and does not need this TTF.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

GLYPH = '\u9748'  # 靈, not the simplified 霊 (U+970A)


def render(font_path):
    font = ImageFont.truetype(str(font_path), 256)
    left, top, right, bottom = font.getbbox(GLYPH)
    mask = Image.new('L', (right - left + 16, bottom - top + 16))
    ImageDraw.Draw(mask).text((8 - left, 8 - top), GLYPH, font=font, fill=255)
    bounds = mask.getbbox()
    if bounds is None:
        raise ValueError('font produced an empty glyph')
    mask = mask.crop(bounds)
    scale = min(30 / mask.width, 30 / mask.height)
    size = (max(1, round(mask.width * scale)), max(1, round(mask.height * scale)))
    mask = mask.resize(size, Image.Resampling.LANCZOS)
    coverage = Image.new('L', (32, 32))
    coverage.paste(mask, ((32 - mask.width) // 2, (32 - mask.height) // 2))

    # Reserve index0 for DS transparency, but never use it: even the black
    # background must be opaque. Black +14 antialias reds fit the other slots.
    image = Image.new('P', (32, 32))
    palette = [255, 0, 255, 0, 0, 0]
    for level in range(1, 15):
        red5 = (level * 31 + 7) // 14
        palette.extend(((red5 * 255 + 15) // 31, 0, 0))
    image.putpalette(palette)
    image.putdata([1 + (value * 14 + 127) // 255 for value in coverage.getdata()])
    return image


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('font', type=Path, help='YujiSyuku-Regular.ttf')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    render(args.font).save(args.output, format='GIF', optimize=False)
