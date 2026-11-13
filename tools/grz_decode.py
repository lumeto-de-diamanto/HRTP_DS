#!/usr/bin/env python3
"""TH01 GRZ/GRX byte patches, from ReC98 th01/formats/grz.{h,cpp}.

Each patch is (VRAM byte offset, B/R/G/E bytes). GC_RUN puts runs+1 bytes
when its command is zero; skip runs also consume the outer loop's one byte.
Skipped bytes are untouched, not a transparent palette color. GRX palettes
are metadata only: grx_put() never loads them.
"""
import struct

PLANE_SIZE = 80 * 400


def decode_stream(rle: bytes, planar: bytes):
    cursor = source = offset = 0
    patches = []

    def byte():
        nonlocal cursor
        if cursor >= len(rle):
            raise ValueError('truncated GRX RLE stream')
        value = rle[cursor]
        cursor += 1
        return value

    def put():
        nonlocal source
        if offset >= PLANE_SIZE or source + 4 > len(planar):
            raise ValueError('GRX put exceeds VRAM or planar stream')
        patches.append((offset, planar[source:source + 4]))
        source += 4

    while offset < PLANE_SIZE:
        command = byte()
        if command == 1:
            runs = byte()
            if runs:
                command = byte()
                for _ in range(runs):
                    if command == 0:
                        put()
                    offset += 1
                    if offset > PLANE_SIZE:
                        raise ValueError('GRX run exceeds VRAM')
        if command == 0:
            put()
        offset += 1
    if offset != PLANE_SIZE or cursor != len(rle) or source != len(planar):
        raise ValueError('GRX stream sizes do not match decoded image')
    return patches


def decode_grz(data: bytes, image: int, stream: int = 0):
    if len(data) < 96 or data[:4] != b'HGRZ':
        raise ValueError('invalid GRZ header')
    count = data[4]
    if not 1 <= count <= 16 or not 0 <= image < count:
        raise ValueError('invalid GRZ image count/index')
    if struct.unpack_from('<I', data, 72)[0] != len(data):
        raise ValueError('invalid GRZ total size')
    offsets = struct.unpack_from('<16I', data, 8)[:count]
    if offsets[0] < 96 or any(a >= b for a, b in zip(offsets, offsets[1:])):
        raise ValueError('invalid GRZ image offsets')
    start, end = offsets[image], offsets[image + 1] if image + 1 < count else len(data)
    if start + 64 > end or end > len(data) or data[start:start + 4] != b'HGRX':
        raise ValueError('invalid GRX header/extent')
    streams = data[start + 4]
    rle_size, planar_size = struct.unpack_from('<HH', data, start + 8)
    if not 1 <= streams <= 16 or not 0 <= stream < streams:
        raise ValueError('invalid GRX planar stream count/index')
    if start + 64 + rle_size + streams * planar_size != end:
        raise ValueError('invalid GRX stream extents')
    rle = data[start + 64:start + 64 + rle_size]
    start += 64 + rle_size + stream * planar_size
    return decode_stream(rle, data[start:start + planar_size])
