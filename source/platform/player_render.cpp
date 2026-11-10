#include "player_render.hpp"
#include "main/player/player.hpp"
#include "video.hpp"
#include "camera.hpp"
#include "obj_gfx.hpp"
#include <nds.h>

#include "sprites/Miko.h"
#include "sprites/Miko32.h"
#include "sprites/Miko48.h"

const int REGULAR_FRAME_SIZE = 16;
const int REGULAR_FRAME_COUNT = 28;

const int REGULAR_FRAME_BYTES = (REGULAR_FRAME_SIZE * REGULAR_FRAME_SIZE * 4) / 8;

const int MIKO_32_FRAME_COUNT = 6;
const int MIKO_32_FRAME_BYTES = (16 * 32 * 4) / 8;

const int MIKO_48_FRAME_COUNT = 28;
const int MIKO_48_FRAME_BYTES = (32 * 32 * 4) / 8;

u16 *g_regular_gfx[REGULAR_FRAME_COUNT];
u16 *g_32_gfx[MIKO_32_FRAME_COUNT];
u16 *g_48_gfx[MIKO_48_FRAME_COUNT];

bool player_render_init() {
    if (g_regular_gfx[0]) return true;
    ObjGfxTransaction<REGULAR_FRAME_COUNT + MIKO_32_FRAME_COUNT + MIKO_48_FRAME_COUNT> allocation;
    // Load Reimu's sprites into memory
    
    const uint8_t *regular_bytes = reinterpret_cast<const uint8_t *>(Miko_tiles);
    for (int i = 0; i < REGULAR_FRAME_COUNT; i++)
    {
        if (!allocation.allocate(g_regular_gfx[i], SpriteSize_16x16)) return false;
        memcpy(g_regular_gfx[i], regular_bytes + (i * REGULAR_FRAME_BYTES),
               REGULAR_FRAME_BYTES);
    }

    const uint8_t *miko_32_bytes = reinterpret_cast<const uint8_t *>(Miko32_tiles);
    for (int i = 0; i < MIKO_32_FRAME_COUNT; i++)
    {
        if (!allocation.allocate(g_32_gfx[i], SpriteSize_32x16)) return false;
        memcpy(g_32_gfx[i], miko_32_bytes + (i * MIKO_32_FRAME_BYTES),
               MIKO_32_FRAME_BYTES);
    }

    const uint8_t *miko_48_bytes = reinterpret_cast<const uint8_t *>(Miko48_tiles);
    for (int i = 0; i < MIKO_48_FRAME_COUNT; i++)
    {
        if (!allocation.allocate(g_48_gfx[i], SpriteSize_32x32)) return false;
        memcpy(g_48_gfx[i], miko_48_bytes + (i * MIKO_48_FRAME_BYTES),
               MIKO_48_FRAME_BYTES);
    }

    memcpy(SPRITE_PALETTE, Miko_palette, sizeof(Miko_palette));
    allocation.commit();
    return true;
}

void player_render_frame() {
    const PlayerState &state = player_get_state();

    int oam_x, oam_y;
    SpriteSize size;
    u16 *gfx;

    // Defaults for any unexpected spritesheet value.
    oam_x = camera.x(state.x_position);
    oam_y = camera.y(state.y_position);
    size = SpriteSize_16x16;
    gfx = g_regular_gfx[state.sprite_number];

    switch (state.spritesheet) {
    case PLAYER:
        oam_x = camera.x(state.x_position);
        oam_y = camera.y(state.y_position);
        size = SpriteSize_16x16;
        gfx = g_regular_gfx[state.sprite_number];
    break;
    case PLAYER_32:
        oam_x = camera.x(state.x_position) - 8;
        oam_y = camera.y(state.y_position);
        size = SpriteSize_32x16;
        gfx = g_32_gfx[state.sprite_number];
    break;
    case PLAYER_48:
        oam_x = camera.x(state.x_position) - 8;
        oam_y = camera.y(state.y_position) - 16;
        size = SpriteSize_32x32;
        gfx = g_48_gfx[state.sprite_number];
    break;
    }

    oamSet(&oamMain, 0,
           oam_x, oam_y,
           OAM_PRIORITY_GAMEPLAY,
           0,
           size, SpriteColorFormat_16Color,
           gfx,
           -1,
           false,
           false,
           false, false,
           false);
}
