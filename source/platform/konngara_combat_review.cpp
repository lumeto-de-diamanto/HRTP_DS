#include "konngara_combat_render.hpp"
#include "main/boss/konngara.hpp"
#include "stage_entrance.hpp"
#include "stage_render.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "player_render.hpp"
#include "shot_render.hpp"
#include "orb_render.hpp"
#include "pellet_render.hpp"
#include "upper_hud.hpp"
#include "hud_render.hpp"
#include "text_layer.hpp"
#include "music.hpp"
#include "sound_effects.hpp"
#include "subscreen.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/bullet/pellet.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/timer.hpp"

bool konngara_combat_review(int start_set)
{
    KonngaraCombatView view;
    stage_entrance_prepare();
    camera.snap_center();
    if (!view.begin()) return false;
    struct Cleanup {
        KonngaraCombatView &view;
        ~Cleanup() {
            sfx_stop(); music_stop();
            CanvasForegroundScope foreground;
            canvas_playfield_clip(false);
            canvas_fill_rect(0, 0, VIEW_W, VIEW_H, 0);
            view.end();
            stage_entrance_prepare(); text_layer_clear(); hud_invalidate();upper_hud_invalidate();
        }
    } cleanup{view};
    sfx_stop(); music_stop();
    if (!music_play_track(NATIVE_ALICE)) { canvas_clear(); return false; }
    KonngaraCombat boss;
    boss.reset(start_set); player_initialize(); orb_initialize(); shots_initialize(); Pellets.init();
    if (!view.frame(boss.state())) return false;
    text_layer_clear(); hud_invalidate(); timer_init_for(6, 4);
    int lives = 3;
    unsigned frame = 0;
    bool finished = false;
    int legend = 0;
    while (true) {
        scanKeys();
        const unsigned held = keysHeld(), down = keysDown();
        if (down & KEY_START) break; // review exit, not the normal pause menu
        if (finished) {
            if (down & (KEY_A | KEY_B)) break;
            swiWaitForVBlank();
            continue;
        }
        player_update(held, down);
        boss.update(); // original previous-orb point, before orb movement
        if(boss.state().complete) {
            // The source enters its blocking defeat before any further orb,
            // player, pellet or timer update. Do not turn a fatal hit into a miss.
            Pellets.init();shots_initialize();player_is_hit=false;
            if(stage_score>hiscore) hiscore=stage_score;
            hud_render(lives,20); // lethal-hit score/bonus is visible throughout defeat
            subscreen_fill_rect(8,140,248,16,SUB_COL_BG);
            subscreen_text(8,140,"DEFEAT SEQUENCE",SUB_COL_DIM);
            if(!view.defeat(boss.state().health.invincible)) return false;
            subscreen_fill_rect(8,140,248,16,SUB_COL_BG);
            subscreen_text(8,140,"DEFEAT REVIEW COMPLETE",SUB_COL_DIM);
            text_layer_puts(3,7,"DEFEAT REVIEW COMPLETE",TEXT_PAL_WHITE);
            text_layer_puts(3,9,"A/B: RETURN  START: EXIT",TEXT_PAL_WHITE);
            finished=true;continue;
        }
        boss.update_lasers();
        shots_update(); orb_update();
        if (Pellets.update()) player_is_hit = true;
        if (!player_is_hit && orb_hits_player(player_get_state().x_position, player_top,
            OR_NONE, player_is_invincible(), player_is_invincible_to_orb())) player_is_hit = true;
        if (player_is_hit) {
            --lives;
            if (lives >= 0) {
                mdrv2_se_play(SFX_MISS);
                player_miss(); shots_initialize(); Pellets.init(); // boss miss preserves the orb
            }
            player_is_hit = false;
        }
        if (timer_update(++frame) == TIMER_EVENT_HARRYUP) harryup_animate();
        camera.follow(player_center_x());
        canvas_begin_frame(); canvas_playfield_clip(true);
        player_invincibility_render_frame();
        if (!view.frame(boss.state())) { canvas_clear(); return false; }
        player_render_frame(); shots_render_frame(); orb_render_frame();
        pellet_render_frame(); orb_indicator_render();
        canvas_playfield_clip(false);
        upper_hud_render(lives < 0 ? 0 : lives, 2, stage_timer, UpperHudMode::Boss);
        upper_hud_boss(boss.state().health.hp, boss.state().hp_displayed, 10, 16);
        hud_render(lives, 20);
        if (legend != boss.state().phase) {
            subscreen_fill_rect(8, 140, 248, 16, SUB_COL_BG);
            const char *labels[] = {"PHASE 1 PRACTICE  START: EXIT", "PHASE 2 TRANSITION START: EXIT", "PHASE 3 PRACTICE  START: EXIT",
                                    "PHASE 4 TRANSITION START: EXIT", "PHASE 5 PRACTICE  START: EXIT",
                                    "PHASE 6 TRANSITION START: EXIT", "PHASE 7 FINAL MIX START: EXIT"};
            subscreen_text(8, 140, labels[boss.state().phase - 1], SUB_COL_DIM);
            legend = boss.state().phase;
        }
        if (lives < 0) {
            finished = true;
            Pellets.init(); shots_initialize();
            text_layer_puts(3, 7,"PRACTICE OVER",TEXT_PAL_WHITE);
            text_layer_puts(3, 9, "A/B: RETURN  START: EXIT", TEXT_PAL_WHITE);
        }
        // Flush only after BOTH lasers and pellets have finished composing.
        // Present one coherent frame, rather than clearing/writing the live
        // effects bitmap and changing palettes while it is being scanned out.
        canvas_buffer_prepare();
        do { swiWaitForVBlank(); } while (!view.present());
    }
    return true;
}
