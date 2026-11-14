#include <nds.h>
#include <string.h>

#include "text_layer.hpp"
#include "palette.hpp"
#include "upper_hud.hpp"

#include "fonts/font8x16.h"

// BG0 Text4bpp: tileBase 2 (0x8000), mapBase 30 (0xF000) -- both inside
// the first 64 KB, where text BG maps must live (mapBase <= 31). Tile 0
// stays blank so that zeroed map entries are transparent.
#define TEXT_TILE_BASE 2
#define TEXT_SCREENBLOCK 30
#define TEXT_FIRST_TILE 1
#define TEXT_MAP_W 32 // tiles
#define HUD_DIVIDER_TILE (TEXT_FIRST_TILE + 96 * 2)
#define HUD_COUNT_FIRST (HUD_DIVIDER_TILE + 1)
static unsigned last_hud_timer = ~0u;

void text_layer_init()
{
    int bg = bgInit(0, BgType_Text4bpp, BgSize_T_256x256,
                    TEXT_SCREENBLOCK, TEXT_TILE_BASE);
    bgSetPriority(bg, 0); // front layer

    // Bake each 8x16 glyph as two stacked 8x8 tiles, ink = color 1.
    // 4bpp tiles store two pixels per byte (low nibble = left pixel).
    // NOTE: VRAM is 16-bit -- all tile writes must be halfwords (byte
    // stores are dropped by the bus, see subscreen.cpp/canvas.cpp).
    memset(BG_TILE_RAM(TEXT_TILE_BASE), 0, TEXT_FIRST_TILE * 32); // blank
    u16 *tilewords = (u16 *)((u8 *)BG_TILE_RAM(TEXT_TILE_BASE)
                             + TEXT_FIRST_TILE * 32);
    for (int g = 0; g < 96; g++) {
        const unsigned char *glyph = font8x16[g];
        for (int half = 0; half < 2; half++) {
            u16 *tile = tilewords + (g * 2 + half) * 16; // 32 bytes = 16 u16
            for (int y = 0; y < 8; y++) {
                unsigned char bits = glyph[half * 8 + y];
                u8 bytes[4] = { 0, 0, 0, 0 };
                for (int x = 0; x < 8; x += 2) {
                    u8 lo = (bits & (0x80 >> x)) ? 1 : 0;
                    u8 hi = (bits & (0x40 >> x)) ? 1 : 0;
                    bytes[x / 2] = lo | (hi << 4);
                }
                tile[y * 2 + 0] = bytes[0] | ((u16)bytes[1] << 8);
                tile[y * 2 + 1] = bytes[2] | ((u16)bytes[3] << 8);
            }
        }
    }

    BG_PALETTE[TEXT_PAL_WHITE * 16 + 1] = RGB15(31, 31, 31);
    BG_PALETTE[TEXT_PAL_RED * 16 + 1] = RGB15(31, 0, 0);
    BG_PALETTE[TEXT_PAL_RED * 16 + 2] = RGB15(31, 0, 0);

    // Timer digits reuse the native8x16 ANK tiles above. No squeezed fullwidth
    // glyphs or second digit atlas: alternate font inputs stay supported.
    u16 *base = BG_TILE_RAM(TEXT_TILE_BASE);
    u16 *divider = base + HUD_DIVIDER_TILE * 16;
    for (int i = 0; i < 16; ++i) divider[i] = i >= 14 ? 0x2222 : 0;
    // Normal resource limits fit one ANK digit. Preserve exact values for
    // debug/overflow counts10..12 in the same cell using two narrow digits.
    for (int count = 10; count <= 12; ++count) {
        const int digits[] = {count / 10, count % 10};
        for (int y = 0; y < 16; ++y)
            for (int half = 0; half < 2; ++half) {
                u16 packed = 0;
                const u8 row = font8x16['0' + digits[half] - 0x20][y];
                for (int x = 0; x < 4; ++x)
                    if (row & (0x80 >> (x * 2))) packed |= 1 << (x * 4);
                base[(HUD_COUNT_FIRST + (count - 10) * 2 + y / 8) * 16 + (y % 8) * 2 + half] = packed;
            }
    }
    dmaFillHalfWords(0, BG_MAP_RAM(TEXT_SCREENBLOCK), 32 * 32 * sizeof(u16));
    for (int x = 0; x < 32; ++x)
        BG_MAP_RAM(TEXT_SCREENBLOCK)[2 * 32 + x] = HUD_DIVIDER_TILE | TILE_PALETTE(TEXT_PAL_RED);
    last_hud_timer = ~0u;

    text_layer_clear();
}

void text_layer_clear()
{
    u16 *map = BG_MAP_RAM(TEXT_SCREENBLOCK);
    dmaFillHalfWords(0, map + 3 * 32, 29 * 32 * sizeof(u16));
}

void text_layer_hud_timer(unsigned value)
{
    if (value > 9999) value = 9999;
    if (last_hud_timer == value) return;
    last_hud_timer = value;
    const int bank = value < 200 ? TEXT_PAL_RED : TEXT_PAL_WHITE;
    unsigned divisor = 1000;
    for (int i = 0; i < TEXT_HUD_TIMER_DIGITS; ++i, divisor /= 10) {
        const int tile = TEXT_FIRST_TILE + ('0' + ((value / divisor) % 10) - 0x20) * 2;
        for (int y = 0; y < 2; ++y)
            BG_MAP_RAM(TEXT_SCREENBLOCK)[y * 32 + TEXT_HUD_TIMER_X / 8 + i] =
                (tile + y) | TILE_PALETTE(bank);
    }
}

void text_layer_hud_lives_count(int count)
{
    const int gx = 2;
    const char text[3] = {'x', char('0' + (count >= 10 ? count / 10 : count)),
        count >= 10 ? char('0' + count % 10) : ' '};
    for (int i = 0; i < 3; ++i) {
        const int tile = TEXT_FIRST_TILE + (text[i] - 0x20) * 2;
        for (int y = 0; y < 2; ++y)
            BG_MAP_RAM(TEXT_SCREENBLOCK)[y * 32 + gx + i] = count > UPPER_HUD_MAX_LIVES ?
                (tile + y) | TILE_PALETTE(TEXT_PAL_WHITE) : 0;
    }
}

void text_layer_hud_resources_clear()
{
    for (int y = 0; y < 2; ++y)
        dmaFillHalfWords(0, BG_MAP_RAM(TEXT_SCREENBLOCK) + y * 32, (TEXT_HUD_TIMER_X / 8) * sizeof(u16));
}

void text_layer_hud_resource_number(int gx, int count)
{
    if (count < 0) count = 0;
    if (count > 12) count = 12;
    const int tile = count < 10 ? TEXT_FIRST_TILE + ('0' + count - 0x20) * 2 :
        HUD_COUNT_FIRST + (count - 10) * 2;
    for (int y = 0; y < 2; ++y)
        BG_MAP_RAM(TEXT_SCREENBLOCK)[y * 32 + gx] = (tile + y) | TILE_PALETTE(TEXT_PAL_WHITE);
}

static void put_glyph(int gx, int gy, unsigned char c, int pal_bank)
{
    if (gx < 0 || gx >= TEXT_MAP_W || gy < 2 || gy >= 12)
        return;
    if (c < 0x20 || c > 0x7F)
        c = ' ';
    int tile = TEXT_FIRST_TILE + (c - 0x20) * 2;
    u16 *map = BG_MAP_RAM(TEXT_SCREENBLOCK);
    u16 entry = TILE_PALETTE(pal_bank);
    map[(gy * 2) * TEXT_MAP_W + gx] = tile | entry;
    map[(gy * 2 + 1) * TEXT_MAP_W + gx] = (tile + 1) | entry;
}

void text_layer_puts(int gx, int gy, const char *s, int pal_bank)
{
    for (; *s; s++, gx++)
        put_glyph(gx, gy, (unsigned char)*s, pal_bank);
}

void text_layer_number(int gx, int gy, int value, int digits, int pal_bank)
{
    char buf[12];
    if (digits > 11)
        digits = 11;
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = '0' + (value % 10);
        value /= 10;
    }
    buf[digits] = '\0';
    text_layer_puts(gx, gy, buf, pal_bank);
}
