#include <nds.h>

#include "hud_render.hpp"
#include "sound_effects.hpp"
#include "subscreen.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"

#include "main/stage/stageobj.hpp"
#include "main/stage/item.hpp"
#include "main/stage/timer.hpp"
#include "rank.h"

#include "fonts/font8x16.h"
#include "font16x16.hpp"

// Simple bottom-screen HUD; final styling remains M6. Values right-align to
// shared edges per the project head's feedback.

static const char *rank_name()
{
    static const char *names[] = { "EASY", "NORMAL", "HARD", "LUNATIC" };
    int r = (int)rank;
    return (r >= 0 && r < 4) ? names[r] : "?";
}

namespace {
bool initialized;
struct HudValues { int score, hi, combo, maximum, stage, rank; } previous;

void number_if_changed(int x, int y, int value, int old, int digits, bool wide, bool force)
{
    if (!force && value == old) return;
    subscreen_fill_rect(x, y, digits * (wide ? 16 : 8), 16, SUB_COL_BG);
    if (wide) subscreen_fullwidth_number(x, y, value, digits, SUB_COL_FG);
    else subscreen_number(x, y, value, digits, SUB_COL_FG);
}
}

void hud_invalidate() { initialized = false; }

void hud_render(int /*lives*/, int stage)
{
    const bool force = !initialized;
    const HudValues now{stage_score, hiscore, stage_cardcombo_cur, stage_cardcombo_max, stage, int(rank)};
    if (force) {
        subscreen_clear(SUB_COL_BG);
        // Static frame and labels are drawn once, not every game frame.
        subscreen_fill_rect(0, 0, 256, 4, SUB_COL_FG);
        subscreen_fill_rect(0, 188, 256, 4, SUB_COL_FG);
        subscreen_fill_rect(0, 0, 4, 192, SUB_COL_FG);
        subscreen_fill_rect(252, 0, 4, 192, SUB_COL_FG);
        subscreen_text(8, 8, "SCORE", SUB_COL_DIM);
        subscreen_text(8, 48, "HI", SUB_COL_DIM);
        subscreen_text(8, 88, "COMBO", SUB_COL_DIM);
        subscreen_text(8, 112, "MAX", SUB_COL_DIM);
        subscreen_text(8, 160, "RANK", SUB_COL_DIM);
        subscreen_text(152, 104, "STAGE", SUB_COL_DIM);
    }
    number_if_changed(8, 24, now.score, previous.score, 7, true, force);
    number_if_changed(8, 64, now.hi, previous.hi, 7, true, force);
    number_if_changed(80, 88, now.combo, previous.combo, 3, true, force);
    number_if_changed(80, 112, now.maximum, previous.maximum, 3, true, force);
    number_if_changed(232, 124, now.stage, previous.stage, 2, false, force);
    if (force || now.rank != previous.rank) {
        subscreen_fill_rect(56, 160, 56, 16, SUB_COL_BG);
        subscreen_text(56, 160, rank_name(), SUB_COL_FG);
    }
    previous = now;
    initialized = true;
}

// ReC98's banners (harryup_animate, stage_num_animate): blocking two-phase
// giant letters over the playfield, with fixed pauses between phases. The
// original's "HARRY UP" typo is fixed per the quirk policy (docs/
// ds-port-plan.md section 5).

// PC-98 glyphs carry 2-3 columns of left padding, so text centered by
// advance width sits visibly right of center. Center on the ink instead.
static int ink_span(const char *s, int scale, int &left_pad)
{
    int min_x = 8, max_x = -1, i = 0;
    for (; *s; s++, i++) {
        const unsigned char *glyph = font8x16[(unsigned char)*s - 0x20];
        for (int y = 0; y < 16; y++) {
            unsigned char bits = glyph[y];
            for (int x = 0; x < 8; x++) {
                if (bits & (0x80 >> x)) {
                    int gx = i * 8 + x;
                    if (gx < min_x)
                        min_x = gx;
                    if (gx > max_x)
                        max_x = gx;
                }
            }
        }
    }
    left_pad = min_x * scale;
    return (max_x - min_x + 1) * scale;
}

static void banner_line(const char *s)
{
    int left_pad;
    int w = ink_span(s, 6, left_pad);
    canvas_text_scaled((CANVAS_W - w) / 2 - left_pad, 48, s, 6, RGB15(31, 0, 0));
}

void banner_two_phase(const char *line1, const char *line2, int pause_frames)
{
    CanvasForegroundScope foreground;
    canvas_playfield_clip(false);
    canvas_clear_rect(0, UPPER_HUD_H, VIEW_W, VIEW_PLAY_H);
    banner_line(line1);
    for (int i = 0; i < pause_frames; i++)
        swiWaitForVBlank();

    canvas_clear_rect(0, UPPER_HUD_H, VIEW_W, VIEW_PLAY_H);
    banner_line(line2);
    for (int i = 0; i < pause_frames; i++)
        swiWaitForVBlank();

    canvas_clear_rect(0, UPPER_HUD_H, VIEW_W, VIEW_PLAY_H);
}

void harryup_animate()
{
    mdrv2_se_play(SFX_HURRY_UP);
    banner_two_phase("HURRY", "UP", 27);
}

static void prepare_title_mask(int page, u16 jis)
{
    // BG2's invisible rows 192..255 are already allocated VRAM. Two compact
    // 32x24 masks fit side by side there. Hardware magnification turns each
    // source pixel into an 8x8 screen block, with a centered 16x16 glyph.
    // Prepare only the INACTIVE mask; never black out the displayed glyph.
    const u16 *glyph = font16x16_find(jis);
    for (int y = 0; y < 24; ++y) {
        u16 *out = canvas_at(page * 32, CANVAS_H + y);
        for (int x = 0; x < 32; ++x) {
            const bool ink = glyph && x >= 8 && x < 24 && y >= 4 && y < 20 &&
                (glyph[y - 4] & (0x8000 >> (x - 8)));
            out[x] = ink ? 0 : 0x8000;
        }
    }
}

static void touhou_reiiden_animate()
{
    // Original: each 16x16 CG-ROM glyph is expanded to 256x256 in PC-98
    // TRAM. The DS backend is 50%, so scale 8 reproduces its 128x128 size.
    // Reverse-video spaces mask the screen while the glyph's ink acts as a
    // cut-out to the stage beneath; on BG2, opaque black + transparent glyph
    // pixels reproduces that composition directly.
    // Repainting a full visible black field before punching the next window
    // could expose the reported blank fields even with valid glyphs. Flip a small
    // affine source window instead: each complete mask follows the last one
    // directly at the VBlank ending its 8-frame hold. No DMA/cache or RAM buffer.
    prepare_title_mask(0, JIS_TOUHOU_REIIDEN[0]);
    bgSetScale(g_bg_id_canvas, 32, 32); // inverse scale: 256/8
    for (int i = 0; i < 6; i++) {
        bgSetScroll(g_bg_id_canvas, (i % 2) * 32, CANVAS_H);
        bgUpdate();
        mdrv2_se_play(SFX_TITLE_GLYPH);
        if (i + 1 < 6)
            prepare_title_mask((i + 1) % 2, JIS_TOUHOU_REIIDEN[i + 1]);
        for (int frame = 0; frame < 8; frame++)
            swiWaitForVBlank();
    }
    // Clear normal effects while they are still offscreen, then restore the
    // usual 1:1 canvas before STAGE text. Scenery/camera/HUD layers never move.
    canvas_clear();
    bgSetScale(g_bg_id_canvas, 256, 256);
    bgSetScroll(g_bg_id_canvas, 0, 0);
    bgUpdate();
}

void stage_num_animate(int stage_num, bool show_title)
{
    CanvasForegroundScope foreground;
    canvas_playfield_clip(false);
    if (show_title) {
        // Scene entrances in ReC98 contain only the background/stage field.
        // Suppress stale OAM from the previous scene while the BG2 windows
        // reveal the layers underneath; later stage banners intentionally
        // retain the player, matching the original's alternate path.
        // Hide gameplay only, not the fixed HUD icons/HP OAM at slots48+.
        for (int slot = 0; slot <= 40; ++slot) oamSetHidden(&oamMain, slot, true);
        oamUpdate(&oamMain);
        touhou_reiiden_animate();
    }
    char digits[3];
    digits[0] = '0' + ((stage_num / 10) % 10);
    digits[1] = '0' + (stage_num % 10);
    digits[2] = '\0';
    banner_two_phase("STAGE", digits, 35);
}
