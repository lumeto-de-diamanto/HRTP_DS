#include <nds.h>
#include <string.h>
#include <stdlib.h>

#include "palette_fx.hpp"
#include "canvas.hpp"
#include "video.hpp"

// The background/tile palette and the sprite palette dim via palette RAM;
// the effects canvas is 15bpp direct color, so it is snapshotted and
// scaled in place (one-shot cost per pause/bonus screen).

static u16 bg_snap[256];
static u16 obj_snap[256];
static u16 *canvas_snap;

static void present_buffered_canvas()
{
    if(!g_canvas_target) return;
    canvas_buffer_dirty_all();canvas_buffer_prepare();
    do { swiWaitForVBlank(); } while(!canvas_buffer_present());
}

static u16 dim_color(u16 c, int tone)
{
    if (c == 0x7FFF) // keep white
        return c;
    int r = (c & 31) * tone / 100;
    int g = ((c >> 5) & 31) * tone / 100;
    int b = ((c >> 10) & 31) * tone / 100;
    return r | (g << 5) | (b << 10);
}

void palette_dim(int tone)
{
    memcpy(bg_snap, BG_PALETTE, 256 * sizeof(u16));
    memcpy(obj_snap, SPRITE_PALETTE, 256 * sizeof(u16));
    if(!g_canvas_target) for (int i = 0; i < 256; i++) {
        BG_PALETTE[i] = dim_color(bg_snap[i], tone);
        SPRITE_PALETTE[i] = dim_color(obj_snap[i], tone);
    }

    if (!canvas_snap)
        canvas_snap = (u16 *)malloc(CANVAS_W * CANVAS_H * sizeof(u16));
    if (canvas_snap) {
        u16 *px = canvas_at(0, 0);
        memcpy(canvas_snap, px, CANVAS_W * CANVAS_H * sizeof(u16));
        for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
            u16 c = px[i];
            if (c == 0)
                continue; // transparent stays transparent
            px[i] = 0x8000 | dim_color(c & 0x7FFF, tone);
        }
    }
    video_bomb_pause(true); // pause must dim even a fully white recovery frame
    if(g_canvas_target) {
        // Publish the RAM effects and indexed palettes in the same VBlank.
        present_buffered_canvas();
        for(int i=0;i<256;++i) {
            BG_PALETTE[i]=dim_color(bg_snap[i],tone);
            SPRITE_PALETTE[i]=dim_color(obj_snap[i],tone);
        }
    }
}

void palette_restore()
{
    if(!g_canvas_target) {
        memcpy(BG_PALETTE, bg_snap, 256 * sizeof(u16));
        memcpy(SPRITE_PALETTE, obj_snap, 256 * sizeof(u16));
    }
    if (canvas_snap) {
        memcpy(canvas_at(0, 0), canvas_snap,
               CANVAS_W * CANVAS_H * sizeof(u16));
    }
    if(g_canvas_target) {
        present_buffered_canvas();
        memcpy(BG_PALETTE,bg_snap,256*sizeof(u16));
        memcpy(SPRITE_PALETTE,obj_snap,256*sizeof(u16));
    }
    video_bomb_pause(false);
}

void palette_toward_white(int amount)
{
    if (amount < 0) amount = 0;
    if (amount > 31) amount = 31;
    for (int i = 0; i < 256; ++i) {
        const u16 values[] = {bg_snap[i], obj_snap[i]};
        u16 result[2];
        for (int p = 0; p < 2; ++p) {
            const int r = values[p] & 31, g = (values[p] >> 5) & 31, b = (values[p] >> 10) & 31;
            result[p] = (r + (31 - r) * amount / 31) |
                ((g + (31 - g) * amount / 31) << 5) |
                ((b + (31 - b) * amount / 31) << 10);
        }
        BG_PALETTE[i] = result[0]; SPRITE_PALETTE[i] = result[1];
    }
}

// Sprite palette banks used by the item renderers.
#define ITEM_PAL_BOMB 1
#define ITEM_PAL_POINT 2

#include "backgrounds/st0.h" // round 1 palette (point orb pin)

// The asset conversion renumbers PTN colors as (raw + 1) % 16 so that the
// color key (raw 15) lands on transparent slot 0. A palette built for
// those indices must therefore rotate: slot s holds raw color s-1.
static void build_rotated(u16 *dst, const u16 *round)
{
    for (int s = 0; s < 16; s++)
        dst[s] = round[(s - 1) & 15];
}

void palette_apply_round()
{
    // Stage-object tiles (BG palette bank 15) and bomb items follow the
    // current round; the point orb stays on round 1's look.
    build_rotated(BG_PALETTE + 240, BG_PALETTE);
    build_rotated(SPRITE_PALETTE + ITEM_PAL_BOMB * 16, BG_PALETTE);
    build_rotated(SPRITE_PALETTE + ITEM_PAL_POINT * 16, st0_palette);
}
