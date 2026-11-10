#!/usr/bin/env python3
"""Pack the fixed 8x16 XPM orb marker into DS 4bpp OBJ tiles.

The three colors use the existing shared overlay palette; no runtime palette
or geometry generation is needed. XPM keeps this tiny graphic text-editable
and usable in image editors without adding a host imaging dependency.
"""
import argparse
from pathlib import Path
import re
import sys


def load_arrow(path: Path) -> list[list[int]]:
    strings = re.findall(r'"([^"\n]*)"', path.read_text(encoding="ascii"))
    if not strings or strings[0].split() != ["8", "16", "3", "1"]:
        raise ValueError("expected an 8x16 XPM with 3 colors and 1 character per pixel")
    if len(strings) != 20:
        raise ValueError("expected 3 palette entries and 16 pixel rows")
    slots = {"none": 0, "#ff0000": 1, "#ffffff": 2}
    palette = {}
    for entry in strings[1:4]:
        match = re.fullmatch(r"(.)\s+c\s+(None|#[0-9a-fA-F]{6})", entry, re.IGNORECASE)
        if not match or match[2].lower() not in slots or match[1] in palette:
            raise ValueError("palette must use unique symbols for transparent, red and white")
        palette[match[1]] = slots[match[2].lower()]
    if set(palette.values()) != {0, 1, 2}:
        raise ValueError("palette must contain transparent, red and white")
    rows = strings[4:]
    if any(len(row) != 8 or any(pixel not in palette for pixel in row) for row in rows):
        raise ValueError("expected 16 rows of 8 declared pixel symbols")
    return [[palette[pixel] for pixel in row] for row in rows]


def convert(source: Path, output: Path) -> None:
    pixels = load_arrow(source)
    # Width 8: each row is one word, top 8x8 tile followed by bottom 8x8.
    # Match other OBJ headers' word alignment for the one-time VRAM upload.
    words = [sum(pixel << (4 * x) for x, pixel in enumerate(row)) for row in pixels]
    lines = [f"// Generated from {source.name} by tools/orb_arrow_to_h.py -- do not edit.",
             "// 8x16, 4bpp OBJ tiles; shared overlay palette slots 0/1/2.",
             "#pragma once", "", "static const unsigned int OrbArrow_tiles[16] = {"]
    lines += ["    " + ", ".join(f"0x{word:08X}" for word in words[i:i + 4]) + ","
              for i in range(0, len(words), 4)]
    lines += ["};", ""]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        convert(args.source, args.output)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
