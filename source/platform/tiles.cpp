#include <nds.h>
#include <string.h>

#include "tiles.hpp"
#include "palette.hpp"
#include "video.hpp"

#include "sprites/Stgobj.h"

// tileBase 0, mapBase 8 (2 KB units) -- see video.hpp.
#define TILES_CHARBLOCK 0
#define TILES_SCREENBLOCK 8

// The atlas starts at tile index 1; tile index 0 is left blank so that
// zeroed map entries render transparent.
#define ATLAS_FIRST_TILE 1
#define ATLAS_TILE_BYTES 32 // 8x8 at 4bpp

void tiles_init()
{
    // The atlas color indices are written for palette bank 15; the bank's
    // contents come from the current round palette via
    // palette_apply_round() (the original draws PTN art through the
    // hardware palette of the moment).
    u16 *atlas = (u16 *)((u8 *)BG_TILE_RAM(TILES_CHARBLOCK) + ATLAS_TILE_BYTES);
    memcpy(atlas, Stgobj_tiles, sizeof(Stgobj_tiles));

    video_apply_camera();

    tiles_clear_all();
}

void tiles_clear_all()
{
    dmaFillHalfWords(0, BG_MAP_RAM(TILES_SCREENBLOCK), 2 * 32 * 32 * sizeof(u16));
}

static inline void cell_entry(int cx, int cy, int quadrant, int tile)
{
    // 2x2 tiles per cell; map entry: bits 0-9 tile, 10-12 palette bank.
    int map_x = (cx * 2) + (quadrant & 1);
    int map_y = (cy * 2) + (quadrant >> 1) + 3; // 24px upper HUD
    // NDS text BG map entry: bits 0-9 tile, 10-11 flips, 12-15 palette.
    // 512px text maps consist of two side-by-side 32x32 screenblocks,
    // not a flat 64-entry scanline. Columns 16..19 live in block 1.
    BG_MAP_RAM(TILES_SCREENBLOCK)[(map_x / 32) * 1024 + map_y * 32 + map_x % 32] =
        tile | TILE_PALETTE(BG_PAL_STGOBJ_BANK);
}

void tiles_put(int cx, int cy, StgObj obj)
{
    if (cx < 0 || cx >= TILES_COLS || cy < 0 || cy >= TILES_ROWS)
        return;
    int first = ATLAS_FIRST_TILE + ((int)obj * 4);
    cell_entry(cx, cy, 0, first + 0);
    cell_entry(cx, cy, 1, first + 1);
    cell_entry(cx, cy, 2, first + 2);
    cell_entry(cx, cy, 3, first + 3);
}

void tiles_erase(int cx, int cy)
{
    if (cx < 0 || cx >= TILES_COLS || cy < 0 || cy >= TILES_ROWS)
        return;
    for (int q = 0; q < 4; q++)
        cell_entry(cx, cy, q, 0);
}
