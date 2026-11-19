#include "yuugenmagan_combat_render.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/bullet/pellet.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/item.hpp"
#include "main/stage/timer.hpp"
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

bool yuugenmagan_death_animate(YuugenMaganCombatView &view,const YuugenMaganPhase1 &boss,bool fatal)
{
    video_bomb_reset();
    const auto &player=player_get_state();
    PlayerDeathAnimation animation(fatal,player.x_position,player.y_position,(irand()%8)==0);
    PlayerDeathFrame frame;bool decayed=false;
    if(fatal) Items.init();
    yuugenmagan_radar_render(boss.state()); // expose the current, now-frozen hitboxes on a miss too
    while(animation.next(frame)) {
        if(frame.returning && !decayed) {shots_initialize();Pellets.decay();decayed=true;}
        if(frame.returning) camera.follow((frame.left[0]+frame.left[1]+PLAYER_W)/2);
        canvas_begin_frame();canvas_playfield_clip(true);
        if(!view.compose(boss.state(),&boss.missiles())) return false;
        shots_render_frame();orb_render_frame();pellet_render_frame();orb_indicator_render();player_render_death(frame);
        canvas_playfield_clip(false);canvas_buffer_prepare();
        do {swiWaitForVBlank();} while(!view.present(frame.shake_y));
        for(int hold=1;hold<frame.hold;++hold) swiWaitForVBlank();
    }
    return true;
}

namespace {
bool render(YuugenMaganCombatView &view,const YuugenMaganPhase1 &boss,int lives,unsigned frame)
{
    video_bomb_prepare(player_bomb().active(),player_bomb().frame(),frame);
    camera.follow(player_center_x());canvas_begin_frame();canvas_playfield_clip(true);
    player_invincibility_render_frame();
    if(!view.compose(boss.state(),&boss.missiles())) return false;
    player_render_frame();shots_render_frame();orb_render_frame();pellet_render_frame();orb_indicator_render();bomb_render_frame();
    canvas_playfield_clip(false);
    const auto &s=boss.state();
    upper_hud_render(lives,rem_bombs,stage_timer,UpperHudMode::Boss);upper_hud_boss(s.health.hp,s.hp_displayed,8,12);
    hud_render(lives,10);yuugenmagan_radar_render(s);
    canvas_buffer_prepare();do {swiWaitForVBlank();} while(!view.present());
    return true;
}
void label(const char *title,const char *controls,const char *detail)
{
    subscreen_fill_rect(8,140,248,48,SUB_COL_BG);
    subscreen_text(8,140,title,SUB_COL_DIM);subscreen_text(8,156,controls,SUB_COL_DIM);subscreen_text(8,172,detail,SUB_COL_DIM);
}
}

bool yuugenmagan_phase1_review(bool phase3,bool phase5,bool phase9,bool phase12,bool phase13,bool defeat)
{
    phase5=phase3 && phase5; // false retains the approved first-block practice
    phase9=phase5 && phase9;
    phase12=phase9 && phase12;
    phase13=phase12 && phase13;
    if(g_canvas_target) return false;
    YuugenMaganCombatView view;
    struct Cleanup {
        YuugenMaganCombatView &view;
        int score=stage_score,high=hiscore,bombs=rem_bombs,speed=pellet_speed;
        unsigned timer=stage_timer;
        ~Cleanup() {
            player_bomb_cancel();player_is_hit=false;video_bomb_reset();video_stage_fade_out();
            view.end();music_stop();sfx_stop();Pellets.init();shots_initialize();Items.init();
            stage_entrance_prepare();text_layer_clear();bgShow(g_bg_id_tiles);
            hud_radar_render(); // no stale eye icons while the caller reloads its card stage
            hud_invalidate();upper_hud_invalidate();
            stage_score=score;hiscore=high;rem_bombs=bombs;pellet_speed=speed;stage_timer=timer;
        }
    } cleanup{view};
    video_stage_fade_out();camera.snap_center();
    if(!view.begin()) return false;
    music_stop();sfx_stop();if(!music_play_track(NATIVE_LEGEND)) return false;
    stage_entrance_prepare();text_layer_clear();bgHide(g_bg_id_tiles);
    static const uint8_t empty[STAGE_ROWS][STAGE_COLS]{};
    stage_init(empty,empty);stage_reset();Items.init();Pellets.init();shots_initialize();
    player_initialize();orb_initialize();player_is_hit=false;timer_init_for(1,4);
    stage_score=0;rem_bombs=2;pellet_speed=-4;int lives=3;unsigned frame=0;
    upper_hud_invalidate();hud_invalidate();
    label("YUUGENMAGAN OPENING","START:PAUSE SELECT:EXIT","MOVE/SHOOT/STRIKE/BOMB");
    YuugenMaganPhase1 boss;boss.reset(phase3,phase5,phase9,phase12,phase13);
    if(!render(view,boss,lives,frame)) return false; // complete first frame before releasing black
    bool can_exit=false,finished=false;int shown_phase=0,shown_attack=-1;
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
        if(defeat && boss.state().checkpoint && boss.state().health.hp==0) {
            // The source blocks inside boss update: no orb/shot/pellet/timer
            // tick or pending player miss may run after the lethal boss hit.
            player_is_hit=false;boss.clear_defeat_projectiles();Pellets.reset_nonclouds();
            if(stage_score>hiscore) hiscore=stage_score;
            upper_hud_render(lives,rem_bombs,stage_timer,UpperHudMode::Boss);upper_hud_boss(0,0,8,12);
            hud_render(lives,10);hud_radar_render();
            label("YUUGENMAGAN DEFEAT","FROZEN SOURCE ANIMATION","NO RUN RESOURCES SPENT");
            if(!view.defeat(boss.state(),boss.missiles())) return false;
            player_bomb_cancel();finished=true;
            label("DEFEAT REVIEW COMPLETE","A/B/START:RETURN","PROGRESSION NOT IMPLEMENTED");
            continue;
        }
        shots_update();orb_update();
        if(timer_update(++frame)==TIMER_EVENT_HARRYUP) harryup_animate();
        if(Pellets.update()) player_is_hit=true;
        if(!player_is_hit && orb_hits_player(player_get_state().x_position,player_top,OR_NONE,
            player_is_invincible(),player_is_invincible_to_orb())) player_is_hit=true;
        if(player_is_hit) {
            player_is_hit=false;
            const bool fatal=lives<=0;
            if(!fatal) {--lives;mdrv2_se_play(SFX_MISS);}
            if(!yuugenmagan_death_animate(view,boss,fatal)) return false;
            if(fatal) {
                // First-block completion is not a boss defeat: simultaneous
                // player death still resolves, and never grants a fake victory.
                player_bomb_cancel();video_bomb_reset();finished=true;
                label("PRACTICE OVER","A/B/START:RETURN","NO RUN RESOURCES SPENT");continue;
            }
            player_miss();if(rem_bombs<5) ++rem_bombs;
            pellet_speed=pellet_speed>0 ? 0 : pellet_speed-2;if(pellet_speed< -15) pellet_speed=-15;
        }
        const auto &s=boss.state();
        if(!s.checkpoint && (s.phase!=shown_phase || (s.phase==13 && s.pentagram.attack_phase!=shown_attack))) {
            shown_phase=s.phase;shown_attack=s.pentagram.attack_phase;
            label(s.phase==13 ? (shown_attack==6 || shown_attack==9 ? "YUUGENMAGAN STAR SLAM" :
                shown_attack>=7 ? "YUUGENMAGAN STAR GROWTH" : "YUUGENMAGAN STAR SPINS") : s.phase==1 ? "YUUGENMAGAN FIRST PATTERN" :
                (s.phase==2 || s.phase==4 || s.phase==6 || s.phase==8) ? "YUUGENMAGAN EYE SHIFT" :
                s.phase==3 ? "YUUGENMAGAN MISSILES" : s.phase==7 ? "YUUGENMAGAN SHIFTED MISSILES" :
                s.phase==9 ? "YUUGENMAGAN NORTHERN LASERS" : s.phase==10 ? "YUUGENMAGAN ALL-EYE SHIFT" :
                s.phase==11 ? "YUUGENMAGAN PENTAGRAM" : s.phase==12 ? "YUUGENMAGAN STAR RISE" : "YUUGENMAGAN CIRCULAR SPRAY",
                "START:PAUSE SELECT:EXIT","MOVE/SHOOT/STRIKE/BOMB");
        }
        if(s.checkpoint) {player_bomb_cancel();video_bomb_reset();}
        if(!render(view,boss,lives,frame)) return false;
        if(s.checkpoint) {
            finished=true;music_pause();
            label(phase13 ? "FINAL PATTERN HP LIMIT" : phase12 ? (s.health.hp<=0 ? "PENTAGRAM HP LIMIT" : "PENTAGRAM ASSEMBLED") :
                phase9 ? (s.timed_out ? "NORTHERN LASERS COMPLETE" : "NORTHERN LASERS HP LIMIT") :
                phase5 ? (s.timed_out ? "CIRCULAR SPRAY TIMEOUT" : "CIRCULAR SPRAY COMPLETE") :
                phase3 ? (s.timed_out ? "MISSILE PATTERN TIMEOUT" : "MISSILE PATTERN COMPLETE") :
                (s.timed_out ? "FIRST PATTERN TIMEOUT" : "FIRST PATTERN COMPLETE"),
                "A/B/START:RETURN",phase13 ? "DEFEAT NOT IMPLEMENTED" : phase12 ? (s.health.hp<=0 ? "DEFEAT NOT IMPLEMENTED" : "PHASE 13 NOT IMPLEMENTED") :
                phase9 ? "PHASE 10 NOT IMPLEMENTED" : phase5 ? "PHASE 6 NOT IMPLEMENTED" :
                phase3 ? "PHASE 4 NOT IMPLEMENTED" : "PHASE 2 NOT IMPLEMENTED");
        }
    }
    return true;
}
