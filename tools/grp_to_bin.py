'''
The palette and the image itself will be saved seperately;
    the palette as a plain .h file,
    and the image as an bitmap,
    where each byte in the bitmap represents two pixels.
Other tools can then be used to compress the image.
'''

import argparse
import math
from pathlib import Path
import struct
import sys
import halve_image

INITIAL_COLORS = 16

def decode_palette(pal_bytes: bytes) -> list[tuple[int, int, int]]:
    colors = []
    for i in range(INITIAL_COLORS):
        r, g, b = pal_bytes[i * 3 : i * 3 + 3]
        # r, g and b range from 0 to 15.
        ds_r = r * 0x11 #(r * 0x11 >> 3)
        ds_g = g * 0x11 #(g * 0x11 >> 3)
        ds_b = b * 0x11 #(b * 0x11 >> 3)
        #ds_color = (ds_b << 10) | (ds_g << 5) | (ds_r << 0)
        ds_color = (ds_r, ds_g, ds_b)
        colors.append(ds_color)
    return colors #[colors[15]] + colors[:15]

class BitReader:
    """
    Reads bits MSB-first (0x80 down to 0x01 within each byte), matching
    the spec's explicit statement: "Bits are read from high to low, $80
    to $01."
    """

    def __init__(self, data: bytes, offset: int):
        self.data = data
        self.byte_offset = offset
        self.bit_mask = 0x80

    def read_bit(self) -> int:
        if self.byte_offset >= len(self.data):
            return 0
        byte = self.data[self.byte_offset]
        bit = 1 if (byte & self.bit_mask) else 0
        self.bit_mask >>= 1
        if self.bit_mask == 0:
            self.bit_mask = 0x80
            self.byte_offset += 1
        return bit

    def read_bits(self, n: int) -> int:
        value = 0
        for _ in range(n):
            value = (value << 1) | self.read_bit()
        return value

def read_length_code(reader: BitReader) -> int:
    ones = 0
    while reader.read_bit() == 1:
        ones += 1
    if ones == 0:
        return 1
    data_bits = ones
    range_start = 1 << ones  # 1->2, 2->4, 3->8, 4->16, ...
    data = reader.read_bits(data_bits)
    return range_start + data

# 4-bit (16-color) delta code prefix table: (prefix_bits, prefix_length,
# data_bit_count, range_start). Verified directly against the spec's
# worked example (see verify_against_worked_example()).
DELTA_PREFIXES = [
    (0b1, 1, 1, 0),      # 1x     -> 0-1
    (0b00, 2, 1, 2),     # 00x    -> 2-3
    (0b010, 3, 2, 4),    # 010xx  -> 4-7
    (0b011, 3, 3, 8),    # 011xxx -> 8-15
]

def decode_pixels(
    data: bytes, offset: int, width: int, height: int
) -> bytearray:
    reader = BitReader(data, offset)
    out = bytearray(width * height)
    out_pos = 0
    table = [
        [(INITIAL_COLORS + a - b) % INITIAL_COLORS for b in range(INITIAL_COLORS)]
        for a in range(INITIAL_COLORS)
    ]

    last_byte_out = 0
    last_rep_type = 255  # sentinel: no real repetition type code equals this
    doing_repetition = True

    # Repetition type codes as small integers matching pi.pas's own
    # literal encoding (00, 01, 10, 110, 111 read as decimal-looking
    # ints 0/1/10/110/111) -- kept as these exact "shaped" values,
    # rather than translated to 0-4, specifically so this stays
    # trivially diffable against pi.pas if either ever needs re-checking.
    REP_LAST4 = 0
    REP_ROW1 = 1
    REP_ROW2 = 10
    REP_ROW1_AHEAD = 110
    REP_ROW1_BACK = 111

    def read_repetition_type() -> int:
        if reader.read_bit():
            if reader.read_bit():
                # pi.pas: `if ReadBit then result := 111 else result := 110`
                # -- third-bit=1 -> 111 (row1_back), third-bit=0 -> 110
                # (row1_ahead). An earlier version of this had these
                # swapped (a real transcription error, not a
                # re-derivation from prose this time) -- caught by
                # cross-checking this function's bit-for-bit output
                # against a direct, separate port of pi.pas's own
                # _GetRepetitionType over hundreds of random bitstreams,
                # after every other primitive (delta codes, move-to-
                # front table, length codes) had already checked out
                # clean under the same kind of test, narrowing the
                # search down to this specific function.
                return REP_ROW1_BACK if reader.read_bit() else REP_ROW1_AHEAD
            return REP_ROW2
        return REP_ROW1 if reader.read_bit() else REP_LAST4

    def read_delta_code(reader: BitReader, prefixes) -> int:
        accumulated = 0
        length = 0
        for prefix_bits, prefix_len, data_bits, range_start in sorted(
            prefixes, key=lambda p: p[1]
        ):
            while length < prefix_len:
                accumulated = (accumulated << 1) | reader.read_bit()
                length += 1
            if accumulated == prefix_bits:
                data = reader.read_bits(data_bits)
                return range_start + data
        raise ValueError(f"no matching delta code prefix (read {length} bits: {accumulated:0{length}b})")

    def move_to_front_update(table, a: int, b: int):
        value = table[a][b]
        row = table[a]
        for i in range(b, 0, -1):
            row[i] = row[i - 1]
        row[0] = value
        return value

    def process_delta_code():
        nonlocal out_pos, last_byte_out
        if out_pos >= len(out):
            return
        code = read_delta_code(reader, DELTA_PREFIXES)
        value = move_to_front_update(table, last_byte_out, code)
        out[out_pos] = value
        out_pos += 1
        last_byte_out = value

    def copy_bytes(rep_len: int, rep_ofs: int, oob_filler: bytes):
        nonlocal out_pos
        oob_count = rep_ofs - out_pos
        if oob_count > 0:
            oob_count = min(oob_count, rep_len)
            for i in range(oob_count):
                if out_pos >= len(out):
                    return
                out[out_pos] = oob_filler[i % 2]
                out_pos += 1
            rep_len -= oob_count
        src = out_pos - rep_ofs
        for _ in range(rep_len):
            if out_pos >= len(out):
                return
            out[out_pos] = out[src]
            out_pos += 1
            src += 1

    def process_repetition_code(minus_reps: int):
        nonlocal last_rep_type, doing_repetition, out_pos, last_byte_out
        rep_type = read_repetition_type()
        if last_rep_type == rep_type:
            doing_repetition = False
            last_byte_out = out[out_pos - 1] if out_pos > 0 else 0
            return
        last_rep_type = rep_type

        rep_length = (read_length_code(reader) - minus_reps) * 2
        rep_length = max(0, rep_length)
        rep_length = min(rep_length, len(out) - out_pos)
        if rep_length == 0:
            return

        if rep_type == REP_LAST4:
            # "Location 0": repeat the last 4 bytes, UNLESS the last two
            # bytes are equal (or we're within the first 4 bytes of the
            # image), in which case repeat the last 2 bytes instead.
            b0 = out[out_pos - 2] if out_pos >= 2 else 0
            b1 = out[out_pos - 1] if out_pos >= 1 else 0
            if out_pos < 4 or b0 == b1:
                pair = bytes([b0, b1])
                for i in range(rep_length):
                    if out_pos >= len(out):
                        break
                    out[out_pos] = pair[i % 2]
                    out_pos += 1
            else:
                quad = bytes(out[out_pos - 4 : out_pos])
                for i in range(rep_length):
                    if out_pos >= len(out):
                        break
                    out[out_pos] = quad[i % 4]
                    out_pos += 1
        elif rep_type == REP_ROW1:
            filler = bytes([out[0], out[1] if len(out) > 1 else out[0]])
            copy_bytes(rep_length, width, filler)
        elif rep_type == REP_ROW2:
            filler = bytes([out[0], out[1] if len(out) > 1 else out[0]])
            copy_bytes(rep_length, width * 2, filler)
        elif rep_type == REP_ROW1_AHEAD:
            # pi.pas: swap(word(startp^)) -- the first two output bytes,
            # byte-swapped.
            filler = bytes([out[1] if len(out) > 1 else out[0], out[0]])
            copy_bytes(rep_length, width - 1, filler)
        elif rep_type == REP_ROW1_BACK:
            filler = bytes([out[1] if len(out) > 1 else out[0], out[0]])
            copy_bytes(rep_length, width + 1, filler)

    # Two delta codes, then one FORCED repetition (length - 1).
    process_delta_code()
    process_delta_code()
    process_repetition_code(minus_reps=1)

    while out_pos < len(out):
        if doing_repetition:
            process_repetition_code(minus_reps=0)
        else:
            process_delta_code()
            process_delta_code()
            if not reader.read_bit():
                doing_repetition = True
                last_rep_type = 255

    return out

def decode_grp(data: bytes):
    magic = data[0:2]
    if magic != b'ZN':
        raise ValueError(f"bad magic {magic!r}, expected ZN")

    pos = 2
    pos = data.index(b"\x1a", pos) + 1
    pos = data.index(b"\x00", pos) + 1

    mode_byte = data[pos]; pos += 1
    aspect_x = data[pos]; pos += 1
    aspect_y = data[pos]; pos += 1
    bitdepth = data[pos]; pos += 1

    compressor_sig = data[pos : pos + 4]; pos += 4
    compressor_data_size = struct.unpack_from(">H", data, pos)[0]; pos += 2
    pos += compressor_data_size

    width = struct.unpack_from(">H", data, pos)[0]; pos += 2
    height = struct.unpack_from(">H", data, pos)[0]; pos += 2

    palette_bytes = INITIAL_COLORS * 3
    palette_raw = data[pos : pos + palette_bytes]
    pos += palette_bytes

    palette = decode_palette(palette_raw)

    bytes = decode_pixels(data, pos, width, height)
    bytes = [list(bytes[i:i + width]) for i in range(0, width*height, width)]

    return bytes, palette, (height, width)

from collections import Counter

# The background bitmap shares the 256-color BG palette with the tiled layers.
# Budget (see docs/ds-port-plan.md, landmine #1):
#   0..207  background bitmap (0..15 = original GRP colors, preserved for
#           boss palette effects such as Sariel's recolors)
#   208..239  text layer banks 13 (red) and 14 (white) - source/platform/
#             text_layer.hpp
#   240..255  stage-object tiles (BG1) - palette bank 15
BG_COLORS = 208

def halve_background(image: list[list[int]], palette: list[tuple[int, int, int]]):
    height = len(image)
    width = len(image[0])

    new_image = []
    for y in range(height // 2):
        new_row = []
        for x in range(width // 2):
            indices = [
                image[y * 2    ][x * 2    ],
                image[y * 2    ][x * 2 + 1],
                image[y * 2 + 1][x * 2    ],
                image[y * 2 + 1][x * 2 + 1]
            ]
            new_r = 0
            new_g = 0
            new_b = 0
            for i in indices:
                color = palette[i]
                new_r += color[0] // 4
                new_g += color[1] // 4
                new_b += color[2] // 4
            new_row.append((new_r, new_g, new_b))
        new_image.append(new_row)
    pixels_and_counts = Counter([i for s in new_image for i in s])
    # Intentionally preserve the 16 initial colors for Sariel palette changes
    pixels_and_counts.subtract(palette) 
    pixels_and_counts = pixels_and_counts.most_common(BG_COLORS - INITIAL_COLORS)

    new_palette = palette + [p[0] for p in pixels_and_counts]

    for y in range(height // 2):
        for x in range(width // 2):
            color = new_image[y][x]
            if color in new_palette:
                new_image[y][x] = new_palette.index(color)
            else:
                distances = [
                    math.pow(p[0] - color[0], 2) +
                    math.pow(p[1] - color[1], 2) +
                    math.pow(p[2] - color[2], 2)
                    for p in new_palette]
                min_index = distances.index(min(distances))
                new_image[y][x] = min_index

    return new_image, new_palette

def pack_palette(palette: list[tuple[int, int, int]]) -> list[int]:
    new_palette = []
    for p in palette:
        r = p[0] >> 3
        g = p[1] >> 3
        b = p[2] >> 3
        ds_color = (b << 10) | (g << 5) | (r << 0)
        new_palette.append(ds_color)
    return new_palette


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("grp_file", type=Path, help="input .GRP file")
    parser.add_argument("palette_outdir", type=Path, help="directory for generated palette .h file")
    parser.add_argument("bin_outdir", type=Path, help="directory for generated image .bin file")
    parser.add_argument("name", type=str, help="name of the generated image")
    # I need to come up with a better resizing method for other bitmap backgrounds,
    #     e.g. the game over screen and the ending screens
    parser.add_argument("--halve", type=str, default="", help="method to halve the size of sprites", required=False)
    args = parser.parse_args()

    if args.grp_file.suffix.lower() != ".grp":
        parser.error(f"input does not have a .GRP extension: {args.grp_file}")
    if not args.grp_file.is_file():
        parser.error(f"input file does not exist: {args.grp_file}")

    if args.halve and args.halve not in halve_image.HALF_METHODS_LIST:
        parser.error(f"invalid halving method {args.halve}")

    try:
        data = args.grp_file.read_bytes()
        image, palette, size = decode_grp(data)
        # halving
        #if args.halve:
        #    image = halve_image.shrink_image(image, args.halve)
        #    size = (size[0] // 2, size[1] // 2)
        image, palette = halve_background(image, palette)
        size = (size[0] // 2, size[1] // 2)
        # Only discard the top 8 halved rows: y=16..399 maps to DS y=0..191,
        # with the original playfield y=64 starting below the 24px HUD. Keep
        # all 320 columns; the runtime streams a camera-dependent 256px view.
        y_topleft = size[0] - 192
        image = image[y_topleft:y_topleft+192]
        size = (192, size[1])
        # packing
        image = bytes([i for s in image for i in s])
        palette = pack_palette(palette)
        args.palette_outdir.mkdir(parents=True, exist_ok=True)
        palette_output_path = args.palette_outdir / f"{args.name}.h"
        write_palette_header(palette_output_path, args.name, palette)
        args.bin_outdir.mkdir(parents=True, exist_ok=True)
        bin_output_path = args.bin_outdir / f"{args.name}.bin"
        with open(bin_output_path, "wb") as binary_file:
            binary_file.write(image)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0

def write_palette_header(path: Path, name: str, palette: list[int]) -> None:
    '''
    lines = [f"// Background size = {size[0]}x{size[1]}", 
            f"const unsigned int {name}_tiles[{len(image)}] =", 
            "{"]
    for j in range(0, len(image), 8):
        chunk = image[j : j + 8]
        suffix = ","
        lines.append("\t" + ",".join(f"0x{value:08X}" for value in chunk) + suffix)
    lines.append("};\n")

    lines.append(f"const unsigned short {name}_palette[{len(palette)}] __attribute__((aligned(4))) = ")
    lines.append("{")
    '''
    lines = [f"const unsigned short {name}_palette[{len(palette)}] __attribute__((aligned(4))) = ", "{"]
    for i in range(0, len(palette), 8):
        lines.append("\t" + ", ".join(f"0x{value:04X}" for value in palette[i:i+8]) + ",")
    lines.append("};")

    path.write_text("\n".join(lines) + "\n", encoding="ascii")

if __name__ == "__main__":
    raise SystemExit(main())
