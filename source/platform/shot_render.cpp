#include "shot_render.hpp"
#include <nds.h>
#include "main/player/shot.hpp"
#include "video.hpp"
#include "camera.hpp"
#include "obj_gfx.hpp"

#include "sprites/Shot.h"

const int FRAME_W = 8;
const int FRAME_H = 8;
const int FRAME_COUNT = 4;

const int FRAME_SIZE_BYTES = (FRAME_W * FRAME_H * 4) / 8;

const int OAM_ID_BASE = 1;

u16 *g_shot_gfx[FRAME_COUNT];

bool shots_render_init(void)
{
    if (g_shot_gfx[0]) return true;
    ObjGfxTransaction<FRAME_COUNT> allocation;
    const uint8_t *tiles_bytes = reinterpret_cast<const uint8_t *>(Shot_tiles);

    for (int i = 0; i < FRAME_COUNT; i++)
    {
        if (!allocation.allocate(g_shot_gfx[i], SpriteSize_8x8)) return false;
        memcpy(g_shot_gfx[i], tiles_bytes + (i * FRAME_SIZE_BYTES),
               FRAME_SIZE_BYTES);
    }

    // Uses the same palette as Reimu
    // memcpy(SPRITE_PALETTE + 16, Shot_palette, sizeof(Shot_palette));

    for (int i = 0; i < SHOT_COUNT; i++)
    {
        oamSet(&oamMain, OAM_ID_BASE + i,
               0, 0,
               OAM_PRIORITY_GAMEPLAY, 4,
               SpriteSize_8x8, SpriteColorFormat_16Color,
               g_shot_gfx[0],
               -1, false,
               true, // hide
               false, false, false);
    }
    allocation.commit();
    return true;
}

void shots_render_frame() {
    const ShotsState &state = shots_get_state();
    for (int i = 0; i < SHOT_COUNT; i++)
    {
        int oam_id = OAM_ID_BASE + i;

        const int x = camera.x(state.x_position[i]), y = camera.y(state.y_position[i]);
        if (!state.alive[i] || !view_visible(x, y, FRAME_W, FRAME_H))
        {
            oamSet(&oamMain, oam_id,
                   0, 0,
                   OAM_PRIORITY_GAMEPLAY, 0,
                   SpriteSize_8x8, SpriteColorFormat_16Color,
                   g_shot_gfx[0],
                   -1, false,
                   true, // hide
                   false, false, false);
            continue;
        }

        oamSet(&oamMain, oam_id,
               x, y,
               OAM_PRIORITY_GAMEPLAY, 0,
               SpriteSize_8x8, SpriteColorFormat_16Color,
               g_shot_gfx[state.sprite_number[i]],
               -1, false,
               false, // not hidden
               false, false, false);
    }
}
