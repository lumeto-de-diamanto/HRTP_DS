#include <string.h>

#include "stage_render.hpp"
#include "tiles.hpp"

#include "main/stage/stageobj.hpp"

// Mirrors ReC98's CARD_ANIM table (th01/main/stage/stageobj.cpp:99): for a
// card with hp (stored minus 1), the five flip-animation cels: current
// face, front half, edge, back half, next face (or the removed state).
static const StgObj CARD_ANIM[CARD_HP_MAX][CARD_CELS] = {
    { STGOBJ_CARD_0HP, STGOBJ_CARD_0HP_HALF, STGOBJ_CARD_0HP_EDGE,
      STGOBJ_CARD_REMOVED_HALF, STGOBJ_CARD_REMOVED },
    { STGOBJ_CARD_1HP, STGOBJ_CARD_1HP_HALF, STGOBJ_CARD_1HP_EDGE,
      STGOBJ_CARD_0HP_HALF, STGOBJ_CARD_0HP },
    { STGOBJ_CARD_2HP, STGOBJ_CARD_2HP_HALF, STGOBJ_CARD_2HP_EDGE,
      STGOBJ_CARD_1HP_HALF, STGOBJ_CARD_1HP },
    { STGOBJ_CARD_3HP, STGOBJ_CARD_3HP_HALF, STGOBJ_CARD_3HP_EDGE,
      STGOBJ_CARD_2HP_HALF, STGOBJ_CARD_2HP },
    { STGOBJ_CARD_3HP, STGOBJ_CARD_3HP_HALF, STGOBJ_CARD_3HP_EDGE,
      STGOBJ_CARD_3HP_HALF, STGOBJ_CARD_3HP },
};

static int16_t rendered[TILES_ROWS][TILES_COLS];

void stage_render_clear()
{
    memset(rendered, 0xFF, sizeof(rendered));
    tiles_clear_all();
}

static void render_cell(int cx, int cy, StgObj obj)
{
    if (rendered[cy][cx] == (int16_t)obj)
        return;
    rendered[cy][cx] = obj;
    tiles_put(cx, cy, obj);
}

void stage_render_frame()
{
    for (int i = 0; i < stage_card_count(); i++) {
        const StageCard &c = stage_card(i);
        StgObj obj;
        if (c.flag == CARD_REMOVED) {
            obj = STGOBJ_CARD_REMOVED;
        } else if (c.flag == CARD_FLIPPING) {
            int cel = c.flip_frame / CARD_FRAMES_PER_CEL;
            if (cel > CARD_CEL_FLIPPED)
                cel = CARD_CEL_FLIPPED;
            obj = CARD_ANIM[c.hp][cel];
        } else {
            obj = CARD_ANIM[c.hp][CARD_CEL_NOT_FLIPPING];
        }
        render_cell(stage_cell_x(c.left), stage_cell_y(c.top), obj);
    }

    for (int i = 0; i < stage_obstacle_count(); i++) {
        const StageObstacle &o = stage_obstacle(i);
        StgObj obj;
        switch (o.type) {
        case OT_BUMPER:      obj = STGOBJ_BUMPER; break;
        case OT_PORTAL: {
            int anim = stage_portal_anim(i);
            obj = (anim == 0) ? STGOBJ_PORTAL_ANIM
                : (anim == 1) ? STGOBJ_PORTAL_ANIM2
                : STGOBJ_PORTAL;
            break;
        }
        case OT_BAR_TOP:     obj = STGOBJ_BAR_TOP; break;
        case OT_BAR_BOTTOM:  obj = STGOBJ_BAR_BOTTOM; break;
        case OT_BAR_LEFT:    obj = STGOBJ_BAR_LEFT; break;
        case OT_BAR_RIGHT:   obj = STGOBJ_BAR_RIGHT; break;
        default:
            if (o.type >= OT_TURRET_FIRST && o.type <= OT_TURRET_LAST)
                obj = stage_turret_firing(i) ? STGOBJ_TURRET_FIRING
                                             : STGOBJ_TURRET;
            else
                continue;
            break;
        }
        render_cell(stage_cell_x(o.left), stage_cell_y(o.top), obj);
    }
}
