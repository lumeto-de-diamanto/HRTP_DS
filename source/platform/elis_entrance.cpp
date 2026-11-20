#include "elis_render.hpp"
#include "canvas.hpp"
#include "video.hpp"
#include "music.hpp"
#include "player_render.hpp"
#include "orb_render.hpp"
#include "hud_render.hpp"

bool elis_entrance_play(ElisView &view)
{
    ElisEntrance timeline;unsigned tick=0;
    CanvasForegroundScope foreground; // native spheres/wave cross the upper HUD
    while(true) {
        music_playback_tick(tick); // keep the last snapshot if ARM7 raced
        const auto &f=timeline.at(tick);
        if(!view.compose(f)) return false;
        player_render_default();orb_render_frame();orb_indicator_render(true);hud_radar_render();
        do {swiWaitForVBlank();} while(!view.present());
        if(f.complete) return true;
    }
}
