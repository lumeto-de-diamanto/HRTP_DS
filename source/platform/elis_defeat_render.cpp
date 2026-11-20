#include "elis_render.hpp"
#include "main/boss/elis_defeat.hpp"
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

bool ElisView::defeat(const ElisPhase1State &s,const MissilePool &missiles)
{
    if(!active_ || defeating_ || defeated_ || g_canvas_target!=effects_ ||
       s.phase!=5 || !s.checkpoint || s.health.hp!=0 || camera.left<0 || camera.left>VIEW_MAX_LEFT) return false;
    music_fade_out_nonblock();video_bomb_reset();
    const int orb_top=orb_get_state().y_position;
    const int center=orb_top< -16 ? 0 : orb_top>383 ? 399 : orb_top+16;
    ElisDefeat timeline(center);ElisDefeatFrame f;
    // b15m.cpp2089 restores the girl background before the burst. A bat may
    // remain until the white-covered page swap; don't turn it into a girl cel.
    auto visual=s;if(!visual.bat_form) {visual.image=-1;visual.attack_overlay=false;}
    canvas_begin_frame();canvas_playfield_clip(true);
    if(!compose(visual,missiles)) {canvas_playfield_clip(false);return false;}
    player_invincibility_render_frame();player_render_frame();shots_render_frame();orb_render_frame();pellet_render_colored(pellet_color());
    canvas_playfield_clip(false);canvas_buffer_prepare();pending_=false;defeating_=true;
    struct Cleanup {
        bool &busy;bool foreground=false;
        ~Cleanup() {busy=false;if(foreground) video_canvas_foreground_end();}
    } cleanup{defeating_};
    bool black=false;
    while(timeline.next(f)) {
        int rows[2],count=0;
        if(f.phase==ED_LINES && (f.line_steps&1)) {
            // Preserve alternating rows at50%, using retained RAM ink and
            // sparse uploads rather than repainting the whole frozen world.
            const int step=f.line_steps-1;
            for(int y:{center-step*2,center+step*2}) if(y>=0 && y<400) {
                const int row=Camera::y(y);
                if(row<0 || row>=192 || (count && rows[0]==row)) continue;
                canvas_hline(0,row,256,RGB15(31,31,31));rows[count++]=row;
            }
        }
        const bool first_black=f.phase==ED_BLACK && !black;
        do {swiWaitForVBlank();} while(REG_VCOUNT<192 || REG_VCOUNT>=200);
        if(!cleanup.foreground) {video_canvas_foreground_begin();cleanup.foreground=true;}
        if(f.phase==ED_SETTLE) {publish_palette();bgShow(g_bg_id_background);}
        else if(first_black) {
            // Native full white hides the source page1/default-player handoff.
            // Elis's post-defeat palette is ALL BLACK, unlike YuugenMagan's.
            REG_MASTER_BRIGHT=(1<<14)|16;
            canvas_fill_rect(0,0,256,192,0);canvas_buffer_prepare();
            player_render_default();for(int slot=1;slot<48;++slot) oamSetHidden(&oamMain,slot,true);
            black=true;
        }
        oamSetHidden(&oamMain,OAM_SLOT_ORB_ARROW,true);oamSetHidden(&oamMain,OAM_SLOT_BOSS_ARROW,true);
        video_apply_death_shake(0,true);oamUpdate(&oamMain);video_stage_reveal(true);
        REG_MASTER_BRIGHT=f.phase==ED_BLACK && f.fade_steps>=15 ? (2<<14)|16 :
            f.brightness ? (1<<14)|f.brightness : 0;
        if(f.phase==ED_SETTLE || first_black) {
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
