#include "mima_combat_render.hpp"
#include "main/boss/mima_intro.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/bullet/pellet.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/timer.hpp"
#include "main/stage/item.hpp"
#include "main/random.hpp"
#include "stage_entrance.hpp"
#include "player_render.hpp"
#include "orb_render.hpp"
#include "shot_render.hpp"
#include "pellet_render.hpp"
#include "bomb_render.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "upper_hud.hpp"
#include "hud_render.hpp"
#include "subscreen.hpp"
#include "text_layer.hpp"
#include "menu_render.hpp"
#include "music.hpp"
#include "sound_effects.hpp"
#include "backgrounds/MimaPaletteFx.h"

bool mima_death_animate(MimaCombatView &view,const MimaPhase1 &boss,bool fatal)
{
    video_bomb_reset(); // keep the approved miss free of bomb flash/recovery
    const auto &player=player_get_state();
    PlayerDeathAnimation animation(fatal,player.x_position,player.y_position,(irand()%8)==0);
    PlayerDeathFrame presentation;
    bool decayed=false;
    if(fatal) Items.init();
    const auto &s=boss.state();
    while(animation.next(presentation)) {
        if(presentation.returning && !decayed) {
            shots_initialize();Pellets.decay();decayed=true;
        }
        if(presentation.returning)
            camera.follow((presentation.left[0]+presentation.left[1]+PLAYER_W)/2);
        canvas_begin_frame();canvas_playfield_clip(true);
        if(!view.compose(s,boss.missiles())) { canvas_playfield_clip(false);return false; }
        shots_render_frame();orb_render_frame();
        pellet_render_colored(Mima_palette_fx[s.darkness/4][s.flash ? 1+s.flash_darkness/4 : 0][7]);
        orb_indicator_render();player_render_death(presentation);
        canvas_playfield_clip(false);canvas_buffer_prepare();
        // This buffered renderer commits at the END of a frame. Adding the
        // direct-renderer's extra wait would slow every shake/effect step.
        do { swiWaitForVBlank(); } while(!view.present(presentation.shake_y));
        for(int hold=1;hold<presentation.hold;++hold) swiWaitForVBlank();
    }
    return true;
}

bool mima_entrance_play(MimaCombatView &view)
{
    MissilePool empty;
    MimaEntrance timeline;MimaEntranceFrame entrance{};MimaPhase1State still{};
    while(timeline.next(entrance)) {
        canvas_begin_frame();still.image=entrance.asset_frame;
        if(!view.compose(still,empty)) return false;
        player_render_frame();orb_render_frame();
        canvas_buffer_prepare();do { swiWaitForVBlank(); } while(!view.present());
    }
    return true;
}

bool mima_phase1_review()
{
    MimaCombatView view;
    if(!view.begin()) return false;
    struct Cleanup {
        MimaCombatView &view;
        int score=stage_score,high=hiscore,bombs=rem_bombs,speed=pellet_speed;
        unsigned timer=stage_timer;
        ~Cleanup() {
            player_bomb_cancel();view.end();music_stop();sfx_stop();
            Pellets.init();shots_initialize();stage_entrance_prepare();text_layer_clear();
            bgShow(g_bg_id_tiles);hud_invalidate();upper_hud_invalidate();
            stage_score=score;hiscore=high;rem_bombs=bombs;pellet_speed=speed;stage_timer=timer;
        }
    } cleanup{view};
    sfx_stop();music_stop();if(!music_play_track(NATIVE_LEGEND)) return false;
    stage_entrance_prepare();text_layer_clear();bgHide(g_bg_id_tiles);camera.snap_center();
    static const uint8_t empty_stage[STAGE_ROWS][STAGE_COLS]={};
    stage_init(empty_stage,empty_stage);stage_reset();Items.init();
    player_initialize();orb_initialize();shots_initialize();Pellets.init();timer_init_for(2,4);
    stage_score=0;rem_bombs=2;int lives=3;unsigned frame=0;
    upper_hud_invalidate();hud_invalidate();
    upper_hud_render(lives,rem_bombs,stage_timer,UpperHudMode::Boss);upper_hud_boss(12,0,2,6);
    hud_render(lives,10);subscreen_fill_rect(8,140,248,48,SUB_COL_BG);
    subscreen_text(8,140,"MIMA FIRST BLOCK",SUB_COL_DIM);
    subscreen_text(8,156,"START:PAUSE SELECT:EXIT",SUB_COL_DIM);
    subscreen_text(8,172,"MOVE/SHOOT/STRIKE/BOMB",SUB_COL_DIM);
    if(!mima_entrance_play(view)) return false;
    MimaPhase1 boss;boss.reset(true);bool finished=false,can_exit=false;
    int shown_phase=1;
    while(true) {
        scanKeys();const unsigned held=keysHeld(),down=keysDown();
        if(!(held&KEY_SELECT)) can_exit=true;
        if(can_exit && (down&KEY_SELECT)) break;
        if(finished) {
            if(down&(KEY_A|KEY_B|KEY_START)) break;
            swiWaitForVBlank();continue;
        }
        if(down&KEY_START) {
            music_pause();const bool quit=pause_menu_run();
            if(quit) break;
            music_resume();continue;
        }
        player_update(held,down);
        boss.update(boss_bomb_damage_allowed(player_bomb().damaging(),lives,frame+1));
        if(boss.state().checkpoint) {
            // Lethal boss hits win over a pending player hit. The original
            // blocks here, before lasers/orb/pellets/shots/timer can tick again.
            player_is_hit=false;boss.clear_defeat_projectiles();Pellets.reset_nonclouds();
            if(stage_score>hiscore) hiscore=stage_score;
            upper_hud_render(lives,rem_bombs,stage_timer,UpperHudMode::Boss);
            upper_hud_boss(0,0,2,6);hud_render(lives,10);hud_radar_render();
            subscreen_fill_rect(8,140,248,48,SUB_COL_BG);
            subscreen_text(8,140,"MIMA DEFEAT",SUB_COL_DIM);
            if(!view.defeat(boss.state())) return false;
            player_bomb_cancel();finished=true;
            subscreen_text(8,140,"DEFEAT REVIEW COMPLETE",SUB_COL_DIM);
            subscreen_text(8,156,"A/B/START:RETURN",SUB_COL_DIM);
            subscreen_text(8,172,"PROGRESSION NOT IMPLEMENTED",SUB_COL_DIM);
            continue;
        }
        boss.update_lasers();
        shots_update();orb_update();if(Pellets.update()) player_is_hit=true;
        if(!player_is_hit && orb_hits_player(player_get_state().x_position,player_top,OR_NONE,
            player_is_invincible(),player_is_invincible_to_orb())) player_is_hit=true;
        if(player_is_hit) {
            player_is_hit=false;
            const bool fatal=lives<=0;
            if(!fatal) { --lives;mdrv2_se_play(SFX_MISS); }
            if(!mima_death_animate(view,boss,fatal)) return false;
            if(fatal) {
                // Keep the final burst on screen. No respawn, recovery grant,
                // ordinary avatar redraw or extra gameplay tick on final life.
                finished=true;player_bomb_cancel();video_bomb_reset();
                subscreen_fill_rect(8,140,248,48,SUB_COL_BG);
                subscreen_text(8,140,"PRACTICE OVER",SUB_COL_DIM);
                subscreen_text(8,156,"A/B/START:RETURN",SUB_COL_DIM);
                continue;
            }
            player_miss();
            if(rem_bombs<5) ++rem_bombs;
            pellet_speed=pellet_speed>0 ? 0 : pellet_speed-2;if(pellet_speed<-15) pellet_speed=-15;
        }
        if(timer_update(++frame)==TIMER_EVENT_HARRYUP) harryup_animate();
        const auto &s=boss.state();
        if(s.phase!=shown_phase) {
            shown_phase=s.phase;subscreen_fill_rect(8,140,248,16,SUB_COL_BG);
            subscreen_text(8,140,s.phase==3 ? "MIMA SECOND BLOCK" : "SIX-HP INTERLUDE",SUB_COL_DIM);
        }
        video_bomb_prepare(player_bomb().active(),player_bomb().frame(),frame);
        camera.follow(player_center_x());canvas_begin_frame();canvas_playfield_clip(true);
        player_invincibility_render_frame();
        if(!view.compose(s,boss.missiles())) return false;
        player_render_frame();shots_render_frame();orb_render_frame();
        pellet_render_colored(Mima_palette_fx[s.darkness/4][s.flash ? 1+s.flash_darkness/4 : 0][7]);
        orb_indicator_render();bomb_render_frame();canvas_playfield_clip(false);
        upper_hud_render(lives,rem_bombs,stage_timer,UpperHudMode::Boss);upper_hud_boss(s.health.hp,s.hp_displayed,2,6);
        hud_render(lives,10);hud_radar_render(hud_radar_boss(s.entity));
        canvas_buffer_prepare();do { swiWaitForVBlank(); } while(!view.present());
    }
    return true;
}
