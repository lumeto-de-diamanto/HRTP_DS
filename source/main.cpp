#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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

#include "player_render.hpp"
#include "shot_render.hpp"
#include "orb_render.hpp"
#include "item_render.hpp"
#include "popup.hpp"
#include "menu_render.hpp"
#include "text_layer.hpp"
#include "boss_render.hpp"
#include "canvas.hpp"
#include <main/boss/singyoku.hpp>
#include <main/boss/konngara.hpp>
#include <main/progression.hpp>
#include "camera.hpp"
#include "upper_hud.hpp"
#include "konngara_intro_render.hpp"
#include "konngara_combat_render.hpp"

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
static unsigned int frame;
static bool wait_for_shot; // ReC98 stage_wait_for_shot_to_begin
static KonngaraCombat konngara;
static KonngaraCombatView konngara_view;
static bool jigoku_cleared;
static const PlayerDeathFrame *player_death_frame;

static void check_konngara(bool okay,const char *part)
{
    if(okay) return;
    konngara_view.end();music_stop();
    consoleDemoInit();
    printf("Konngara %s asset/audio error\n%s",part,music_error());
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
    upper_hud_render(lives, rem_bombs, stage_timer, boss ? UpperHudMode::Boss : UpperHudMode::Regular);
    if (stage_has_singyoku(current_scene,current_stage)) {
        const auto &s = singyoku_state();
        upper_hud_boss(s.health.hp, s.hp_displayed);
    } else if(stage_has_konngara(current_scene,current_stage)) {
        const auto &s=konngara.state();
        upper_hud_boss(s.health.hp,s.hp_displayed,10,16);
    } else upper_hud_boss_hide();
    hud_render(lives, global_stage_num());
}

static void render_frame()
{
    const bool boss = stage_has_singyoku(current_scene, current_stage);
    const bool final_boss=stage_has_konngara(current_scene,current_stage);
    if(!final_boss && !player_death_frame) video_apply_camera();
    stage_render_frame();
    // Erase only previous effects; the background scrolls in hardware and
    // BG0/OAM HUD elements never go through the software canvas.
    canvas_begin_frame();
    canvas_playfield_clip(true);
    if(final_boss) check_konngara(konngara_view.frame(konngara.state()),"frame");
    if(!player_death_frame) player_render_frame();
    shots_render_frame();
    orb_render_frame();
    pellet_render_frame();
    item_render_frame();
    item_rings_frame();
    if(!player_death_frame) popup_render_frame(); // rendering normally ages these
    orb_indicator_render();
    if (boss) boss_render_frame();
    if(player_death_frame) player_render_death(*player_death_frame);
    canvas_playfield_clip(false);
    if(!player_death_frame) render_hud(); // source updates lost-life resources after recovery
    if(player_death_frame) video_apply_death_shake(player_death_frame->shake_y,final_boss);
    if(final_boss) {
        canvas_buffer_prepare(); // after lasers, pellets and all other effects
        do { swiWaitForVBlank(); } while(!konngara_view.present());
    } else {
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
    const bool previous_boss = stage_has_boss(current_scene, current_stage);
    const int entering_score=preserve_score ? stage_score : 0;
    // Remove modal/result text before restoring scenery or revealing it through
    // the next entrance's transparent glyphs. Preserve the fixed upper HUD.
    text_layer_clear();
    player_death_frame=nullptr;
    bgSetScroll(g_bg_id_canvas,0,0);
    konngara_view.end(); // also releases the temporary OBJ lease on navigation/restart
    jigoku_cleared=false;
    upper_hud_invalidate();
    // Route confirmation must survive into the new scene's title glyphs.
    // Other boundaries cancel stale effects; music never owns SFX channels.
    if (!preserve_sfx) sfx_stop();
    // A scene change eventually selects a different native song. Stop the previous
    // scene before its entrance rather than letting it bleed into missing BGM.
    if (scene != current_scene || boss != previous_boss)
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
    if (stage_has_singyoku(scene,stage)) load_boss_background(); else load_background(scene);
    palette_apply_round();

    if (boss) {
        static const uint8_t empty[STAGE_ROWS][STAGE_COLS] = {};
        stage_init(empty, empty);
    } else {
        const ds_stage_layout_t *layout = scene_layout(scene, stage);
        stage_init(layout->obstacle, layout->card);
    }
    stage_score=entering_score;
    stage_reset();
    Pellets.init();
    Items.init();
    timer_init_for(scene, stage);
    orb_initialize();
    player_initialize();
    shots_initialize();
    popup_clear();
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
    render_hud();
    // No fully rendered gameplay frame may expose the orb/cards before the
    // entrance. Later-stage card ripple precedes STAGE NN, as in ReC98.
    video_apply_camera();
    stage_entrance_animate(global_stage_num(), current_stage == 0 || boss);
    wait_for_shot = !boss;
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
    stage_load(0,0);
}

static void player_death_animate(bool fatal)
{
    const auto &player=player_get_state();
    PlayerDeathAnimation animation(fatal,player.x_position,player.y_position,(irand()%8)==0);
    PlayerDeathFrame presentation;
    bool decayed=false;
    if(fatal) Items.init();
    while(animation.next(presentation)) {
        if(presentation.returning && !decayed) {
            shots_initialize();Pellets.decay();decayed=true;
        }
        player_death_frame=&presentation;
        if(presentation.returning)
            camera.follow((presentation.left[0]+presentation.left[1]+PLAYER_W)/2);
        // Game-over composition commits at the END of the frame, as does the
        // final-boss renderer. Keep the approved miss path's original timing.
        if(!fatal && !stage_has_konngara(current_scene,current_stage)) swiWaitForVBlank();
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
    const bool entrance=stage_has_singyoku(current_scene,current_stage) && singyoku_state().phase==SG_ENTRANCE;
    if(entrance) singyoku_update();
    else player_update(keys_held,keys_down);
    if(!serving && !entrance) {
        // Boss collision uses the previous orb point, before orb movement.
        if(final_boss) {
            konngara.update();
            if(konngara.state().complete) {
                player_is_hit=false; // victory wins a simultaneous player hit
                Pellets.init();shots_initialize();Items.init();
                if(stage_score>hiscore) hiscore=stage_score;
                hud_render(lives,20); // includes the lethal-hit50000 immediately
                check_konngara(konngara_view.defeat(konngara.state().health.invincible),"defeat");
                for(int f=0;f<120;++f) swiWaitForVBlank(); // source ending handoff delay
                text_layer_clear();
                text_layer_puts(7,4,"JIGOKU CLEARED",TEXT_PAL_WHITE);
                text_layer_puts(3,6,"ENDING NOT IMPLEMENTED",TEXT_PAL_WHITE);
                text_layer_puts(4,9,"A/B/START: NEW GAME",TEXT_PAL_WHITE);
                jigoku_cleared=true;
                return;
            }
            konngara.update_lasers();
        } else if(boss) {
            singyoku_update();
            if(singyoku_state().phase==SG_DEFEATED) {
                player_is_hit=false;
                music_stop();
                Pellets.init();shots_initialize();Items.init();render_frame();
                const RouteChoice route=boss_defeat_and_select_route();
                stage_load(route_first_scene(route),0,true);
                return;
            }
        }
        shots_update();orb_update();
        if(!boss) { cards_hittest();stage_obstacles_update();cards_update(); }
        Items.update();
        // Only active gameplay advances the shared timer cadence.
        if(timer_update(++frame)==TIMER_EVENT_HARRYUP) harryup_animate();
        if(Pellets.update()) player_is_hit=true;
    }
    if(!entrance && !player_is_hit && orb_hits_player(player_get_state().x_position,
        player_get_state().y_position,OR_NONE,player_is_invincible(),player_is_invincible_to_orb())) player_is_hit=true;
    if(!boss && stage_is_cleared()) player_is_hit=false; // source stage-clear wins simultaneous contact
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
            if(!final_boss) swiWaitForVBlank(); // hold the last effect before recovery
        }
    }
    if(!boss && stage_is_cleared()) {
        stagebonus_run(stagebonus_compute(global_stage_num(),lives),global_stage_num());
        int next=current_stage+1;
        if(next>=scene_stage_count(current_scene)) next=0; // other unported bosses remain gated
        stage_load(current_scene,next,false,stage_has_konngara(current_scene,next));
        if(stage_has_konngara(current_scene,current_stage)) return;
    }
    if(stage_score>hiscore) hiscore=stage_score;
    camera.follow(player_center_x());render_frame();
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
        if(!stage_has_konngara(current_scene,current_stage)) swiWaitForVBlank();

        scanKeys();
        uint16_t keys_held = keysHeld();
        uint16_t keys_down = keysDown();

        // Dev stage navigator (SELECT + d-pad).
        if (keys_held & KEY_SELECT) {
            if (keys_down & KEY_LEFT) {
                int s = current_stage - 1;
                if (s < 0)
                    s = scene_stage_count(current_scene) - 1;
                stage_load(current_scene, s);
                continue;
            }
            if (keys_down & KEY_RIGHT) {
                stage_load(current_scene, (current_stage + 1) % scene_stage_count(current_scene));
                continue;
            }
            if (keys_down & KEY_UP) {
                const int scene = (current_scene + SCENES - 1) % SCENES;
                stage_load(scene, current_stage < scene_stage_count(scene) ? current_stage : scene_stage_count(scene) - 1);
                continue;
            }
            if (keys_down & KEY_DOWN) {
                const int scene = (current_scene + 1) % SCENES;
                stage_load(scene, current_stage < scene_stage_count(scene) ? current_stage : scene_stage_count(scene) - 1);
                continue;
            }
        }

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
