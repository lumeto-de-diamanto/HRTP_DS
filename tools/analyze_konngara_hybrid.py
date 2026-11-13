#!/usr/bin/env python3
"""Lossless hybrid packing, also used by the shipped cache converter.

Use a stable exact BG palette for original/post-slash pages, patch the original
page once at the first slash, and cover the difference with transparent8bpp
OBJ rectangles. The normal head OBJ is hidden while those rectangles are live.
The standalone command remains an offline report; it never rewrites the ROM.
"""
import argparse
import functools
import json
from pathlib import Path
import struct

from konngara_combat_to_bin import unpack_art, apply_patch

WIDTH, HEIGHT = 320, 192
BG_CAPACITY, OBJ_FIRST, OBJ_CAPACITY = 207, 112, 144
BUFFER_BYTES, MAX_OBJECTS = 8192, 32
# Actual non-affine DS shapes. 64x16 and16x64 are NOT valid sprite sizes.
SHAPES = ((8,8),(16,16),(32,32),(64,64),(16,8),(32,8),
          (32,16),(64,32),(8,16),(8,32),(16,32),(32,64))


@functools.lru_cache(maxsize=1)
def rectangles():
    result = []
    for w,h in SHAPES:
        tw,th = w//8,h//8
        row = (1 << tw)-1
        for y in range(24-th+1):
            for x in range(40-tw+1):
                mask = sum(row << ((y+j)*40+x) for j in range(th))
                result.append((x*8,y*8,w,h,mask,w*h))
    return result


def pack(mask):
    if not mask:
        return []
    candidates = [r for r in rectangles() if r[4]&mask]
    alternatives = []
    # Weighted rectangle cover: retain transparent holes, allow overlap with
    # identical pixels, and trade padded bytes against OBJ entry count.
    for overhead in (0,16,32,64,96,128,192,256,384,512,768):
        remaining, chosen = mask, []
        while remaining:
            best, best_ink = None, 0
            for r in candidates:
                ink = (r[4]&remaining).bit_count()
                if not ink:
                    continue
                if best is None or ink*(best[5]+overhead) > best_ink*(r[5]+overhead) or (
                        ink*(best[5]+overhead) == best_ink*(r[5]+overhead) and r[5]<best[5]):
                    best, best_ink = r, ink
            chosen.append(best)
            remaining &= ~best[4]
        size = sum(r[5] for r in chosen)
        if size <= BUFFER_BYTES and len(chosen) <= MAX_OBJECTS:
            alternatives.append(chosen)
    if not alternatives:
        raise ValueError('no measured packing fits two4KiB buffers /32 OBJ slots')
    return min(alternatives,key=lambda plan:(len(plan),sum(r[5] for r in plan)))


def prototype(art):
    palette,base,patches = unpack_art(art)
    def pose(page,expression):
        apply_patch(page,patches[2])
        if expression: apply_patch(page,patches[5 if expression==1 else 11])
    post = base[:]; pose(post,1)
    for cel in range(7): apply_patch(post,patches[12+cel])
    apply_patch(post,patches[2])
    cache = {}
    def sampled(page):
        result = []
        for y in range(16,400,2):
            for x in range(0,640,2):
                signature = tuple(sorted((page[y*640+x],page[y*640+x+1],page[(y+1)*640+x],page[(y+1)*640+x+1])))
                if signature not in cache:
                    cache[signature] = tuple(sum((sum(((t,max(9,t),max(10,t))[c] if p==9 else
                        15 if flash and p in (3,4,5) else palette[p*3+c])*17//4 for p in signature)>>3)<<(c*5)
                        for c in range(3)) for t in range(16) for flash in (False,True))
                result.append(cache[signature])
        return result
    original,background = sampled(base),sampled(post)
    bg_colors = sorted(set(original)|set(background))
    if len(bg_colors)>BG_CAPACITY:
        raise ValueError('stable original/post BG palette exceeds207 exact colors')
    lookup = {color:i+1 for i,color in enumerate(bg_colors)}
    original_indices = bytes(lookup[p] for p in original)
    post_indices = bytes(lookup[p] for p in background)
    patch = []
    for y in range(192):
        for x in range(0,320,2):
            i=y*320+x
            if original_indices[i:i+2]!=post_indices[i:i+2]:
                patch.append((y*256+x//2,post_indices[i]|post_indices[i+1]<<8))
    frames = []
    for repeat,start in enumerate((base,post)):
        for expression in (1,3):
            page = start[:]; pose(page,expression)
            posed = sampled(page)
            unchanged_head_colors = {posed[y*320+x][:2] for y in range(32,72) for x in range(140,180)}
            for cel in range(7):
                apply_patch(page,patches[12+cel])
                reference = sampled(page)
                separate_head_colors = unchanged_head_colors | {p[:2] for i,p in enumerate(reference) if p[:2]!=posed[i][:2]}
                raw_background = original if not repeat else background
                naive_colors = {p[:2] for i,p in enumerate(reference) if p[:2]!=raw_background[i][:2]}
                differences = {i:p[:2] for i,p in enumerate(reference) if p[:2]!=background[i][:2]}
                colors = sorted(set(differences.values()))
                if len(colors)>OBJ_CAPACITY:
                    raise ValueError('overlay exceeds144 exact normal/flash color pairs')
                obj_lookup = {color:OBJ_FIRST+i for i,color in enumerate(colors)}
                indices = bytearray(WIDTH*HEIGHT)
                mask = 0
                for i,color in differences.items():
                    indices[i] = obj_lookup[color]
                    mask |= 1 << ((i//WIDTH//8)*40+(i%WIDTH//8))
                plan = pack(mask)
                banks = [0,0]; objects = []
                for x,y,w,h,_,size in sorted(plan,key=lambda r:-r[5]):
                    bank = next((i for i in (0,1) if banks[i]+size<=4096),None)
                    if bank is None:
                        raise ValueError('packing fits total bytes but not the actual two4KiB buffers')
                    offset = banks[bank]; banks[bank] += size
                    assert offset%64==0
                    texture = bytearray(size)
                    for sy in range(h):
                        for sx in range(w):
                            texture[((sy//8)*(w//8)+sx//8)*64+(sy%8)*8+sx%8] = indices[(y+sy)*320+x+sx]
                    objects.append(dict(x=x,y=y,w=w,h=h,bank=bank,offset=offset,texture=bytes(texture)))
                frames.append(dict(repeat=repeat,expression=expression,cel=cel,colors=colors,objects=objects,
                    separate_head_colors=len(separate_head_colors),naive_overlay_colors=len(naive_colors),
                    changed_pixels=len(differences),occupied_tiles=mask.bit_count(),bytes=sum(banks),banks=banks,
                    reference=tuple(struct.pack('<61440H',*(p[flash]|0x8000 for p in reference)) for flash in (0,1))))
    return dict(bg_colors=bg_colors,original_indices=original_indices,post_indices=post_indices,
                background=background,patch=patch,frames=frames)


def compose(model,frame,flash):
    # Hardware-style compositor: indexed background plus tiled8bpp objects.
    out = [color[flash] for color in model['background']]
    for obj in frame['objects']:
        x,y,w,h = (obj[k] for k in ('x','y','w','h'))
        for sy in range(h):
            for sx in range(w):
                index = obj['texture'][((sy//8)*(w//8)+sx//8)*64+(sy%8)*8+sx%8]
                if index: out[(y+sy)*320+x+sx] = frame['colors'][index-OBJ_FIRST][flash]
    return struct.pack('<61440H',*(p|0x8000 for p in out))


def verify(model):
    # Apply the sparse first-slash patch using the actual512-byte VRAM stride.
    vram = bytearray(512*256)
    for y in range(192): vram[y*512:y*512+320] = model['original_indices'][y*320:(y+1)*320]
    for offset,value in model['patch']: struct.pack_into('<H',vram,offset*2,value)
    assert b''.join(vram[y*512:y*512+320] for y in range(192))==model['post_indices']
    for frame in model['frames']:
        for flash in (0,1):
            rendered = compose(model,frame,flash)
            assert rendered==frame['reference'][flash],(frame['repeat'],frame['expression'],frame['cel'],flash)
            # Full-world equality also proves every integer camera crop0..64,
            # not only selected sample offsets.
    return len(model['frames'])*2*WIDTH*HEIGHT


def report(model):
    frames = []
    for f in model['frames']:
        overlap,span = 0,0
        for camera in (0,1,32,63,64):
            for y in range(192):
                active = [o for o in f['objects'] if o['y']<=y<o['y']+o['h'] and
                          o['x']<camera+256 and o['x']+o['w']>camera]
                overlap = max(overlap,len(active)); span = max(span,sum(o['w'] for o in active))
        frames.append(dict(repeat=f['repeat'],expression=f['expression'],cel=f['cel'],colors=len(f['colors']),
            separate_head_colors=f['separate_head_colors'],naive_overlay_colors=f['naive_overlay_colors'],
            pixels=f['changed_pixels'],tiles=f['occupied_tiles'],objects=len(f['objects']),bytes=f['bytes'],
            banks=f['banks'],peak_objects_per_line=overlap,peak_obj_width_per_line=span))
    return dict(scope='Offline report of the packer used by the KHS2 combat converter',
        stable_bg_colors=len(model['bg_colors']),bg_color_budget=BG_CAPACITY,
        first_slash_bg_halfword_writes=len(model['patch']),first_slash_bg_bytes=len(model['patch'])*2,
        later_keyframe_bg_bytes=0,max_overlay_colors=max(f['colors'] for f in frames),obj_color_budget=OBJ_CAPACITY,
        max_colors_retaining_original_head=max(f['separate_head_colors'] for f in frames),
        max_colors_hiding_head_without_bg_change=max(f['naive_overlay_colors'] for f in frames),
        max_objects=max(f['objects'] for f in frames),max_obj_bytes=max(f['bytes'] for f in frames),
        max_objects_per_line=max(f['peak_objects_per_line'] for f in frames),
        max_obj_width_per_line=max(f['peak_obj_width_per_line'] for f in frames),
        sprite_buffers=[4096,4096],new_obj_allocation_bytes=4096,
        verified_rgb15_pixels=verify(model),frames=frames)


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artwork',type=Path)
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    result = report(prototype(args.artwork.read_bytes()))
    text = json.dumps(result,indent=2)+'\n'
    if args.output: args.output.write_text(text)
    print(json.dumps({k:v for k,v in result.items() if k!='frames'},indent=2))
