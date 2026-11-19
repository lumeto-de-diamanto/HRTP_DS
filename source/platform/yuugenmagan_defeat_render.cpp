#include "yuugenmagan_combat_render.hpp"
#include "main/boss/yuugenmagan_defeat.hpp"
#include "main/player/orb.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "music.hpp"
#include "player_render.hpp"
#include "orb_render.hpp"
#include "shot_render.hpp"
#include "pellet_render.hpp"
#include <initializer_list>

bool YuugenMaganCombatView::defeat(const YuugenMaganPhase1State &s,const MissilePool &missiles)
{
    if(!active_ || defeating_ || defeated_ || g_canvas_target!=effects_ || !s.checkpoint || s.health.hp!=0 ||
        (s.defeat_phase!=11 && s.defeat_phase!=13) ||
        (s.defeat_phase==11 ? s.phase!=11 : s.phase!=13 && s.phase!=10) || camera.left<0 || camera.left>VIEW_MAX_LEFT) return false;
    music_fade_out_nonblock();video_bomb_reset();
    const int orb_top=orb_get_state().y_position;
    const int center=orb_top< -16 ? 0 : orb_top>383 ? 399 : orb_top+16;
    YuugenMaganDefeat timeline(center);YuugenMaganDefeatFrame f;
    canvas_begin_frame();canvas_playfield_clip(true);
    if(!compose(s,&missiles)) return false;
    player_invincibility_render_frame();player_render_frame();shots_render_frame();orb_render_frame();pellet_render_frame();orb_indicator_render();
    canvas_playfield_clip(false);canvas_buffer_prepare();pending_=false;defeating_=true;
    struct Cleanup {
        bool &busy;bool foreground=false;
        ~Cleanup() {busy=false;if(foreground) video_canvas_foreground_end();}
    } cleanup{defeating_};
    bool arena=false;
    while(timeline.next(f)) {
        int rows[2],count=0;
        if(f.phase==YMD_LINES && (f.line_steps&1)) {
            // Preserve the approved alternating-row appearance at50% scale.
            // Ink is retained in RAM; no gameplay or cloud/missile clock ticks.
            const int step=f.line_steps-1;
            for(int y:{center-step*2,center+step*2}) if(y>=0 && y<400) {
                const int row=Camera::y(y);
                if(row<0 || row>=192 || (count && rows[0]==row)) continue;
                canvas_hline(0,row,256,RGB15(31,31,31));rows[count++]=row;
            }
        }
        const bool first_arena=f.phase==YMD_ARENA && !arena;
        do {swiWaitForVBlank();} while(REG_VCOUNT<192 || REG_VCOUNT>=200);
        if(!cleanup.foreground) {video_canvas_foreground_begin();cleanup.foreground=true;}
        if(f.phase==YMD_SETTLE) {
            if(!sprite_.frame(visual_,flash_)) return false;
        } else if(first_arena) {
            // Switch scenery palette/default pose only while final-output white
            // hides the handoff. YuugenMagan targets raw BOSS2's initial colors,
            // not the combat recolor, a black arena, or a palette-tinted Reimu.
            REG_MASTER_BRIGHT=(1<<14)|16;
            canvas_clear_rect(0,0,256,192);canvas_buffer_prepare();
            if(!sprite_.frame({})) return false;
            player_render_default();for(int slot=1;slot<48;++slot) oamSetHidden(&oamMain,slot,true);
            arena=true;
        }
        oamSetHidden(&oamMain,OAM_SLOT_ORB_ARROW,true);oamSetHidden(&oamMain,OAM_SLOT_BOSS_ARROW,true);
        video_apply_death_shake(0,true);oamUpdate(&oamMain);video_stage_reveal(true);
        REG_MASTER_BRIGHT=f.brightness ? (1<<14)|f.brightness : 0;
        if(f.phase==YMD_SETTLE || first_arena) {
            if(!canvas_buffer_present(true)) return false;
        } else if(count) {
            const int first=rows[0]<rows[count-1] ? rows[0] : rows[count-1];
            const int last=rows[0]>rows[count-1] ? rows[0] : rows[count-1];
            DC_FlushRange(canvas_at(0,first),(last-first+1)*256*sizeof(u16));
            for(int i=0;i<count;++i) dmaCopyHalfWords(3,canvas_at(0,rows[i]),BG_BMP_RAM(16)+rows[i]*256,512);
        }
    }
    defeated_=true;return true;
}
