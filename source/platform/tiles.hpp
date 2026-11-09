#pragma once

#include <stdint.h>

// Stage-object layer (BG1): original 20x10 grid, halved to 16x16 cells on
// a scrolling 512x256 tile map. Row 0 starts at screen y=24.
//
// The atlas is generated from STG.PTN images 7..33 by tools/ptn_to_h.py
// (assets/processed/sprites/Stgobj.h), halved to 16x16 and packed as
// 4bpp tiles in TL,TR,BL,BR order. It is loaded at tile index 1; tile
// index 0 stays blank so that a zeroed map entry renders transparent.

typedef enum {
    STGOBJ_CARD_UNUSED = 0, // STG.PTN image 7
    STGOBJ_CARD_3HP,
    STGOBJ_CARD_3HP_HALF,
    STGOBJ_CARD_3HP_EDGE,
    STGOBJ_CARD_2HP,
    STGOBJ_CARD_2HP_HALF,
    STGOBJ_CARD_2HP_EDGE,
    STGOBJ_CARD_1HP,
    STGOBJ_CARD_1HP_HALF,
    STGOBJ_CARD_1HP_EDGE,
    STGOBJ_CARD_0HP,
    STGOBJ_CARD_0HP_HALF,
    STGOBJ_CARD_0HP_EDGE,
    STGOBJ_CARD_REMOVED_HALF,
    STGOBJ_CARD_REMOVED,
    STGOBJ_BUMPER,
    STGOBJ_TURRET,
    STGOBJ_TURRET_FIRING,
    STGOBJ_BAR_TOP,
    STGOBJ_BAR_LEFT,
    STGOBJ_BAR_BOTTOM,
    STGOBJ_BAR_RIGHT,
    STGOBJ_PORTAL,
    STGOBJ_PORTAL_ANIM,
    STGOBJ_PORTAL_ANIM2,
    STGOBJ_ITEM_BOMB,
    STGOBJ_ITEM_POINT,
    STGOBJ_COUNT
} StgObj;

static const int TILES_COLS = 20;
static const int TILES_ROWS = 10;

void tiles_init();
void tiles_clear_all();
void tiles_put(int cx, int cy, StgObj obj); // cell coordinates
void tiles_erase(int cx, int cy);
