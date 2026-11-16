#include "mima_render.hpp"
#include "main/boss/mima_intro.hpp"
#include "background.hpp"
#include "stage_entrance.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "player_render.hpp"
#include "orb_render.hpp"
#include "upper_hud.hpp"
#include "hud_render.hpp"
#include "subscreen.hpp"
#include "text_layer.hpp"
#include "music.hpp"
#include "sound_effects.hpp"

bool mima_art_review(int lives,int bombs,unsigned timer)
{
    if(g_canvas_target) return false;
    MimaView view;
    if(!view.begin()) return false;
    struct Cleanup {
        MimaView &view;
        ~Cleanup() {
            music_stop();sfx_stop();
            view.end(); // hide BOTH body OBJs before publishing entrance OAM
            stage_entrance_prepare();text_layer_clear();
            bgShow(g_bg_id_tiles);hud_invalidate();upper_hud_invalidate();
        }
    } cleanup{view};
    if(!load_mima_background()) return false;
    music_stop();sfx_stop();
    if(!music_play_track(NATIVE_LEGEND)) return false;
    camera.snap_center();
    stage_entrance_prepare();canvas_clear();text_layer_clear();
    bgHide(g_bg_id_tiles);
    upper_hud_invalidate();hud_invalidate();
    upper_hud_render(lives,bombs,timer,UpperHudMode::Boss);upper_hud_boss(12,12,2,6);
    hud_render(lives,10);
    subscreen_fill_rect(8,140,248,48,SUB_COL_BG);
    subscreen_text(8,140,"MIMA ART REVIEW",SUB_COL_DIM);
    subscreen_text(8,156,"A:POSE B:INTRO Y:AUTO START:EXIT",SUB_COL_DIM);
    subscreen_text(8,172,"D-PAD:PAN L/R:MOVE BOSS",SUB_COL_DIM);
    MimaEntrance entrance;
    MimaEntranceFrame frame{};
    bool entering=true,automatic=true;
    int pose=0,clock=0,left=MIMA_BASE_LEFT;
    while(true) {
        // Commit OBJ/palette/affine writes in VBlank. No gameplay updates/RNG.
        do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
        scanKeys();const unsigned down=keysDown(),held=keysHeld();
        if(down&KEY_START) break;
        if(down&KEY_B) { entrance=MimaEntrance{};entering=true;left=MIMA_BASE_LEFT; }
        if(!entering) {
            if(down&KEY_A) { pose=(pose+1)%6;automatic=false; }
            if(down&KEY_Y) { automatic=!automatic;clock=0; }
            if(held&KEY_LEFT) camera.left=view_clamp(camera.left-1,0,VIEW_MAX_LEFT);
            if(held&KEY_RIGHT) camera.left=view_clamp(camera.left+1,0,VIEW_MAX_LEFT);
            if(held&KEY_L) left=view_clamp(left-8,0,512);
            if(held&KEY_R) left=view_clamp(left+8,0,512);
        }
        int image;
        if(entering) {
            entrance.next(frame);image=frame.asset_frame;
            if(frame.complete) { entering=false;clock=0;pose=0; }
        } else image=automatic ? MIMA_METEOR_FRAME+((clock++/8)%4) : MIMA_STILL_FRAME+pose;
        if(!view.frame(image,left)) return false;
        player_render_frame();orb_render_frame();orb_indicator_render();
        hud_radar_render({left+64,MIMA_BASE_TOP+80,true});
        video_apply_camera();oamUpdate(&oamMain);video_stage_reveal(true);
    }
    return true;
}
