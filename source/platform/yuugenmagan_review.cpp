#include "yuugenmagan_render.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "stage_entrance.hpp"
#include "player_render.hpp"
#include "upper_hud.hpp"
#include "hud_render.hpp"
#include "subscreen.hpp"
#include "text_layer.hpp"
#include "video.hpp"
#include "music.hpp"
#include "sound_effects.hpp"

bool yuugenmagan_art_review(int lives,int bombs,unsigned timer)
{
    if(g_canvas_target) return false;
    YuugenMaganView view;
    if(!view.begin()) return false;
    video_stage_fade_out();
    struct Cleanup {
        YuugenMaganView &view;
        ~Cleanup() {
            video_stage_fade_out(); // leave black for the caller's stage reload
            music_stop();sfx_stop();view.end();
            stage_entrance_prepare();text_layer_clear();bgShow(g_bg_id_tiles);
            upper_hud_invalidate();hud_invalidate();
        }
    } cleanup{view};
    music_stop();sfx_stop();
    if(!music_play_track(NATIVE_LEGEND)) return false;
    camera.snap_center();stage_entrance_prepare();text_layer_clear();bgHide(g_bg_id_tiles);
    canvas_playfield_clip(true);
    upper_hud_invalidate();hud_invalidate();
    upper_hud_render(lives,bombs,timer,UpperHudMode::Boss);upper_hud_boss(16,16,8,12);hud_render(lives,10);
    subscreen_fill_rect(8,140,248,48,SUB_COL_BG);
    subscreen_text(8,140,"YUUGENMAGAN ART REVIEW",SUB_COL_DIM);
    subscreen_text(8,156,"A:POSE B:OPENING Y:AUTO",SUB_COL_DIM);
    subscreen_text(8,172,"D-PAD:PAN START:EXIT/RETRY",SUB_COL_DIM);
    YuugenMaganOpening opening;
    if(!view.frame(opening.state())) return false; // one-time upload under black
    bool playing=true,automatic=false;
    int pose=YMG_DOWN,clock=0;
    while(true) {
        do {swiWaitForVBlank();} while(REG_VCOUNT<192 || REG_VCOUNT>=200);
        scanKeys();const unsigned down=keysDown(),held=keysHeld();
        if(down&KEY_START) break;
        if(held&KEY_LEFT) camera.left=view_clamp(camera.left-1,0,VIEW_MAX_LEFT);
        if(held&KEY_RIGHT) camera.left=view_clamp(camera.left+1,0,VIEW_MAX_LEFT);
        if(down&KEY_B) {opening=YuugenMaganOpening{};playing=true;automatic=false;}
        if(down&KEY_A) {playing=automatic=false;pose=(pose+1)%YUUGENMAGAN_CELS;}
        if(down&KEY_Y) {playing=false;automatic=!automatic;clock=0;}
        YuugenMaganOpeningFrame frame=opening.state();
        if(playing) {opening.next();frame=opening.state();}
        else {
            frame.recolor=8;frame.lasers=0;
            for(auto &cel:frame.shown) cel=automatic ? (clock/30)%YUUGENMAGAN_CELS : pose;
            if(automatic) clock=(clock+1)%(30*YUUGENMAGAN_CELS);
        }
        canvas_begin_frame();
        if(!view.frame(frame)) return false;
        player_render_default(); // frozen source position, no controller tick
        hud_radar_render();oamUpdate(&oamMain);video_stage_reveal(true); // art inspection has no active hitboxes
    }
    return true;
}
