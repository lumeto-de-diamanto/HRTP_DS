// Real player/orb/projectiles + hardware-scrolled combat/review lifecycle.
#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include "heap_probe.hpp"
#include "konngara_combat_render.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "video.hpp"
#include "background.hpp"
#include "music.hpp"
#include "upper_hud.hpp"
#include "boss_gfx.hpp"
#include "boss_render.hpp"
#include "font16x16.hpp"
#include "main/boss/singyoku.hpp"
#include "main/random.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/bullet/pellet.hpp"
#include "main/stage/stageobj.hpp"
#include "sprites/Miko.h"
#include "sprites/Shape8x8.h"

extern std::vector<int> test_effects;
extern std::string test_practice_label;

namespace {
bool allow_music = true;
bool checkpoint, saw_checkpoint,block_defeat_allocations;
int start_set, phase3_frames, phase5_frames, phase7_frames, misses_checked;
int defeat_frames,death_orb_x,death_orb_y,death_player_x,death_player_y,death_camera,death_score;
size_t death_effects;
int starts, stops, sfx_stops, ticks;
int scene_plays[7];
size_t baseline_heap, baseline_blocks, heap_attempts, obj_attempts, owned_bytes;

void check_pause_dimming()
{
    assert(REG_BLDCNT==(BLEND_FADE_BLACK|BLEND_SRC_BG1|BLEND_SRC_BG2|
        BLEND_SRC_BG3|BLEND_SRC_SPRITE|BLEND_SRC_BACKDROP));
    assert(REG_BLDY.written==10 && video_fade()==10 && (REG_WINOUT&0x20) && (REG_WININ&0x2020)==0x2020 && REG_MASTER_BRIGHT==0);
    assert(REG_MASTER_BRIGHT_SUB==0x800a);
}

void observe()
{
    ++ticks;
    assert(ticks <= (checkpoint ? 3000 : 400));
    if(test_practice_label=="DEFEAT SEQUENCE") {
        assert(checkpoint && ++defeat_frames<=421);
        if(defeat_frames==1) {
            death_orb_x=orb_get_state().x_position;death_orb_y=orb_get_state().y_position;
            death_player_x=player_get_state().x_position;death_player_y=player_get_state().y_position;
            death_camera=camera.left;death_score=stage_score;death_effects=test_effects.size();
            const int lethal_score=(18-start_set*3)*10000+50000;
            assert(death_score>=lethal_score && death_score<lethal_score+10000 && hiscore>=death_score);
            assert(test_heap_count==baseline_blocks+3 && test_heap_bytes==baseline_heap+owned_bytes);
            assert(test_obj_count==80);
        } else {
            assert(test_heap_count==baseline_blocks+1 && test_heap_bytes==baseline_heap+262144);
            assert(test_obj_count==79 && test_obj_used==27840);
            assert(!test_bgs[0].visible && !test_bgs[1].visible && !test_bgs[2].visible);
            for(int i=0;i<128;++i) assert(test_objs[i].hidden);
        }
        assert(test_heap_attempts==heap_attempts && test_obj_attempts==obj_attempts && g_canvas_target);
        assert(orb_get_state().x_position==death_orb_x && orb_get_state().y_position==death_orb_y);
        assert(player_get_state().x_position==death_player_x && player_get_state().y_position==death_player_y);
        assert(camera.left==death_camera && stage_score==death_score && Pellets.alive_count()==0);
        test_keys_down=test_keys_held=0;return;
    }
    if(test_practice_label=="DEFEAT REVIEW COMPLETE") {
        assert(defeat_frames==421 && stage_score==death_score);
        assert(test_effects.size()==death_effects+44);
        assert(std::all_of(test_effects.begin()+death_effects,test_effects.end(),[](int id){return id==7;}));
        assert(test_heap_count==baseline_blocks+1 && test_heap_bytes==baseline_heap+262144);
        assert(test_obj_count==79 && test_obj_used==27840 && test_bgs[0].visible);
        const u16 d=(1+('D'-0x20)*2)|(14<<12);
        saw_checkpoint=BG_MAP_RAM(30)[14*32+3]==d;
        assert(saw_checkpoint);test_keys_down=KEY_A;test_keys_held=0;return;
    }
    assert(test_bgs[3].type == BgType_Bmp8 && test_bgs[3].size == BgSize_B8_512x256);
    assert(test_bgs[3].priority == 3 && test_bgs[2].priority == 0 && test_bgs[0].priority == 0);
    assert(REG_WININ == 0x1919 && camera.left >= 0 && camera.left <= 64);
    assert(test_bgs[3].x == camera.left && test_bgs[3].y == 0);
    assert(test_objs[BOSS_OAM_SLOT].format == SpriteColorFormat_256Color);
    assert(test_heap_count == baseline_blocks + 3 && test_heap_bytes == baseline_heap + owned_bytes);
    assert(test_heap_attempts == heap_attempts && test_obj_attempts == obj_attempts);
    assert(test_obj_count==80 && test_obj_used==31936 && g_canvas_target);
    if(ticks==1) assert(canvas_buffer_ready()); // hook runs immediately BEFORE commit
    if (checkpoint) {
        if (ticks % 100 == 1) player_miss(); // test-only invincibility, not an AI shortcut
        if (test_practice_label.find("PHASE 3") == 0) ++phase3_frames;
        if (test_practice_label.find("PHASE 5") == 0) ++phase5_frames;
        if (test_practice_label.find("PHASE 7") == 0) ++phase7_frames;
        if(block_defeat_allocations && phase7_frames>300) test_heap_fail_after=0;
        const bool hit = (start_set == 0 && ticks <= 90) || (phase3_frames > 0 && phase3_frames <= 90) ||
                          (phase5_frames > 0 && phase5_frames <= 90) || (phase7_frames > 0 && phase7_frames <= 400);
        orb_set_position(hit ? 300 : 600, hit ? 130 : 80);
        test_keys_held = 0;
        test_keys_down = 0;
    } else {
        if (ticks == 60) { orb_set_position(123,147); player_is_hit = true; }
        if (ticks == 61) {
            const auto &orb = orb_get_state();
            assert(orb.x_position < 200 && orb.y_position < 250); // no boss re-serve on real miss path
            ++misses_checked;
        }
        test_keys_held = KEY_B | (ticks % 160 < 80 ? KEY_LEFT : KEY_RIGHT);
        test_keys_down = ticks == 400 ? KEY_START : 0;
    }
}
}

int test_mima_music_starts=0,test_mima_pauses=0,test_mima_resumes=0,test_mima_stops=0,test_mima_fades=0;
bool test_mima_music_allowed=true;
void music_pause() { ++test_mima_pauses; }
void music_resume() { ++test_mima_resumes; }
bool music_play_track(NativeTrackId id)
{
    if(id==NATIVE_LEGEND) { ++test_mima_music_starts;return test_mima_music_allowed; }
    if(id==NATIVE_POSITIVE) { ++starts;return allow_music; }
    assert(id == NATIVE_ALICE);
    ++starts;
    test_keys_down = test_keys_held = 0;
    test_practice_label.clear(); phase3_frames = phase5_frames = phase7_frames = defeat_frames = 0;
    // Two caches + combined BG3 backup/effects staging are three allocations.
    heap_attempts = test_heap_attempts;
    obj_attempts = test_obj_attempts;
    return allow_music;
}
void music_stop() { ++stops;++test_mima_stops; }
void music_fade_out_nonblock() { ++test_mima_fades; }
void sfx_stop() { ++sfx_stops; }
const char *music_error() { return ""; }
void music_play_scene(int scene) { assert(scene>=0 && scene<7);++scene_plays[scene]; }

void test_konngara_review()
{
    load_background(0);
    std::ifstream asset("nitro:/konngara_combat.bin", std::ios::binary | std::ios::ate);
    std::ifstream slash("nitro:/konngara_slash.bin", std::ios::binary | std::ios::ate);
    std::ifstream defeat("nitro:/konngara_defeat.bin",std::ios::binary|std::ios::ate);
    assert(asset.good());
    assert(slash.good());
    assert(defeat.good());
    owned_bytes = size_t(asset.tellg()) + size_t(slash.tellg()) + 262144;
    const std::vector<u16> resident(BG_BMP_RAM(4), BG_BMP_RAM(4) + 65536);
    const std::vector<u16> bg_palette(BG_PALETTE, BG_PALETTE + 256), obj_palette(SPRITE_PALETTE, SPRITE_PALETTE + 256);
    baseline_heap = test_heap_bytes; baseline_blocks = test_heap_count;
    obj_attempts = test_obj_attempts;
    for (int fail = 0; fail < 3; ++fail) {
        test_heap_fail_after = fail;
        assert(!konngara_combat_review());
        assert(starts == 0 && test_heap_count == baseline_blocks && test_heap_bytes == baseline_heap);
        assert(test_bgs[3].type == BgType_Bmp8);
        assert(std::equal(resident.begin(), resident.end(), BG_BMP_RAM(4)));
        assert(std::equal(bg_palette.begin(), bg_palette.end(), BG_PALETTE));
        assert(std::equal(obj_palette.begin(), obj_palette.end(), SPRITE_PALETTE));
    }
    test_heap_fail_after = -1;
    test_obj_fail_after = 0;
    assert(!konngara_combat_review());
    test_obj_fail_after = -1;
    assert(test_heap_count==baseline_blocks && test_heap_bytes==baseline_heap);
    assert(test_obj_count==79 && test_obj_used==27840 && !g_canvas_target);
    assert(std::equal(resident.begin(),resident.end(),BG_BMP_RAM(4)));
    assert(std::equal(bg_palette.begin(),bg_palette.end(),BG_PALETTE));
    assert(std::equal(obj_palette.begin(),obj_palette.end(),SPRITE_PALETTE));
    test_vblank_hook = observe;
    for (int replay = 0; replay < 40; ++replay) {
        start_set = replay / 10;
        initialize_irand(100 + replay);
        ticks = 0;
        assert(konngara_combat_review(start_set) && ticks == 400);
        assert(test_heap_count == baseline_blocks && test_heap_bytes == baseline_heap);
        assert(test_obj_attempts == obj_attempts);
        assert(test_bgs[3].type == BgType_Bmp8 && test_bgs[3].size == BgSize_B8_512x256);
        assert(REG_WININ == 0x1919 && test_bgs[0].priority == 0 && test_objs[OAM_SLOT_ORB_ARROW].priority == 0);
        assert(std::equal(resident.begin(), resident.end(), BG_BMP_RAM(4)));
        assert(std::equal(bg_palette.begin(), bg_palette.end(), BG_PALETTE));
        assert(std::equal(obj_palette.begin(), obj_palette.end(), SPRITE_PALETTE));
        assert(test_objs[BOSS_OAM_SLOT].hidden);
        for(int slot=75;slot<107;++slot) assert(test_objs[slot].hidden);
        assert(test_obj_count==79 && test_obj_used==27840 && !g_canvas_target);
        assert(std::all_of(canvas_at(0, 0), canvas_at(0, VIEW_H), [](u16 p) { return p == 0; }));
    }
    assert(misses_checked == 40);
    for (int direct : {0,1,2,3}) {
        start_set = direct;
        checkpoint = true; saw_checkpoint = false; ticks = 0; stage_score = 0; test_effects.clear();
        initialize_irand(42);
        assert(konngara_combat_review(direct) && saw_checkpoint && ticks > 300 && ticks <= 3000);
        // The real orb may also shatter pellets for10 points during this run.
        const int hits = 18 - direct*3;
        assert(std::count(test_effects.begin(), test_effects.end(), 3) == hits);
        assert(stage_score >= hits * 10000+50000 && stage_score < (hits + 1) * 10000+50000);
        assert((direct==3 || phase5_frames>200) && phase7_frames>300);
        assert(test_heap_count == baseline_blocks && test_heap_bytes == baseline_heap);
        assert(std::equal(resident.begin(), resident.end(), BG_BMP_RAM(4)));
        // A same-mode boss return must not leave cached HUD sprites hidden by
        // the fullscreen defeat. No reallocation or mode switch is required.
        upper_hud_render(3,2,100,UpperHudMode::Boss);upper_hud_boss(8,8,2,6);
        assert(!test_objs[OAM_SLOT_HUD_LIVES].hidden && !test_objs[OAM_SLOT_HUD_BOMBS].hidden && !test_objs[OAM_SLOT_HUD_HP].hidden);
    }
    // No large allocation is permitted at the lethal hit: the existing staging
    // buffer must carry all421 frames even after the heap becomes unavailable.
    block_defeat_allocations=true;start_set=3;ticks=0;stage_score=0;saw_checkpoint=false;
    initialize_irand(42);assert(konngara_combat_review(3));
    block_defeat_allocations=false;test_heap_fail_after=-1;
    assert(saw_checkpoint && defeat_frames==421 && stage_score>=140000 && stage_score<150000);
    assert(test_heap_count==baseline_blocks && test_heap_bytes==baseline_heap && !g_canvas_target);
    assert(test_obj_count==79 && test_obj_used==27840);
    assert(std::equal(resident.begin(),resident.end(),BG_BMP_RAM(4)));
    checkpoint = false;
    test_vblank_hook = nullptr;
    allow_music = false;
    assert(!konngara_combat_review());
    allow_music = true;
    assert(test_heap_count == baseline_blocks && test_heap_bytes == baseline_heap);
    assert(std::equal(resident.begin(), resident.end(), BG_BMP_RAM(4)));
    assert(REG_WININ == 0x1919 && test_bgs[3].type == BgType_Bmp8);
    assert(starts == 46 && stops >= 92 && sfx_stops >= 92);
    assert(std::equal(bg_palette.begin(), bg_palette.end(), BG_PALETTE));
    assert(std::equal(obj_palette.begin(), obj_palette.end(), SPRITE_PALETTE));
    // Returning to SinGyoku must replace the shared 8bpp texture with the
    // original 4bpp sheet, not retain a stale image/format cache.
    load_boss_background(); boss_render_hide(); singyoku_init(); boss_render_frame();
    assert(test_objs[BOSS_OAM_SLOT].format == SpriteColorFormat_16Color);
    assert(test_objs[BOSS_OAM_SLOT].gfx == boss_gfx() && test_obj_attempts == obj_attempts);
    const std::vector<u16> singyoku(boss_gfx(), boss_gfx() + 1024);
    assert(boss_gfx_init() && boss_render_init());
    assert(std::equal(singyoku.begin(), singyoku.end(), boss_gfx()));
    boss_render_hide();
    test_keys_down = test_keys_held = 0;
    camera.snap_center();
    std::puts("Konngara review: 40 replays/orb preservation, five real421-frame defeats including denied late allocations, frozen gameplay/44 SFX7/+50000 once, restored heap/OBJ/palettes/background and SinGyoku handoff passed");
}

// Exercise the ACTUAL normal-game stage loader/tick, not a second model of it.
// Startup/main's infinite loop is excluded; all renderer/gameplay code is real.
#define HRTP_GAME_FLOW_TEST
#include "../source/main.cpp"
#undef HRTP_GAME_FLOW_TEST

namespace {
int bonus_from_stage,bonus_expected_score,bonus_visible_frames;
int bonus_fade_frames,bonus_black_frames,bonus_kanji_frames,bonus_waits,bonus_frozen_timer;
unsigned bonus_frozen_frame;
bool regular_background_tracked,regular_dismiss_overrun,regular_init_overrun,regular_banner_overrun;
int regular_commits,regular_incoming_waits,regular_entrance_frames;
std::vector<u16> regular_background;

void check_regular_background()
{
    assert(std::equal(regular_background.begin(),regular_background.end(),BG_BMP_RAM(4)));
}

void observe_regular_dma()
{
    if(!regular_background_tracked) return;
    // Observe intermediate clears too: re-uploading the same file must not
    // pass merely because the final background bytes happen to match.
    check_regular_background();
    if(current_stage==bonus_from_stage) {
        // Simulate scanout advancing AFTER the menu's own VBlank gate, while
        // it erases the dismissed panel. Never force its wait loop to retry.
        if(bonus_visible_frames==29 && !regular_dismiss_overrun &&
           (REG_BLDCNT&0x3f)==BLEND_SRC_BG0) {
            regular_dismiss_overrun=true;REG_VCOUNT=80;
        }
        return;
    }
    if(!regular_commits && !regular_init_overrun) {
        regular_init_overrun=true;REG_VCOUNT=80; // loading/HUD work missed VBlank
    } else if(regular_incoming_waits==regular_entrance_frames && !regular_banner_overrun) {
        regular_banner_overrun=true;REG_VCOUNT=80; // final banner erase ran long
    }
}

void observe_regular_scroll()
{
    if(!regular_background_tracked) return;
    assert(REG_VCOUNT>=192 && REG_VCOUNT<200);
    assert(current_stage==bonus_from_stage+1 && camera.left==32);
    assert(test_bgs[1].x==32 && test_bgs[3].x==32);
    assert(!test_bgs[1].y && !test_bgs[3].y && !test_bgs[2].y);
    check_regular_background();
    assert(++regular_commits<=2); // entrance, then ONE first-serve frame
}

int route_expected_score,route_result_frames,route_result_timer;
unsigned route_result_frame;
size_t route_sfx_start;
int route_target_scene,route_exit_fade_frames,route_black_waits,route_kanji_frames;
int route_cleanup_commits,route_load_commits;
bool route_first_mask;

void observe_route_commit()
{
    if(route_exit_fade_frames!=16 || route_first_mask) return;
    // All result restoration and scene-loading commits must stay hidden, not
    // briefly restore the old boss or reveal the arena ahead of the mask.
    assert(REG_MASTER_BRIGHT==0x8010 && REG_MASTER_BRIGHT_SUB==0x8010);
    if(current_scene==0) ++route_cleanup_commits;
    else {
        assert(current_scene==route_target_scene && current_stage==0 && camera.left==32);
        ++route_load_commits;
        if(test_bgs[2].sx==32 && test_bgs[2].y==CANVAS_H) {
            assert(REG_VCOUNT>=192 && REG_VCOUNT<200 && route_black_waits==1);
            const u16 *glyph=font16x16_find(JIS_TOUHOU_REIIDEN[0]);assert(glyph);
            for(int y=0;y<24;++y) for(int x=0;x<32;++x) {
                const bool ink=x>=8 && x<24 && y>=4 && y<20 && (glyph[y-4]&(0x8000>>(x-8)));
                assert(*canvas_at(x,CANVAS_H+y)==(ink ? 0 : 0x8000));
            }
            route_first_mask=true;
        }
    }
}

void observe_route_handoff()
{
    test_keys_down=KEY_B;test_keys_held=0;
    if((REG_BLDCNT&(3<<6))==BLEND_FADE_BLACK) {
        assert(REG_BLDCNT==(BLEND_FADE_BLACK|BLEND_SRC_BG1|BLEND_SRC_BG3|BLEND_SRC_SPRITE|BLEND_SRC_BACKDROP));
        assert(REG_BLDY.written==10 && REG_WININ==0x3d3d && REG_MASTER_BRIGHT==0);
        assert(REG_MASTER_BRIGHT_SUB==0x800a);
        if(route_target_scene==2) test_keys_down|=KEY_DOWN;
    }
    if(test_bgs[0].map==31) {
        assert(++route_result_frames<1200);
        assert(stage_score==route_expected_score && hiscore>=stage_score);
        assert(frame==route_result_frame && stage_timer==unsigned(route_result_timer));
        assert(std::find(test_effects.begin()+route_sfx_start,test_effects.end(),SFX_ROUTE_CONFIRM)!=test_effects.end());
        if(REG_MASTER_BRIGHT&31) {
            assert(++route_exit_fade_frames<=16 && REG_MASTER_BRIGHT==(0x8000|route_exit_fade_frames));
            assert(REG_MASTER_BRIGHT_SUB==REG_MASTER_BRIGHT && test_bgs[3].map==16);
            assert(BG_MAP_RAM(31)[22*32+10]&1023); // finished tally/prompt still displayed
        }
    } else if(current_scene==route_target_scene) {
        assert(current_stage==0 && stage_score==route_expected_score && frame==route_result_frame);
        assert(route_exit_fade_frames==16);
        if(!route_first_mask) {
            assert(REG_MASTER_BRIGHT==0x8010 && ++route_black_waits==1);
            assert(REG_MASTER_BRIGHT_SUB==0x8010);
            assert(test_bgs[2].sx==32 && test_bgs[2].y==CANVAS_H);
        } else {
            assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);
            if(test_bgs[2].sx==32) {
                assert(++route_kanji_frames<=48 && test_bgs[2].y==CANVAS_H);
                assert(test_bgs[2].x==((route_kanji_frames-1)/8%2)*32);
            }
        }
    }
}

void observe_bonus_award()
{
    assert(stage_score==bonus_expected_score && hiscore>=bonus_expected_score);
    test_keys_down=test_keys_held=0;
    if(regular_background_tracked) {
        check_regular_background();
        if(current_stage!=bonus_from_stage) {
            assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);
            assert(frame==bonus_frozen_frame);
            if(regular_commits) ++regular_incoming_waits;
        }
    }
    assert(++bonus_waits<1200);
    if(bonus_waits==1) { bonus_frozen_timer=stage_timer;bonus_frozen_frame=frame; }
    if(current_stage==bonus_from_stage) {
        assert(stage_timer==unsigned(bonus_frozen_timer) && frame==bonus_frozen_frame);
        if(REG_MASTER_BRIGHT&31) {
            assert(bonus_from_stage==3 && ++bonus_fade_frames<=16);
            assert(REG_MASTER_BRIGHT==(0x8000|bonus_fade_frames));
            assert(!REG_MASTER_BRIGHT_SUB);
        }
    } else if(REG_MASTER_BRIGHT&31) {
        assert(bonus_from_stage==3 && ++bonus_black_frames==1);
        assert(current_stage==4 && REG_MASTER_BRIGHT==0x8010 && !REG_MASTER_BRIGHT_SUB);
        assert(test_bgs[2].sy==32 && test_bgs[2].y==CANVAS_H);
        assert(bonus_fade_frames==16); // never expose a partially loaded arena
    } else if(test_bgs[2].sy==32) {
        assert(bonus_from_stage==3 && bonus_black_frames==1);
        assert(!REG_MASTER_BRIGHT_SUB);
        ++bonus_kanji_frames;
    }
    if(current_stage==bonus_from_stage && (REG_BLDCNT&0x3f)==BLEND_SRC_BG0) {
        // Hold the result for several input frames, then confirm with both
        // buttons held. Neither drawing nor repeated input may add it again.
        if(++bonus_visible_frames>=29) {
            test_keys_down=test_keys_held=KEY_A|KEY_B;
        }
    }
}

bool stage20_entering;
int stage20_entry_waits,stage20_defeat_waits,stage20_entry_frame;
int stage20_fade_waits,stage20_black_waits,stage20_fade_timer;
int stage20_carry_score;
int stage20_frozen_score,stage20_frozen_timer,stage20_frozen_frame;

void observe_stage20()
{
    if(!stage_has_konngara(current_scene,current_stage)) {
        test_keys_down=KEY_A; // dismiss the existing Stage19 bonus screen
        if(stage20_entering && (REG_MASTER_BRIGHT&31)) {
            assert(current_scene==6 && current_stage==3);
            assert(++stage20_fade_waits<=16 && REG_MASTER_BRIGHT==(0x8000|stage20_fade_waits));
            assert(!REG_MASTER_BRIGHT_SUB && stage_score==stage20_carry_score);
            assert(lives==6 && rem_bombs==4 && frame==unsigned(stage20_entry_frame));
            if(stage20_fade_waits==1) stage20_fade_timer=stage_timer;
            assert(stage_timer==unsigned(stage20_fade_timer));
        }
        return;
    }
    test_keys_down=0;
    if(stage20_entering) {
        ++stage20_entry_waits;
        assert(stage_score==stage20_carry_score && lives==6 && rem_bombs==4 && stage_timer==8000);
        assert(frame==unsigned(stage20_entry_frame) && konngara.state().frame==0 && konngara.state().health.hp==18);
        assert(REG_WININ==0x1D1D); // intro mask retained through first combat commit
        assert(stage20_fade_waits==16 && !REG_MASTER_BRIGHT_SUB);
        if(stage20_entry_waits==1) {
            assert(REG_MASTER_BRIGHT==0x8010 && ++stage20_black_waits==1);
            // First black-in frame is complete and opaque, not a transparent
            // canvas exposing the Stage20 arena during loading.
            assert(!g_canvas_target);
            for(int y=0;y<VIEW_H;++y) for(int x=0;x<VIEW_W;++x)
                assert(*canvas_at(x,y)&0x8000);
        } else assert(!REG_MASTER_BRIGHT);
    } else if(konngara.state().complete) {
        if(!stage20_defeat_waits) {
            stage20_frozen_score=stage_score;stage20_frozen_timer=stage_timer;stage20_frozen_frame=frame;
            assert(stage_score>=stage20_carry_score+230000 && stage_score<stage20_carry_score+240000 && hiscore>=stage_score);
            const auto *sub=reinterpret_cast<const u8*>(BG_BMP_RAM_SUB(0));
            for(int y=HUD_RADAR_TOP;y<HUD_RADAR_TOP+HUD_RADAR_H;++y)
                for(int x=HUD_RADAR_LEFT;x<HUD_RADAR_LEFT+HUD_RADAR_W;++x)
                    assert(sub[y*256+x]!=SUB_COL_RADAR_BOSS && sub[y*256+x]!=SUB_COL_RADAR_ITEM);
        }
        assert(++stage20_defeat_waits<=541);
        assert(stage_score==stage20_frozen_score && stage_timer==unsigned(stage20_frozen_timer) && frame==unsigned(stage20_frozen_frame));
        assert(lives==5 && rem_bombs==5 && !player_is_hit);
    }
}
}

void test_stage_bonus_awards()
{
    const int saved_high=hiscore;
    for(int stage:{0,1,2,3}) {
        test_vblank_hook=nullptr;stage_load(0,stage);
        lives=6;rem_bombs=5;stage_score=123450;hiscore=100;
        stage_timer=1000;stage_cardcombo_max=7;
        const StageBonus bonus=stagebonus_compute(stage_number(0,stage),lives);
        assert(bonus.total==(3000+700+1700+stage*200)*10);
        bonus_from_stage=stage;bonus_expected_score=stage_score+bonus.total;bonus_visible_frames=0;
        bonus_fade_frames=bonus_black_frames=bonus_kanji_frames=bonus_waits=0;
        regular_background_tracked=stage<3;
        regular_dismiss_overrun=regular_init_overrun=regular_banner_overrun=false;
        regular_commits=regular_incoming_waits=0;
        if(regular_background_tracked) {
            camera.left=stage%2 ? 64 : 0;video_apply_camera();
            regular_background.assign(BG_BMP_RAM(4),BG_BMP_RAM(4)+65536);
            int cards=0;
            const auto *layout=scene_layout(0,stage+1);
            for(const auto &row:layout->card) for(auto card:row) cards+=bool(card);
            regular_entrance_frames=(cards ? cards+13 : 0)+70;
        }
        stage_cards_removed_count=stage_card_total;
        test_dma_fill_hook=observe_regular_dma;test_bg_update_hook=observe_regular_scroll;
        test_vblank_hook=observe_bonus_award;
        gameplay_tick(0,0);
        test_vblank_hook=nullptr;test_dma_fill_hook=nullptr;test_bg_update_hook=nullptr;
        test_keys_down=test_keys_held=0;
        assert(bonus_visible_frames==29 && current_scene==0 && current_stage==stage+1);
        assert(bonus_fade_frames==(stage==3 ? 16 : 0));
        assert(bonus_black_frames==(stage==3 ? 1 : 0));
        assert(bonus_kanji_frames==(stage==3 ? 48 : 0));
        assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);
        if(regular_background_tracked) {
            assert(regular_dismiss_overrun && regular_init_overrun && regular_banner_overrun && regular_commits==2);
            assert(regular_incoming_waits==regular_entrance_frames+1); // safe first-serve resync
            check_regular_background();
            assert(wait_for_shot && !test_objs[0].hidden);
        }
        regular_background_tracked=false;
        assert(stage_score==bonus_expected_score && hiscore==bonus_expected_score);
        for(int tick=0;tick<3;++tick) gameplay_tick(0,0);
        assert(stage_score==bonus_expected_score); // next stage/entrance cannot repeat the award
    }
    // The actual render loop must publish the same offscreen boss marker,
    // then suppress it throughout a subsequent regular-stage entrance.
    stage_load(0,4,false,true);
    auto &offscreen_boss=const_cast<SinGyokuState&>(singyoku_state());
    offscreen_boss.phase=SG_SPHERE;offscreen_boss.entity.left=544;offscreen_boss.entity.top=96;
    camera.left=0;render_frame();
    assert(!test_objs[OAM_SLOT_BOSS_ARROW].hidden && !test_objs[OAM_SLOT_BOSS_ARROW].hflip);
    assert(test_objs[OAM_SLOT_BOSS_ARROW].x==247 && test_objs[OAM_SLOT_BOSS_ARROW].y==57);
    assert(test_objs[OAM_SLOT_BOSS_ARROW].gfx!=test_objs[OAM_SLOT_ORB_ARROW].gfx);
    test_vblank_hook=[](){assert(test_objs[OAM_SLOT_BOSS_ARROW].hidden);};
    stage_load(0,0,false,true);test_vblank_hook=nullptr;
    assert(test_objs[OAM_SLOT_BOSS_ARROW].hidden);

    // The natural boss/route transition must not throw away the accumulated
    // card-stage bonus either. Use the real lethal-orb/route UI path.
    for(int target:{1,2}) {
        stage_load(0,4,false,true);
        for(int i=0;i<200;++i) singyoku_update();
        auto &health=const_cast<BossHealth&>(singyoku_state().health);
        health.hp=1;health.invincible=false;health.invincibility_frame=0;
        const auto &entity=singyoku_state().entity;
        orb_set_position(entity.left+24,entity.top+24);
        const int before=stage_score;
        const int round_bonus=roundbonus_compute(5,lives).total;
        route_expected_score=before+3000+round_bonus;route_result_frames=0;
        route_result_timer=stage_timer;route_result_frame=frame;route_sfx_start=test_effects.size();
        route_target_scene=target;route_exit_fade_frames=route_black_waits=route_kanji_frames=0;
        route_cleanup_commits=route_load_commits=0;route_first_mask=false;
        const int old_lives=lives,old_bombs=rem_bombs;
        const int old_starts=starts;
        const int old_scene_plays=scene_plays[target];
        test_vblank_hook=observe_route_handoff;test_bg_update_hook=observe_route_commit;
        gameplay_tick(0,0);
        test_vblank_hook=nullptr;test_bg_update_hook=nullptr;test_keys_down=test_keys_held=0;
        assert(current_scene==target && current_stage==0 && stage_score==before+3000+round_bonus && hiscore>=stage_score);
        assert(route_result_frames>200 && route_exit_fade_frames==16);
        assert(route_cleanup_commits==1 && route_load_commits>=3 && route_first_mask);
        assert(route_black_waits==1 && route_kanji_frames==48);
        assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB && wait_for_shot);
        assert(lives==old_lives && rem_bombs==old_bombs && starts==old_starts);
        assert(scene_plays[target]==old_scene_plays+1);
        assert(std::count(test_effects.begin()+route_sfx_start,test_effects.end(),SFX_TITLE_GLYPH)==6);
        for(int tick=0;tick<3;++tick) gameplay_tick(0,0);
        assert(stage_score==before+3000+round_bonus); // no repeat after results/route handoff
    }
    hiscore=std::max(hiscore,saved_high);
    start_new_game();assert(stage_score==0);
    std::puts("Stage bonus: credited once, resident regular scenery/VBlank-safe scroll, card ripple/banner/one serve frame, Stage4->5 fade/kanji, both Makai/Jigoku results fade before cleanup/load/first kanji, unchanged resources/music and boss/route carry passed");
}

void test_konngara_stage20()
{
    test_vblank_hook=nullptr;
    // Reproduce the reported history: SinGyoku's actual defeat, route choice
    // and round results must precede Stage20, not just a fresh direct jump.
    stage_load(0,4);
    for(int i=0;i<200;++i) singyoku_update();
    auto &health=const_cast<BossHealth&>(singyoku_state().health);
    health.hp=1;health.invincible=false;health.invincibility_frame=0;
    const auto &entity=singyoku_state().entity;
    orb_set_position(entity.left+24,entity.top+24);
    test_vblank_hook=[](){test_keys_down=KEY_B;test_keys_held=0;};
    gameplay_tick(0,0);
    test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
    assert(current_scene==1 && current_stage==0);
    stage_load(6,3); // actual Stage19, no new fake route/boss prerequisites
    lives=6;rem_bombs=4;stage_score=123450;frame=17;
    stage_cards_removed_count=stage_card_total; // test-only cleared card field
    stage20_carry_score=stage_score+stagebonus_compute(19,lives).total;
    stage20_entry_frame=frame;stage20_entering=true;
    stage20_entry_waits=stage20_fade_waits=stage20_black_waits=0;
    const int plays=starts;
    test_vblank_hook=observe_stage20;
    gameplay_tick(0,0); // real19->20 progression, score/lives/resources preserved
    stage20_entering=false;
    assert(stage_has_konngara(current_scene,current_stage) && global_stage_num()==20);
    assert(stage20_entry_waits==712 && starts==plays+1 && !wait_for_shot && g_canvas_target);
    assert(stage20_fade_waits==16 && stage20_black_waits==1);
    assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);
    assert(test_obj_count==80 && test_obj_used==31936 && REG_WININ==0x1919);
    // A normal-game boss miss keeps both the orb and the real run's lives.
    orb_set_position(123,147);player_is_hit=true;gameplay_tick(0,0);
    assert(lives==5 && rem_bombs==5 && konngara.state().health.hp==18);
    assert(orb_get_state().x_position<200 && orb_get_state().y_position<250);
    // Real pause rendering on buffered BG2: no missing effects on dim/restore,
    // no extra AI/timer/HP ticks and no palette remapping on resume.
    canvas_fill_rect(48,80,4,4,RGB15(20,5,8));canvas_fill_rect(64,80,4,4,0x7fff);
    canvas_buffer_prepare();swiWaitForVBlank();assert(canvas_buffer_present());
    const std::vector<u16> pixels(BG_BMP_RAM(CANVAS_BMP_BLOCK),BG_BMP_RAM(CANVAS_BMP_BLOCK)+256*192);
    const std::vector<u16> palette(BG_PALETTE,BG_PALETTE+256),objects(SPRITE_PALETTE,SPRITE_PALETTE+256);
    const unsigned paused_frame=frame,paused_timer=stage_timer;
    const int paused_boss_frame=konngara.state().frame;
    test_vblank_hook=[](){check_pause_dimming();test_keys_down=KEY_B;};
    assert(!pause_menu_run());test_keys_down=0;test_vblank_hook=observe_stage20;
    assert(frame==paused_frame && stage_timer==paused_timer && konngara.state().frame==paused_boss_frame);
    assert(std::equal(pixels.begin(),pixels.end(),BG_BMP_RAM(CANVAS_BMP_BLOCK)));
    assert(std::equal(palette.begin(),palette.end(),BG_PALETTE) && std::equal(objects.begin(),objects.end(),SPRITE_PALETTE));
    int phase_frames[8]={};
    size_t defeat_attempts=0;
    initialize_irand(42);
    for(int tick=0;tick<3000 && !jigoku_cleared;++tick) {
        if(tick%80==0) player_miss(); // test-only invincibility, not extra lives
        const auto &s=konngara.state();int age=++phase_frames[s.phase];
        bool hit=((s.phase==1 || s.phase==3 || s.phase==5) && age<=90) || (s.phase==7 && age<=400);
        orb_set_position(hit?300:600,hit?130:80);
        if(s.phase==7 && s.health.hp==1 && !s.health.invincible) {
            player_is_hit=true;
            defeat_attempts=test_heap_attempts;test_heap_fail_after=0;
        }
        gameplay_tick(0,0);
    }
    test_heap_fail_after=-1;
    assert(jigoku_cleared && stage20_defeat_waits==541 && current_scene==6 && current_stage==4);
    assert(defeat_attempts && test_heap_attempts==defeat_attempts);
    assert(stage_score==stage20_frozen_score && lives==5 && rem_bombs==5);
    const u16 j=(1+('J'-0x20)*2)|(14<<12);
    assert(BG_MAP_RAM(30)[8*32+7]==j && test_obj_count==79);
    gameplay_tick(0,0); // terminal state cannot re-run defeat/bonus or loop to16
    assert(stage20_defeat_waits==541 && stage_score==stage20_frozen_score);
    // The actual new-game helper must remove ALL result/menu rows before the
    // first entrance VBlank, not merely cover the stale text with BG2.
    static int new_game_waits;
    new_game_waits=0;
    test_vblank_hook=[](){
        ++new_game_waits;
        assert(std::all_of(BG_MAP_RAM(30)+3*32,BG_MAP_RAM(30)+32*32,[](u16 tile){return tile==0;}));
    };
    const int saved_hi=hiscore;
    start_new_game();
    test_vblank_hook=nullptr;
    assert(new_game_waits>0 && current_scene==0 && current_stage==0);
    assert(lives==START_LIVES && rem_bombs==2 && pellet_speed==-4 && frame==0 && stage_score==0 && hiscore==saved_hi);
    assert(std::all_of(BG_MAP_RAM(30)+3*32,BG_MAP_RAM(30)+32*32,[](u16 tile){return tile==0;}));
    assert(!jigoku_cleared && !g_canvas_target && test_obj_count==79 && test_obj_used==27840);
    assert(test_heap_count==baseline_blocks && test_heap_bytes==baseline_heap && wait_for_shot);
    camera.snap_center();
    std::puts("Actual Stage20:19->20 top-only black fade with bottom HUD unchanged, black-held load/first opaque image,711-frame intro+one handoff, single ALICE start, carry/resources, boss miss/pause, lethal bonus, defeat+clear hold and terminal cleanup passed");
}

// Extend the existing real-game integration check rather than create another
// animation suite or a second game loop model.
namespace {
struct DeathProbe {
    bool fatal=false, new_run=false;
    int waits=0, returning=0, prompt=0, burst_commits=0, orb_x=0, orb_y=0, player_x=0, boss_frame=0;
    unsigned game_frame=0, timer=0;
    int score=0, pellet_x=0, pellet_y=0, pellet_vx=0, pellet_vy=0;
    size_t allocations=0, objects=0;
    bool mima_active=false;
    MimaPhase1 frozen_mima;
} death_probe;

void observe_player_death()
{
    auto &p=death_probe;
    if(player_death_frame) {
        if(!p.waits) {
            p.game_frame=frame;p.timer=stage_timer;p.score=stage_score;
            p.orb_x=orb_get_state().x_position;p.orb_y=orb_get_state().y_position;
            p.player_x=player_get_state().x_position;
            p.boss_frame=stage_has_konngara(current_scene,current_stage) ? konngara.state().frame :
                stage_has_mima(current_scene,current_stage) ? mima.state().frame : singyoku_state().frame;
            p.mima_active=stage_has_mima(current_scene,current_stage);
            if(p.mima_active) p.frozen_mima=mima;
            p.pellet_x=Pellets.at(0).cur_left.v;p.pellet_y=Pellets.at(0).cur_top.v;
            p.pellet_vx=Pellets.at(0).velocity.x.v;p.pellet_vy=Pellets.at(0).velocity.y.v;
        }
        ++p.waits;
        assert(frame==p.game_frame && stage_timer==p.timer && stage_score==p.score);
        assert(orb_get_state().x_position==p.orb_x && orb_get_state().y_position==p.orb_y);
        assert(player_get_state().x_position==p.player_x);
        if(p.mima_active) assert(!std::memcmp(&p.frozen_mima,&mima,sizeof(mima)));
        assert((stage_has_konngara(current_scene,current_stage) ? konngara.state().frame :
            stage_has_mima(current_scene,current_stage) ? mima.state().frame : singyoku_state().frame)==p.boss_frame);
        assert(Pellets.at(0).cur_left.v==p.pellet_x && Pellets.at(0).cur_top.v==p.pellet_y);
        if(player_death_frame->returning) {
            ++p.returning;
            assert(Pellets.at(0).decay_frame==1);
            assert(Pellets.at(0).velocity.x.v==p.pellet_vx*2/3 && Pellets.at(0).velocity.y.v==p.pellet_vy*2/3);
        }
        assert(test_heap_attempts==p.allocations && test_obj_attempts==p.objects);
        assert(REG_WININ==0x1919);
        return;
    }
    const u16 g=(224+3*32+11)|(TEXT_PAL_RED<<12); // modal tile at GAME OVER's first cell
    if(BG_MAP_RAM(30)[6*32+11]!=g) return;
    check_pause_dimming(); // game-over shares native dimming, not palette scaling
    assert(BG_PALETTE[TEXT_PAL_RED*16+TEXT_MODAL_WHITE]==0x7fff);
    assert(BG_PALETTE[TEXT_PAL_WHITE*16+1]==RGB15(12,12,12));
    ++p.prompt;
    // A held attack cannot accept the prompt. Release, optionally move to NEW
    // GAME, then explicitly confirm; the ordinary startup/entrance is real.
    test_keys_held=p.prompt==1 ? KEY_A : 0;
    test_keys_down=p.prompt==1 ? KEY_A : p.prompt==2 ? 0 : p.prompt==3 && p.new_run ? KEY_DOWN : KEY_A;
}
}

void test_player_death()
{
    // Existing player lifecycle/render check: source sparkle phase and spawn
    // bounds, pure red pixels, both moving camera edges, expiry/dirty clearing,
    // redraw stability and no gameplay-RNG, heap or sprite-slot consumption.
    initialize_irand(123);const int next_random=irand();initialize_irand(123);
    const auto sparkle_objects=test_obj_attempts,sparkle_heap=test_heap_attempts,sparkle_oam=test_oam_changes;
    int seen_cels=0;
    canvas_clear();
    for(int direction:{KEY_LEFT,KEY_RIGHT}) {
        player_initialize();player_miss();
        for(int tick=0;tick<=PLAYER_MISS_INVINCIBILITY_FRAMES;++tick) {
            const auto previous=player_invincibility_sparkles();
            if(tick) player_update(direction,0);
            const auto &sparkles=player_invincibility_sparkles();
            assert(sparkles.active()==(tick<PLAYER_MISS_INVINCIBILITY_FRAMES));
            camera.follow(player_center_x());
            static u16 expected_sparkles[VIEW_W*VIEW_H];
            std::fill(std::begin(expected_sparkles),std::end(expected_sparkles),0);
            if(sparkles.active()) for(int i=0;i<InvincibilitySparkles::count;++i) {
                const auto &p=sparkles.particles()[i],&old=previous.particles()[i];
                assert(p.frame>=2 && p.frame<=9 && p.cel>=0 && p.cel<4);
                const int age=p.frame-1;
                assert(p.cel==(age<=4?age-1:8-age));seen_cels|=1<<p.cel;
                if(!tick || old.frame==9) {
                    assert(p.left>=player_get_state().x_position-8 && p.left<player_get_state().x_position+40);
                    assert(p.top>=player_top-16 && p.top<player_top+32);
                } else assert(p.left==old.left && p.top==old.top && p.frame==old.frame+1);
                for(int y=0;y<4;++y) for(int x=0;x<4;++x) {
                    const int px=camera.x(p.left)+x,py=Camera::y(p.top)+y;
                    if(px>=0 && px<VIEW_W && py>=UPPER_HUD_H && py<VIEW_H && Shape8x8_cels[3+p.cel][y*4+x])
                        expected_sparkles[py*VIEW_W+px]=0x801f;
                }
            }
            const auto frozen=sparkles;
            for(int redraw=0;redraw<2;++redraw) {
                canvas_begin_frame();canvas_playfield_clip(true);
                player_invincibility_render_frame();
                assert(std::equal(std::begin(expected_sparkles),std::end(expected_sparkles),canvas_at(0,0)));
                for(int i=0;i<InvincibilitySparkles::count;++i) {
                    const auto &p=sparkles.particles()[i],&old=frozen.particles()[i];
                    assert(p.left==old.left && p.top==old.top && p.frame==old.frame && p.cel==old.cel);
                }
            }
        }
    }
    assert(seen_cels==15 && irand()==next_random);
    assert(test_obj_attempts==sparkle_objects && test_heap_attempts==sparkle_heap && test_oam_changes==sparkle_oam);
    player_initialize();canvas_clear();canvas_playfield_clip(false);camera.snap_center();
    std::puts("Invincibility sparkles: source eight-particle cycle/spawns, red BG2 pixels, camera/edge clipping,150-frame expiry/cleanup, stable redraws and zero RNG/heap/OBJ impact passed");
    for(bool fatal:{false,true}) for(int left:{0,304,608}) for(bool alternate:{false,true}) {
        PlayerDeathAnimation animation(fatal,left,player_top,alternate);
        PlayerDeathFrame f;
        int steps=0,waits=0,pairs=0;
        camera.follow(left+PLAYER_W/2);
        const size_t allocations=test_obj_attempts;
        canvas_clear();
        while(animation.next(f)) {
            assert(f.pose==(alternate?23:22) && f.pose_left==left && f.pose_top==player_top);
            assert(f.shake_y==0 || f.shake_y==4);
            assert(f.shake_y==(steps<16 ? (steps%2)*4 : 0));
            canvas_begin_frame();canvas_playfield_clip(true);
            player_render_death(f);
            // Independent per-pixel reconstruction: all five old puffs remain
            // whole, including the horizontal trails and the portions of the
            // diagonals below Reimu's top. No stale canvas pixels may supply it.
            static u16 expected_trails[VIEW_W*VIEW_H];
            std::fill(std::begin(expected_trails),std::end(expected_trails),0);
            for(int old=0;old<f.burst_step;++old) {
                const int distance=(old+1)*8,diagonal=(old+1)*370728/65536;
                const int dx[]={-distance,-diagonal,0,diagonal,distance};
                const int dy[]={0,-diagonal,-distance,-diagonal,0};
                const auto *tiles=reinterpret_cast<const u8*>(Miko_tiles)+(20+old%2)*128;
                for(int spoke=0;spoke<5;++spoke) {
                    const int world_x=left+dx[spoke];
                    if(world_x<PLAYFIELD_LEFT || world_x>=PLAYFIELD_RIGHT-PLAYER_W) continue;
                    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
                        const int px=camera.x(world_x)+x,py=Camera::y(player_top+dy[spoke])+y;
                        if(px<0 || px>=VIEW_W || py<UPPER_HUD_H || py>=VIEW_H) continue;
                        const int offset=(y/8*2+x/8)*32+(y%8)*4+(x%8)/2;
                        const int index=(tiles[offset]>>((x%2)*4))&15;
                        if(index) expected_trails[py*VIEW_W+px]=0x8000|SPRITE_PALETTE[index];
                    }
                }
            }
            assert(std::equal(std::begin(expected_trails),std::end(expected_trails),canvas_at(0,0)));
            assert(test_objs[0].hidden==!f.show_pose);
            for(int i=0;i<f.count;++i) assert(test_objs[OAM_SLOT_DEATH_EFFECT+i].priority==0);
            if(f.returning) {
                assert(!fatal && f.count==2 && f.left[0]<=608 && f.left[1]>=0);
                if(pairs==0) assert(f.left[0]==(left+16>608?608:left+16) && f.left[1]==(left-16<0?0:left-16));
                ++pairs;
            }
            if(fatal && steps==0) {
                assert(f.count==5 && f.left[1]==left-5 && f.left[3]==left+5);
                assert(f.top[1]==player_top-5 && f.top[2]==player_top-8);
            }
            waits+=f.hold;++steps;
        }
        assert(steps==(fatal?17:47) && waits==(fatal?31:99) && pairs==(fatal?0:30));
        assert(test_obj_attempts==allocations);
        player_render_frame();
        for(int i=0;i<5;++i) assert(test_objs[OAM_SLOT_DEATH_EFFECT+i].hidden);
    }
    canvas_playfield_clip(false);
    // A full trail redraw can outlast VBlank on hardware. Simulate reaching
    // visible scanout during direct-canvas drawing; every burst scroll commit
    // must wait for a fresh early VBlank, not restart affine BGs mid-screen.
    test_dma_fill_hook=[](){
        if(player_death_frame && player_death_frame->burst_step>=0) REG_VCOUNT=80;
    };
    test_bg_update_hook=[](){
        if(!player_death_frame || player_death_frame->burst_step<0) return;
        assert(REG_VCOUNT>=192 && REG_VCOUNT<200);
        assert(test_bgs[1].y==-player_death_frame->shake_y);
        assert(test_bgs[2].y==test_bgs[1].y && test_bgs[3].y==test_bgs[1].y);
        ++death_probe.burst_commits;
    };
    // Ordinary stages, all three bosses, and deaths during a large-sprite attack.
    for(int scene:{0,2,6}) for(int stage:{0,4}) {
        test_vblank_hook=nullptr;stage_load(scene,stage);
        if(stage_has_singyoku(scene,stage)) for(int i=0;i<200;++i) singyoku_update();
        const int starting_bombs=scene==6 && stage==0 ? 0 : 5;
        wait_for_shot=false;lives=1;rem_bombs=starting_bombs;stage_timer=100;stage_score=12345;
        player_update(KEY_A,KEY_A);assert(player_is_deflecting());
        assert(!player_invincibility_sparkles().active()); // orb-only immunity is not full invincibility
        orb_set_position(500,100);Pellets.init();Pellets.add_single(100,100,64,16);
        death_probe={};death_probe.allocations=test_heap_attempts;death_probe.objects=test_obj_attempts;
        test_vblank_hook=observe_player_death;
        player_is_hit=true;gameplay_tick(0,0);
        test_vblank_hook=nullptr;
        assert(death_probe.waits==99 && death_probe.returning==50 && lives==0 && rem_bombs==(starting_bombs<5?starting_bombs+1:5));
        assert(player_get_state().x_position==PLAYER_LEFT_START && player_get_state().spritesheet==PLAYER);
        assert(!player_is_deflecting() && !player_is_sliding() && !player_is_invincible_to_orb() && player_is_invincible());
        assert(player_invincibility_sparkles().active());
        assert(orb_get_state().x_position==death_probe.orb_x && orb_get_state().y_position==death_probe.orb_y);
        assert(stage_timer==death_probe.timer+200 && stage_score==death_probe.score);
        assert(test_bgs[1].y==0 && test_bgs[2].y==0 && test_bgs[3].y==0);
        for(int i=0;i<5;++i) assert(test_objs[OAM_SLOT_DEATH_EFFECT+i].hidden);
        for(int i=0;i<149;++i) player_update(0,0);
        assert(player_is_invincible());player_update(0,0);assert(!player_is_invincible());
        assert(!player_invincibility_sparkles().active());

        // Final life: no recovery bomb/timer grant; prompt, then fresh stage
        // state/resources and no lingering death OAM, buffers or menu text.
        lives=0;stage_score=54321;Pellets.init();Pellets.add_single(100,100,64,16);
        death_probe={};death_probe.fatal=true;
        death_probe.allocations=test_heap_attempts;death_probe.objects=test_obj_attempts;
        test_keys_down=test_keys_held=0;test_vblank_hook=observe_player_death;
        player_is_hit=true;gameplay_tick(0,0);
        test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
        assert(death_probe.waits==31 && death_probe.returning==0 && death_probe.prompt>=3 && death_probe.burst_commits==17);
        assert(current_scene==scene && current_stage==stage && lives==START_LIVES && rem_bombs==1 && stage_score==0 && hiscore>=54321);
        assert(test_obj_count==(scene==6 && stage==4 ? 80u:79u));
        assert(std::all_of(BG_MAP_RAM(30)+3*32,BG_MAP_RAM(30)+32*32,[](u16 tile){return tile==0;}));
    }
    test_vblank_hook=nullptr;stage_load(6,4);
    lives=0;stage_score=65432;Pellets.init();Pellets.add_single(100,100,64,16);
    death_probe={};death_probe.fatal=true;death_probe.new_run=true;
    death_probe.allocations=test_heap_attempts;death_probe.objects=test_obj_attempts;
    test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);
    test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
    assert(death_probe.waits==31 && death_probe.prompt>=4 && death_probe.burst_commits==17);
    test_dma_fill_hook=nullptr;test_bg_update_hook=nullptr;
    assert(current_scene==0 && current_stage==0 && lives==START_LIVES && rem_bombs==2 && stage_score==0 && hiscore>=65432);
    assert(!g_canvas_target && test_obj_count==79 && test_heap_count==baseline_blocks && test_heap_bytes==baseline_heap);
    // Clearing the last card on the lethal-contact frame must not trigger a
    // death/game-over animation or spend a life before the stage result.
    static int simultaneous_death_waits;
    simultaneous_death_waits=0;lives=0;player_is_hit=true;
    stage_cards_removed_count=stage_card_total;
    test_vblank_hook=[](){if(player_death_frame) ++simultaneous_death_waits;test_keys_down=KEY_A;};
    gameplay_tick(0,0);
    test_vblank_hook=nullptr;test_keys_down=0;
    assert(current_scene==0 && current_stage==1 && lives==0 && simultaneous_death_waits==0);
    start_new_game();
    camera.snap_center();
    std::puts("Player death: original cels/edge clipping,99-frame miss/31-frame fatal burst, frozen gameplay, full attack reset/grace, resource recovery, retry/new-run input gating and boss buffer/OBJ cleanup passed");
}

static u16 bomb_master_reference(int f,u16 normal)
{
    // Independent inventory for the existing eight-frame fade, then the
    // unchanged neutral/Up5 cadence on its original discharge clock.
    static const u16 bridge[]={0x4010,0x400e,0x400c,0x400a,0x4008,0x4006,0x4004,0x4002};
    if(f>=50 && f<=57) return bridge[f-50];
    if(f>=58 && f<140) return ((f-50)/5)%2 ? 0x4005 : 0;
    return normal;
}

static void observe_bomb_flash_commit()
{
    const int f=player_bomb().frame();
    if(f>=50) assert(REG_VCOUNT>=192 && REG_VCOUNT<200);
    if(f>=50 && f<=58) assert(REG_MASTER_BRIGHT==bomb_master_reference(f,0));
}

void test_bombs()
{
    const u16 original_control=REG_BLDCNT,original_alpha=REG_BLDALPHA,original_y=REG_BLDY.written,original_window=REG_WINOUT;
    const u16 original_master=REG_MASTER_BRIGHT,original_sub_master=REG_MASTER_BRIGHT_SUB;
    const std::vector<u16> initial_palette(BG_PALETTE,BG_PALETTE+256);
    assert(video_bomb_indigo(0)==0x7fff && video_bomb_indigo(0x7fff)==RGB15(12,12,31));
    int previous=32;
    for(int grey=0;grey<32;++grey) {
        const u16 color=video_bomb_indigo(RGB15(grey,grey,grey));
        assert((color&31)<=previous && (color&31)==((color>>5)&31) && (color>>10)==31);
        previous=color&31;
    }
    // Exact restore of non-default colour state. The only fade targets are
    // BG3 and its index0 backdrop; never stage objects, player, bullets or HUD.
    REG_BLDCNT=0x0123;REG_BLDALPHA=0x0506;video_set_fade(7);REG_WINOUT=0x401f;
    REG_MASTER_BRIGHT=0x8003;REG_MASTER_BRIGHT_SUB=0x4002;
    BG_PALETTE[0]=0;BG_PALETTE[1]=0x7fff;
    video_bomb_prepare(true,50,1000);video_bomb_commit();
    assert(BG_PALETTE[0]==0x7fff && BG_PALETTE[1]==RGB15(12,12,31));
    assert(REG_BLDCNT==0 && REG_BLDALPHA==0x0506 && REG_WINOUT==0x403f);
    assert(!(REG_WININ&0x2020));
    assert(REG_MASTER_BRIGHT==0x4010 && REG_MASTER_BRIGHT_SUB==0x4002);
    video_bomb_pause(true);assert(REG_MASTER_BRIGHT==0x8003);
    video_bomb_pause(false);assert(REG_MASTER_BRIGHT==0x4010);
    { CanvasForegroundScope foreground;video_canvas_effect_commit();assert(REG_MASTER_BRIGHT==0x8003); }
    video_bomb_commit();assert(REG_MASTER_BRIGHT==0x4010);
    video_bomb_reset(); // cancellation at full white cannot leave the screen white
    assert(REG_MASTER_BRIGHT==0x8003 && REG_MASTER_BRIGHT_SUB==0x4002);
    assert(REG_BLDCNT==0x0123 && REG_BLDALPHA==0x0506 && video_fade()==7 && REG_WINOUT==0x401f);
    assert(BG_PALETTE[0]==0 && BG_PALETTE[1]==0x7fff);
    video_bomb_prepare(true,50,1000);video_bomb_commit();assert(REG_MASTER_BRIGHT==0x4010);
    for(int f=51;f<=58;++f) {
        video_bomb_prepare(true,f,950+f);video_bomb_commit();
        assert(REG_MASTER_BRIGHT==bomb_master_reference(f,0) && REG_MASTER_BRIGHT_SUB==0x4002);
        assert(BG_PALETTE[0]==0x7fff && BG_PALETTE[1]==RGB15(12,12,31));
        video_bomb_pause(true);assert(REG_MASTER_BRIGHT==0x8003);
        video_bomb_pause(false);assert(REG_MASTER_BRIGHT==bomb_master_reference(f,0));
    }
    video_bomb_prepare(true,59,1009);video_bomb_commit();assert(REG_MASTER_BRIGHT==0x4005);
    { CanvasForegroundScope foreground;video_canvas_effect_commit();assert(REG_MASTER_BRIGHT==0x8003); }
    video_bomb_commit();assert(REG_MASTER_BRIGHT==0x4005);
    video_bomb_pause(true);assert(REG_MASTER_BRIGHT==0x8003);
    video_bomb_pause(false);assert(REG_MASTER_BRIGHT==0x4005);
    video_bomb_prepare(false,140,1090);video_bomb_commit();
    assert(REG_MASTER_BRIGHT==0x8003);
    for(int age=1;age<=32;++age) {
        video_bomb_prepare(false,140,1090+age);video_bomb_commit();
        if(age<32) {
            assert(REG_BLDY.written==(age<=16 ? age : 32-age));
            assert(REG_BLDCNT==(BLEND_FADE_WHITE|BLEND_SRC_BG3|BLEND_SRC_BACKDROP));
            assert(BG_PALETTE[0]==(age<16 ? 0x7fff : 0));
            if(age==16) {
                assert(REG_BLDY.written==16 && BG_PALETTE[1]==0x7fff); // swap hidden by full white
                { CanvasForegroundScope foreground;video_canvas_effect_commit();assert(REG_BLDCNT==0); }
                video_bomb_commit();assert(REG_BLDCNT);
            }
        }
    }
    assert(!video_bomb_effect_active());
    assert(REG_BLDCNT==0x0123 && REG_BLDALPHA==0x0506 && REG_BLDY.written==7 && video_fade()==7 && REG_WINOUT==0x401f);
    assert(REG_MASTER_BRIGHT==0x8003 && REG_MASTER_BRIGHT_SUB==0x4002);
    REG_BLDCNT=original_control;REG_BLDALPHA=original_alpha;video_set_fade(original_y);REG_WINOUT=original_window;
    REG_MASTER_BRIGHT=original_master;REG_MASTER_BRIGHT_SUB=original_sub_master;
    std::copy(initial_palette.begin(),initial_palette.end(),BG_PALETTE);
    // Interrupting recovery with another invocation restores normal colours
    // immediately; its first50 frames must not inherit brightness or tint.
    video_bomb_prepare(true,60,2000);video_bomb_commit();
    video_bomb_prepare(false,140,2080);video_bomb_commit();
    video_bomb_prepare(false,140,2088);video_bomb_commit();assert(REG_BLDY.written==8);
    video_bomb_prepare(true,0,2089);video_bomb_commit();
    assert(!video_bomb_effect_active() && REG_BLDY.written==original_y);
    assert(REG_MASTER_BRIGHT==original_master);
    assert(std::equal(initial_palette.begin(),initial_palette.end(),BG_PALETTE));
    PlayerBomb input;
    assert(!input.input(true,true,false,false,false));
    assert(!input.input(false,false,false,false,false));
    assert(input.input(true,true,false,false,false)); // original double-tap pair
    assert(input.input(false,false,false,false,false)); // source input latch survives the first-special gate
    input.arm();assert(!input.input(false,false,false,false,false));
    input.reset();assert(!input.input(true,true,false,false,false));
    for(int i=0;i<20;++i) assert(!input.input(false,false,false,false,false));
    assert(!input.input(true,true,false,false,false)); // expired first pair
    assert(!input.input(false,false,true,false,true)); // navigator must not bomb
    input.reset();assert(input.input(true,false,false,true,false));

    test_vblank_hook=nullptr;stage_load(0,0);
    player_initialize();rem_bombs=0;player_update(KEY_X,KEY_X);
    assert(!player_bomb().pending() && !player_bomb().active());
    rem_bombs=2;player_update(0,0);assert(!player_bomb().pending());
    orb_set_position(10000,200);pellet_speed=-4;Pellets.init();
    Pellets.add_single(100,100,0,16);Pellets.at(0).velocity.x.v=9;Pellets.at(0).velocity.y.v=6;
    const size_t sounds=test_effects.size(),objects=test_obj_attempts,heap=test_heap_attempts;
    const std::vector<u16> bg(BG_PALETTE,BG_PALETTE+256),obj(SPRITE_PALETTE,SPRITE_PALETTE+256);
    player_update(KEY_X,KEY_X);
    assert(player_bomb().pending() && rem_bombs==2 && pellet_speed==-3);
    int previous_x=player_get_state().x_position;
    int white_frames=0;
    for(int f=0;f<=140;++f) {
        player_update(KEY_X|KEY_LEFT|KEY_B,KEY_X); // even repeated direct presses cannot retrigger
        assert(player_bomb().frame()==f && player_bomb().active()==(f<140));
        assert(player_bomb().damaging()==(f>=50 && f<140));
        assert(rem_bombs==1 && player_is_invincible() && player_get_state().x_position==previous_x);
        if(f==0) assert(Pellets.at(0).decay_frame==1 && Pellets.at(0).velocity.x.v==6 && Pellets.at(0).velocity.y.v==4);
        if(f==5) { Pellets.add_single(100,120,0,16);assert(!Pellets.at(1).decay_frame); }
        if(f==10) assert(Pellets.at(1).decay_frame==1);
        if(f==51) { Pellets.add_single(100,140,0,16);assert(!Pellets.at(2).decay_frame); }
        if(f>51) assert(!Pellets.at(2).decay_frame); // source's late decay branch is unreachable
        canvas_begin_frame();canvas_playfield_clip(true);bomb_render_frame();
        video_bomb_prepare(player_bomb().active(),f,f);video_bomb_commit();
        assert(video_bomb_effect_active()==(f>=50));
        assert(REG_BLDCNT==(f>=50 ? 0 : original_control));
        static const int shakes[]={4,0,8,0};
        assert(video_bomb_shake()==(f>=52 && f<140 ? shakes[(f/4)%4] : 0));
        assert(REG_MASTER_BRIGHT==bomb_master_reference(f,original_master));
        white_frames+=REG_MASTER_BRIGHT==0x4010;
        assert(REG_MASTER_BRIGHT_SUB==original_sub_master);
        for(int y=0;y<VIEW_H;++y) for(int x=0;x<VIEW_W;++x) {
            const u16 c=*canvas_at(x,y);
            if(y<UPPER_HUD_H) assert(!c);
            if(f>=50) assert(!c); // no software discharge pixels at all
        }
        if((f>=50 && f<=58) || f==100) {
            const size_t bytes=test_dma_fill_bytes,copies=test_dma_copy_bytes;
            bomb_render_frame();video_bomb_prepare(true,f,f);video_bomb_commit();
            assert(bytes==test_dma_fill_bytes && copies==test_dma_copy_bytes); // register/palette-only redraw
            canvas_fill_rect(10,40,1,1,0x7fff);bomb_render_frame();assert(*canvas_at(10,40)==0xffff);
        }
        for(int i=0;i<256;++i) assert(BG_PALETTE[i]==(f>=50 && i<208 ? video_bomb_indigo(bg[i]) : bg[i]));
        assert(std::equal(obj.begin(),obj.end(),SPRITE_PALETTE));
    }
    assert(std::count(test_effects.begin()+sounds,test_effects.end(),SFX_BOMB_START)==1);
    assert(white_frames==1);
    assert(std::count(test_effects.begin()+sounds,test_effects.end(),SFX_BOMB_ACTIVE)==8);
    assert(test_obj_attempts==objects && test_heap_attempts==heap);
    for(int age=1;age<=32;++age) { video_bomb_prepare(false,140,140+age);video_bomb_commit(); }
    assert(std::equal(bg.begin(),bg.end(),BG_PALETTE));
    assert(REG_BLDCNT==original_control && REG_BLDALPHA==original_alpha && REG_BLDY.written==original_y && REG_WINOUT==original_window);
    assert(REG_MASTER_BRIGHT==original_master);
    for(int i=0;i<99;++i) player_update(0,0);
    assert(player_is_invincible());player_update(0,0);assert(!player_is_invincible());
    canvas_clear();canvas_playfield_clip(false);

    // Actual serve/cards and sparse uploads; recovery never extends mechanics.
    stage_load(0,0);rem_bombs=2;lives=3;frame=0;
    for(int i=0;i<stage_card_total;++i) stage_cards[i].hp=CARD_HP_MAX-1; // keep the fixture stage alive
    gameplay_tick(KEY_X,KEY_X);assert(!wait_for_shot && player_bomb().pending());
    const int hud_y=test_objs[OAM_SLOT_HUD_LIVES].y;
    white_frames=0;
    for(int f=0;f<=172;++f) {
        const size_t bytes=test_dma_copy_bytes;
        test_bg_update_hook=observe_bomb_flash_commit;
        orb_set_position(10000,200);gameplay_tick(0,0);
        test_bg_update_hook=nullptr;
        assert(player_bomb().frame()==(f<140?f:140) && rem_bombs==1 && lives==3 && current_stage==0);
        assert(player_bomb().active()==(f<140) && player_bomb().damaging()==(f>=50 && f<140));
        assert(bool(g_canvas_target)==(f<172) && regular_bomb_canvas_owned==(f<172));
        assert(REG_MASTER_BRIGHT==bomb_master_reference(f,original_master));
        white_frames+=REG_MASTER_BRIGHT==0x4010;
        assert(REG_MASTER_BRIGHT_SUB==original_sub_master);
        if(f>=52) assert(test_dma_copy_bytes-bytes<86016); // no compulsory84KiB stream
        if(f>=50 && f<172) {
            assert(REG_BLDALPHA==original_alpha && (REG_WINOUT&0x20));
            assert(REG_BLDY.written==(f<=140 ? 0 : f<=156 ? f-140 : 172-f));
            assert(test_bgs[g_bg_id_canvas].y==-video_bomb_shake());
            assert(test_bgs[g_bg_id_tiles].y==-video_bomb_shake());
            assert(test_bgs[g_bg_id_background].y==-video_bomb_shake());
            assert(test_objs[0].y==Camera::y(player_get_state().y_position)+video_bomb_shake());
            assert(test_objs[OAM_SLOT_HUD_LIVES].y==hud_y);
        } else assert(REG_BLDCNT==original_control && REG_BLDALPHA==original_alpha && REG_BLDY.written==original_y && REG_WINOUT==original_window);
        if(f==49) assert(stage_cardcombo_cur==0);
        if((f>=50 && f<=58) || f==85 || f==148 || f==156) {
            const unsigned paused_frame=frame,paused_timer=stage_timer;
            const u16 blend=REG_BLDCNT,alpha=REG_BLDALPHA,brightness=REG_BLDY.written,window=REG_WINOUT;
            const u16 master=REG_MASTER_BRIGHT;
            const std::vector<u16> pixels(BG_BMP_RAM(CANVAS_BMP_BLOCK),BG_BMP_RAM(CANVAS_BMP_BLOCK)+VIEW_W*VIEW_H);
            const std::vector<u16> palette(BG_PALETTE,BG_PALETTE+256);
            const std::vector<u16> obj_palette(SPRITE_PALETTE,SPRITE_PALETTE+256);
            const auto attempts=test_heap_attempts;
            test_heap_fail_after=0;
            test_vblank_hook=[](){check_pause_dimming();video_canvas_effect_commit();check_pause_dimming();test_keys_down=KEY_B;};assert(!pause_menu_run());
            test_heap_fail_after=-1;
            test_vblank_hook=nullptr;test_keys_down=0;
            assert(test_heap_attempts==attempts);
            assert(player_bomb().frame()==(f<140?f:140) && frame==paused_frame && stage_timer==paused_timer);
            assert(REG_BLDCNT==blend && REG_BLDALPHA==alpha && REG_BLDY.written==brightness && video_fade()==brightness && REG_WINOUT==window);
            assert(REG_MASTER_BRIGHT==master);
            assert(std::equal(pixels.begin(),pixels.end(),BG_BMP_RAM(CANVAS_BMP_BLOCK)));
            assert(std::equal(palette.begin(),palette.end(),BG_PALETTE));
            assert(std::equal(obj_palette.begin(),obj_palette.end(),SPRITE_PALETTE));
        }
    }
    assert(stage_cardcombo_cur>0 && white_frames==1);
    Pellets.init();Items.init();shots_initialize();popup_clear();player_initialize();
    canvas_begin_frame();assert(std::all_of(canvas_at(0,0),canvas_at(0,0)+VIEW_W*VIEW_H,[](u16 c){return c==0;}));

    // All production bosses: source last-life restriction and GLOBAL frame
    // cadence, not bomb-local %50. Two damage events land at global100/150.
    for(int scene:{0,2,6}) for(int remaining_lives:{1,0}) {
        stage_load(scene,4);
        if(scene==0) for(int i=0;i<200;++i) singyoku_update();
        lives=remaining_lives;rem_bombs=1;frame=48;
        const int hp=scene==0 ? singyoku_state().health.hp : scene==2 ? mima.state().health.hp : konngara.state().health.hp;
        const auto allocation_count=test_heap_attempts,obj_count=test_obj_attempts;
        orb_set_position(10000,200);gameplay_tick(KEY_X,KEY_X);
        white_frames=0;
        for(int f=0;f<=172;++f) {
            const size_t bytes=test_dma_copy_bytes;
            test_bg_update_hook=observe_bomb_flash_commit;
            orb_set_position(10000,200);gameplay_tick(0,0);
            test_bg_update_hook=nullptr;
            if(f>=52) assert(test_dma_copy_bytes-bytes<86016);
            assert(REG_MASTER_BRIGHT==bomb_master_reference(f,original_master));
            white_frames+=REG_MASTER_BRIGHT==0x4010;
            assert(REG_MASTER_BRIGHT_SUB==original_sub_master);
        }
        assert((scene==0?singyoku_state().health.hp:scene==2?mima.state().health.hp:konngara.state().health.hp)==hp-(remaining_lives==0?2:0));
        assert(white_frames==1);
        assert(rem_bombs==0 && lives==remaining_lives && player_is_invincible());
        assert(test_heap_attempts==allocation_count && test_obj_attempts==obj_count);
        assert(!regular_bomb_canvas_owned && bool(g_canvas_target)==(scene==2 || scene==6));
        rem_bombs=1;orb_set_position(10000,200);gameplay_tick(KEY_X,KEY_X);
        for(int f=0;f<=65;++f) { orb_set_position(10000,200);gameplay_tick(0,0); }
        assert(REG_MASTER_BRIGHT==0x4005);
        assert(player_bomb().active() && video_bomb_effect_active());stage_load(0,0);
        assert(!player_bomb().active() && !player_bomb().pending() && !g_canvas_target && !regular_bomb_canvas_owned);
        assert(REG_BLDCNT==original_control && REG_BLDALPHA==original_alpha && REG_BLDY.written==original_y && REG_WINOUT==original_window);
        assert(REG_MASTER_BRIGHT==original_master && REG_MASTER_BRIGHT_SUB==original_sub_master);
    }
    start_new_game();camera.snap_center();
    assert(test_obj_count==79 && test_heap_count==baseline_blocks && test_heap_bytes==baseline_heap);
    std::puts("Bombs: unchanged mechanics/kuji, existing eight-frame native white-flash fade on regular/SinGyoku/Mima/Konngara stages, bottom unchanged, original pulse/shake/recovery/SFX, sparse redraws, pause at every fade step and exact brightness cleanup passed");
}

namespace {
struct MimaGameProbe {
    bool first_mask=false;
    int title_waits=0,spread_waits=0,outgoing_fade=0,defeat_waits=0,results_waits=0,exit_fade=0,black_waits=0;
    int score=0,bonus=0,old_starts=0,old_stops=0,old_fades=0;
    unsigned timer=0,game_frame=0;
    OrbState orb{};
    size_t heap=0,objects=0,blocks=0,bytes=0;
} mima_game;

void observe_mima_entry()
{
    auto &p=mima_game;test_keys_down=KEY_A;test_keys_held=0;
    assert(stage_score==p.score && lives==6 && rem_bombs==4 && frame==p.game_frame);
    if(current_stage==3) {
        if(REG_MASTER_BRIGHT&31) {
            assert(++p.outgoing_fade<=16 && REG_MASTER_BRIGHT==(0x8000|p.outgoing_fade));
            assert(!REG_MASTER_BRIGHT_SUB);
        }
        return;
    }
    assert(current_scene==2 && current_stage==4 && stage_timer==2000 && camera.left==32);
    assert(p.outgoing_fade==16 && test_obj_count==79 && test_obj_used==27840);
    if(!g_canvas_target) {
        ++p.title_waits;
        for(int slot=0;slot<=41;++slot) assert(test_objs[slot].hidden);
        assert(test_mima_music_starts==p.old_starts);
    } else {
        assert(++p.spread_waits<=102 && p.title_waits>0);
        assert(test_mima_music_starts==p.old_starts+1);
        assert(!regular_bomb_canvas_owned && !player_bomb().active() && Pellets.alive_count()==0);
        assert(player_get_state().x_position==PLAYER_LEFT_START && orb_get_state().x_position==PLAYFIELD_RIGHT-ORB_W-8);
    }
}

u16 capture_mima_default(int x,int y)
{
    assert(!g_canvas_target && !player_is_hit && !player_bomb().active());
    assert(test_objs[40].hidden && test_objs[41].hidden && test_objs[OAM_SLOT_BOSS_ARROW].hidden);
    assert(test_objs[0].size==SpriteSize_16x16 && !test_objs[0].hflip);
    assert(!std::memcmp(test_objs[0].gfx,Miko_tiles,128)); // source idle-left, not final attack pose
    // The capture itself writes bank D (the old canvas), so inspect its
    // required empty input only before the first captured pixel is stored.
    if(x==0 && y==0) assert(std::all_of(canvas_at(0,0),canvas_at(0,CANVAS_H),[](u16 ink){return ink==0;}));
    assert(test_heap_count==mima_game.blocks && test_heap_bytes==mima_game.bytes);
    return 0x8000|((x+y)&31); // recognizable outgoing capture for the real zipper
}

void observe_mima_result_commit()
{
    auto &p=mima_game;
    if(p.exit_fade!=16 || p.first_mask) return;
    assert(REG_MASTER_BRIGHT==0x8010 && REG_MASTER_BRIGHT_SUB==0x8010 && !g_canvas_target);
    if(current_scene==4 && test_bgs[2].sx==32 && test_bgs[2].y==CANVAS_H) {
        assert(current_stage==0 && camera.left==32 && REG_VCOUNT>=192 && REG_VCOUNT<200);
        const auto *glyph=font16x16_find(JIS_TOUHOU_REIIDEN[0]);assert(glyph);
        for(int y=0;y<24;++y) for(int x=0;x<32;++x) {
            const bool ink=x>=8 && x<24 && y>=4 && y<20 && (glyph[y-4]&(0x8000>>(x-8)));
            assert(*canvas_at(x,CANVAS_H+y)==(ink ? 0 : 0x8000));
        }
        p.first_mask=true;
    }
}

void observe_mima_defeat()
{
    auto &p=mima_game;test_keys_down=KEY_B;test_keys_held=0;
    assert(!player_death_frame && !player_is_hit && lives==0 && rem_bombs==2 && frame==p.game_frame);
    assert(test_obj_attempts==p.objects);
    assert(test_mima_fades==p.old_fades+1);
    if(current_scene==2) {
        assert(test_heap_attempts==p.heap);
        assert(current_stage==4 && stage_timer==p.timer && mima.state().checkpoint && mima.state().health.hp==0);
        assert(orb_get_state().x_position==p.orb.x_position && orb_get_state().y_position==p.orb.y_position);
        assert(test_mima_stops==p.old_stops); // no abrupt stop during defeat/results
        if(g_canvas_target) {
            assert(++p.defeat_waits<300 && stage_score==p.score);
            assert(mima.missiles().alive_count()==0);
            for(const auto &laser:mima.state().lasers) assert(!laser.alive);
        } else {
            assert(stage_score==p.score+p.bonus && hiscore>=stage_score);
            if(test_bgs[0].map==31) {
                assert(++p.results_waits<1200);
                if(REG_MASTER_BRIGHT&31) {
                    assert(++p.exit_fade<=16 && REG_MASTER_BRIGHT==(0x8000|p.exit_fade));
                    assert(REG_MASTER_BRIGHT_SUB==REG_MASTER_BRIGHT && test_bgs[3].map==16);
                }
            }
        }
    } else {
        assert(current_scene==4 && current_stage==0 && stage_score==p.score+p.bonus && p.exit_fade==16);
        if(!p.first_mask) {
            assert(++p.black_waits==1 && REG_MASTER_BRIGHT==0x8010 && REG_MASTER_BRIGHT_SUB==0x8010);
        } else assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);
    }
}
}

void test_mima_stage10()
{
    // Natural Stage9 clear awards its usual tally, fades once, and enters the
    // real Stage10 title/spread-in without borrowing practice score/resources.
    start_new_game();stage_load(2,3);
    const auto resident_blocks=test_heap_count,resident_bytes=test_heap_bytes;
    lives=6;rem_bombs=4;stage_score=45678;stage_timer=600;frame=100;
    stage_cards_removed_count=stage_card_total;wait_for_shot=true;
    mima_game={};
    mima_game.score=stage_score+stagebonus_compute(9,lives).total;
    mima_game.game_frame=frame;mima_game.old_starts=test_mima_music_starts;
    test_vblank_hook=observe_mima_entry;
    gameplay_tick(0,0);test_vblank_hook=nullptr;test_keys_down=0;
    assert(current_scene==2 && current_stage==4 && !wait_for_shot);
    assert(mima_game.spread_waits==102 && mima.state().frame==0 && mima.state().health.hp==12);
    assert(g_canvas_target && test_heap_count==resident_blocks+2 && test_heap_bytes==resident_bytes+144432+131072);
    assert(stage_score==mima_game.score && lives==6 && rem_bombs==4);

    // Loader/entrance consume exactly the controller's one direction draw,
    // after presentation. No title/spread-in frame advances timer or combat.
    initialize_irand(3456);irand();const int next_random=irand();initialize_irand(3456);
    stage_load(2,4,false,true);assert(irand()==next_random);
    assert(mima.state().frame==0 && mima.state().hp_displayed==0);
    const int initial_hp=mima.state().health.hp;
    orb_set_position(mima.state().entity.left+48,mima.state().entity.top+48);
    gameplay_tick(0,0);assert(mima.state().health.hp==initial_hp-1 && stage_score==mima_game.score+5000);
    assert(mima.state().hp_displayed==1 && frame==101); // original one-point HP fill cadence

    // Real six-HP handoff continues into block2 rather than the isolated
    // first-block oracle's checkpoint. Retain run score, timer and resources.
    auto &state=const_cast<MimaPhase1State &>(mima.state());
    state.health.hp=6;state.health.invincible=false;state.pattern=99;
    orb_set_position(500,100);gameplay_tick(0,0);assert(state.phase==2);
    for(int i=0;state.phase==2;++i) { assert(i<80);orb_set_position(500,100);gameplay_tick(0,0); }
    assert(state.phase==3 && !state.checkpoint && state.health.hp==6);
    const auto boss=mima;const unsigned paused_frame=frame,paused_timer=stage_timer;
    const auto paused_orb=orb_get_state();
    const auto allocations=test_heap_attempts,objects=test_obj_attempts;
    music_pause();test_vblank_hook=[](){check_pause_dimming();test_keys_down=KEY_B;};
    assert(!pause_menu_run());music_resume();test_vblank_hook=nullptr;test_keys_down=0;
    assert(!std::memcmp(&boss,&mima,sizeof(mima)) && frame==paused_frame && stage_timer==paused_timer);
    assert(!std::memcmp(&paused_orb,&orb_get_state(),sizeof(paused_orb)));
    assert(test_heap_attempts==allocations && test_obj_attempts==objects);

    // Surviving miss: normal recovery, with the same frozen Mima state and
    // one VBlank per approved buffered death step (no extra recovery wait).
    lives=1;rem_bombs=1;Pellets.init();Pellets.add_single(100,100,64,16);
    death_probe={};death_probe.allocations=test_heap_attempts;death_probe.objects=test_obj_attempts;
    test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);
    test_vblank_hook=nullptr;
    assert(death_probe.waits==99 && lives==0 && rem_bombs==2 && state.health.hp==6 && state.phase==3);
    assert(stage_timer==death_probe.timer+200 && player_is_invincible());

    // Lethal contact wins a pending fatal miss. The approved defeat freezes
    // orb/timer/frame and sends exactly one fade; results credit only the
    // existing Stage10 round tally, then the black-held load enters Stage11.
    state.health.hp=1;state.health.invincible=false;state.health.invincibility_frame=0;
    assert(const_cast<MissilePool &>(mima.missiles()).add(100,100,1,1));
    assert(shootout_laser_spawn(state.lasers[0],100,100,120,120));
    orb_set_position(state.entity.left+48,state.entity.top+48);
    stage_timer=777;
    const int old_scene_plays=scene_plays[4];const auto captures=test_capture_count;
    mima_game={};mima_game.score=stage_score+5000;mima_game.bonus=roundbonus_compute(10,lives).total;
    mima_game.timer=stage_timer;mima_game.game_frame=frame;mima_game.orb=orb_get_state();
    mima_game.old_fades=test_mima_fades;mima_game.old_stops=test_mima_stops;
    mima_game.heap=test_heap_attempts;mima_game.objects=test_obj_attempts;
    mima_game.blocks=resident_blocks;mima_game.bytes=resident_bytes;
    test_capture_source=capture_mima_default;test_vblank_hook=observe_mima_defeat;
    test_bg_update_hook=observe_mima_result_commit;
    player_is_hit=true;gameplay_tick(0,0);
    test_capture_source=nullptr;test_vblank_hook=nullptr;test_bg_update_hook=nullptr;
    test_keys_down=test_keys_held=0;
    assert(mima_game.defeat_waits>79 && mima_game.results_waits>200 && mima_game.first_mask && mima_game.black_waits==1);
    assert(test_capture_count==captures+1 && test_mima_fades==mima_game.old_fades+1 && scene_plays[4]==old_scene_plays+1);
    assert(current_scene==4 && current_stage==0 && global_stage_num()==11 && wait_for_shot && !jigoku_cleared);
    assert(stage_score==mima_game.score+mima_game.bonus && lives==0 && rem_bombs==2 && stage_timer==1400);
    assert(!g_canvas_target && test_heap_count==resident_blocks && test_heap_bytes==resident_bytes);
    assert(test_obj_count==79 && test_obj_used==27840 && test_objs[40].hidden && test_objs[41].hidden);
    const int credited=stage_score;gameplay_tick(0,0);assert(stage_score==credited); // no repeated boss bonus

    // Navigation/restart also release both Mima allocations and leave shared
    // SinGyoku/Konngara cache ownership intact, including repeated re-entry.
    for(int replay=0;replay<3;++replay) {
        stage_load(2,4);assert(g_canvas_target && mima.state().health.hp==12 && !mima.state().checkpoint);
        stage_load(6,4);assert(g_canvas_target && test_obj_count==80);
        stage_load(2,4);assert(g_canvas_target && test_obj_count==79);
        stage_load(0,4);assert(!g_canvas_target && test_obj_count==79 && test_objs[41].hidden);
        assert(test_heap_count==resident_blocks && test_heap_bytes==resident_bytes);
    }
    // Fatal Mima miss -> NEW GAME uses the real menu and clears buffered state.
    stage_load(2,4);lives=0;stage_score=65432;
    death_probe={};death_probe.fatal=true;death_probe.new_run=true;
    death_probe.allocations=test_heap_attempts;death_probe.objects=test_obj_attempts;
    test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);
    test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
    assert(death_probe.waits==31 && death_probe.prompt>=4 && current_scene==0 && current_stage==0);
    assert(lives==START_LIVES && rem_bombs==2 && !g_canvas_target && stage_score==0 && hiscore>=65432);
    assert(test_heap_count==resident_blocks && test_heap_bytes==resident_bytes && test_obj_count==79);
    start_new_game();camera.snap_center();
    std::puts("Stage10: real9->10 title/101-frame spread-in, one LEGEND start, preserved run/RNG, both blocks, buffered pause/miss/fatal/new-run, lethal priority/native fade, default-pose unbuffered capture, one round tally, black-held10->11 and repeated boss-cache cleanup passed");
}
