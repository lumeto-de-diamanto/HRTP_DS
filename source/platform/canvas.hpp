#pragma once

#include <nds.h>

// Effects canvas (BG2, Bmp16 256x256): bullets, particles, lasers and
// other transient effects. This is the port's replacement for TH01's
// "put onto VRAM page 0 / unput by restoring from page 1" model: layer
// compositing does the work, so "unput" is simply clearing a rect to
// transparent (bit 15 clear = invisible).
//
// Pixel format: 0x8000 | BGR15. Coordinates are screen coordinates
// (256x192 visible; rows 192..255 are scratch).

static const int CANVAS_W = 256;
static const int CANVAS_H = 192;

// Bmp16 at mapBase 16 (16 KB units) = 0x40000, in VRAM D -- see video.hpp.
#define CANVAS_BMP_BLOCK 16
extern u16 *g_canvas_target; // null: direct VRAM; otherwise caller-owned256x256 RAM

static inline u16 *canvas_at(int x, int y)
{
    return (g_canvas_target ? g_canvas_target : BG_BMP_RAM(CANVAS_BMP_BLOCK)) + (y * CANVAS_W) + x;
}

void canvas_init();
// Erase only last frame's touched 8x8 regions, then track this frame's ink.
void canvas_begin_frame();
// Optional buffered presentation for combat. Compose ALL effects, flush once,
// then present in VBlank. No allocation; the owner supplies128KiB of RAM.
bool canvas_buffer_begin(u16 *pixels);
// Preserve the published ink map when handing an ordinary stage back to direct
// drawing, so its next begin_frame can erase the last buffered effects.
void canvas_buffer_end(bool preserve_ink = false);
void canvas_buffer_prepare();
bool canvas_buffer_ready();
// already_gated is for a grouped early-VBlank commit that publishes BG/OAM
// registers first, then streams pixels top-to-bottom without a second gate.
bool canvas_buffer_present(bool already_gated = false);
// Direct RAM writers (e.g. pause snapshot/dim/restore) must publish their writes.
void canvas_buffer_dirty_all();
// Gameplay draw calls clip below the transparent HUD; screen-space menus and
// the HUD itself disable this clip. Clearing the whole canvas always works.
void canvas_playfield_clip(bool enabled);
void canvas_clear();                       // whole visible area
void canvas_clear_rect(int x, int y, int w, int h); // the "unput"
void canvas_fill_rect(int x, int y, int w, int h, u16 bgr15);
void canvas_hline(int x, int y, int w, u16 bgr15);
void canvas_vline(int x, int y, int h, u16 bgr15);

// Blits indexed pixels (row-major w*h), palette index 0 transparent.
// pal[] holds plain BGR15 values (without the alpha bit).
void canvas_put_indexed(int x, int y, int w, int h,
                        const u8 *pixels, const u16 *pal);

// 8x16 ANK text drawn at an integer scale (used for the HARRY UP banner).
void canvas_text_scaled(int x, int y, const char *s, int scale, u16 bgr15);

// One 16x16 PC-98 JIS glyph drawn at an integer scale.
void canvas_jis_glyph_scaled(int x, int y, u16 jis, int scale, u16 bgr15);

// Clears the ink pixels of one scaled JIS glyph, making a glyph-shaped window
// through BG2 to the layers beneath.
void canvas_jis_window_scaled(int x, int y, u16 jis, int scale);

// 3x5 tiny font text for the playfield (score popups); 4px pitch.
void canvas_text_3x5(int x, int y, const char *s, u16 bgr15);

// 32-dot circle outline (ReC98 shape_circle_put with angle step 0x08).
// Pass `clear` to erase the same dots instead (shape_circle_sloppy_unput).
void canvas_circle_dots(int cx, int cy, int radius, u16 bgr15, bool clear);
