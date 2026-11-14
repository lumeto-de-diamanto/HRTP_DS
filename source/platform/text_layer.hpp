#pragma once

// Text on the top screen: the reserved BG0 (Text4bpp) layer, used by the
// pause menu and the stage bonus tally. Glyphs come from the PC-98 8x16
// ANK font (tools/font_atlas.py), baked as tiles once per glyph and
// colored per palette bank (white = bank 14, red = bank 13).

// Bank/palette layout (see palette.hpp): ink color index 1.
#define TEXT_PAL_WHITE 14
#define TEXT_PAL_RED 13

void text_layer_init();
// Clear menu rows only; the upper three tile rows belong to the fixed HUD.
void text_layer_clear();
// Four native8x16 ANK digits, right-aligned; shared with HUD layout bounds.
constexpr int TEXT_HUD_TIMER_DIGITS = 4;
constexpr int TEXT_HUD_TIMER_X = 256 - TEXT_HUD_TIMER_DIGITS * 8;
void text_layer_hud_timer(unsigned value);
void text_layer_hud_resources_clear(); // preserves divider and timer/cache
void text_layer_hud_lives_count(int count); // diagnostic fallback for >6 lives
void text_layer_hud_resource_number(int gx, int count); // 8x16 boss count cell, 0..12

// Glyph-grid coordinates: gx in 8px columns (0..31), gy in 16px rows
// (0..11).
void text_layer_puts(int gx, int gy, const char *s, int pal_bank);
void text_layer_number(int gx, int gy, int value, int digits, int pal_bank);

// Source-style modal artwork in BG0, below the approved24px HUD. A fixed
// tile-backed bitmap supports full16x16 JIS and native8x16 ANK glyphs at pixel positions
// without OAM/heap allocation or modifying the gameplay effects canvas.
constexpr int TEXT_MODAL_BLACK = 3;
constexpr int TEXT_MODAL_WHITE = 4;
constexpr int TEXT_MODAL_RED = 5;
void text_layer_modal_begin();
void text_layer_modal_pixel(int x, int y, int ink);
void text_layer_modal_fill_rect(int x, int y, int w, int h, int ink);
void text_layer_modal_clear_rect(int x, int y, int w, int h);
void text_layer_modal_glyph(int x, int y, unsigned jis, int ink, int weight = 2);
void text_layer_modal_text(int x, int y, const char *text, int ink);

// Horizontal size of a string in glyph columns.
static inline int text_layer_width(const char *s)
{
    int w = 0;
    while (*s++)
        w++;
    return w;
}
