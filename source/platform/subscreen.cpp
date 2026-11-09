#include <stdio.h>
#include <string.h>

#include "subscreen.hpp"
#include "hud_render.hpp"

#include "fonts/font8x16.h"
#include "font16x16.hpp"

#define SUB_BMP_BLOCK 0 // VRAM_C_SUB_BG at 0x06200000

static inline u8 *sub_px(int x, int y)
{
    return (u8 *)BG_BMP_RAM_SUB(SUB_BMP_BLOCK) + (y * 256) + x;
}

// NOTE: the DS VRAM bus is 16 bits wide -- byte (STRB) writes to VRAM are
// silently discarded, so every pixel write goes through a halfword
// read-modify-write. Fills use 16-bit DMA for the same reason.
static inline void sub_pixel(int x, int y, u8 color)
{
    u16 *p = (u16 *)((u8 *)BG_BMP_RAM_SUB(SUB_BMP_BLOCK) + (y * 256) + (x & ~1));
    u16 v = *p;
    v = (x & 1) ? ((v & 0x00FF) | ((u16)color << 8))
                : ((v & 0xFF00) | color);
    *p = v;
}

void subscreen_init()
{
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);

    // Layer 2: the BlocksDS bitmap examples use layer 2 on the sub
    // engine (bg_bmp_8bit_shared_pal); follow the proven pattern.
    int bg = bgInitSub(2, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    if (bg < 0) {
        consoleDemoInit();
        printf("sub bg init failed!");
        while (1)
            swiWaitForVBlank();
    }

    BG_PALETTE_SUB[SUB_COL_BG] = RGB15(2, 2, 4);
    BG_PALETTE_SUB[SUB_COL_FG] = RGB15(31, 31, 31);
    BG_PALETTE_SUB[SUB_COL_DIM] = RGB15(22, 22, 26);
    BG_PALETTE_SUB[SUB_COL_ACCENT] = RGB15(31, 10, 12);

    subscreen_clear(SUB_COL_BG);
    hud_invalidate();
}

void subscreen_clear(u8 color)
{
    dmaFillHalfWords((color << 8) | color, sub_px(0, 0), 256 * 192);
}

void subscreen_fill_rect(int x, int y, int w, int h, u8 color)
{
    if ((x | w) & 1) {
        for (int row = y; row < y + h; row++)
            for (int col = 0; col < w; col++)
                sub_pixel(x + col, row, color);
        return;
    }
    u16 fill = ((u16)color << 8) | color;
    for (int row = y; row < y + h; row++)
        dmaFillHalfWords(fill, sub_px(x, row), w);
}

void subscreen_text(int x, int y, const char *s, u8 color)
{
    for (; *s; s++, x += 8) {
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
                    sub_pixel(x + gx, y + gy, color);
            }
        }
    }
}

void subscreen_number(int x, int y, int value, int digits, u8 color)
{
    char buf[12];
    if (digits > 11)
        digits = 11;
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = '0' + (value % 10);
        value /= 10;
    }
    buf[digits] = '\0';
    subscreen_text(x, y, buf, color);
}

void subscreen_fullwidth_number(int x, int y, int value, int digits, u8 color)
{
    if (digits > 10)
        digits = 10;
    int divisor = 1;
    for (int i = 1; i < digits; i++)
        divisor *= 10;

    for (int i = 0; i < digits; i++, x += 16) {
        int digit = (value / divisor) % 10;
        if (divisor > 1)
            divisor /= 10;
        const u16 *glyph = font16x16_find(JIS_FULLWIDTH_DIGIT_0 + digit);
        if (!glyph)
            continue;
        for (int gy = 0; gy < 16; gy++) {
            u16 bits = glyph[gy];
            for (int gx = 0; gx < 16; gx++) {
                if (bits & (0x8000 >> gx))
                    sub_pixel(x + gx, y + gy, color);
            }
        }
    }
}

void subscreen_put_indexed(int x, int y, int w, int h,
                           const u8 *pixels, int pal_base)
{
    for (int row = 0; row < h; row++) {
        const u8 *src = pixels + (row * w);
        for (int col = 0; col < w; col++) {
            u8 index = src[col];
            if (index != 0)
                sub_pixel(x + col, y + row, pal_base + index);
        }
    }
}
