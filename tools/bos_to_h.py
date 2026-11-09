import argparse
from pathlib import Path
import struct
import sys
import halve_image

HEADER_SIZE = 16
PALETTE_SIZE = 48

PLANES_PER_IMAGE = 5

HEADER_AND_PALETTE_SIZE = HEADER_SIZE + PALETTE_SIZE

def pad_image(image: list[list[int]], target_size: tuple[int, int]):
    # Remember: target_size is (width, height)
    width_difference = target_size[0] - len(image[0])
    height_difference = target_size[1] - len(image)

    new_image = []
    for i in range(height_difference):
        new_image.append([0] * target_size[0])
    for row in image:
        new_image.append(([0] * (width_difference // 2)) + row + ([0] * (width_difference // 2)))
    return new_image

def decode_image(image_data: bytes, byte_width: int, height: int):
    plane_size = byte_width * height
    width = byte_width * 8

    planes = {
        "A": image_data[0 * plane_size : 1 * plane_size],
        "B": image_data[1 * plane_size : 2 * plane_size],
        "R": image_data[2 * plane_size : 3 * plane_size],
        "G": image_data[3 * plane_size : 4 * plane_size],
        "E": image_data[4 * plane_size : 5 * plane_size],
    }

    image = []
    for y in range(height):
        row_off = y * byte_width
        a_row = planes["A"][row_off : row_off + byte_width]
        b_row = planes["B"][row_off : row_off + byte_width]
        r_row = planes["R"][row_off : row_off + byte_width]
        g_row = planes["G"][row_off : row_off + byte_width]
        e_row = planes["E"][row_off : row_off + byte_width]
        image_row = []
        for x in range(width):
            byte_idx = x // 8
            bit_idx = 7 - (x % 8)  # MSB-first
            a = (a_row[byte_idx] >> bit_idx) & 1
            b = (b_row[byte_idx] >> bit_idx) & 1
            r = (r_row[byte_idx] >> bit_idx) & 1
            g = (g_row[byte_idx] >> bit_idx) & 1
            e = (e_row[byte_idx] >> bit_idx) & 1
            if a > 0:
                image_row.append(0)
            else:
                index = (e << 3) | (g << 2) | (r << 1) | b
                index = (index + 1) % 16
                image_row.append(index)
        image.append(image_row)
    return image

def decode_bos(data: bytes):
    magic, byte_width, _, height, _, image_count = struct.unpack_from("<4sBbBbB", data, 0)
    if magic != b"BOSS":
        raise ValueError(f"bad magic {magic!r}, expected BOSS")

    plane_size = byte_width * height
    per_image_size = PLANES_PER_IMAGE * plane_size

    images = []
    offset = HEADER_AND_PALETTE_SIZE
    for i in range(image_count):
        chunk = data[offset : offset + per_image_size]
        if len(chunk) < per_image_size:
            raise ValueError(
                f"file truncated: image {i}/{image_count} needs "
                f"{per_image_size} bytes at offset {offset}, only "
                f"{len(chunk)} available"
            )
        images.append(decode_image(chunk, byte_width, height))
        offset += per_image_size
    return images, (byte_width * 8, height)

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

frame_sizes = [
    (8, 8),
    (16, 16),
    (32, 32),
    (64, 64),
    (16, 8),
    (32, 8),
    (32, 16),
    (64, 32),
    (8, 16),
    (8, 32),
    (16, 32),
    (32, 64)
]

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ptn_file", type=Path, help="input .PTN file")
    parser.add_argument("outdir", type=Path, help="directory for generated .h file")
    parser.add_argument("name", type=str, help="name of the generated sprite")
    parser.add_argument("--halve", type=str, default="", help="method to halve the size of sprites", required=False)
    parser.add_argument("-s", "--sprite", nargs='+', type=int, default=[], help="sprite indices", required=False)
    args = parser.parse_args()

    if args.ptn_file.suffix.lower() != ".bos":
        parser.error(f"input does not have a .BOS extension: {args.ptn_file}")
    if not args.ptn_file.is_file():
        parser.error(f"input file does not exist: {args.ptn_file}")

    if args.halve and args.halve not in halve_image.HALF_METHODS_LIST:
        parser.error(f"invalid halving method {args.halve}")

    try:
        data = args.ptn_file.read_bytes()
        images, size = decode_bos(data)
        if args.sprite:
            images = [images[i] for i in args.sprite]
        # halving
        if args.halve:
            images = [halve_image.shrink_image(image, args.halve) for image in images]
            size = (size[0] // 2, size[1] // 2)
        # padding
        best_size = (64, 64)
        for test_size in frame_sizes:
            if size[0] <= test_size[0] and \
               size[1] <= test_size[1] and \
               test_size[0] * test_size[1] < best_size[0] * best_size[1]:
                best_size = test_size
        if best_size[0] != size[0] or best_size[1] != size[1]:
            images = [pad_image(image, best_size) for image in images]
            size = best_size
        # packing
        images = [pack_frame_4bpp(image) for image in images]
        args.outdir.mkdir(parents=True, exist_ok=True)
        output_path = args.outdir / f"{args.name}.h"
        write_header(output_path, args.name, images, size)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0

def write_header(path: Path, name: str, frames: list[list[int]], size: tuple[int, int]) -> None:
    lines = [f"// Frame size = {size[0]}x{size[1]}", 
            f"// {len(frames)} frames",
            f"const unsigned int {name}_tiles[{len(frames) * len(frames[0])}] =", 
            "{"]
    for frame in frames:
        for j in range(0, len(frame), 8):
            chunk = frame[j : j + 8]
            suffix = ","
            lines.append("\t" + ",".join(f"0x{value:08X}" for value in chunk) + suffix)
        lines.append("")
    lines.append("};")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")

if __name__ == "__main__":
    raise SystemExit(main())