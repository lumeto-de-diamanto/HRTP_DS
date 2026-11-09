#include <string.h>

#include "canvas.hpp"
#include "camera.hpp"

static int clip_top;
static u32 dirty_rows[CANVAS_H / 8];
void canvas_playfield_clip(bool enabled) { clip_top = enabled ? UPPER_HUD_H : 0; }

static inline u16 opaque(u16 bgr15)
{
    return 0x8000 | bgr15;
}

// Clips a rect to the canvas. Without this, out-of-bounds draws wrap to
// adjacent rows (256-pixel stride) -- the pellet wrap-around bug.
static bool clip_rect(int &x, int &y, int &w, int &h)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < clip_top) {
        h -= clip_top - y;
        y = clip_top;
    }
    if (x + w > CANVAS_W)
        w = CANVAS_W - x;
    if (y + h > CANVAS_H)
        h = CANVAS_H - y;
    return (w > 0 && h > 0);
}

static void mark_dirty(int x, int y, int w, int h)
{
    if (!clip_rect(x, y, w, h)) return;
    const int first = x / 8, count = (x + w - 1) / 8 - first + 1;
    const u32 mask = count == 32 ? ~u32(0) : ((u32(1) << count) - 1) << first;
    for (int row = y / 8; row <= (y + h - 1) / 8; ++row)
        dirty_rows[row] |= mask;
}

void canvas_begin_frame()
{
    // Regions are screen-space: clear the OLD projected locations before
    // drawing the new ones, regardless of the current camera offset/clip.
    for (int row = 0; row < CANVAS_H / 8; ++row) {
        const u32 mask = dirty_rows[row];
        dirty_rows[row] = 0;
        for (int x = 0; x < 32;) {
            if (!(mask & (u32(1) << x))) { ++x; continue; }
            const int first = x++;
            while (x < 32 && (mask & (u32(1) << x))) ++x;
            for (int y = row * 8; y < row * 8 + 8; ++y)
                dmaFillHalfWords(0, canvas_at(first * 8, y), (x - first) * 8 * sizeof(u16));
        }
    }
}

void canvas_init()
{
    canvas_clear();
}

void canvas_clear()
{
    dmaFillHalfWords(0, canvas_at(0, 0), CANVAS_W * CANVAS_H * sizeof(u16));
    memset(dirty_rows, 0, sizeof(dirty_rows));
}

void canvas_clear_rect(int x, int y, int w, int h)
{
    if (!clip_rect(x, y, w, h))
        return;
    // 16-bit DMA: byte stores to VRAM are dropped by the hardware (see
    // subscreen.cpp), and memset can fall back to them for small or
    // 2-byte-aligned ranges -- which caused pellet trails.
    for (int row = y; row < y + h; row++)
        dmaFillHalfWords(0, canvas_at(x, row), w * sizeof(u16));
}

void canvas_fill_rect(int x, int y, int w, int h, u16 bgr15)
{
    if (!clip_rect(x, y, w, h))
        return;
    mark_dirty(x, y, w, h);
    u16 px = opaque(bgr15);
    for (int row = y; row < y + h; row++) {
        u16 *p = canvas_at(x, row);
        for (int col = 0; col < w; col++)
            p[col] = px;
    }
}

void canvas_hline(int x, int y, int w, u16 bgr15)
{
    canvas_fill_rect(x, y, w, 1, bgr15);
}

void canvas_vline(int x, int y, int h, u16 bgr15)
{
    canvas_fill_rect(x, y, 1, h, bgr15);
}

void canvas_put_indexed(int x, int y, int w, int h,
                        const u8 *pixels, const u16 *pal)
{
    mark_dirty(x, y, w, h);
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < clip_top || py >= CANVAS_H)
            continue;
        const u8 *src = pixels + (row * w);
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || px >= CANVAS_W)
                continue;
            u8 index = src[col];
            if (index != 0)
                canvas_at(px, py)[0] = opaque(pal[index]);
        }
    }
}

#include "fonts/font8x16.h"
#include "font3x5.hpp"
#include "font16x16.hpp"

void canvas_text_scaled(int x, int y, const char *s, int scale, u16 bgr15)
{
    for (; *s; s++, x += 8 * scale) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7F)
            continue;
        const unsigned char *glyph = font8x16[c - 0x20];
        for (int gy = 0; gy < 16; gy++) {
            unsigned char bits = glyph[gy];
            if (!bits)
                continue;
            for (int gx = 0; gx < 8; gx++) {
                if (bits & (0x80 >> gx))
                    canvas_fill_rect(x + gx * scale, y + gy * scale,
                                     scale, scale, bgr15);
            }
        }
    }
}

void canvas_jis_glyph_scaled(int x, int y, u16 jis, int scale, u16 bgr15)
{
    const u16 *glyph = font16x16_find(jis);
    if (!glyph)
        return;
    for (int gy = 0; gy < 16; gy++) {
        u16 bits = glyph[gy];
        for (int gx = 0; gx < 16; gx++) {
            if (bits & (0x8000 >> gx))
                canvas_fill_rect(x + gx * scale, y + gy * scale,
                                 scale, scale, bgr15);
        }
    }
}

void canvas_jis_window_scaled(int x, int y, u16 jis, int scale)
{
    const u16 *glyph = font16x16_find(jis);
    if (!glyph)
        return;
    for (int gy = 0; gy < 16; gy++) {
        u16 bits = glyph[gy];
        for (int gx = 0; gx < 16; gx++) {
            if (bits & (0x8000 >> gx))
                canvas_clear_rect(x + gx * scale, y + gy * scale,
                                  scale, scale);
        }
    }
}

void canvas_text_3x5(int x, int y, const char *s, u16 bgr15)
{
    for (; *s; s++, x += 4) {
        mark_dirty(x, y, 3, 6);
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7F)
            continue;
        for (int line = 0; line < 6; line++) {
            int py = y + line;
            if (py < clip_top || py >= CANVAS_H)
                continue;
            uint8_t bits = font3x5_line((char)c, line);
            if (!bits)
                continue;
            for (int gx = 0; gx < 3; gx++) {
                int px = x + gx;
                if (px < 0 || px >= CANVAS_W)
                    continue;
                if (bits & (0x8 >> gx))
                    canvas_at(px, py)[0] = opaque(bgr15);
            }
        }
    }
}

#include "main/math/trig.hpp"

void canvas_circle_dots(int cx, int cy, int radius, u16 bgr15, bool clear)
{
    for (int i = 0; i < 32; i++) {
        unsigned char angle = (unsigned char)(i * 0x08);
        int x = cx + ((radius * Cos8(angle)) >> 8);
        int y = cy + ((radius * Sin8(angle)) >> 8);
        if (x < 0 || x >= CANVAS_W || y < clip_top || y >= CANVAS_H)
            continue;
        u16 *p = canvas_at(x, y);
        if (!clear) mark_dirty(x, y, 1, 1);
        *p = clear ? 0 : opaque(bgr15);
    }
}
