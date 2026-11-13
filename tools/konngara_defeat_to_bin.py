#!/usr/bin/env python3
"""KGD1: lossless indexed kuji pages7..15 and the final scrolling GRP.

Each8bpp index denotes a sorted four-source-pixel signature, not a quantized
color. Runtime RGB4 palette effects therefore precede the existing2x2 average.
All320x200 pixels survive; scroll and shake offsets are always even source Y.
"""
import argparse
from pathlib import Path
import struct
from grp_to_bin import decode_grp
from grz_decode import decode_grz

HEADER = struct.Struct('<4sHHHHI')
ENTRY = struct.Struct('<HHII')
COUNT, WIDTH, HEIGHT = 10, 320, 200


def build(directory):
    directory = Path(directory)
    _, palette, _ = decode_grp((directory/'BOSS8_A1.GRP').read_bytes())
    combat = [[c//17 for c in rgb] for rgb in palette]
    combat[9] = [0,9,10]
    # Keep the base colors: runtime whites3/4/5 for a lethal phase7 hit, but
    # early zero HP can reach7 after the preceding hit grace has expired.
    scroll, palette, size = decode_grp((directory/'BOSS7_D1.GRP').read_bytes())
    if size != (400,640): raise ValueError('expected640x400 scroll GRP')
    palettes = bytes(c for rgb in combat for c in rgb)+bytes(c//17 for rgb in palette for c in rgb)
    archive = (directory/'BOSS8.GRZ').read_bytes()
    pages = []
    for image in range(7,16):
        # Source clears the page before7, then again before EVERY later image.
        page = bytearray(640*400)
        for offset,planes in decode_grz(archive,image):
            for bit in range(8):
                page[offset*8+bit] = sum(((p>>(7-bit))&1)<<i for i,p in enumerate(planes))
        pages.append(page)
    pages.append(bytearray(p for row in scroll for p in row))
    offset = HEADER.size+96+COUNT*ENTRY.size
    entries,payload = bytearray(),bytearray()
    for page_number,page in enumerate(pages):
        signatures = []
        for y in range(0,400,2):
            for x in range(0,640,2):
                pixels=sorted((page[y*640+x],page[y*640+x+1],page[(y+1)*640+x],page[(y+1)*640+x+1]))
                signatures.append(sum(p<<(i*4) for i,p in enumerate(pixels)))
        colors=sorted(set(signatures))
        if not 1<=len(colors)<=255: raise ValueError('defeat page exceeds255 exact signatures')
        if page_number==9 and len(colors)>207: raise ValueError('scroll page overlaps restored result-screen inks')
        lookup={color:i+1 for i,color in enumerate(colors)}
        entries+=ENTRY.pack(len(colors),0,offset,offset+len(colors)*2)
        block=struct.pack('<'+'H'*len(colors),*colors)+bytes(lookup[s] for s in signatures)
        payload+=block;offset+=len(block)
    return HEADER.pack(b'KGD1',WIDTH,HEIGHT,COUNT,0,offset)+palettes+entries+payload


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();data=build(args.assets)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(data)
    print(f'Konngara defeat: {len(data)} bytes, nine kuji pages + final scroll')
