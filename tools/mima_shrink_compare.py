#!/usr/bin/env python3
"""Export a synchronized meteor comparison without changing any ROM assets."""
import argparse
import base64
import hashlib
from io import BytesIO
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from bos_to_h import decode_bos
from grp_to_bin import decode_grp, halve_background, pack_palette
from halve_image import shrink_image
from mima_to_bin import shrink_animation

METHODS = {
    'dither': 'Previous: palette-index error diffusion across the full image; can shimmer.',
    'stable_dither': 'Current: reuse the shrunken base pixel wherever all four source pixels match; dither changed blocks normally.',
    'topleft': 'Nearest sample: takes the upper-left pixel of each 2x2 block.',
    'topleft2': 'Block sample: retains the upper-left 2x2 pixels of each 4x4 block.',
    'stagger': 'Staggered sample: fixed alternating sampling positions in each 4x4 block.',
    'salient': 'Contrast-aware 2x2 selection; favors thin high-contrast details.',
    'rare': 'Favors globally rare colors; may change as each animation cel changes color counts.',
}


def rgb15(color):
    return tuple(((color >> shift) & 31) * 255 // 31 for shift in (0, 5, 10))


def load_comparison(packfile, background):
    body, size = decode_bos((packfile / 'BOSS3_1.BOS').read_bytes())
    animation, cel_size = decode_bos((packfile / 'BOSS3_2.BOS').read_bytes())
    if (size, len(body), cel_size, len(animation)) != ((128, 160), 1, (128, 64), 5):
        raise ValueError('expected original Mima body, cast cel and four meteor cels')
    arena, colors, dimensions = decode_grp(background.read_bytes())
    if dimensions != (400, 640):
        raise ValueError('expected original BOSS3.GRP dimensions')
    palette = [0] + pack_palette(colors[:15])
    pages = []
    # Match production: overlay nontransparent source pixels at row48 BEFORE
    # shrinking the entire body. Cropping just the meteor hides dither ripple.
    base = [row[:] for row in body[0]] + [[0] * 128 for _ in range(32)]
    base_half = shrink_image(base, 'dither')
    for cel in animation[1:]:
        page = [row[:] for row in base]
        for y, row in enumerate(cel):
            for x, pixel in enumerate(row):
                if pixel:
                    page[48+y][x] = pixel
        pages.append(page)
    frames = {method: [shrink_animation(base, page, base_half) if method == 'stable_dither' else
                       shrink_image(page, method, palette) for page in pages]
              for method in METHODS}
    ds_arena, ds_colors = halve_background(arena, colors)
    # Default Mima world position256,120. Boss scenery scrolls1:1 with her.
    raw_bg = Image.new('RGB', (128, 192))
    raw_palette = list(map(rgb15, pack_palette(colors)))
    raw_bg.putdata([raw_palette[arena[y][x]] for y in range(120, 312) for x in range(256, 384)])
    ds_bg = Image.new('RGB', (64, 96))
    ds_palette = list(map(rgb15, pack_palette(ds_colors)))
    ds_bg.putdata([ds_palette[ds_arena[y][x]] for y in range(60, 156) for x in range(128, 192)])
    return pages, frames, palette, raw_bg, ds_bg


def sprite_image(pixels, palette):
    image = Image.new('RGBA', (len(pixels[0]), len(pixels)))
    colors = [(*rgb15(color), 255) for color in palette]
    image.putdata([colors[int(index)] if index else (0, 0, 0, 0) for row in pixels for index in row])
    return image


def png_url(image):
    data = BytesIO()
    image.save(data, format='PNG')
    return 'data:image/png;base64,' + base64.b64encode(data.getvalue()).decode('ascii')


def label(image, x, y, text):
    # Binary glyph mask avoids introducing antialiased colors into GIF pixels.
    mask = Image.new('1', image.size)
    ImageDraw.Draw(mask).text((x, y), text, font=ImageFont.load_default(), fill=1)
    image.paste((235, 235, 235), mask=mask)


def export_comparison(packfile, background, output):
    pages, frames, palette, raw_bg, ds_bg = load_comparison(packfile, background)
    sprites = {method: [sprite_image(frame, palette) for frame in cels] for method, cels in frames.items()}
    sources = [sprite_image(page, palette) for page in pages]
    output.mkdir(parents=True, exist_ok=True)
    report = {
        'source_sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in
                          (packfile / 'BOSS3_1.BOS', packfile / 'BOSS3_2.BOS', background)},
        'asset_frames': [43, 44, 45, 46],
        'source_frames_per_cel': 8,
        'preview_hz': 60,
        'current_method': 'stable_dither',
        'methods': {},
    }
    for method, cels in frames.items():
        # These rows are outside the source animation's48..111 overlay.
        # Any change here is caused by resizing, not by moving source artwork.
        changes = [int(sum(a[y][x] != b[y][x] for y in range(96) if y < 24 or y >= 56 for x in range(64)))
                   for a, b in zip(cels, cels[1:] + cels[:1])]
        report['methods'][method] = {'description': METHODS[method], 'unchanged_body_pixel_changes': changes}

    panels = []
    for cel in range(4):
        board = Image.new('RGB', (1068, ((len(sprites)+2)//3)*432), (24, 26, 32))
        for i, (method, cels) in enumerate(sprites.items()):
            x, y = (i % 3) * 356, (i // 3) * 432
            label(board, x+12, y+8, method + (' (current)' if method == 'stable_dither' else ''))
            label(board, x+12, y+26, '1x                 4x (nearest enlargement)')
            composed = ds_bg.copy()
            composed.paste(cels[cel], (0, 0), cels[cel])
            board.paste(composed, (x+12, y+48))
            board.paste(composed.resize((256, 384), Image.Resampling.NEAREST), (x+88, y+48))
        panels.append(board)
    # Exact common palette: never apply another dither/quantizer to the preview.
    colors = sorted({color for panel in panels for color in panel.getdata()})
    if len(colors) > 256:
        raise ValueError('comparison GIF exceeds an exact256-color palette')
    indices = {color: i for i, color in enumerate(colors)}
    gif_frames = []
    for panel in panels:
        indexed = Image.new('P', panel.size)
        indexed.putpalette([channel for color in colors for channel in color] + [0] * (768-len(colors)*3))
        indexed.putdata([indices[color] for color in panel.getdata()])
        gif_frames.append(indexed)
    # GIF durations are quantized to10ms. The HTML uses the exact8/60s clock.
    gif_frames[0].save(output / 'comparison.gif', save_all=True, append_images=gif_frames[1:],
                       duration=[130, 140, 130, 130], loop=0, disposal=2, optimize=False)
    panels[0].save(output / 'first-frame.png')
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    data = {'methods': METHODS, 'report': report, 'background': png_url(ds_bg), 'sourceBackground': png_url(raw_bg),
            'source': list(map(png_url, sources)),
            'frames': {method: list(map(png_url, cels)) for method, cels in sprites.items()}}
    html = '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Mima meteor shrinking comparison</title>
<style>body{background:#181a20;color:#eee;font:15px system-ui;margin:24px}button,select{font:inherit;margin:4px}
main{display:grid;grid-template-columns:repeat(auto-fit,minmax(350px,1fr));gap:20px}article{border:1px solid #555;padding:12px}
canvas{image-rendering:pixelated;margin-right:12px;vertical-align:top}p{max-width:1000px}code{color:#aef}
small{display:block;margin:8px 0;color:#ccc}.source{margin-bottom:20px}</style>
<h1>Mima meteor: shrinking methods and stable-base dither</h1>
<p>All four meteor cels are composited over the full original body <em>before</em> shrinking, exactly as in the ROM.
Same DS palette, transparent input mask, position and8-gameplay-frame cadence.
Current stable_dither restores the base pixel for each identical source2×2 block; other variants are comparisons only.</p>
<button id="play">Pause</button><button id="step">Next cel</button>
<label>Speed <select id="speed"><option value="1">Normal</option><option value="0.5">Half speed</option></select></label>
<label>Background <select id="bg"><option value="arena">Approved arena crop</option><option value="checker">Checkerboard</option></select></label>
<span id="cel"></span><p>Each method shows native64×96 and a4× nearest enlargement (including transparent OBJ padding).
The source reference is128×192, enlarged2× to the same displayed size.</p>
<details class="source"><summary>Unshrunk source reference (DS palette)</summary><canvas id="source" width="128" height="192" style="width:256px;height:384px"></canvas></details>
<main id="methods"></main>
<p>Unchanged-body changes count differing output pixels outside the animated overlay, for transitions1→2→3→4→1.
This is a temporal-stability diagnostic, not a visual-quality score.</p>
<p>Offline, self-contained preview. <a href="comparison.gif">Animated GIF</a> (10ms-rounded timing),
<a href="first-frame.png">first-frame contact sheet</a>, <a href="report.json">source hashes and measurements</a>.</p>
<script>const data=DATA;
let cel=0,playing=true,last=0,elapsed=0;const canvases={};
async function image(url){const i=new Image();i.src=url;await i.decode();return i;}
async function start(){
 const background=await image(data.background),sourceBackground=await image(data.sourceBackground);
 const source=await Promise.all(data.source.map(image)),frames={};
 for(const [method,urls] of Object.entries(data.frames)){
  frames[method]=await Promise.all(urls.map(image));const article=document.createElement('article');
  const title=document.createElement('h2');title.textContent=method+(method===data.report.current_method?' (current)':'');article.append(title);
  const description=document.createElement('p');description.textContent=data.methods[method];article.append(description);
  canvases[method]=[1,4].map(scale=>{const c=document.createElement('canvas');c.width=64;c.height=96;
   c.style.width=64*scale+'px';c.style.height=96*scale+'px';article.append(c);return c;});
  const stats=document.createElement('small');stats.textContent='Unchanged-body changes: '+data.report.methods[method].unchanged_body_pixel_changes.join(', ');
  article.append(stats);document.getElementById('methods').append(article);
 }
 function draw(c,sprite,bg){const ctx=c.getContext('2d');ctx.imageSmoothingEnabled=false;ctx.clearRect(0,0,c.width,c.height);
  if(document.getElementById('bg').value==='arena')ctx.drawImage(bg,0,0);
  else{const step=c.width/16;for(let y=0;y<c.height;y+=step)for(let x=0;x<c.width;x+=step){ctx.fillStyle=((x/step+y/step)&1)?'#444852':'#292d35';ctx.fillRect(x,y,step,step);}}
  ctx.drawImage(sprite,0,0);}
 function paint(){for(const method of Object.keys(frames))for(const c of canvases[method])draw(c,frames[method][cel],background);
  draw(document.getElementById('source'),source[cel],sourceBackground);document.getElementById('cel').textContent='Cel '+(cel+1)+'/4';}
 document.getElementById('play').onclick=()=>{playing=!playing;elapsed=0;document.getElementById('play').textContent=playing?'Pause':'Play';};
 document.getElementById('step').onclick=()=>{cel=(cel+1)%4;elapsed=0;paint();};document.getElementById('bg').onchange=paint;
 function tick(now){if(last&&playing){elapsed+=(now-last)*Number(document.getElementById('speed').value);const period=8*1000/60;
  if(elapsed>=period){cel=(cel+Math.floor(elapsed/period))%4;elapsed%=period;paint();}}last=now;requestAnimationFrame(tick);}
 paint();requestAnimationFrame(tick);
}start().catch(error=>{document.body.append('Preview error: '+error);});</script></html>'''
    (output / 'index.html').write_text(html.replace('DATA', json.dumps(data)), encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packfile', type=Path, default=Path('assets/packfile'))
    parser.add_argument('--background', type=Path, default=Path('assets/hdi_dump/BOSS3.GRP'))
    parser.add_argument('--output', type=Path, default=Path('build/mima-shrink-comparison'))
    args = parser.parse_args()
    report = export_comparison(args.packfile, args.background, args.output)
    print(args.output / 'index.html')
    for method, values in report['methods'].items():
        print(method, 'unchanged-body changes:', values['unchanged_body_pixel_changes'])


if __name__ == '__main__':
    main()
