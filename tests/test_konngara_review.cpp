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
#include "sprites/Miko.h"

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
        assert(lives==5 && rem_bombs==5 && !player_is_hit);
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
    std::puts("Actual Stage20:19->20 carry,711-frame intro+one handoff, single ALICE start,18HP/8000 timer, global lives/bombs, boss miss, buffered pause, lethal bonus, defeat+120-frame clear hold and terminal/exit cleanup passed");
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
} death_probe;

void observe_player_death()
{
    auto &p=death_probe;
    if(player_death_frame) {
        if(!p.waits) {
            p.game_frame=frame;p.timer=stage_timer;p.score=stage_score;
            p.orb_x=orb_get_state().x_position;p.orb_y=orb_get_state().y_position;
            p.player_x=player_get_state().x_position;
            p.boss_frame=stage_has_konngara(current_scene,current_stage) ? konngara.state().frame : singyoku_state().frame;
            p.pellet_x=Pellets.at(0).cur_left.v;p.pellet_y=Pellets.at(0).cur_top.v;
            p.pellet_vx=Pellets.at(0).velocity.x.v;p.pellet_vy=Pellets.at(0).velocity.y.v;
        }
        ++p.waits;
        assert(frame==p.game_frame && stage_timer==p.timer && stage_score==p.score);
        assert(orb_get_state().x_position==p.orb_x && orb_get_state().y_position==p.orb_y);
        assert(player_get_state().x_position==p.player_x);
        assert((stage_has_konngara(current_scene,current_stage) ? konngara.state().frame : singyoku_state().frame)==p.boss_frame);
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
    const u16 g=(1+('G'-0x20)*2)|(TEXT_PAL_WHITE<<12);
    if(BG_MAP_RAM(30)[6*32+11]!=g) return;
    ++p.prompt;
    // A held attack cannot accept the prompt. Release, optionally move to NEW
    // GAME, then explicitly confirm; the ordinary startup/entrance is real.
    test_keys_held=p.prompt==1 ? KEY_A : 0;
    test_keys_down=p.prompt==1 ? KEY_A : p.prompt==2 ? 0 : p.prompt==3 && p.new_run ? KEY_DOWN : KEY_A;
}
}

void test_player_death()
{
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
    // Ordinary stage, both bosses, and deaths during a large-sprite attack.
    for(int scene:{0,6}) for(int stage:{0,4}) {
        test_vblank_hook=nullptr;stage_load(scene,stage);
        if(stage_has_singyoku(scene,stage)) for(int i=0;i<200;++i) singyoku_update();
        const int starting_bombs=scene==6 && stage==0 ? 0 : 5;
        wait_for_shot=false;lives=1;rem_bombs=starting_bombs;stage_timer=100;stage_score=12345;
        player_update(KEY_A,KEY_A);assert(player_is_deflecting());
        orb_set_position(500,100);Pellets.init();Pellets.add_single(100,100,64,16);
        death_probe={};death_probe.allocations=test_heap_attempts;death_probe.objects=test_obj_attempts;
        test_vblank_hook=observe_player_death;
        player_is_hit=true;gameplay_tick(0,0);
        test_vblank_hook=nullptr;
        assert(death_probe.waits==99 && death_probe.returning==50 && lives==0 && rem_bombs==(starting_bombs<5?starting_bombs+1:5));
        assert(player_get_state().x_position==PLAYER_LEFT_START && player_get_state().spritesheet==PLAYER);
        assert(!player_is_deflecting() && !player_is_sliding() && !player_is_invincible_to_orb() && player_is_invincible());
        assert(orb_get_state().x_position==death_probe.orb_x && orb_get_state().y_position==death_probe.orb_y);
        assert(stage_timer==death_probe.timer+200 && stage_score==death_probe.score);
        assert(test_bgs[1].y==0 && test_bgs[2].y==0 && test_bgs[3].y==0);
        for(int i=0;i<5;++i) assert(test_objs[OAM_SLOT_DEATH_EFFECT+i].hidden);
        for(int i=0;i<149;++i) player_update(0,0);
        assert(player_is_invincible());player_update(0,0);assert(!player_is_invincible());

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
