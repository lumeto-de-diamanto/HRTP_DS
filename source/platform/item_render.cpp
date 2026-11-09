#include <nds.h>
#include <string.h>

#include "item_render.hpp"
#include "canvas.hpp"
#include "video.hpp"
#include "camera.hpp"

#include "main/stage/item.hpp"

#include "sprites/Item.h"

// Fixed OAM layout: slots 24-27 = bomb items, 28-37 = point items.
#define ITEM_FIRST_SLOT 24
#define ITEM_FRAME_BYTES 128 // 16x16 at 4bpp

static u16 *g_item_gfx[2]; // 0 = bomb, 1 = point

void item_render_init()
{
    const uint8_t *tiles = reinterpret_cast<const uint8_t *>(Item_tiles);
    for (int i = 0; i < 2; i++) {
        g_item_gfx[i] = oamAllocateGfx(&oamMain, SpriteSize_16x16,
                                       SpriteColorFormat_16Color);
        memcpy(g_item_gfx[i], tiles + (i * ITEM_FRAME_BYTES),
               ITEM_FRAME_BYTES);
    }
    // Palette banks are filled by palette_apply_round(): bombs follow the
    // current round, the point orb stays on round 1's palette.
}

static void item_slot_update(int slot, const Item &item, int frame,
                             int pal_bank)
{
    const int x = camera.x(item.left), y = camera.y(item.top);
    if (CItems::is_visible(item) && view_visible(x, y, 16, 16)) {
        oamSet(&oamMain, slot,
               x, y,
               OAM_PRIORITY_GAMEPLAY, pal_bank,
               SpriteSize_16x16, SpriteColorFormat_16Color,
               g_item_gfx[frame],
               -1, false,
               false, false, false, false);
    } else {
        oamSet(&oamMain, slot, 0, 0, OAM_PRIORITY_GAMEPLAY, 0,
               SpriteSize_16x16, SpriteColorFormat_16Color,
               g_item_gfx[frame],
               -1, false,
               true, false, false, false);
    }
}

void item_render_frame()
{
    for (int i = 0; i < ITEM_BOMB_COUNT; i++)
        item_slot_update(ITEM_FIRST_SLOT + i, Items.bomb[i], 0, 1);
    for (int i = 0; i < ITEM_POINT_COUNT; i++)
        item_slot_update(ITEM_FIRST_SLOT + ITEM_BOMB_COUNT + i, Items.point[i],
                         1, 2);
}

// Splash rings (drawn on the canvas above the tiles). ReC98 grows the
// radius 4..48 in steps of 4 during IF_SPLASH; halved for the screen.
void item_rings_frame()
{
    const Item *all[ITEM_BOMB_COUNT + ITEM_POINT_COUNT];
    for (int i = 0; i < ITEM_BOMB_COUNT; i++)
        all[i] = &Items.bomb[i];
    for (int i = 0; i < ITEM_POINT_COUNT; i++)
        all[ITEM_BOMB_COUNT + i] = &Items.point[i];

    for (int i = 0; i < ITEM_BOMB_COUNT + ITEM_POINT_COUNT; i++) {
        const Item &item = *all[i];
        if (item.flag == IF_SPLASH) {
            int radius = item.splash_radius / 2;
            canvas_circle_dots(camera.x(item.left + ITEM_W / 2),
                               camera.y(item.top + ITEM_H / 2),
                               radius, 0x7FFF, false);
        }
    }
}
