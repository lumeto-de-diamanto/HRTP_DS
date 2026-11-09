#!/usr/bin/env python3
import argparse
import struct
import sys
from pathlib import Path

import halve_image

HEADER_SIZE = 6
PALETTE_COLORS = 16
PALETTE_SIZE = PALETTE_COLORS * 3

PLANE_ROW_BYTES = 4
PLANE_SIZE = 128
IMAGE_DATA_SIZE = 1 + (4 * PLANE_SIZE)

def decode_palette(pal_bytes: bytes) -> list[int]:
    colors = []
    for i in range(PALETTE_COLORS):
        r, g, b = pal_bytes[i * 3 : i * 3 + 3]
        # r, g and b range from 0 to 15.
        ds_r = (r * 0x11 >> 3)
        ds_g = (g * 0x11 >> 3)
        ds_b = (b * 0x11 >> 3)
        ds_color = (ds_b << 10) | (ds_g << 5) | (ds_r << 0)
        colors.append(ds_color)
    return [colors[15]] + colors[:15]

def decode_chunk(chunk: bytes) -> list[list[int]]:
    plane_bytes = chunk[1:]
    planes = {
        "B": plane_bytes[0 * PLANE_SIZE : 1 * PLANE_SIZE],
        "R": plane_bytes[1 * PLANE_SIZE : 2 * PLANE_SIZE],
        "G": plane_bytes[2 * PLANE_SIZE : 3 * PLANE_SIZE],
        "E": plane_bytes[3 * PLANE_SIZE : 4 * PLANE_SIZE],
    }

    image = []
    for y in range(32):
        row_off = y * PLANE_ROW_BYTES
        b_row = planes["B"][row_off : row_off + PLANE_ROW_BYTES]
        r_row = planes["R"][row_off : row_off + PLANE_ROW_BYTES]
        g_row = planes["G"][row_off : row_off + PLANE_ROW_BYTES]
        e_row = planes["E"][row_off : row_off + PLANE_ROW_BYTES]
        image_row = [];
        for x in range(32):
            byte_idx = x // 8
            bit_idx = 7 - (x % 8)  # MSB-first
            b = (b_row[byte_idx] >> bit_idx) & 1
            r = (r_row[byte_idx] >> bit_idx) & 1
            g = (g_row[byte_idx] >> bit_idx) & 1
            e = (e_row[byte_idx] >> bit_idx) & 1
            index = (e << 3) | (g << 2) | (r << 1) | b
            index = (index + 1) % 16
            image_row.append(index)
        image.append(image_row)
    return image

def pack_frame_4bpp(pixels: list[list[int]]) -> list[int]:
    height = len(pixels)
    width = len(pixels[0]) if height else 0
    if width == 0 or height == 0:
        raise ValueError("empty frame")
    if width % 8 or height % 8:
        raise ValueError(
            f"sprite dimensions must be multiples of 8 for 4bpp tiles; got {width}x{height}"
        )

    out: list[int] = []
    for tile_y in range(0, height, 8):
        for tile_x in range(0, width, 8):
            for y in range(8):
                for x in range(0, 8, 2):
                    lo = pixels[tile_y + y][tile_x + x] & 0x0F
                    hi = pixels[tile_y + y][tile_x + x + 1] & 0x0F
                    out.append(lo | (hi << 4))
    words = [
        int.from_bytes(out[i : i + 4], byteorder="little")
        for i in range(0, len(out), 4)
    ]
    return words

def decode_ptn(data: bytes):
    magic, _, image_count = struct.unpack_from("<4sbb", data, 0)
    if magic != b"HPTN":
        raise ValueError(f"bad magic {magic!r}, expected HPTN")

    pal_start = HEADER_SIZE
    pal_end = pal_start + PALETTE_SIZE
    palette = decode_palette(data[pal_start:pal_end])

    images = []
    offset = pal_end
    for i in range(image_count):
        chunk = data[offset : offset + IMAGE_DATA_SIZE]
        if len(chunk) < IMAGE_DATA_SIZE:
            raise ValueError(
                f"file truncated: image {i}/{image_count} needs "
                f"{IMAGE_DATA_SIZE} bytes at offset {offset}, only "
                f"{len(chunk)} available"
            )
        images.append(decode_chunk(chunk))
        offset += IMAGE_DATA_SIZE
    return palette, images

def parse_index_spec(spec: str) -> list[int]:
    out: list[int] = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            lo, hi = part.split("-", 1)
            out.extend(range(int(lo), int(hi) + 1))
        else:
            out.append(int(part))
    return out

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ptn_file", type=Path, help="input .PTN file")
    parser.add_argument("outdir", type=Path, help="directory for generated .h file")
    parser.add_argument("name", type=str, help="name of the generated sprite")
    parser.add_argument("--halve", action="append", default=[],
                        help="halving method for all sprites, or method:indices "
                             "to mix methods per image (ranges allowed, e.g. "
                             "dither:7-21 rare:22-33); repeatable", required=False)
    parser.add_argument("-s", "--sprite", nargs='+', type=int, default=[], help="sprite indices", required=False)
    parser.add_argument("-q", "--quarter", action=argparse.BooleanOptionalAction, help="whether frames should be split into quarters")
    parser.add_argument("--linear", action=argparse.BooleanOptionalAction,
                        help="emit row-major indexed pixels instead of 4bpp "
                             "tiles (for software-blitted sprites)")
    args = parser.parse_args()

    if args.ptn_file.suffix.lower() != ".ptn":
        parser.error(f"input does not have a .PTN extension: {args.ptn_file}")
    if not args.ptn_file.is_file():
        parser.error(f"input file does not exist: {args.ptn_file}")

    group_specs = [s for s in args.halve if ":" in s]
    bare_specs = [s for s in args.halve if ":" not in s]
    if len(bare_specs) > 1:
        parser.error("at most one bare --halve method")
    if group_specs and args.sprite:
        parser.error("cannot combine -s with method:indices --halve specs")
    for spec in args.halve:
        method = spec.split(":", 1)[0]
        if method not in halve_image.HALF_METHODS_LIST:
            parser.error(f"invalid halving method {method}")

    try:
        data = args.ptn_file.read_bytes()
        palette, all_images = decode_ptn(data)

        # selection: either method:indices groups (sorted by index), or -s /
        # everything, with at most one bare method for all of them
        if group_specs:
            selections: list[tuple[int, str]] = []
            for spec in group_specs:
                method, idx_spec = spec.split(":", 1)
                for i in parse_index_spec(idx_spec):
                    selections.append((i, method))
            selections.sort(key=lambda s: s[0])
            for i, _ in selections:
                if i < 0 or i >= len(all_images):
                    raise ValueError(f"sprite index {i} out of range")
            images = [all_images[i] for i, _ in selections]
            methods = [m for _, m in selections]
        else:
            if args.sprite:
                for i in args.sprite:
                    if i < 0 or i >= len(all_images):
                        raise ValueError(f"sprite index {i} out of range")
                images = [all_images[i] for i in args.sprite]
            else:
                images = list(all_images)
            methods = [(bare_specs[0] if bare_specs else "")] * len(images)

        # quartering
        if args.quarter:
            new_images = []
            new_methods = []
            for image, m in zip(images, methods):
                new_images.append([row[:16] for row in image[:16]])
                new_images.append([row[16:] for row in image[:16]])
                new_images.append([row[:16] for row in image[16:]])
                new_images.append([row[16:] for row in image[16:]])
                new_methods += [m] * 4
            images, methods = new_images, new_methods

        # halving
        images = [
            halve_image.shrink_image(image, m, palette) if m else image
            for image, m in zip(images, methods)
        ]
        # packing
        if args.linear:
            args.outdir.mkdir(parents=True, exist_ok=True)
            output_path = args.outdir / f"{args.name}.h"
            write_header_linear(output_path, args.name, images, palette)
        else:
            images = [pack_frame_4bpp(image) for image in images]
            args.outdir.mkdir(parents=True, exist_ok=True)
            output_path = args.outdir / f"{args.name}.h"
            write_header(output_path, args.name, images, palette)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0

def write_header_linear(path: Path, name: str, frames: list[list[list[int]]], palette: list[int]) -> None:
    h = len(frames[0])
    w = len(frames[0][0])
    lines = [f"// {len(frames)} frames of {w}x{h}, row-major indexed pixels",
        f"static const unsigned char {name}_pixels[{len(frames)}][{w * h}] = {{"]
    for frame in frames:
        flat = [p for row in frame for p in row]
        for j in range(0, len(flat), 16):
            chunk = flat[j:j + 16]
            lines.append("\t" + ", ".join(f"{v:3d}" for v in chunk) + ",")
    lines.append("};\n")
    lines.append(f"static const unsigned short {name}_palette[{len(palette)}] __attribute__((aligned(4))) = ")
    lines.append("{")
    lines.append("\t" + ", ".join(f"0x{value:04X}" for value in palette[:8]) + ",")
    lines.append("\t" + ", ".join(f"0x{value:04X}" for value in palette[8:]))
    lines.append("};")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")

def write_header(path: Path, name: str, frames: list[list[int]], palette: list[int]) -> None:
    lines = [f"// {len(frames)} frames",
        f"const unsigned int {name}_tiles[{len(frames) * len(frames[0])}] =", "{"]
    for frame in frames:
        for j in range(0, len(frame), 8):
            chunk = frame[j : j + 8]
            suffix = ","
            lines.append("\t" + ",".join(f"0x{value:08X}" for value in chunk) + suffix)
        lines.append("")
    lines.append("};\n")
    
    lines.append(f"const unsigned short {name}_palette[{len(palette)}] __attribute__((aligned(4))) = ")
    lines.append("{")
    lines.append("\t" + ", ".join(f"0x{value:04X}" for value in palette[:8]) + ",")
    lines.append("\t" + ", ".join(f"0x{value:04X}" for value in palette[8:]))
    lines.append("};")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")

if __name__ == "__main__":
    raise SystemExit(main())