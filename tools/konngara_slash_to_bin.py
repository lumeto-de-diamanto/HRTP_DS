#!/usr/bin/env python3
"""Build the stable-palette KGC2 arena and KHS2 hybrid slash sprite cache."""
import argparse
from pathlib import Path
import struct

from analyze_konngara_hybrid import prototype
from konngara_combat_to_bin import build as build_gpu, unpack_art, apply_patch, BACKDROP

HEADER = struct.Struct('<4sIIIIIII')
ENTRY = struct.Struct('<HHHHIIII')
OBJECT = struct.Struct('<HHHHHHI')
COUNT = 28


def build_pair(art):
    model = prototype(art)
    _,base,patches = unpack_art(art)
    apply_patch(base,patches[2]); apply_patch(base,patches[5])
    for cel in range(7): apply_patch(base,patches[12+cel])
    apply_patch(base,patches[2])
    packed = bytes(base[i]|base[i+1]<<4 for i in range(0,len(base),2))
    main = bytearray(build_gpu(art))
    post = bytearray(build_gpu(art[:216]+packed+art[128216:]))
    colors = model['bg_colors']
    palettes = b''.join(struct.pack('<H',0 if i==0 or i>len(colors) else colors[i-1][state])
                        for state in range(32) for i in range(208))
    for data,indices in ((main,model['original_indices']),(post,model['post_indices'])):
        struct.pack_into('<H',data,10,len(colors))
        data[48:48+len(palettes)] = palettes
        data[BACKDROP:BACKDROP+61440] = indices
    post_offset = HEADER.size+COUNT*ENTRY.size
    patch_offset = post_offset+len(post)
    patch_data = b''.join(struct.pack('<HH',*p) for p in model['patch'])
    payload,entries = bytearray(post+patch_data),bytearray()
    for frame in model['frames']:
        count = len(frame['colors']); objects = frame['objects']; used = frame['banks']
        palettes = b''.join(struct.pack('<H',frame['colors'][i][flash] if i<count else 0)
                            for flash in (0,1) for i in range(144))
        descriptors = b''.join(OBJECT.pack(o['x'],o['y'],o['w'],o['h'],o['bank'],o['offset'],0) for o in objects)
        banks = [bytearray(used[0]),bytearray(used[1])]
        for o in objects: banks[o['bank']][o['offset']:o['offset']+len(o['texture'])] = o['texture']
        palette_offset = post_offset+len(payload)
        object_offset = palette_offset+len(palettes)
        bank0_offset = object_offset+len(descriptors)
        entries += ENTRY.pack(len(objects),count,*used,palette_offset,object_offset,bank0_offset,bank0_offset+used[0])
        payload += palettes+descriptors+banks[0]+banks[1]
    header = HEADER.pack(b'KHS2',post_offset+len(payload),post_offset,len(post),patch_offset,len(model['patch']),HEADER.size,COUNT)
    return bytes(main),header+entries+payload


def build(art):
    return build_pair(art)[1]


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artwork',type=Path); parser.add_argument('arena',type=Path); parser.add_argument('slash',type=Path)
    args = parser.parse_args()
    main,slash = build_pair(args.artwork.read_bytes())
    # Preserve an unchanged extraction peer when only one grouped output was
    # deleted. Touch stale peers when a dependency changed, for no-op rebuilds.
    dependencies = [args.artwork,Path(__file__),Path(__file__).with_name('analyze_konngara_hybrid.py'),
                    Path(__file__).with_name('konngara_combat_to_bin.py')]
    newest = max(p.stat().st_mtime_ns for p in dependencies)
    for path,data in ((args.arena,main),(args.slash,slash)):
        path.parent.mkdir(parents=True,exist_ok=True)
        if not path.exists() or path.read_bytes()!=data or path.stat().st_mtime_ns<newest: path.write_bytes(data)
    print(f'Konngara hybrid cache: arena {len(main)} bytes; sprites/post-page {len(slash)} bytes')
