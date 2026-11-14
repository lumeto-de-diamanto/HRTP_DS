#include <nds.h>
#include <string.h>

#include "palette_fx.hpp"
#include "video.hpp"
#include "text_layer.hpp"

// Only palettes need a small snapshot: modal text owns reserved ink slots,
// and SinGyoku's white-line transition still animates indexed colours.
// Scene/OBJ dimming never touches their palettes, canvas pixels or the heap.
static u16 bg_snap[256];
static u16 obj_snap[256];
static bool palette_owned;

static void palette_effect_begin()
{
    if(palette_owned) return;
    memcpy(bg_snap,BG_PALETTE,sizeof(bg_snap));
    memcpy(obj_snap,SPRITE_PALETTE,sizeof(obj_snap));
    palette_owned=true;
    video_modal_effect_begin();
}

void palette_dim(int tone,bool keep_canvas_bright)
{
    palette_effect_begin();
    if(tone<0) tone=0;
    if(tone>100) tone=100;
    // BLDY has 16 steps; round to the nearest native intensity (40% ->10).
    const int amount=((100-tone)*16+50)/100;
    video_set_fade(amount);
    REG_BLDCNT=tone==100 ? 0 : BLEND_FADE_BLACK|BLEND_SRC_BG1|
        BLEND_SRC_BG3|BLEND_SRC_SPRITE|BLEND_SRC_BACKDROP|
        (keep_canvas_bright ? 0 : BLEND_SRC_BG2);
    // Both halves of the upper HUD now admit colour effects for its OBJ and
    // scenery. BG0 stays excluded so modal/route text is not dimmed with it.
    if(tone<100) REG_WININ|=0x2020;
    else REG_WININ&=~0x2020;
    REG_MASTER_BRIGHT_SUB=(2<<14)|amount;
    // BG0 HUD counters/divider share a layer with the bright menu. Only their
    // three ink slots need the same native BLDY equation, including white.
    const int inks[]={TEXT_PAL_WHITE*16+1,TEXT_PAL_RED*16+1,TEXT_PAL_RED*16+2};
    for(int ink:inks) {
        const u16 c=bg_snap[ink];u16 result=0;
        for(int shift=0;shift<=10;shift+=5) {
            const int component=(c>>shift)&31;
            result|=(component-component*amount/16)<<shift;
        }
        BG_PALETTE[ink]=result;
    }
}

void palette_white_begin()
{
    palette_effect_begin();
    REG_BLDCNT=0;video_set_fade(0);
}

void palette_restore()
{
    if(!palette_owned) return;
    memcpy(BG_PALETTE,bg_snap,sizeof(bg_snap));
    memcpy(SPRITE_PALETTE,obj_snap,sizeof(obj_snap));
    video_modal_effect_end();
    palette_owned=false;
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
