#include "kikuri_render.hpp"
#include "main/boss/kikuri_intro.hpp"
#include "music.hpp"
#include "player_render.hpp"
#include "orb_render.hpp"
#include "hud_render.hpp"
#include "video.hpp"

bool kikuri_entrance_play(KikuriView &view)
{
    KikuriEntrance timeline;KikuriEntranceFrame frame{};unsigned tick=0;
    while(true) {
        music_playback_tick(tick); // retain the previous snapshot if ARM7 raced
        if(!timeline.next(frame,tick) || !view.entrance(timeline,frame)) return false;
        do {swiWaitForVBlank();} while(REG_VCOUNT<192 || REG_VCOUNT>=200);
        if(!view.present_entrance()) return false;
        player_render_default();orb_render_frame();orb_indicator_render(true);
        hud_radar_render({320,180,true});oamUpdate(&oamMain);video_stage_reveal(true);
        if(frame.phase==KikuriEntranceFrame::Complete) return true;
    }
}
