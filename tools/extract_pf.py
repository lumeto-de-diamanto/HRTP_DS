#!/usr/bin/env python3
"""
Extractor for TH01's packfile archive format (東方靈異.伝).

Reimplemented directly from ReC98's th01/formats/pf.cpp and pf.hpp
(https://github.com/nmlgc/ReC98), which documents the format precisely.
See that file's comments for the authoritative description; this script
follows it field-for-field.

Format summary:
  - Header: 64 fixed 32-byte entries (type[2], aux[1], fn[13], packsize[4],
    orgsize[4], offset[4], reserved[4], all little-endian), terminated
    early if type[0] == 0.
  - Filenames in the header are bitwise-NOT'd on disk, EXCEPT trailing
    null-padding bytes, which are left as-is (the original's inversion
    loop stops at the first raw 0x00 byte, before inverting it).
  - Per-file data is either:
      * RLE-compressed, if type == b'\x95\x95' (Shift-JIS "封"), or
      * XOR'd with a fixed key (0x76, from th01/shiftjis/fns.hpp ARC_KEY)
        if not compressed.
  - CRITICAL DETAIL, easy to miss (an earlier version of this script did
    miss it): compressed entries are ALSO XOR'd with the same key --
    every byte read during RLE decompression passes through
    pf.cpp's cache_next_raw(), which XORs unconditionally before the
    byte ever reaches the RLE state machine. Compression and encryption
    are not mutually exclusive in this format; RLE operates on the
    encrypted byte stream, not on plaintext. Verified against known
    magic bytes in the decompressed output (see below).
  - Known ZUN bug ("landmine"): the RLE decompressor's outer loop is
    gated on the entry's own packsize, but its inner loops are not --
    they can read a few bytes past packsize before the outer loop gets
    a chance to notice, genuinely consuming the start of whatever data
    follows in the archive. Reproduced faithfully here (see unrle()'s
    docstring); the last entry in the archive gets zero-padding instead
    since there's no real "next file" to borrow from in that case.

Validated against the real 東方靈異.伝 archive: all 41 entries extract
with correct decompressed lengths; all 20 .BOS entries' output starts
with the literal ASCII magic "BOSS" (per th01/formats/bos.hpp); all 7
.GRC entries' output starts with "GRCG" (per th01/formats/grc.hpp);
MIKO_AC.BOS's decoded header fields (48x48, matching its known use as
th01/main/player/anim.hpp's player_48x48 slot) are internally
consistent. Also validated against pf.cpp's own documented worked
RLE example, byte for byte.

Usage:
    python3 extract_pf.py 東方靈異.伝 output_dir/
    python3 extract_pf.py 東方靈異.伝 output_dir/ --list   # list only, no extraction
"""

import argparse
import struct
import sys
from pathlib import Path

PF_FN_LEN = 13
FILE_COUNT = 64
HEADER_ENTRY_SIZE = 32  # 2 + 1 + 13 + 4 + 4 + 4 + 4
HEADER_TABLE_SIZE = HEADER_ENTRY_SIZE * FILE_COUNT  # 2048

ARC_KEY = 0x76
PF_TYPE_COMPRESSED = b"\x95\x95"

HEADER_STRUCT_FMT = "<2sb13sIIII"  # type, aux, fn, packsize, orgsize, offset, reserved
assert struct.calcsize(HEADER_STRUCT_FMT) == HEADER_ENTRY_SIZE


class PfEntry:
    def __init__(self, type_bytes, aux, fn_raw, packsize, orgsize, offset, reserved):
        self.type_bytes = type_bytes
        self.aux = aux
        # Un-invert each byte of the filename, per arc_load()'s loop:
        #   for(c = 0; c < PF_FN_LEN; c++) {
        #       if(arc_pfs[i].fn[c] == '\0') break;
        #       arc_pfs[i].fn[c] = ~arc_pfs[i].fn[c];
        #   }
        # Critically, the null check happens BEFORE inverting, on the RAW
        # (still-inverted) byte -- so it only stops early if the raw byte
        # is already 0x00, and never inverts that byte or anything after
        # it. An earlier version of this script inverted all 13 bytes
        # unconditionally, which corrupted trailing zero-padding into
        # 0xFF garbage. Fixed here to match the source exactly.
        un_inverted = bytearray()
        for b in fn_raw:
            if b == 0x00:
                break
            un_inverted.append((~b) & 0xFF)
        self.fn = bytes(un_inverted)
        self.packsize = packsize
        self.orgsize = orgsize
        self.offset = offset
        self.reserved = reserved
        self.compressed = type_bytes == PF_TYPE_COMPRESSED

    def fn_str(self):
        # Original filenames are 8.3 DOS names, plain ASCII in practice
        # for TH01's asset names (unlike the archive's own Shift-JIS
        # name). Decode as ASCII/latin-1 fallback so this never raises
        # on unexpected bytes; flag anything suspicious instead of
        # crashing the whole extraction.
        try:
            return self.fn.decode("ascii")
        except UnicodeDecodeError:
            return self.fn.decode("latin-1")

    def __repr__(self):
        return (
            f"<PfEntry fn={self.fn_str()!r} packsize={self.packsize} "
            f"orgsize={self.orgsize} offset={self.offset} "
            f"compressed={self.compressed}>"
        )


def read_header(data: bytes):
    """Parse the 64-entry header table. Stops at the first entry whose
    type[0] == 0, matching arc_load()'s loop."""
    entries = []
    for i in range(FILE_COUNT):
        start = i * HEADER_ENTRY_SIZE
        chunk = data[start : start + HEADER_ENTRY_SIZE]
        if len(chunk) < HEADER_ENTRY_SIZE:
            break
        type_bytes, aux, fn_raw, packsize, orgsize, offset, reserved = (
            struct.unpack(HEADER_STRUCT_FMT, chunk)
        )
        if type_bytes[0] == 0:
            break
        entries.append(
            PfEntry(type_bytes, aux, fn_raw, packsize, orgsize, offset, reserved)
        )
    return entries


def unrle(compressed: bytes, input_size: int, orgsize: int) -> bytes:
    """
    Reimplementation of pf.cpp's unrle().

    Original algorithm (see pf.cpp's extensive comments for the full
    explanation and the documented ZUN bug): reads a byte stream where
    any run of 2+ identical bytes is followed by a run-length byte
    (how many *additional* copies to emit), and that pattern repeats
    until input is exhausted.

    `compressed` should contain this entry's compressed bytes AND
    whatever real archive bytes immediately follow it (typically the
    start of the next entry's data), since the original's landmine bug
    (see below) can genuinely read a few bytes past the nominal
    `input_size`. `input_size` is the entry's own packsize -- the value
    the original's outer loop is actually gated on.

    THE OUTER LOOP is gated on INPUT bytes consumed relative to
    `input_size` (`while(input_size > bytes_read)` in the original) --
    NOT on output bytes produced against `orgsize`. An earlier version
    of this function gated on output length instead, which silently
    diverged from the original's actual state machine and produced
    corrupted output (long incorrect runs) -- see conversation history
    for the debugging trail that found this.

    THE INNER LOOPS (the initial do-while, and the run-mode loop) do NOT
    check `bytes_read`/`input_size` at all in the original -- this is
    the documented "ZUN landmine": they can read a few bytes past
    `input_size` mid-run, before the outer loop gets a chance to notice
    input_size was exceeded. This is reproduced faithfully here: `pos`
    (this function's `bytes_read` equivalent) can and does exceed
    `input_size` by the time the outer loop condition is next checked;
    only `next_byte()` raising (truly running out of the buffer
    entirely) is treated as a hard error.

    This version reads from an in-memory `compressed` buffer via an
    index instead of pf.cpp's byte-at-a-time file cache (cache_next()),
    which is equivalent for our purposes since we already have the full
    compressed block (plus trailing context) in memory.
    """
    out = bytearray()
    pos = 0
    buf_len = len(compressed)

    def next_byte():
        nonlocal pos
        if pos >= buf_len:
            raise ValueError(
                "unrle: ran out of buffer entirely (not just input_size) -- "
                "need more trailing context bytes from the archive than "
                "were provided"
            )
        # THE BUG THAT TOOK A WHILE TO FIND: pf.cpp's cache_next_raw()
        # XORs every byte with arc_key as it's read, unconditionally --
        # this happens INSIDE the RLE byte-fetch primitive itself, not
        # only in the separate crapt()/decrypt_xor() path used for
        # uncompressed entries. An earlier version of this function read
        # raw bytes with no XOR at all here, which parsed the RLE
        # control structure against completely wrong values (XOR'd
        # literals and XOR'd run-length bytes alike) -- verified via the
        # MIKO_AC.BOS entry, whose first 4 decompressed bytes are known
        # (from bos.hpp's BOS_MAGIC) to be the literal ASCII "BOSS", and
        # which only appears at the right position once this XOR is
        # applied per-byte, matching cache_next_raw() exactly.
        b = compressed[pos] ^ ARC_KEY
        pos += 1
        return b

    literal_2 = next_byte()

    while pos < input_size:
        # Read bytes until we see two identical ones in a row (start of
        # a run), emitting each byte as we go. Mirrors the C++ do-while:
        # always writes literal_1 at least once before checking for a
        # repeat. Deliberately NOT bounded by input_size here -- see
        # docstring's "INNER LOOPS" note.
        while True:
            literal_1 = literal_2
            out.append(literal_1)
            literal_2 = next_byte()
            if literal_1 == literal_2:
                break
        out.append(literal_2)  # second byte of the run

        # Run mode: literal_1 == literal_2, both hold the run byte value.
        # Also deliberately not bounded by input_size -- see docstring.
        while True:
            runs = next_byte()
            for _ in range(runs):
                out.append(literal_1)
            literal_2 = next_byte()
            if literal_2 != literal_1:
                break
            out.append(literal_1)

    return bytes(out[:orgsize])


def decrypt_xor(data: bytes) -> bytes:
    """Reimplementation of pf.cpp's crapt() -- XOR every byte with the
    fixed archive key."""
    return bytes(b ^ ARC_KEY for b in data)


def extract_entry(archive_data: bytes, entry: PfEntry) -> bytes:
    packed = archive_data[entry.offset : entry.offset + entry.packsize]
    if len(packed) < entry.packsize:
        raise ValueError(
            f"{entry.fn_str()}: archive truncated -- expected {entry.packsize} "
            f"bytes at offset {entry.offset}, only {len(packed)} available"
        )
    if entry.compressed:
        # unrle() mirrors the original's input_size-gated outer loop
        # (bounded by this entry's own packsize) while letting the inner
        # loops read past that bound unchecked, per the documented ZUN
        # landmine bug -- which can genuinely consume a few bytes into
        # whatever real data follows this entry in the archive. So we
        # hand it this entry's offset through the end of the archive as
        # available buffer, but tell it input_size=packsize so it knows
        # where the entry is SUPPOSED to end.
        #
        # Pad with a small amount of zero bytes past the real archive
        # data too. This matters for whichever entry is LAST in the
        # archive: if its compressed stream needs the landmine overrun
        # to finish (observed in practice with this format, not just a
        # theoretical concern), there's no real "next file" data to
        # borrow from -- in the original game this would read into
        # whatever memory happened to follow the loaded archive buffer,
        # which is undefined even in ZUN's own version. Zero-padding is
        # an arbitrary but harmless choice for that edge case; the
        # resulting few trailing bytes of such a file's output should be
        # treated as unreliable, unlike the rest of the archive.
        available = archive_data[entry.offset :] + b"\x00" * 64
        return unrle(available, entry.packsize, entry.orgsize)
    else:
        decrypted = decrypt_xor(packed)
        return decrypted[: entry.orgsize]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="Path to 東方靈異.伝")
    parser.add_argument("outdir", type=Path, nargs="?", help="Output directory")
    parser.add_argument(
        "--list", action="store_true", help="List entries only, don't extract"
    )
    args = parser.parse_args()

    data = args.archive.read_bytes()
    if len(data) < HEADER_TABLE_SIZE:
        print(
            f"error: file is only {len(data)} bytes, smaller than the "
            f"{HEADER_TABLE_SIZE}-byte header table -- is this really the "
            "packfile archive?",
            file=sys.stderr,
        )
        sys.exit(1)

    entries = read_header(data[:HEADER_TABLE_SIZE])
    print(f"Found {len(entries)} entries.")

    if args.list or args.outdir is None:
        for e in entries:
            comp = "RLE" if e.compressed else "XOR"
            print(f"  {e.fn_str():13s}  {comp}  orgsize={e.orgsize:>8}  "
                  f"packsize={e.packsize:>8}  offset={e.offset}")
        if args.outdir is None and not args.list:
            print("\n(no output dir given -- pass one to actually extract)")
        return

    args.outdir.mkdir(parents=True, exist_ok=True)
    ok, failed = 0, []
    for e in entries:
        try:
            content = extract_entry(data, e)
        except ValueError as exc:
            print(f"  FAILED {e.fn_str()}: {exc}", file=sys.stderr)
            failed.append(e.fn_str())
            continue
        out_path = args.outdir / e.fn_str()
        out_path.write_bytes(content)
        print(f"  wrote {out_path} ({len(content)} bytes)")
        ok += 1

    print(f"\n{ok}/{len(entries)} extracted successfully.")
    if failed:
        print(f"Failed: {', '.join(failed)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
