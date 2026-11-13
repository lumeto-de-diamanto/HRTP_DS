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
#include "main/boss/singyoku.hpp"
#include "main/random.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/bullet/pellet.hpp"
#include "main/stage/stageobj.hpp"

extern std::vector<int> test_effects;
extern std::string test_practice_label;

namespace {
bool allow_music = true;
bool checkpoint, saw_checkpoint,fail_defeat_memory;
int start_set, phase3_frames, phase5_frames, phase7_frames, misses_checked;
int defeat_frames,death_orb_x,death_orb_y,death_player_x,death_player_y,death_camera,death_score;
size_t defeat_bytes,death_effects;
int starts, stops, sfx_stops, ticks;
size_t baseline_heap, baseline_blocks, heap_attempts, obj_attempts, owned_bytes;

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
            assert(test_heap_count==baseline_blocks+4 && test_heap_bytes==baseline_heap+owned_bytes+defeat_bytes);
            assert(test_obj_count==80);
        } else {
            assert(test_heap_count==baseline_blocks+2 && test_heap_bytes==baseline_heap+262144+defeat_bytes);
            assert(test_obj_count==79 && test_obj_used==27840);
            assert(!test_bgs[0].visible && !test_bgs[1].visible && !test_bgs[2].visible);
            for(int i=0;i<128;++i) assert(test_objs[i].hidden);
        }
        assert(test_heap_attempts==heap_attempts+1 && test_obj_attempts==obj_attempts && g_canvas_target);
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
        if(fail_defeat_memory && phase7_frames>300) test_heap_fail_after=0;
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

bool music_play_track(NativeTrackId id)
{
    assert(id == NATIVE_ALICE);
    ++starts;
    test_keys_down = test_keys_held = 0;
    test_practice_label.clear(); phase3_frames = phase5_frames = phase7_frames = defeat_frames = 0;
    // Two caches + combined BG3 backup/effects staging are three allocations.
    heap_attempts = test_heap_attempts;
    obj_attempts = test_obj_attempts;
    return allow_music;
}
void music_stop() { ++stops; }
void sfx_stop() { ++sfx_stops; }
const char *music_error() { return ""; }
void music_play_scene(int scene) { assert(scene>=0 && scene<7); }

void test_konngara_review()
{
    load_background(0);
    std::ifstream asset("nitro:/konngara_combat.bin", std::ios::binary | std::ios::ate);
    std::ifstream slash("nitro:/konngara_slash.bin", std::ios::binary | std::ios::ate);
    std::ifstream defeat("nitro:/konngara_defeat.bin",std::ios::binary|std::ios::ate);
    assert(asset.good());
    assert(slash.good());
    assert(defeat.good());defeat_bytes=size_t(defeat.tellg());
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
    // A failed cinematic must not roll back the bonus already earned on the
    // lethal hit, retain a cinematic buffer, or leave an overlay visible.
    fail_defeat_memory=true;start_set=3;ticks=0;stage_score=0;saw_checkpoint=false;
    initialize_irand(42);assert(!konngara_combat_review(3));
    fail_defeat_memory=false;test_heap_fail_after=-1;
    assert(!saw_checkpoint && defeat_frames==0 && stage_score>=140000 && stage_score<150000);
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
    std::puts("Konngara review: 40 replays/orb preservation, four real421-frame defeats, frozen gameplay/44 SFX7/+50000 once, late defeat-allocation failure, restored heap/OBJ/palettes/background and SinGyoku handoff passed");
}

// Exercise the ACTUAL normal-game stage loader/tick, not a second model of it.
// Startup/main's infinite loop is excluded; all renderer/gameplay code is real.
#define HRTP_GAME_FLOW_TEST
#include "../source/main.cpp"
#undef HRTP_GAME_FLOW_TEST

namespace {
bool stage20_entering;
int stage20_entry_waits,stage20_defeat_waits,stage20_entry_frame;
int stage20_frozen_score,stage20_frozen_timer,stage20_frozen_frame;

void observe_stage20()
{
    if(!stage_has_konngara(current_scene,current_stage)) {
        test_keys_down=KEY_A; // dismiss the existing Stage19 bonus screen
        return;
    }
    test_keys_down=0;
    if(stage20_entering) {
        ++stage20_entry_waits;
        assert(stage_score==123450 && lives==6 && rem_bombs==4 && stage_timer==8000);
        assert(frame==unsigned(stage20_entry_frame) && konngara.state().frame==0 && konngara.state().health.hp==18);
        assert(REG_WININ==0x1D1D); // intro mask retained through first combat commit
    } else if(konngara.state().complete) {
        if(!stage20_defeat_waits) {
            stage20_frozen_score=stage_score;stage20_frozen_timer=stage_timer;stage20_frozen_frame=frame;
            assert(stage_score>=353450 && stage_score<363450 && hiscore>=stage_score);
        }
        assert(++stage20_defeat_waits<=541);
        assert(stage_score==stage20_frozen_score && stage_timer==unsigned(stage20_frozen_timer) && frame==unsigned(stage20_frozen_frame));
        assert(lives==5 && rem_bombs==4 && !player_is_hit);
    }
}
}

void test_konngara_stage20()
{
    test_vblank_hook=nullptr;
    stage_load(6,3); // actual Stage19, no new fake route/boss prerequisites
    lives=6;rem_bombs=4;stage_score=123450;frame=17;
    stage_cards_removed_count=stage_card_total; // test-only cleared card field
    stage20_entry_frame=frame;stage20_entering=true;
    const int plays=starts;
    test_vblank_hook=observe_stage20;
    gameplay_tick(0,0); // real19->20 progression, score/lives/resources preserved
    stage20_entering=false;
    assert(stage_has_konngara(current_scene,current_stage) && global_stage_num()==20);
    assert(stage20_entry_waits==712 && starts==plays+1 && !wait_for_shot && g_canvas_target);
    assert(test_obj_count==80 && test_obj_used==31936 && REG_WININ==0x1919);
    // A normal-game boss miss keeps both the orb and the real run's lives.
    orb_set_position(123,147);player_is_hit=true;gameplay_tick(0,0);
    assert(lives==5 && rem_bombs==4 && konngara.state().health.hp==18);
    assert(orb_get_state().x_position<200 && orb_get_state().y_position<250);
    // Real pause rendering on buffered BG2: no missing effects on dim/restore,
    // no extra AI/timer/HP ticks and no palette remapping on resume.
    canvas_fill_rect(48,80,4,4,RGB15(20,5,8));canvas_fill_rect(64,80,4,4,0x7fff);
    canvas_buffer_prepare();swiWaitForVBlank();assert(canvas_buffer_present());
    const std::vector<u16> pixels(BG_BMP_RAM(CANVAS_BMP_BLOCK),BG_BMP_RAM(CANVAS_BMP_BLOCK)+256*192);
    const std::vector<u16> palette(BG_PALETTE,BG_PALETTE+256),objects(SPRITE_PALETTE,SPRITE_PALETTE+256);
    const unsigned paused_frame=frame,paused_timer=stage_timer;
    const int paused_boss_frame=konngara.state().frame;
    test_vblank_hook=[](){test_keys_down=KEY_B;};
    assert(!pause_menu_run());test_keys_down=0;test_vblank_hook=observe_stage20;
    assert(frame==paused_frame && stage_timer==paused_timer && konngara.state().frame==paused_boss_frame);
    assert(std::equal(pixels.begin(),pixels.end(),BG_BMP_RAM(CANVAS_BMP_BLOCK)));
    assert(std::equal(palette.begin(),palette.end(),BG_PALETTE) && std::equal(objects.begin(),objects.end(),SPRITE_PALETTE));
    int phase_frames[8]={};
    initialize_irand(42);
    for(int tick=0;tick<3000 && !jigoku_cleared;++tick) {
        if(tick%80==0) player_miss(); // test-only invincibility, not extra lives
        const auto &s=konngara.state();int age=++phase_frames[s.phase];
        bool hit=((s.phase==1 || s.phase==3 || s.phase==5) && age<=90) || (s.phase==7 && age<=400);
        orb_set_position(hit?300:600,hit?130:80);
        if(s.phase==7 && s.health.hp==1 && !s.health.invincible) player_is_hit=true;
        gameplay_tick(0,0);
    }
    assert(jigoku_cleared && stage20_defeat_waits==541 && current_scene==6 && current_stage==4);
    assert(stage_score==stage20_frozen_score && lives==5 && rem_bombs==4);
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
    std::puts("Actual Stage20:19->20 carry,711-frame intro+one handoff, single ALICE start,18HP/8000 timer, global lives/bombs, boss miss, buffered pause, lethal bonus, defeat+120-frame clear hold and terminal/exit cleanup passed");
}
