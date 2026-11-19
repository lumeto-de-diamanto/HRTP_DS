#!/usr/bin/env python3
"""Pack Kikuri's original disc/background and review sprites, without new art."""
import argparse
from pathlib import Path
import struct

from bos_to_h import decode_bos
from grp_to_bin import decode_grp
from halve_image import shrink_image
from ptn_to_h import decode_ptn

HEADER = struct.Struct('<4s6H4I')
SOURCE = HEADER.size + 48
INDICES = SOURCE + 640*400//2
SIGNATURES = INDICES + 320*192


def build(packfile, background):
    source,palette,size = decode_grp(background.read_bytes())
    if size != (400,640):
        raise ValueError('Kikuri requires a640x400 background')
    signatures = [tuple(sorted(source[y+dy][x+dx] for dy in (0,1) for dx in (0,1)))
                  for y in range(16,400,2) for x in range(0,640,2)]
    inventory = sorted(set(signatures))
    if len(inventory)>192:
        raise ValueError('Kikuri background exceeds the208-color BG budget')
    lookup = {key:16+i for i,key in enumerate(inventory)}
    raw_palette = bytes(c//17 for color in palette for c in color)
    packed = bytes(row[x] | row[x+1]<<4 for row in source for x in range(0,640,2))
    soul,soul_size = decode_bos((packfile/'TAMASII.BOS').read_bytes())
    tear,tear_size = decode_bos((packfile/'TAMASII2.BOS').read_bytes())
    _,ripples = decode_ptn((packfile/'TAMAYEN.PTN').read_bytes())
    if soul_size!=(32,32) or tear_size!=(16,16) or len(soul)!=3 or len(tear)!=3 or len(ripples)!=1:
        raise ValueError('unexpected Kikuri sprite inventory')
    sheet = ripples[0]
    quarters = [[row[x:x+16] for row in sheet[y:y+16]] for y in (0,16) for x in (0,16)]
    sprites = b''.join(bytes(pixel for row in shrink_image(image,'dither') for pixel in row)
                       for image in (*soul,*tear,*quarters))
    signatures_data = b''.join(struct.pack('<H',sum(p<<(4*i) for i,p in enumerate(key))) for key in inventory)
    header = HEADER.pack(b'KIK1',640,400,3,3,4,len(inventory),SOURCE,INDICES,SIGNATURES,
                         SIGNATURES+len(signatures_data))
    return header+raw_palette+packed+bytes(lookup[key] for key in signatures)+signatures_data+sprites


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packfile',type=Path);parser.add_argument('background',type=Path)
    parser.add_argument('output',type=Path);args=parser.parse_args()
    data=build(args.packfile,args.background)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(data)
    print(f'Kikuri art: {len(data)} bytes; original background,3 soul/3 tear/4 ripple cels')
