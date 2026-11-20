#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#ifndef HRTP_GAME_FLOW_TEST
#include <filesystem.h>
#endif
#include <main/random.hpp>

#include <nds.h>

#include "background.hpp"
#include "video.hpp"
#include "tiles.hpp"
#include "stage_render.hpp"
#include "stage_entrance.hpp"
#include "pellet_render.hpp"
#include "subscreen.hpp"
#include "hud_render.hpp"
#include "round_results.hpp"
#include "palette_fx.hpp"
#include "music.hpp"
#include "sound_effects.hpp"

#include <main/player/player.hpp>
#include <main/player/shot.hpp>
#include <main/player/orb.hpp>
#include <main/bullet/pellet.hpp>
#include <main/stage/stageobj.hpp>
#include <main/stage/card.hpp>
#include <main/stage/item.hpp>
#include <main/stage/timer.hpp>
#include <main/bonus.hpp>
#include <main/extend.hpp>

#include "player_render.hpp"
#include "shot_render.hpp"
#include "orb_render.hpp"
#include "item_render.hpp"
#include "popup.hpp"
#include "menu_render.hpp"
#include "text_layer.hpp"
#include "boss_render.hpp"
#include "canvas.hpp"
#include "bomb_render.hpp"
#include <main/boss/singyoku.hpp>
#include <main/boss/konngara.hpp>
#include <main/progression.hpp>
#include "camera.hpp"
#include "upper_hud.hpp"
#include "konngara_intro_render.hpp"
#include "konngara_combat_render.hpp"
#include "mima_combat_render.hpp"
#include "kikuri_render.hpp"
#include "yuugenmagan_combat_render.hpp"
#include "elis_render.hpp"
#include "backgrounds/MimaPaletteFx.h"

#include "stagedat/stagedat0.h"
#include "stagedat/stagedat1.h"
#include "stagedat/stagedat2.h"
#include "stagedat/stagedat3.h"
#include "stagedat/stagedat4.h"
#include "stagedat/stagedat5.h"
#include "stagedat/stagedat6.h"
#include "stagedat/stagedat7.h"

// --- M3: HUD, bullets, turrets ---------------------------------------------
// SELECT + d-pad: dev stage navigator (jump scenes/stages for testing).

static const int SCENES = 7; // navigable scenes 0-6 (scene 7 = unused
                             // leftover content, not part of the game)
static const int START_LIVES = 3;

static int current_scene;
static int current_stage;
static int lives;
static ScoreExtends score_extends;
static unsigned int frame;
// Source frame_rand cadence, independent of timer/bomb presentation counters.
// Preserve across stage loads, misses and continues; reset for a new run only.
static unsigned int pellet_speed_frame;
static bool wait_for_shot; // ReC98 stage_wait_for_shot_to_begin
static KonngaraCombat konngara;
static KonngaraCombatView konngara_view;
static MimaPhase1 mima;
static MimaCombatView mima_view;
static KikuriPhase1 kikuri;
static KikuriView kikuri_view;
static YuugenMaganPhase1 yuugenmagan;
static YuugenMaganCombatView yuugenmagan_view;
static ElisPhase1 elis;
static ElisView elis_view;
static bool jigoku_cleared;
static const PlayerDeathFrame *player_death_frame;
alignas(4) static u16 regular_bomb_canvas[256*256];
static bool regular_bomb_canvas_owned;

static bool score_extend_update()
{
    if(!score_extends.update(stage_score,lives)) return false;
    // Source pellet_speed_raise(1) selects the INTEGER/raw-unit overload.
    // The DS float-only helper would add40 here instead of the required1.
    pellet_speed=pellet_speed<20 ? pellet_speed+1 : 20;
    mdrv2_se_play(SFX_PICKUP);
    return true; // the next normal HUD commit presents the changed life count
}

static void check_konngara(bool okay,const char *part)
{
    if(okay) return;
    const char *audio_error=music_error();
    konngara_view.end();music_stop();
    video_stage_reveal(); // an entrance failure must not hide the error console
    consoleDemoInit();
    if(strcmp(part,"defeat")==0) {
        const auto &d=konngara_defeat_diagnostics();
        printf("Konngara defeat error\nStep: %s\n",konngara_defeat_failure_name(d.failure));
        printf("errno: %d\nAsset: %lu B\nRead: %lu B\n",d.error_number,
            (unsigned long)d.requested_bytes,(unsigned long)d.read_bytes);
        printf("Active/defeated: %u/%u\n",unsigned(d.active),unsigned(d.already_defeated));
        if(d.page>=0) printf("Page: %d\n",d.page);
        if(d.heap_available) {
            printf("Heap BEFORE cleanup:\nBase: %08lX\nEnd: %08lX\nLimit: %08lX\n",
                (unsigned long)d.heap_start,(unsigned long)d.heap_end,(unsigned long)d.heap_limit);
            printf("Used: %lu  Free: %lu\nGrowable: %lu\n",(unsigned long)d.heap_used,
                (unsigned long)d.heap_free,(unsigned long)(d.heap_limit>d.heap_end?d.heap_limit-d.heap_end:0));
        }
        if(audio_error[0]) printf("Audio: %s\n",audio_error);
    } else printf("Konngara %s asset/audio error\n%s",part,audio_error);
    while(true) swiWaitForVBlank();
}

static void check_music()
{
    if (!music_error()[0])
        return;
    consoleDemoInit();
    printf("Native audio error:\n%s\n", music_error());
    while (true)
        swiWaitForVBlank();
}

static void check_mima(bool okay,const char *part)
{
    if(okay) return;
    mima_view.end();music_stop();video_stage_reveal();consoleDemoInit();
    printf("Mima %s asset/audio error\n%s",part,music_error());
    while(true) swiWaitForVBlank();
}

static void check_kikuri(bool okay,const char *part)
{
    if(okay) return;
    kikuri_view.end();music_stop();video_stage_reveal();consoleDemoInit();
    printf("Kikuri %s asset/audio error\n%s",part,music_error());
    while(true) swiWaitForVBlank();
}

static void check_yuugenmagan(bool okay,const char *part)
{
    if(okay) return;
    yuugenmagan_view.end();music_stop();video_stage_reveal();consoleDemoInit();
    printf("YuugenMagan %s asset/audio error\n%s",part,music_error());
    while(true) swiWaitForVBlank();
}

static void check_elis(bool okay,const char *part)
{
    if(okay) return;
    elis_view.end();music_stop();video_stage_reveal();consoleDemoInit();
    printf("Elis %s asset/audio error\n%s",part,music_error());
    while(true) swiWaitForVBlank();
}

// The game's own 1-based stage number (Shrine = 1-5, then route stages
// 6-20). Makai and Jigoku are *parallel* routes: scenes 1/2 are both
// stages 6-10, 3/4 are 11-15, 5/6 are 16-20.
static int global_stage_num()
{
    return stage_number(current_scene, current_stage);
}

static void render_hud()
{
    const bool boss = stage_has_boss(current_scene, current_stage);
    HudRadarBoss radar_boss;
    upper_hud_render(lives, rem_bombs, stage_timer, boss ? UpperHudMode::Boss : UpperHudMode::Regular);
    if (stage_has_singyoku(current_scene,current_stage)) {
        const auto &s = singyoku_state();
        upper_hud_boss(s.health.hp, s.hp_displayed);
        radar_boss=hud_radar_boss(s.entity,s.phase!=SG_DEFEATED);
    } else if(stage_has_konngara(current_scene,current_stage)) {
        const auto &s=konngara.state();
        upper_hud_boss(s.health.hp,s.hp_displayed,10,16);
        radar_boss=hud_radar_boss(konngara_hitbox(),!s.complete);
    } else if(stage_has_mima(current_scene,current_stage)) {
        const auto &s=mima.state();
        upper_hud_boss(s.health.hp,s.hp_displayed,2,6);
        radar_boss=hud_radar_boss(s.entity,!s.checkpoint);
    } else if(stage_has_kikuri(current_scene,current_stage)) {
        const auto &s=kikuri.state();
        upper_hud_boss(s.hp_displayed,s.hp_displayed,6,10); // preserve hidden phase6 timeout
        radar_boss=hud_radar_boss(s.entity,!s.checkpoint);
    } else if(stage_has_yuugenmagan(current_scene,current_stage)) {
        const auto &s=yuugenmagan.state();
        upper_hud_boss(s.health.hp,s.hp_displayed,8,12);
    } else if(stage_has_elis(current_scene,current_stage)) {
        const auto &s=elis.state();const auto &active=s.bat_form ? s.bat : s.entity;
        upper_hud_boss(s.health.hp,s.hp_displayed,6,10);
        radar_boss=hud_radar_boss(active,!s.checkpoint && active.orb_active);
    } else upper_hud_boss_hide();
    hud_render(lives, global_stage_num());
    if(stage_has_yuugenmagan(current_scene,current_stage) && !yuugenmagan.state().checkpoint)
        yuugenmagan_radar_render(yuugenmagan.state());
    else hud_radar_render(radar_boss);
}

static void render_frame()
{
    const bool boss = stage_has_singyoku(current_scene, current_stage);
    const bool final_boss=stage_has_konngara(current_scene,current_stage);
    const bool mima_boss=stage_has_mima(current_scene,current_stage);
    const bool kikuri_boss=stage_has_kikuri(current_scene,current_stage);
    const bool yuugenmagan_boss=stage_has_yuugenmagan(current_scene,current_stage);
    const bool elis_boss=stage_has_elis(current_scene,current_stage);
    video_bomb_prepare(!player_death_frame && player_bomb().active(),player_bomb().frame(),frame);
    if(!final_boss && (player_bomb().active() || video_bomb_effect_active()) && !g_canvas_target)
        regular_bomb_canvas_owned=canvas_buffer_begin(regular_bomb_canvas);
    if(!final_boss && !mima_boss && !kikuri_boss && !yuugenmagan_boss && !elis_boss && !player_death_frame && !g_canvas_target) video_apply_camera();
    if(kikuri_boss) kikuri_view.prepare_combat(kikuri.state());
    stage_render_frame();
    // Erase only previous effects; the background scrolls in hardware and
    // BG0/OAM HUD elements never go through the software canvas.
    canvas_begin_frame();
    canvas_playfield_clip(true);
    if(!player_death_frame) player_invincibility_render_frame();
    if(final_boss) check_konngara(konngara_view.frame(konngara.state()),"frame");
    if(mima_boss) check_mima(mima_view.compose(mima.state(),mima.missiles()),"frame");
    if(kikuri_boss) kikuri_view.draw_combat(kikuri.state());
    if(yuugenmagan_boss) check_yuugenmagan(yuugenmagan_view.compose(yuugenmagan.state(),&yuugenmagan.missiles()),"frame");
    if(elis_boss) check_elis(elis_view.compose(elis.state(),elis.missiles()),"frame");
    if(!player_death_frame) player_render_frame();
    shots_render_frame();
    orb_render_frame();
    if(mima_boss) {
        const auto &s=mima.state();
        pellet_render_colored(Mima_palette_fx[s.darkness/4][s.flash ? 1+s.flash_darkness/4 : 0][7]);
    } else if(elis_boss) pellet_render_colored(elis_view.pellet_color());
    else pellet_render_frame();
    item_render_frame();
    item_rings_frame();
    if(!player_death_frame) popup_render_frame(); // rendering normally ages these
    orb_indicator_render((boss && singyoku_state().phase==SG_ENTRANCE) ||
        (yuugenmagan_boss && yuugenmagan.state().phase==0));
    if (boss) boss_render_frame();
    if(!player_death_frame) bomb_render_frame();
    if(player_death_frame) player_render_death(*player_death_frame);
    canvas_playfield_clip(false);
    if(!player_death_frame) render_hud(); // source updates lost-life resources after recovery
    // Buffered boss presenters own early-VBlank palette, camera and shake commits.
    if(!mima_boss && !kikuri_boss && !yuugenmagan_boss && !elis_boss) {
        if(player_death_frame) video_apply_death_shake(player_death_frame->shake_y,final_boss);
        else {
            bgSetScroll(g_bg_id_canvas,0,0);
            bgSetScroll(g_bg_id_tiles,camera.left,0);
            if(!final_boss) bgSetScroll(g_bg_id_background,background_scroll_x(),0);
            if(player_bomb().active() || video_bomb_effect_active())
                video_apply_death_shake(video_bomb_shake(),final_boss);
        }
    }
    if(mima_boss) {
        canvas_buffer_prepare();
        do { swiWaitForVBlank(); } while(!mima_view.present(player_death_frame ? player_death_frame->shake_y : -1));
    } else if(kikuri_boss) {
        canvas_buffer_prepare();
        do { swiWaitForVBlank(); } while(!kikuri_view.present_combat(player_death_frame ? player_death_frame->shake_y : -1));
    } else if(yuugenmagan_boss) {
        canvas_buffer_prepare();
        do { swiWaitForVBlank(); } while(!yuugenmagan_view.present(player_death_frame ? player_death_frame->shake_y : -1));
    } else if(final_boss) {
        canvas_buffer_prepare(); // after lasers, pellets and all other effects
        do { swiWaitForVBlank(); } while(!konngara_view.present());
    } else if(elis_boss) {
        canvas_buffer_prepare();
        do { swiWaitForVBlank(); } while(!elis_view.present(player_death_frame ? player_death_frame->shake_y : -1));
    } else if(g_canvas_target) {
        canvas_buffer_prepare();
        do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
        video_bomb_commit();
        if(player_death_frame || player_bomb().active() || regular_bomb_canvas_owned) bgUpdate();
        else video_apply_camera();
        oamUpdate(&oamMain);
        canvas_buffer_present(true);
        if(regular_bomb_canvas_owned && !player_bomb().active() && !video_bomb_effect_active()) {
            canvas_buffer_end(true);regular_bomb_canvas_owned=false;
        }
    } else {
        video_bomb_commit();
        if(player_death_frame) {
            // Full game-over trails can take longer than VBlank to compose.
            // Writing affine BG reference points afterward, mid-scanout,
            // restarts the background there and makes it appear to fall down.
            if(player_death_frame->burst_step>=0)
                do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
            bgUpdate();
        }
        oamUpdate(&oamMain);
    }
}

static const ds_stage_layout_t *scene_layout(int scene, int stage)
{
    switch (scene) {
    case 0: return &ds_stagedat0[stage];
    case 1: return &ds_stagedat1[stage];
    case 2: return &ds_stagedat2[stage];
    case 3: return &ds_stagedat3[stage];
    case 4: return &ds_stagedat4[stage];
    case 5: return &ds_stagedat5[stage];
    case 6: return &ds_stagedat6[stage];
    default: return &ds_stagedat7[stage];
    }
}

static void stage_load(int scene, int stage, bool preserve_sfx = false, bool preserve_score = false)
{
    if (scene < 0 || scene >= SCENES) scene = 0;
    if (stage < 0 || stage >= scene_stage_count(scene)) stage = 0;
    const bool boss = stage_has_boss(scene, stage);
    const bool final_boss=stage_has_konngara(scene,stage);
    const bool mima_boss=stage_has_mima(scene,stage);
    const bool kikuri_boss=stage_has_kikuri(scene,stage);
    const bool yuugenmagan_boss=stage_has_yuugenmagan(scene,stage);
    const bool elis_boss=stage_has_elis(scene,stage);
    const bool previous_boss = stage_has_boss(current_scene, current_stage);
    if(yuugenmagan_boss || elis_boss || stage_has_yuugenmagan(current_scene,current_stage) || stage_has_elis(current_scene,current_stage)) {
        video_bomb_reset();video_stage_fade_out(); // protect shared cache/canvas cleanup on retries/navigation too
    }
    const bool regular_handoff=preserve_score && !boss && !previous_boss &&
        scene==current_scene && stage==current_stage+1;
    const int entering_score=preserve_score ? stage_score : 0;
    // A normal next-stage entrance reuses the resident scenery, like the
    // source's page restore. Start replacing actors/tiles in a fresh VBlank,
    // not halfway through scanout after dismissing the bonus panel.
    if(regular_handoff)
        do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
    // Remove modal/result text before restoring scenery or revealing it through
    // the next entrance's transparent glyphs. Preserve the fixed upper HUD.
    text_layer_clear();
    player_death_frame=nullptr;
    if(regular_bomb_canvas_owned) {
        canvas_buffer_end();canvas_clear();regular_bomb_canvas_owned=false;
    }
    bgSetScroll(g_bg_id_canvas,0,0);
    konngara_view.end(); // also releases the temporary OBJ lease on navigation/restart
    mima_view.end();
    kikuri_view.end();
    yuugenmagan_view.end();
    elis_view.end();
    video_bomb_reset();
    jigoku_cleared=false;
    upper_hud_invalidate();
    // Route confirmation must survive into the new scene's title glyphs.
    // Other boundaries cancel stale effects; music never owns SFX channels.
    if (!preserve_sfx) sfx_stop();
    // A scene change eventually selects a different native song. Stop the previous
    // scene before its entrance rather than letting it bleed into missing BGM.
    // A same-stage Kikuri/YuugenMagan retry must restart the music clock too;
    // music_play_track() intentionally reuses an already-playing native song.
    if (scene != current_scene || boss != previous_boss || kikuri_boss || yuugenmagan_boss || elis_boss)
        music_stop();
    boss_render_hide();
    stage_entrance_prepare();
    // Snap before loading/displaying scenery or drawing any stage text. A
    // previous life/scene's camera must never leak into the entrance.
    camera.snap_center();
    current_scene = scene;
    current_stage = stage;

    // Per-scene background: its palette becomes the round palette that
    // cards/items render through (the original's ptn_load "retains the
    // current hardware palette").
    if(!regular_handoff) {
        if (stage_has_singyoku(scene,stage)) load_boss_background();
        else if(mima_boss) check_mima(load_mima_background(),"background");
        else if(yuugenmagan_boss) {
            // Keep native black through the one-time upload. BG2 must remain
            // unleased until its hardware title masks have finished.
            check_yuugenmagan(yuugenmagan_view.prepare_entrance(),"background");
            background_set_boss_scroll();
        }
        else if(elis_boss) {
            check_elis(elis_view.prepare_entrance(),"background");background_set_boss_scroll();
        } else load_background(scene);
    }
    palette_apply_round();

    if (boss) {
        static const uint8_t empty[STAGE_ROWS][STAGE_COLS] = {};
        stage_init(empty, empty);
    } else {
        const ds_stage_layout_t *layout = scene_layout(scene, stage);
        stage_init(layout->obstacle, layout->card);
    }
    stage_score=entering_score;
    if(!preserve_score) score_extends.reset(); // new game, continue, quit or dev jump
    stage_reset();
    Pellets.init();
    Items.init();
    timer_init_for(scene, stage);
    orb_initialize();
    player_initialize();
    shots_initialize();
    popup_clear();
    if(kikuri_boss) kikuri.reset(true,true,true);
    if(yuugenmagan_boss) yuugenmagan.reset(true,true,true,true,true);
    if(elis_boss) elis.reset(true,true);
    if (stage_has_singyoku(scene,stage)) {
        singyoku_init();
        // Preserve the approved boss palette/frame cache beneath its title.
        // Entrance presentation hides this shadow OAM before any update.
        boss_render_frame();
    }
    if(final_boss) {
        konngara.reset(); // real18HP encounter; no practice HP/resource presets
        wait_for_shot=false;
        hud_invalidate();hud_render(lives,20);
        hud_radar_render(hud_radar_boss(konngara_hitbox()));
        // Source Konngara has its own entrance, not STAGE NN/title banners.
        // Keep the final opaque intro image over old scenery until the first
        // buffered combat frame commits its BG, actors and fixed HUD together.
        CanvasForegroundScope foreground;
        check_konngara(konngara_intro_play(),"entrance");
        check_konngara(konngara_view.begin(),"combat");
        render_frame();
        check_music();
        return; // ALICE continues from the entrance, without a second play call
    }
    if(mima_boss) {
        // The controller's direction draw belongs AFTER the spread-in. Do not
        // reset it early merely to populate the title's fixed HUD.
        upper_hud_render(lives,rem_bombs,stage_timer,UpperHudMode::Boss);
        constexpr BossEntity initial_mima=MimaPhase1State{}.entity;
        upper_hud_boss(12,0,2,6);hud_render(lives,10);hud_radar_render(hud_radar_boss(initial_mima));
    } else render_hud();
    // No fully rendered gameplay frame may expose the orb/cards before the
    // entrance. Later-stage card ripple precedes STAGE NN, as in ReC98.
    // Initialization may overrun VBlank. Affine BG reference-point writes
    // during scanout restart the image at that scanline, despite a zero Y.
    if(regular_handoff && (REG_VCOUNT<192 || REG_VCOUNT>=200))
        do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
    video_apply_camera();
    stage_entrance_animate(global_stage_num(), current_stage == 0 || boss, kikuri_boss);
    wait_for_shot = !boss;
    if(yuugenmagan_boss) {
        check_yuugenmagan(yuugenmagan_view.begin(),"combat");
        render_frame(); // source default actors appear before LEGEND starts
        check_yuugenmagan(music_play_track(NATIVE_LEGEND),"music");
        check_music();return; // phase0 advances only in the normal gameplay tick
    }
    if(mima_boss) {
        check_mima(mima_view.begin(true),"combat");
        check_mima(music_play_track(NATIVE_LEGEND),"music");
        check_mima(mima_entrance_play(mima_view),"entrance");
        mima.reset(true); // both approved blocks; no practice resource presets
        render_frame();
        check_music();return; // LEGEND continues, never restart after spread-in
    }
    if(kikuri_boss) {
        // The kanji revealed the stage scenery, then the banner hid BG3.
        // Clear its now-invisible storage before the view saves the source
        // black field; only KAMI's music-clock shroud may reveal Kikuri's disc.
        dmaFillHalfWords(0,BG_BMP_RAM(4),128*1024);BG_PALETTE[0]=0;
        check_kikuri(kikuri_view.begin(),"combat");
        check_kikuri(music_play_track(NATIVE_KAMI),"music");
        check_kikuri(kikuri_entrance_play(kikuri_view),"entrance");
        render_frame();check_music();return;
    }
    if(elis_boss) {
        check_elis(elis_view.begin(),"combat");
        check_elis(music_play_track(NATIVE_KAMI2),"music");
        check_elis(elis_entrance_play(elis_view),"entrance");
        render_frame();check_music();return; // KAMI2 continues from the approved entrance
    }
    if(regular_handoff && (REG_VCOUNT<192 || REG_VCOUNT>=200))
        do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
    render_frame(); // source life-loop handoff: player/orb appear before BGM

    // ReC98 starts regular-stage BGM after the entrance and keeps it running
    // across the scene's non-boss stages. music_play_scene() is idempotent for
    // an already-playing scene.
    if (boss) {
        music_play_track(NATIVE_POSITIVE);
    } else
        music_play_scene(scene);
    check_music();
}

static void start_new_game()
{
    lives=START_LIVES;rem_bombs=2;point_value=0;
    pellet_speed=-4; // ReC98 to_pellet_speed(-0.1)
    frame=0;
    pellet_speed_frame=0;
    stage_load(0,0);
}

static void player_death_animate(bool fatal)
{
    video_bomb_reset(); // recovery is cosmetic; do not tint the approved miss
    const auto &player=player_get_state();
    PlayerDeathAnimation animation(fatal,player.x_position,player.y_position,(irand()%8)==0);
    PlayerDeathFrame presentation;
    bool decayed=false;
    if(fatal) Items.init();
    if(stage_has_yuugenmagan(current_scene,current_stage))
        yuugenmagan_radar_render(yuugenmagan.state()); // publish this update's hitbox changes before freezing the miss
    while(animation.next(presentation)) {
        if(presentation.returning && !decayed) {
            shots_initialize();Pellets.decay();decayed=true;
        }
        player_death_frame=&presentation;
        if(presentation.returning)
            camera.follow((presentation.left[0]+presentation.left[1]+PLAYER_W)/2);
        // Game-over composition commits at the END of the frame, as does the
        // final-boss renderer. Keep the approved miss path's original timing.
        if(!fatal && !g_canvas_target && !stage_has_konngara(current_scene,current_stage)) swiWaitForVBlank();
        render_frame();
        for(int hold=1;hold<presentation.hold;++hold) swiWaitForVBlank();
    }
    player_death_frame=nullptr;
}

// One normal game tick, shared with the existing host viewport integration
// checks. Stage20 uses this same life/timer/item/miss path, not the practice loop.
static void gameplay_tick(uint16_t keys_held,uint16_t keys_down)
{
    if(jigoku_cleared) return;
    bool serving=wait_for_shot;
    if(serving && (keys_held&KEY_B)) { wait_for_shot=false;serving=false; }
    const bool boss=stage_has_boss(current_scene,current_stage);
    const bool final_boss=stage_has_konngara(current_scene,current_stage);
    const bool mima_boss=stage_has_mima(current_scene,current_stage);
    const bool kikuri_boss=stage_has_kikuri(current_scene,current_stage);
    const bool yuugenmagan_boss=stage_has_yuugenmagan(current_scene,current_stage);
    const bool elis_boss=stage_has_elis(current_scene,current_stage);
    const bool entrance=stage_has_singyoku(current_scene,current_stage) && singyoku_state().phase==SG_ENTRANCE;
    const int speed_lives=lives,speed_bombs=rem_bombs;
    if(entrance) singyoku_update();
    else player_update(keys_held,keys_down);
    if(!entrance && (player_bomb().pending() || player_bomb().active())) wait_for_shot=serving=false;
    const bool bomb_damage=boss_bomb_damage_allowed(player_bomb().damaging(),lives,frame+1);
    if(!serving && !entrance) {
        // ReC98 checks before bombs consume resources or score grants lives.
        // Count lethal boss ticks too, without advancing the frozen timer/bomb
        // frame used by the existing defeat/results presentation.
        pellet_speed_update(++pellet_speed_frame,speed_lives,speed_bombs);
        // Boss collision uses the previous orb point, before orb movement.
        if(final_boss) {
            konngara.update(bomb_damage);
            if(konngara.state().complete) {
                player_is_hit=false; // victory wins a simultaneous player hit
                player_bomb_cancel();
                Pellets.init();shots_initialize();Items.init();
                if(stage_score>hiscore) hiscore=stage_score;
                hud_render(lives,20); // includes the lethal-hit50000 immediately
                hud_radar_render(); // clear the defeated boss and collected items
                check_konngara(konngara_view.defeat(konngara.state().health.invincible),"defeat");
                score_extend_update(); // include the lethal-hit 50000, after the frozen defeat
                for(int f=0;f<120;++f) swiWaitForVBlank(); // source ending handoff delay
                text_layer_clear();
                text_layer_puts(7,4,"JIGOKU CLEARED",TEXT_PAL_WHITE);
                text_layer_puts(3,6,"ENDING NOT IMPLEMENTED",TEXT_PAL_WHITE);
                text_layer_puts(4,9,"A/B/START: NEW GAME",TEXT_PAL_WHITE);
                jigoku_cleared=true;
                return;
            }
            konngara.update_lasers();
        } else if(mima_boss) {
            mima.update(bomb_damage);
            if(mima.state().checkpoint) {
                // Lethal boss contact wins before any laser/orb/shot/pellet or
                // timer pass, including an already-pending player hit.
                player_is_hit=false;mima.clear_defeat_projectiles();Pellets.reset_nonclouds();
                if(stage_score>hiscore) hiscore=stage_score;
                render_hud();hud_radar_render();
                check_mima(mima_view.defeat(mima.state()),"defeat");
                score_extend_update(); // source gameplay check, before computing resource bonus
                player_bomb_cancel();
                // Defeat leaves an empty canvas and the default Reimu pose.
                // Release buffering BEFORE the existing hardware results capture.
                mima_view.end();
                const StageBonus bonus=roundbonus_compute(10,lives);
                stage_score+=bonus.total;
                score_extend_update(); // approved fix: boss-result points also qualify
                if(stage_score>hiscore) hiscore=stage_score;
                round_results_run(bonus,10);
                stage_load(4,0,false,true);
                return;
            }
            mima.update_lasers();
        } else if(kikuri_boss) {
            kikuri.update(bomb_damage);
            if(kikuri.state().checkpoint) {
                player_is_hit=false;player_bomb_cancel();video_bomb_reset();
                kikuri.clear_defeat_projectiles();Pellets.reset_nonclouds();
                if(stage_score>hiscore) hiscore=stage_score;
                render_hud();hud_radar_render();
                check_kikuri(kikuri_view.defeat(kikuri.state()),"defeat");
                score_extend_update();
                const u16 normal_master=kikuri_view.normal_brightness();
                do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
                kikuri_view.end(true); // release both leases before results borrow VRAM D
                // Display capture can precede MASTER_BRIGHT on real hardware.
                // Supply a truly black composed input, without recoloring OBJ.
                for(int slot=0;slot<128;++slot) oamSetHidden(&oamMain,slot,true);
                oamUpdate(&oamMain);bgHide(0);bgHide(g_bg_id_tiles);
                dmaFillHalfWords(0,BG_BMP_RAM(4),128*1024);BG_PALETTE[0]=0;
                const StageBonus bonus=roundbonus_compute(15,lives);
                stage_score+=bonus.total;if(stage_score>hiscore) hiscore=stage_score;
                score_extend_update(); // no HUD redraw over the genuinely black capture input
                round_results_run(bonus,15,normal_master);
                bgShow(0);bgShow(g_bg_id_tiles); // results exit still holds both screens black
                stage_load(6,0,false,true);
                return;
            }
            kikuri.update_lasers();
        } else if(yuugenmagan_boss) {
            yuugenmagan.update(bomb_damage);
            if(yuugenmagan.state().checkpoint) {
                // Lethal boss contact blocks here, before pending player misses
                // or another orb/shot/pellet/timer update, as in the source.
                player_is_hit=false;yuugenmagan.clear_defeat_projectiles();Pellets.reset_nonclouds();
                if(stage_score>hiscore) hiscore=stage_score;
                render_hud();
                check_yuugenmagan(yuugenmagan_view.defeat(yuugenmagan.state(),yuugenmagan.missiles()),"defeat");
                score_extend_update(); // gameplay damage points precede resource accounting
                player_bomb_cancel();
                yuugenmagan_view.end(); // release canvas/asset before hardware results capture
                const StageBonus bonus=roundbonus_compute(10,lives);
                stage_score+=bonus.total;
                score_extend_update();
                if(stage_score>hiscore) hiscore=stage_score;
                round_results_run(bonus,10);
                stage_load(3,0,false,true);
                return;
            }
        } else if(elis_boss) {
            elis.update(bomb_damage);
            for(int wait=0;wait<elis.state().hit_pause;++wait) swiWaitForVBlank();
            if(elis.state().checkpoint) {
                // Source blocks on lethal contact before all global passes/misses.
                player_is_hit=false;player_bomb_cancel();video_bomb_reset();
                elis.clear_defeat_projectiles();Pellets.reset_nonclouds();
                if(stage_score>hiscore) hiscore=stage_score;
                render_hud();hud_radar_render();
                check_elis(elis_view.defeat(elis.state(),elis.missiles()),"defeat");
                score_extend_update(); // gameplay award precedes resource-bonus accounting
                const u16 normal_master=elis_view.normal_brightness();
                do {swiWaitForVBlank();} while(REG_VCOUNT<192 || REG_VCOUNT>=200);
                elis_view.end(); // release both leases before hardware results capture borrows VRAM D
                // Capture may precede MASTER_BRIGHT: supply genuinely black pixels.
                for(int slot=0;slot<128;++slot) oamSetHidden(&oamMain,slot,true);
                oamUpdate(&oamMain);bgHide(0);bgHide(g_bg_id_tiles);
                dmaFillHalfWords(0,BG_BMP_RAM(4),128*1024);BG_PALETTE[0]=0;
                const StageBonus bonus=roundbonus_compute(15,lives);
                stage_score+=bonus.total;if(stage_score>hiscore) hiscore=stage_score;
                score_extend_update();
                round_results_run(bonus,15,normal_master);
                bgShow(0);bgShow(g_bg_id_tiles);
                stage_load(5,0,false,true);return;
            }
            elis.update_lasers();
        } else if(stage_has_singyoku(current_scene,current_stage)) {
            singyoku_update(bomb_damage);
            if(singyoku_state().phase==SG_DEFEATED) {
                player_is_hit=false;
                if(stage_score>hiscore) hiscore=stage_score;
                player_bomb_cancel();
                video_bomb_reset();
                music_stop();
                Pellets.init();shots_initialize();Items.init();render_frame();
                const RouteChoice route=boss_defeat_and_select_route();
                score_extend_update();
                const StageBonus bonus=roundbonus_compute(global_stage_num(),lives);
                stage_score+=bonus.total;
                score_extend_update();
                if(stage_score>hiscore) hiscore=stage_score;
                round_results_run(bonus,global_stage_num());
                stage_load(route_first_scene(route),0,true,true);
                return;
            }
        }
        shots_update();orb_update();
        if(!boss) { cards_hittest(global_stage_num()-1,player_bomb().damaging(),player_bomb().card_frame());stage_obstacles_update();cards_update(); }
        Items.update();
        // Only active gameplay advances the shared timer cadence.
        if(timer_update(++frame)==TIMER_EVENT_HARRYUP) harryup_animate();
        if(Pellets.update()) player_is_hit=true;
    }
    if(!entrance && !player_is_hit && orb_hits_player(player_get_state().x_position,
        player_get_state().y_position,OR_NONE,player_is_invincible(),player_is_invincible_to_orb())) player_is_hit=true;
    if(!boss && stage_is_cleared()) player_is_hit=false; // source stage-clear wins simultaneous contact
    // The source checks after gameplay scoring even on a hit frame. An extend
    // can therefore turn a would-be Game Over into a normal surviving miss.
    const bool extended=!serving && !entrance && score_extend_update();
    if(player_is_hit) {
        player_is_hit=false;
        const bool fatal=lives<=0;
        if(!fatal) { --lives;mdrv2_se_play(SFX_MISS); }
        if(stage_score>hiscore) hiscore=stage_score;
        player_death_animate(fatal);
        if(fatal) {
            const bool retry=gameover_menu_run();
            music_stop();
            if(retry) {
                lives=START_LIVES;rem_bombs=1;point_value=0;frame=0;
                // ReC98 continue_menu: lower the raw speed offset toward -2.
                pellet_speed=pellet_speed>-2 ? -2 : pellet_speed-5;
                if(pellet_speed<-15) pellet_speed=-15;
                stage_load(current_scene,current_stage);
            } else start_new_game();
            return;
        } else {
            player_miss(); // full attack-state reset, timer extension and grace
            if(rem_bombs<5) ++rem_bombs;
            pellet_speed=pellet_speed>0 ? 0 : pellet_speed-2;
            if(pellet_speed<-15) pellet_speed=-15;
            stage_reset(); // reset obstacle/portal clocks, not cards, HP or orb
            if(!final_boss && !mima_boss && !kikuri_boss && !yuugenmagan_boss && !elis_boss) swiWaitForVBlank(); // buffered bosses already held it
        }
    }
    if(!boss && stage_is_cleared()) {
        if(player_bomb().active() || video_bomb_effect_active()) {
            player_bomb_cancel();video_bomb_reset();render_frame();
        }
        // Commit exactly once at the clear boundary, not in the blocking
        // renderer/input loop. The displayed tally and credited amount agree.
        const StageBonus bonus=stagebonus_compute(global_stage_num(),lives);
        stage_score+=bonus.total;
        const bool bonus_extended=score_extend_update(); // always check, even after a gameplay award
        if(extended || bonus_extended)
            upper_hud_render(lives,rem_bombs,stage_timer,UpperHudMode::Regular);
        if(stage_score>hiscore) hiscore=stage_score;
        hud_render(lives,global_stage_num());
        hud_radar_render(); // remove the final card before the blocking tally
        stagebonus_run(bonus,global_stage_num());
        int next=current_stage+1;
        if(next>=scene_stage_count(current_scene)) next=0; // other unported bosses remain gated
        // All bosses share the outgoing black fade. Keep black until the first
        // kanji mask or Konngara intro image is ready; regular stages retain
        // their existing ripple/banner.
        if(stage_has_boss(current_scene,next))
            video_stage_fade_out();
        stage_load(current_scene,next,false,true);
        // stage_load already presented the first serve frame. A second regular
        // render here could rewrite affine scroll after that VBlank has ended.
        if(!stage_has_singyoku(current_scene,current_stage)) return;
    }
    if(stage_score>hiscore) hiscore=stage_score;
    camera.follow(player_center_x());render_frame();
}

static bool stage_navigate(uint16_t held,uint16_t down)
{
    if(!(held&KEY_SELECT)) return false;
    if(down&KEY_LEFT) {
        const int stage=current_stage ? current_stage-1 : scene_stage_count(current_scene)-1;
        stage_load(current_scene,stage);return true;
    }
    if(down&KEY_RIGHT) {stage_load(current_scene,(current_stage+1)%scene_stage_count(current_scene));return true;}
    if(down&(KEY_UP|KEY_DOWN)) {
        const int scene=(current_scene+SCENES+((down&KEY_UP) ? -1 : 1))%SCENES;
        stage_load(scene,current_stage<scene_stage_count(scene) ? current_stage : scene_stage_count(scene)-1);return true;
    }
    return false;
}

#ifndef HRTP_GAME_FLOW_TEST
int main(int argc, char **argv)
{
    if (!nitroFSInit(NULL))
    {
        consoleDemoInit();
        perror("NitroFS init error!");
        while (1)
            swiWaitForVBlank();
    }

    initialize_irand(time(NULL));

    video_init();
    // One OAM lifetime: stage/game resets reuse the renderer graphics caches.
    oamInit(&oamMain, SpriteMapping_1D_32, false);
    subscreen_init();
    text_layer_init();
    music_init();
    check_music();
    sfx_init();
    check_music();

    // Backgrounds load per stage (stage_load); tiles only need the atlas.
    tiles_init();

    if (!player_render_init() || !shots_render_init() || !orb_render_init() ||
        !item_render_init() || !boss_render_init() || !upper_hud_init()) {
        consoleDemoInit();
        printf("Sprite VRAM allocation failed");
        while (true) swiWaitForVBlank();
    }

    start_new_game();

    while (1)
    {
        if(jigoku_cleared) {
            swiWaitForVBlank();scanKeys();
            if(keysDown()&(KEY_A|KEY_B|KEY_START)) {
                start_new_game();
            }
            continue; // terminal route-clear state, never loop Stage20 to16
        }
        // Konngara composes during visible time and commits at the END of the
        // frame. Do not add a second VBlank wait and halve its gameplay rate.
        if(!stage_has_konngara(current_scene,current_stage) && !g_canvas_target && !player_bomb().pending()) swiWaitForVBlank();

        scanKeys();
        uint16_t keys_held = keysHeld();
        uint16_t keys_down = keysDown();

        if(stage_navigate(keys_held,keys_down)) continue;

        // START = Esc (pause). QUIT restarts at the first stage -- there is
        // deliberately no exit from the game loop (project head's call);
        // the title screen (M6) becomes QUIT's destination later.
        if (keys_down & KEY_START) {
            music_pause();
            if (pause_menu_run()) {
                music_stop();
                lives = START_LIVES;
                stage_load(0, 0);
            } else
                music_resume();
            continue;
        }

        gameplay_tick(keys_held,keys_down);
    }

    return 0;
}
#endif
