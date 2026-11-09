#!/usr/bin/env python3
"""
hdi_extract.py - list / extract files from PC-98 .hdi hard-disk images.

Supports:
  * Anex86 .hdi container (32-byte header, any sector size 256/512/1024)
  * PC-98 partition table (sector 1, 32-byte entries) with several partitions
  * PC-98 MS-DOS FAT12 / FAT16 volumes (logical sector size may differ from
    the physical one, e.g. 1024-byte logical sectors on a 512-byte disk)
  * Shift-JIS (cp932) 8.3 file names, including the 0x05 -> 0xE5 lead-byte
    escape used by Japanese DOS
  * Sub-directories (extracted recursively) and original timestamps

Usage:
  hdi_extract.py IMAGE.hdi                 # extract everything to ./IMAGE/
  hdi_extract.py IMAGE.hdi -o outdir       # choose output directory
  hdi_extract.py IMAGE.hdi -l              # list only, extract nothing
  hdi_extract.py IMAGE.hdi -p 2            # pick partition #2 (default: all FAT ones)
  hdi_extract.py IMAGE.hdi --encoding ascii-hex   # keep non-ASCII names as hex

Only the Python standard library is used.
"""

import argparse
import os
import struct
import sys
import time
from dataclasses import dataclass


# --------------------------------------------------------------------------
# HDI container
# --------------------------------------------------------------------------
@dataclass
class HDI:
    data: memoryview
    header_size: int
    sector_size: int
    sectors: int       # sectors per track
    heads: int
    cylinders: int

    def read(self, lba, count=1):
        """Read `count` physical sectors starting at physical LBA."""
        start = self.header_size + lba * self.sector_size
        return bytes(self.data[start:start + count * self.sector_size])


def parse_hdi(raw):
    if len(raw) < 32:
        raise ValueError("file too small to be an HDI image")
    (_res, _typ, hdr, size, ssz, spt, heads, cyls) = struct.unpack_from("<8I", raw, 0)
    if ssz not in (256, 512, 1024) or hdr < 32 or hdr > len(raw) or not (spt and heads and cyls):
        raise ValueError("not a valid .hdi header (sector size=%d, header=%d)" % (ssz, hdr))
    return HDI(memoryview(raw), hdr, ssz, spt, heads, cyls)


# --------------------------------------------------------------------------
# PC-98 partition table
# --------------------------------------------------------------------------
@dataclass
class Partition:
    index: int
    mid: int
    sid: int
    name: str
    start_lba: int    # in physical sectors
    end_lba: int      # inclusive, physical sectors


def parse_partitions(img):
    # The PC-98 partition table lives in physical sector 1 (IPL is sector 0).
    sec = img.read(1)
    parts = []
    cyl_size = img.heads * img.sectors
    for i in range(0, len(sec) - 31, 32):
        e = sec[i:i + 32]
        mid, sid = e[0], e[1]
        if mid == 0 and sid == 0:
            continue
        ss, sh, sc = e[8], e[9], struct.unpack_from("<H", e, 10)[0]
        es, eh, ec = e[12], e[13], struct.unpack_from("<H", e, 14)[0]
        start = sc * cyl_size + sh * img.sectors + ss
        end = ec * cyl_size + eh * img.sectors + es
        name = e[16:32].split(b"\0")[0].decode("cp932", "replace").strip()
        parts.append(Partition(len(parts) + 1, mid, sid, name, start, end))
    return parts


# --------------------------------------------------------------------------
# FAT volume
# --------------------------------------------------------------------------
ATTR_RO, ATTR_HID, ATTR_SYS, ATTR_VOL, ATTR_DIR, ATTR_ARC = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20


@dataclass
class DirEntry:
    name: str          # decoded display name
    raw_name: bytes
    attr: int
    mtime: float       # POSIX time or None
    cluster: int
    size: int

    @property
    def is_dir(self):
        return bool(self.attr & ATTR_DIR)


class FatVolume:
    def __init__(self, img, part, encoding="cp932"):
        self.encoding = encoding
        base = img.header_size + part.start_lba * img.sector_size
        end = img.header_size + (part.end_lba + 1) * img.sector_size
        self.buf = img.data[base:min(end, len(img.data))]
        b = bytes(self.buf[:512])

        bps, spc, rsvd, nfats, nroot, tot16, media, spf = struct.unpack_from("<HBHBHHBH", b, 11)
        if bps not in (256, 512, 1024, 2048, 4096) or spc == 0 or spc & (spc - 1) \
                or nfats not in (1, 2) or nroot == 0 or spf == 0:
            raise ValueError("no valid FAT BPB in this partition")
        total = tot16 or struct.unpack_from("<I", b, 0x20)[0]
        if total == 0 or total * bps > len(self.buf):
            total = len(self.buf) // bps

        self.bps, self.spc = bps, spc
        self.cluster_bytes = bps * spc
        self.fat_start = rsvd * bps
        self.fat_bytes = spf * bps
        self.root_start = (rsvd + nfats * spf) * bps
        self.root_entries = nroot
        root_secs = (nroot * 32 + bps - 1) // bps
        self.data_start = self.root_start + root_secs * bps
        self.n_clusters = (total - rsvd - nfats * spf - root_secs) // spc
        self.fat16 = self.n_clusters >= 4085
        self.fat = bytes(self.buf[self.fat_start:self.fat_start + self.fat_bytes])
        self.eoc = 0xFFF8 if self.fat16 else 0xFF8
        self.volume_label = None

    # -- FAT access --------------------------------------------------------
    def next_cluster(self, c):
        if self.fat16:
            return struct.unpack_from("<H", self.fat, c * 2)[0]
        off = c + c // 2
        v = self.fat[off] | (self.fat[off + 1] << 8)
        return (v >> 4) if c & 1 else (v & 0xFFF)

    def chain(self, start):
        seen = set()
        c = start
        while 2 <= c < self.n_clusters + 2 and c not in seen:
            seen.add(c)
            yield c
            try:
                c = self.next_cluster(c)
            except (IndexError, struct.error):
                return
            if c >= self.eoc or c < 2:
                return

    def cluster_data(self, c):
        off = self.data_start + (c - 2) * self.cluster_bytes
        return bytes(self.buf[off:off + self.cluster_bytes])

    def read_chain(self, start, size=None):
        out = bytearray()
        for c in self.chain(start):
            out += self.cluster_data(c)
            if size is not None and len(out) >= size:
                break
        return bytes(out if size is None else out[:size])

    # -- directories -------------------------------------------------------
    def decode(self, raw):
        # Japanese DOS stores a real 0xE5 lead byte as 0x05.
        if raw[:1] == b"\x05":
            raw = b"\xE5" + raw[1:]
        try:
            return raw.decode(self.encoding)
        except (UnicodeDecodeError, LookupError):
            return "".join(chr(x) if 0x20 <= x < 0x7F else "%%%02X" % x for x in raw)

    def parse_dir(self, data):
        entries = []
        for i in range(0, len(data) - 31, 32):
            e = data[i:i + 32]
            if e[0] == 0x00:
                break                       # end of directory
            if e[0] == 0xE5:
                continue                    # deleted
            attr = e[11]
            if attr == 0x0F:
                continue                    # VFAT long-name slot (not used on PC-98 DOS)
            name = e[0:8].rstrip(b" ")
            ext = e[8:11].rstrip(b" ")
            raw = name + (b"." + ext if ext else b"")
            time_, date_, cluster, size = struct.unpack_from("<HHHI", e, 22)
            if attr & ATTR_VOL and not attr & ATTR_DIR:
                self.volume_label = self.decode((name + ext) if not ext else name + ext)
                continue
            disp = self.decode(raw)
            if disp in (".", ".."):
                continue
            entries.append(DirEntry(disp, raw, attr, fat_time(date_, time_), cluster, size))
        return entries

    def root(self):
        data = bytes(self.buf[self.root_start:self.root_start + self.root_entries * 32])
        return self.parse_dir(data)

    def children(self, d):
        return self.parse_dir(self.read_chain(d.cluster))

    def walk(self, entries=None, prefix=()):
        """Yield (path_tuple, DirEntry) depth-first."""
        seen_dirs = set()
        stack = [(prefix, entries if entries is not None else self.root())]
        while stack:
            pre, ents = stack.pop(0)
            for e in ents:
                yield pre + (e.name,), e
                if e.is_dir and e.cluster >= 2 and e.cluster not in seen_dirs:
                    seen_dirs.add(e.cluster)
                    stack.append((pre + (e.name,), self.children(e)))


def fat_time(date, tm):
    try:
        y, mo, d = (date >> 9) + 1980, (date >> 5) & 15, date & 31
        h, mi, s = tm >> 11, (tm >> 5) & 63, (tm & 31) * 2
        return time.mktime((y, mo, d, h, mi, s, 0, 0, -1))
    except (ValueError, OverflowError):
        return None


# --------------------------------------------------------------------------
# Extraction
# --------------------------------------------------------------------------
_BAD = '<>:"|?*' if os.name == "nt" else ""


def safe_component(name):
    name = name.replace("/", "_").replace("\\", "_").replace("\0", "_")
    name = "".join("_" if (c in _BAD or ord(c) < 32) else c for c in name)
    return name or "_"


def main(argv=None):
    ap = argparse.ArgumentParser(description="Extract files from PC-98 .hdi images")
    ap.add_argument("image", help="path to .hdi file")
    ap.add_argument("-o", "--output", help="output directory (default: image name without extension)")
    ap.add_argument("-l", "--list", action="store_true", help="list contents only")
    ap.add_argument("-p", "--partition", type=int, help="partition number to use (default: all FAT partitions)")
    ap.add_argument("--encoding", default="cp932", help="file-name encoding (default cp932 / Shift-JIS)")
    args = ap.parse_args(argv)

    with open(args.image, "rb") as f:
        raw = f.read()
    img = parse_hdi(raw)
    print("HDI: %d cyl x %d heads x %d spt, %d-byte sectors, header %d bytes"
          % (img.cylinders, img.heads, img.sectors, img.sector_size, img.header_size))

    parts = parse_partitions(img)
    if args.partition:
        parts = [p for p in parts if p.index == args.partition]
    if not parts:
        sys.exit("no partitions found")

    outroot = args.output or os.path.splitext(os.path.basename(args.image))[0]
    multi = len(parts) > 1
    total_files = 0

    for p in parts:
        try:
            vol = FatVolume(img, p, args.encoding)
        except ValueError as e:
            print("Partition %d '%s' (sid=%02X): skipped (%s)" % (p.index, p.name, p.sid, e))
            continue
        print("Partition %d '%s' (sid=%02X): LBA %d..%d, FAT%d, %d-byte sectors, "
              "%d-byte clusters, %d clusters"
              % (p.index, p.name, p.sid, p.start_lba, p.end_lba, 16 if vol.fat16 else 12,
                 vol.bps, vol.cluster_bytes, vol.n_clusters))
        dest_root = os.path.join(outroot, "partition%d" % p.index) if multi else outroot

        for path, e in vol.walk():
            shown = "/".join(path)
            if e.is_dir:
                print("  %-40s <DIR>" % (shown + "/"))
                if not args.list:
                    os.makedirs(os.path.join(dest_root, *map(safe_component, path)), exist_ok=True)
                continue
            print("  %-40s %9d bytes" % (shown, e.size))
            total_files += 1
            if args.list:
                continue
            dest = os.path.join(dest_root, *map(safe_component, path))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            data = vol.read_chain(e.cluster, e.size) if e.size else b""
            if len(data) < e.size:
                print("    ! warning: chain ended early (%d of %d bytes)" % (len(data), e.size))
            with open(dest, "wb") as out:
                out.write(data)
            if e.mtime is not None:
                try:
                    os.utime(dest, (e.mtime, e.mtime))
                except OSError:
                    pass
        if vol.volume_label:
            print("  Volume label: %s" % vol.volume_label)

    print("%d file(s) %s" % (total_files, "listed" if args.list else "extracted to " + outroot))


if __name__ == "__main__":
    main()
