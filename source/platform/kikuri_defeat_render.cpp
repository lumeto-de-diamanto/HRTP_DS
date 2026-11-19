#include "kikuri_render.hpp"
#include "main/boss/kikuri_defeat.hpp"
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

bool KikuriView::defeat(const KikuriPhase1State &s)
{
    if(!active_ || defeated_ || s.phase!=6 || !s.checkpoint || s.health.hp!=0 || camera.left<0 || camera.left>64) return false;
    music_fade_out_nonblock();video_bomb_reset();
    const int orb_top=orb_get_state().y_position;
    const int center=orb_top<-16 ? 0 : orb_top>383 ? 399 : orb_top+16;
    KikuriDefeat timeline(center);KikuriDefeatFrame f;
    KikuriPhase1State visual=s;for(auto &laser:visual.lasers) laser={};
    prepare_combat(visual);canvas_begin_frame();canvas_playfield_clip(true);
    draw_combat(visual);player_invincibility_render_frame();player_render_frame();
    shots_render_frame();orb_render_frame();pellet_render_frame();
    canvas_playfield_clip(false);canvas_buffer_prepare();
    struct Foreground {
        bool entered=false;
        ~Foreground() {if(entered) video_canvas_foreground_end();}
    } foreground;
    bool black=false;
    while(timeline.next(f)) {
        int rows[2],count=0;
        if(f.phase==KD_LINES && (f.line_steps&1)) {
            // Keep the approved alternating-row appearance after downscaling.
            // Earlier white lines and the frozen combat image remain in RAM.
            const int step=f.line_steps-1;
            for(int y:{center-step*2,center+step*2}) if(y>=0 && y<400) {
                const int row=Camera::y(y);
                if(row<0 || row>=192 || (count && rows[0]==row)) continue;
                canvas_hline(0,row,256,RGB15(31,31,31));rows[count++]=row;
            }
        }
        const bool first_black=f.phase==KD_BLACK && !black;
        if(first_black) {
            canvas_fill_rect(0,0,256,192,0);canvas_buffer_prepare();
            player_render_default();for(int slot=1;slot<48;++slot) oamSetHidden(&oamMain,slot,true);
            black=true;
        }
        do {swiWaitForVBlank();} while(REG_VCOUNT<192 || REG_VCOUNT>=200);
        if(!foreground.entered) {video_canvas_foreground_begin();foreground.entered=true;}
        if(f.phase==KD_SETTLE) {
            publish_palette();
            // Canvas flush must be consumed before an optional BG asset flush.
            if(!canvas_buffer_present(true)) return false;
            install_background();
            present_souls(); // reset prior shake and freeze hardware souls beneath the white rows
        } else if(first_black) {
            if(!canvas_buffer_present(true)) return false;
        } else if(count) {
            const int first=rows[0]<rows[count-1] ? rows[0] : rows[count-1];
            const int last=rows[0]>rows[count-1] ? rows[0] : rows[count-1];
            DC_FlushRange(canvas_at(0,first),(last-first+1)*256*sizeof(u16));
            for(int i=0;i<count;++i) dmaCopyHalfWords(3,canvas_at(0,rows[i]),BG_BMP_RAM(16)+rows[i]*256,512);
        }
        oamSetHidden(&oamMain,OAM_SLOT_ORB_ARROW,true);oamSetHidden(&oamMain,OAM_SLOT_BOSS_ARROW,true);
        video_apply_death_shake(0,true);oamUpdate(&oamMain);video_stage_reveal(true);
        // No OBJ/BG palette ramps: native brightness owns the complete top
        // engine only. Hold final native black across foreground restoration.
        REG_MASTER_BRIGHT=f.phase==KD_BLACK && f.fade_steps>=15 ? (2<<14)|16 :
            f.brightness ? (1<<14)|f.brightness : 0;
    }
    defeated_=true;return true;
}
