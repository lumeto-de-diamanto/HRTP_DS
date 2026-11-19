// Source timing oracle + retained bitmap/native-brightness defeat lifecycle.
#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "kikuri_render.hpp"
#include "main/boss/kikuri_defeat.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/bullet/pellet.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/item.hpp"
#include "main/stage/timer.hpp"
#include "main/random.hpp"
#include "heap_probe.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "player_render.hpp"
#include "orb_render.hpp"
#include "shot_render.hpp"
#include "pellet_render.hpp"
#include "sprites/Miko.h"

extern int test_mima_fades,test_mima_stops;
extern std::vector<int> test_effects;
extern bool invincible;
namespace {
int coefficient(int level) {return std::min(16,(level*16+7)/15);}
void timeline()
{
    for(int center=0;center<400;++center) {
        KikuriDefeat clock(center);KikuriDefeatFrame f;
        assert(clock.next(f) && f.phase==KD_SETTLE && !f.brightness);
        int top=center,bottom=center,frames=0;
        while(true) {
            top-=2;bottom+=2;++frames;
            if(top<0 && bottom>399) break; // source final line has no delay
            assert(clock.next(f) && f.phase==KD_LINES && f.line_steps==frames && f.white_steps==frames/7);
            assert(f.brightness==coefficient(frames/7));
        }
        for(int fade=1;fade<=79;++fade) {
            assert(clock.next(f) && f.phase==KD_BLACK && f.fade_steps==fade/5);
            assert(f.brightness==coefficient(15-fade/5));
        }
        assert(!clock.next(f) && !clock.next(f));
    }
    // Guarded zero-HP laser clear never touches an uncompleted fight or tears.
    KikuriPhase1 boss;boss.reset();auto &s=const_cast<KikuriPhase1State&>(boss.state());
    assert(shootout_laser_spawn(s.lasers[0],307,147,300,400,50,20,8));
    const auto before=s;boss.clear_defeat_projectiles();assert(!std::memcmp(&before,&s,sizeof(s)));
    s.phase=6;s.checkpoint=true;s.health.hp=0;s.tears[0]={300,392,20,1,3,true};
    const auto tear=s.tears[0];boss.clear_defeat_projectiles();
    for(const auto &laser:s.lasers) assert(!laser.alive);
    assert(!std::memcmp(&tear,&s.tears[0],sizeof(tear)));
}
struct Probe {
    int waits=0,center=0,lines=0,cam=0,score=0,bombs=0,speed=0,fades=0,stops=0;
    unsigned timer=0;
    u16 sub=0,blend=0,alpha=0;
    size_t allocations=0,objects=0,sfx=0,uploads=0;
    PlayerState player{};OrbState orb{};ShotsState shots{};CPellets pellets{};
    KikuriPhase1State state{};const KikuriPhase1State *actual=nullptr;
    std::vector<u16> image,bg,palette,obj_palette;
} probe;
void observe()
{
    ++probe.waits;assert(probe.waits<=probe.lines+79);
    assert(!std::memcmp(&probe.player,&player_get_state(),sizeof(probe.player)));
    assert(!std::memcmp(&probe.orb,&orb_get_state(),sizeof(probe.orb)));
    assert(!std::memcmp(&probe.shots,&shots_get_state(),sizeof(probe.shots)));
    assert(!std::memcmp(&probe.pellets,&Pellets,sizeof(Pellets)));
    assert(!std::memcmp(&probe.state,probe.actual,sizeof(probe.state)));
    assert(stage_score==probe.score && stage_timer==probe.timer && rem_bombs==probe.bombs && pellet_speed==probe.speed);
    assert(camera.left==probe.cam && test_heap_attempts==probe.allocations && test_obj_attempts==probe.objects);
    assert(test_effects.size()==probe.sfx && test_mima_fades==probe.fades+1 && test_mima_stops==probe.stops);
    assert(REG_MASTER_BRIGHT_SUB==probe.sub && REG_BLDCNT==probe.blend && REG_BLDALPHA==probe.alpha);
    assert(std::equal(probe.bg.begin(),probe.bg.end(),BG_BMP_RAM(4)));
    assert(std::equal(probe.palette.begin(),probe.palette.end(),BG_PALETTE));
    assert(std::equal(probe.obj_palette.begin(),probe.obj_palette.end(),SPRITE_PALETTE));
    if(probe.waits==1) return;
    const int tick=probe.waits-2;
    const bool fading=tick>=probe.lines;
    const int fade=fading ? (tick-probe.lines+1)/5 : 0;
    const int amount=fading ? coefficient(15-fade) : coefficient(tick/7);
    const u16 master=fading && fade>=15 ? (2<<14)|16 : amount ? (1<<14)|amount : 0;
    assert(REG_MASTER_BRIGHT==master && !video_bomb_effect_active());
    assert(REG_WININ==0x1d1d && test_bgs[0].priority==1);
    assert(test_bgs[3].x==probe.cam && test_bgs[3].y==0 && test_bgs[2].y==0);
    assert(test_objs[OAM_SLOT_BOSS_ARROW].hidden && test_objs[OAM_SLOT_ORB_ARROW].hidden);
    const int origin=probe.center/2-8;
    for(int y=0;y<192;++y) for(int x=0;x<256;++x) {
        const int distance=std::abs(y-origin);
        const bool stripe=!fading && distance<tick && distance%2==0;
        const u16 expected=fading ? 0x8000 : stripe ? 0xffff : probe.image[y*256+x];
        assert(BG_BMP_RAM(16)[y*256+x]==expected);
    }
    const size_t uploaded=test_dma_copy_bytes-probe.uploads;probe.uploads=test_dma_copy_bytes;
    if(tick>0 && tick<probe.lines) {
        int rows=0;
        if(tick%2) {
            const int step=tick-1;
            for(int y:{probe.center-step*2,probe.center+step*2})
                if(y>=16 && y<400) ++rows;
            if(step==0 && rows) rows=1;
        }
        assert(uploaded==size_t(rows*512));
    } else if(tick==probe.lines) assert(uploaded==256*192*2);
    else if(fading) assert(uploaded==0);
    if(fading) {
        assert(test_objs[0].size==SpriteSize_16x16 && !std::memcmp(test_objs[0].gfx,Miko_tiles,128));
        for(int i=1;i<48;++i) assert(test_objs[i].hidden);
    }
    // Input cannot truncate or advance the blocking source effect.
    test_keys_down=KEY_SELECT|KEY_START|KEY_A;test_keys_held=KEY_RIGHT|KEY_B;
}
}
void test_kikuri_defeat()
{
    timeline();
    const int score=stage_score,bombs=rem_bombs,speed=pellet_speed;const unsigned timer=stage_timer;
    const bool old_invincible=invincible;invincible=true;
    const auto heap=test_heap_count,bytes=test_heap_bytes,objects=test_obj_attempts;
    const u16 master=REG_MASTER_BRIGHT,sub=REG_MASTER_BRIGHT_SUB;
    KikuriPhase1State s{};s.phase=6;s.health.hp=0;s.checkpoint=true;s.lightball_step=5;
    s.souls[0]={152,94,1,0,0,true};s.souls[1]={456,94,2,1,1,true};
    s.tears[0]={301,392,25,1,1,true};
    KikuriView view;assert(!view.defeat(s));
    for(int cam:{0,32,64}) for(int center:{16,167,168,399}) {
        assert(view.begin());auto bad=s;bad.health.hp=1;assert(!view.defeat(bad));
        bad=s;bad.phase=5;assert(!view.defeat(bad));bad=s;bad.checkpoint=false;assert(!view.defeat(bad));
        player_initialize();player_update(KEY_A,KEY_A);assert(player_get_state().spritesheet==PLAYER_48);
        orb_initialize();orb_set_position(300,center-16);camera.left=cam;
        shots_initialize();shots_add(240,200);Pellets.init();Pellets.add_single(100,140,0,32);
        Pellets.spawn_with_cloud=true;Pellets.add_single(100,140,0,32);Pellets.spawn_with_cloud=false;
        Pellets.reset_nonclouds();assert(!Pellets.at(0).moving && Pellets.at(1).cloud_frame);
        view.prepare_combat(s);canvas_begin_frame();canvas_playfield_clip(true);view.draw_combat(s);
        player_invincibility_render_frame();player_render_frame();shots_render_frame();orb_render_frame();pellet_render_frame();
        canvas_playfield_clip(false);canvas_buffer_prepare();REG_VCOUNT=192;assert(view.present_combat(4));
        probe.waits=0;probe.center=center;probe.lines=std::max(center,399-center)/2+1;probe.cam=cam;
        probe.player=player_get_state();probe.orb=orb_get_state();probe.shots=shots_get_state();probe.pellets=Pellets;
        probe.state=s;probe.actual=&s;probe.timer=stage_timer;probe.score=stage_score;probe.bombs=rem_bombs;probe.speed=pellet_speed;
        probe.allocations=test_heap_attempts;probe.objects=test_obj_attempts;probe.sfx=test_effects.size();
        probe.fades=test_mima_fades;probe.stops=test_mima_stops;probe.uploads=test_dma_copy_bytes;
        probe.sub=REG_MASTER_BRIGHT_SUB;probe.blend=REG_BLDCNT;probe.alpha=REG_BLDALPHA;
        probe.image.assign(BG_BMP_RAM(16),BG_BMP_RAM(16)+256*192);
        probe.bg.assign(BG_BMP_RAM(4),BG_BMP_RAM(4)+65536);
        probe.palette.assign(BG_PALETTE,BG_PALETTE+256);probe.obj_palette.assign(SPRITE_PALETTE,SPRITE_PALETTE+256);
        const u16 window=REG_WININ;
        initialize_irand(999);const int random=irand();initialize_irand(999);
        test_heap_fail_after=0;test_vblank_hook=observe;assert(view.defeat(s));
        test_vblank_hook=nullptr;test_heap_fail_after=-1;test_keys_down=test_keys_held=0;
        assert(probe.waits==probe.lines+79 && irand()==random && REG_MASTER_BRIGHT==((2<<14)|16));
        assert(REG_WININ==window && test_bgs[0].priority==0 && REG_MASTER_BRIGHT_SUB==sub);
        assert(!view.defeat(s) && !view.present_combat() && !view.still() && !view.sprite(0,0,300,200));
        view.end();assert(test_heap_count==heap && test_heap_bytes==bytes && test_obj_attempts==objects);
        assert(REG_MASTER_BRIGHT==master && !g_canvas_target);
    }
    stage_score=score;rem_bombs=bombs;pellet_speed=speed;stage_timer=timer;invincible=old_invincible;
    player_initialize();orb_initialize();shots_initialize();Pellets.init();camera.snap_center();
    std::puts("Kikuri defeat: independent all400-height source timing, twelve camera/parity replays, sparse alternating-row expansion/native7-frame whiteout/79-frame native white->black, frozen world/RNG/resources, untouched BG/OBJ palettes/red bow, one music fade/no SFX, cloud retention, default pose, guarded replay/full cleanup/no allocations and zero unchanged-frame uploads passed");
}
