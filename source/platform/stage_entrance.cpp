#include <nds.h>
#include "stage_entrance.hpp"
#include "stage_render.hpp"
#include "player_render.hpp"
#include "hud_render.hpp"
#include "canvas.hpp"

void stage_entrance_prepare()
{
    // Includes orb/indicator, shots, items and the boss, but not HUD slots 48+.
    for (int slot = 0; slot <= 40; ++slot) oamSetHidden(&oamMain, slot, true);
    oamUpdate(&oamMain);
    stage_render_clear();
    canvas_clear();
}

void stage_entrance_animate(int stage_num, bool show_title)
{
    // A boss may have prepared its frame/palette since prepare(). Keep that
    // shadow OAM hidden too, without resetting the boss's image/palette cache.
    for (int slot = 0; slot <= 40; ++slot) oamSetHidden(&oamMain, slot, true);
    if (show_title) {
        // Source first_stage_in_scene path: static cards, no player or orb.
        stage_render_frame();
    } else {
        // Later stages keep Reimu visible, but not the orb. Only presentation
        // advances: no collisions, card hits, turret clocks or timer ticks.
        player_render_frame();
        oamUpdate(&oamMain);
        for (int frame = 0; stage_render_entrance_frame(frame); ++frame)
            swiWaitForVBlank();
    }
    stage_num_animate(stage_num, show_title);
}
