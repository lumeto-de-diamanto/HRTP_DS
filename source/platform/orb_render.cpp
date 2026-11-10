#include "orb_render.hpp"
#include "main/player/orb.hpp"
#include "video.hpp"
#include "camera.hpp"
#include "obj_gfx.hpp"
#include <nds.h>

#include "sprites/Orb.h"
#include "sprites/OrbArrow.h"

const int FRAME_H = 16;
const int FRAME_W = 16;

const int FRAME_COUNT = 4;

const int FRAME_BYTES = (FRAME_H * FRAME_W * 4) / 8;

u16 *g_orb_gfx[FRAME_COUNT];
static u16 *arrow_gfx;

bool orb_render_init(void)
{
    if (g_orb_gfx[0]) return true;
    ObjGfxTransaction<FRAME_COUNT + 1> allocation;
    const uint8_t *tiles_bytes = reinterpret_cast<const uint8_t *>(Orb_tiles);

    for (int i = 0; i < FRAME_COUNT; i++)
    {
        if (!allocation.allocate(g_orb_gfx[i], SpriteSize_16x16)) return false;
        memcpy(g_orb_gfx[i], tiles_bytes + (i * FRAME_BYTES),
               FRAME_BYTES);
    }

    if (!allocation.allocate(arrow_gfx, SpriteSize_8x16)) return false;
    static_assert(sizeof(OrbArrow_tiles) == 64);
    memcpy(arrow_gfx, OrbArrow_tiles, sizeof(OrbArrow_tiles));
    oamSetHidden(&oamMain, OAM_SLOT_ORB_ARROW, true);
    allocation.commit();
    return true;
}

void orb_render_frame() {
    const OrbState &state = orb_get_state();
    const int x = camera.x(state.x_position), y = camera.y(state.y_position);
    if (!view_visible(x, y, FRAME_W, FRAME_H)) {
        orb_render_hide();
        return;
    }
    oamSet(&oamMain, 10,
               x, y,
               OAM_PRIORITY_GAMEPLAY, 0,
               SpriteSize_16x16, SpriteColorFormat_16Color,
               g_orb_gfx[state.sprite_number],
               -1, false,
               orb_get_in_portal(), // hidden while warping through a portal
               false, false, false);
}

void orb_render_hide()
{
    oamSetHidden(&oamMain, 10, true);
    oamSetHidden(&oamMain, OAM_SLOT_ORB_ARROW, true);
}

void orb_indicator_render()
{
    const auto &s = orb_get_state();
    const auto marker = orb_indicator(camera, s.x_position, s.y_position, ORB_W, ORB_H);
    if (!marker.direction || orb_get_in_portal() || !arrow_gfx) {
        oamSetHidden(&oamMain, OAM_SLOT_ORB_ARROW, true);
        return;
    }
    // Reuse the fixed arrow graphic as a single flipped/moved OBJ.
    const bool left = marker.direction < 0;
    oamSet(&oamMain, OAM_SLOT_ORB_ARROW, marker.x - (left ? 1 : 6), marker.y - 7,
        0, OBJ_PAL_OVERLAY, SpriteSize_8x16, SpriteColorFormat_16Color,
        arrow_gfx, -1, false, false, left, false, false);
}
