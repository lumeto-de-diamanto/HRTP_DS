#include <stdio.h>
#include <string.h>

#include "subscreen.hpp"
#include "hud_render.hpp"

#include "fonts/font8x16.h"
#include "font16x16.hpp"
#include "backgrounds/HudPattern.h"

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
    if(unsigned(x)>=256 || unsigned(y)>=192) return;
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

    // BG2 overlay64KiB + BG3 pattern16KiB fit in the existing128KiB VRAM C.
    // The pattern repeats in hardware; changing a number never repaints it.
    int bg = bgInitSub(2, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    const int pattern = bgInitSub(3, BgType_Bmp8, BgSize_B8_128x128, 4, 0);
    if (bg < 0 || pattern < 0) {
        consoleDemoInit();
        printf("sub bg init failed!");
        while (1)
            swiWaitForVBlank();
    }
    bgSetPriority(bg,0);bgSetPriority(pattern,3);bgWrapOn(pattern);
    DC_FlushRange(HudPattern_pixels,sizeof(HudPattern_pixels));
    dmaCopyHalfWords(3,HudPattern_pixels,BG_BMP_RAM_SUB(4),sizeof(HudPattern_pixels));

    BG_PALETTE_SUB[SUB_COL_BG] = 0;
    BG_PALETTE_SUB[SUB_COL_FG] = RGB15(31, 31, 31);
    BG_PALETTE_SUB[SUB_COL_DIM] = RGB15(31, 0, 0);
    BG_PALETTE_SUB[SUB_COL_ACCENT] = RGB15(10, 1, 1);
    BG_PALETTE_SUB[4] = RGB15(1,1,1);
    BG_PALETTE_SUB[5] = RGB15(3,2,2);
    BG_PALETTE_SUB[SUB_COL_RADAR_BG] = 0;
    BG_PALETTE_SUB[SUB_COL_RADAR_CARD] = RGB15(10,10,10);
    BG_PALETTE_SUB[SUB_COL_RADAR_OBSTACLE] = RGB15(16,5,5);
    BG_PALETTE_SUB[SUB_COL_RADAR_PORTAL] = RGB15(18,10,31);
    BG_PALETTE_SUB[SUB_COL_RADAR_ITEM] = RGB15(31,25,5);
    BG_PALETTE_SUB[SUB_COL_RADAR_PLAYER] = RGB15(31,0,0);
    BG_PALETTE_SUB[SUB_COL_RADAR_BOSS] = RGB15(31,10,22);

    subscreen_clear(SUB_COL_BG);
    bgUpdate();
    hud_invalidate();
}

void subscreen_clear(u8 color)
{
    dmaFillHalfWords((color << 8) | color, sub_px(0, 0), 256 * 192);
}

void subscreen_fill_rect(int x, int y, int w, int h, u8 color)
{
    if(x<0) { w+=x;x=0; } if(y<0) { h+=y;y=0; }
    if(x+w>256) w=256-x;
    if(y+h>192) h=192-y;
    if(w<=0 || h<=0) return;
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

void subscreen_fullwidth_text(int x, int y, const char *s, u8 color)
{
    for (; *s; ++s, x += 16) {
        const unsigned char c = *s;
        const u16 jis = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            ? 0x2300 + c : c == '?' ? 0x2129 : 0;
        const u16 *glyph = font16x16_find(jis);
        if (!glyph) continue;
        for (int gy = 0; gy < 16; ++gy)
            for (int gx = 0; gx < 16; ++gx)
                if (glyph[gy] & (0x8000 >> gx))
                    sub_pixel(x + gx, y + gy, color);
    }
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
            if(index && col+1<w && src[col+1] && !((x+col)&1) &&
               x+col>=0 && x+col+1<256 && y+row>=0 && y+row<192) {
                // Radar tiles are opaque/nonzero and even-aligned: one safe
                // halfword store for two pixels, no stack-DMA/cache hazards.
                auto *out=reinterpret_cast<u16*>(sub_px(x+col,y+row));
                *out=u16(u8(pal_base+index))|(u16(u8(pal_base+src[col+1]))<<8);
                ++col;continue;
            }
            if (index != 0)
                sub_pixel(x + col, y + row, pal_base + index);
        }
    }
}
