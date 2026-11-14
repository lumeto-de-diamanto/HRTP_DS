#pragma once

#include <nds.h>

// Sub-screen output (ARCHITECTURE.md: the HUD lives on the bottom
// screen). BG2 is a sparse256x256 Bmp8 overlay; BG3 repeats a resident128x128
// source-art pattern. Clearing overlay pixels reveals the pattern. The sub
// engine has its own 256-color palette, so no sharing with the main
// screen's budget (palette.hpp).

// Palette indices used by the HUD (sub palette is free-form).
static const u8 SUB_COL_BG = 0;
static const u8 SUB_COL_FG = 1;
static const u8 SUB_COL_DIM = 2;
static const u8 SUB_COL_ACCENT = 3;
static const u8 SUB_COL_RADAR_BG = 6;
static const u8 SUB_COL_RADAR_CARD = 7;
static const u8 SUB_COL_RADAR_OBSTACLE = 8;
static const u8 SUB_COL_RADAR_PORTAL = 9;
static const u8 SUB_COL_RADAR_ITEM = 10;
static const u8 SUB_COL_RADAR_PLAYER = 11;
static const u8 SUB_COL_RADAR_BOSS = 12;

void subscreen_init();
void subscreen_clear(u8 color);
void subscreen_fill_rect(int x, int y, int w, int h, u8 color);

// 8x16 ANK text (PC-98 font, tools/font_atlas.py). Unsupported
// characters render as blank.
void subscreen_text(int x, int y, const char *s, u8 color);

// Zero-padded number, right-aligned in `digits` columns.
void subscreen_number(int x, int y, int value, int digits, u8 color);

// Zero-padded number using authentic 16x16 PC-98 fullwidth numerals.
void subscreen_fullwidth_number(int x, int y, int value, int digits, u8 color);

// Native 16x16 fullwidth uppercase Latin/digits, space and question mark.
void subscreen_fullwidth_text(int x, int y, const char *s, u8 color);

// Indexed sprite blit (row-major pixels, 0 = transparent). The pixel
// indices are offset by `pal_base` into the sub palette.
void subscreen_put_indexed(int x, int y, int w, int h,
                           const u8 *pixels, int pal_base);
