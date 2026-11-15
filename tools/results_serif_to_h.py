#!/usr/bin/env python3
"""Pack fixed Libre Bodoni numerals and pre-shaped label bitmaps into DS rows.

The checked-in BDF is the master: builds need no font engine or system font.
Private-use glyphs contain complete proportionally spaced strings, not letters.
"""
import argparse
from pathlib import Path
import re
import sys

ALPHABET = '0123456789'
LABELS = ('Stage', 'Complete', 'Time', 'MaxCombo', 'BombLife', 'Clear', 'Total', 'Prompt')
CODES = tuple(map(ord, ALPHABET)) + tuple(range(0xE000, 0xE000 + len(LABELS)))


def glyphs(text):
    lines = text.splitlines()
    if not lines or lines[0] != 'STARTFONT 2.1' or lines[-1] != 'ENDFONT':
        raise ValueError('expected a complete BDF 2.1 font')
    for field in ('FONTBOUNDINGBOX 128 16 0 0', 'FONT_ASCENT 16',
                  'FONT_DESCENT 0', f'CHARS {len(CODES)}'):
        if field not in lines:
            raise ValueError(f'expected {field}')
    found = {}
    records = re.findall(r'^STARTCHAR [^\n]+\n(.*?)\nENDCHAR$', text, re.M | re.S)
    for record in records:
        encoding = re.search(r'^ENCODING (\d+)$', record, re.M)
        if not encoding:
            raise ValueError('missing glyph encoding')
        code = int(encoding[1])
        if code not in CODES or code in found:
            raise ValueError(f'unexpected/duplicate glyph {code}')
        metrics = re.search(r'^BBX (\d+) 16 0 0$', record, re.M)
        if not metrics:
            raise ValueError('expected a 16-pixel-high bitmap strip')
        width = int(metrics[1])
        if not 16 <= width <= 128 or width % 16 or (code < 0xE000 and width != 16):
            raise ValueError('expected 16-pixel cells, with fixed-width numerals')
        if f'DWIDTH {width} 0' not in record.splitlines():
            raise ValueError('inconsistent strip advance')
        bitmap = record.split('BITMAP\n')
        if len(bitmap) != 2:
            raise ValueError('missing glyph bitmap')
        rows = bitmap[1].splitlines()
        if len(rows) != 16 or any(not re.fullmatch(r'[0-9A-Fa-f]{' + str(width // 4) + '}', row) for row in rows):
            raise ValueError('expected sixteen full-width bitmap rows')
        cells = [[int(row[x:x+4], 16) for row in rows] for x in range(0, width // 4, 4)]
        if not any(any(cell) for cell in cells):
            raise ValueError('empty production strip')
        found[code] = cells
    if set(found) != set(CODES):
        raise ValueError('incomplete results numerals/labels')
    return [found[code] for code in CODES]


def build(text):
    strips = glyphs(text)
    rows = [cell for strip in strips for cell in strip]
    first, offset = [], 0
    for strip in strips:
        first.append(offset)
        offset += len(strip)
    lines = [
        '// Generated from assets/fonts/results_serif.bdf; do not edit.',
        '// HRTP Results Serif: Libre Bodoni Bold derivative, SIL OFL 1.1.',
        '// Copyright 2012 The Libre Bodoni Project Authors.',
        '// Full notice: assets/licenses/LibreBodoni-OFL.txt (also in NitroFS).',
        '#pragma once',
        'enum ResultsSerif_label { ' + ', '.join(f'ResultsSerif_{name}={i+10}' for i, name in enumerate(LABELS)) + ' };',
        'inline constexpr unsigned short ResultsSerif_first[] = {' + ', '.join(map(str, first)) + '};',
        'inline constexpr unsigned char ResultsSerif_cells[] = {' + ', '.join(str(len(s)) for s in strips) + '};',
        f'inline constexpr unsigned short ResultsSerif_rows[{len(rows)}][16] = {{',
    ]
    for cell in rows:
        lines.append('    {' + ', '.join(f'0x{row:04X}' for row in cell) + '},')
    return '\n'.join(lines + ['};', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        header = build(args.input.read_text(encoding='ascii'))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(header, encoding='ascii')
    except (OSError, ValueError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
