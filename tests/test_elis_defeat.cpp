#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "elis_render.hpp"
#include "main/boss/elis_defeat.hpp"
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

extern int test_mima_fades;
extern bool invincible;
extern std::vector<int> test_effects;
namespace {
int coefficient(int n) {return std::min(16,(n*16+7)/15);}
void timing_and_clear()
{
    for(int center=0;center<400;++center) {
        ElisDefeat clock(center);ElisDefeatFrame f;
        assert(clock.next(f) && f.phase==ED_SETTLE && !f.brightness);
        int top=center,bottom=center,tick=0;
        while(true) {
            top-=2;bottom+=2;++tick;
            if(top<0 && bottom>399) break; // final source line has no wait
            assert(clock.next(f) && f.phase==ED_LINES && f.line_steps==tick && f.white_steps==tick/7);
            assert(f.brightness==coefficient(tick/7));
        }
        for(int fade=1;fade<=79;++fade) {
            assert(clock.next(f) && f.phase==ED_BLACK && f.fade_steps==fade/5);
            assert(f.brightness==coefficient(15-fade/5));
        }
        assert(!clock.next(f) && !clock.next(f));
    }
    ElisPhase1 boss;boss.reset(true,true);auto &s=const_cast<ElisPhase1State&>(boss.state());
    auto &missiles=const_cast<MissilePool&>(boss.missiles());missiles.add(300,200,0,1);missiles.update(0);
    assert(shootout_laser_spawn(s.lasers[0],312,124,320,400,50,20,8));
    const auto before=s;const auto pool=missiles;boss.clear_defeat_projectiles();
    assert(!std::memcmp(&s,&before,sizeof(s)) && !std::memcmp(&pool,&missiles,sizeof(pool)));
    s.phase=5;s.checkpoint=true;s.health.hp=0;boss.clear_defeat_projectiles();
    assert(!missiles.alive_count());for(const auto &laser:s.lasers) assert(!laser.alive);
}
struct Probe {
    int waits=0,center=0,lines=0,cam=0,score=0,bombs=0,speed=0,fades=0;
    unsigned timer=0;u16 sub=0,blend=0,alpha=0;
    size_t heap=0,objects=0,uploads=0,sfx=0;
    PlayerState player{};OrbState orb{};ShotsState shots{};CPellets pellets{};
    ElisPhase1State state{};MissilePool missiles{};
    const ElisPhase1 *boss=nullptr;ElisView *view=nullptr;
    std::vector<u16> image,bg,palette,actors;
} probe;
void observe()
{
    auto &p=probe;++p.waits;assert(p.waits<=p.lines+79);
    assert(!std::memcmp(&p.state,&p.boss->state(),sizeof(p.state)) && !std::memcmp(&p.missiles,&p.boss->missiles(),sizeof(p.missiles)));
    assert(!std::memcmp(&p.player,&player_get_state(),sizeof(p.player)) && !std::memcmp(&p.orb,&orb_get_state(),sizeof(p.orb)));
    assert(!std::memcmp(&p.shots,&shots_get_state(),sizeof(p.shots)) && !std::memcmp(&p.pellets,&Pellets,sizeof(Pellets)));
    assert(stage_score==p.score && stage_timer==p.timer && rem_bombs==p.bombs && pellet_speed==p.speed);
    assert(camera.left==p.cam && test_heap_attempts==p.heap && test_obj_attempts==p.objects && test_effects.size()==p.sfx);
    assert(REG_MASTER_BRIGHT_SUB==p.sub && REG_BLDCNT==p.blend && REG_BLDALPHA==p.alpha && test_mima_fades==p.fades+1);
    assert(std::equal(p.bg.begin(),p.bg.end(),BG_BMP_RAM(4)) && std::equal(p.palette.begin(),p.palette.end(),BG_PALETTE));
    assert(std::equal(p.actors.begin(),p.actors.end(),SPRITE_PALETTE));
    assert(!p.view->defeat(p.boss->state(),p.boss->missiles()) && !p.view->compose(p.boss->state(),p.boss->missiles()));
    assert(!p.view->compose(ElisEntranceFrame{}) && !p.view->present() && !p.view->begin());
    if(p.waits==1) {
        std::copy(g_canvas_target,g_canvas_target+256*192,p.image.begin());
        const auto &body=p.state.bat_form ? p.state.bat : p.state.entity;int ink=0;
        for(int y=Camera::y(body.top);y<Camera::y(body.top)+body.height/2;++y)
            for(int x=camera.x(body.left);x<camera.x(body.left)+body.width/2;++x) ink+=g_canvas_target[y*256+x]!=0;
        assert(p.state.bat_form ? ink>0 : ink==0); // girl bg restored; bat is not replaced with a girl
        return;
    }
    const int tick=p.waits-2;const bool black=tick>=p.lines;
    const int fade=black ? (tick-p.lines+1)/5 : 0;
    const int amount=black ? coefficient(15-fade) : coefficient(tick/7);
    assert(REG_MASTER_BRIGHT==(black && fade>=15 ? 0x8010 : amount ? 0x4000|amount : 0));
    assert(REG_WININ==0x1d1d && test_bgs[0].priority==1 && !video_bomb_effect_active());
    assert(test_bgs[3].x==p.cam && !test_bgs[3].y && !test_bgs[2].y);
    assert(test_objs[OAM_SLOT_BOSS_ARROW].hidden && test_objs[OAM_SLOT_ORB_ARROW].hidden);
    const int origin=p.center/2-8;
    for(int y=0;y<192;++y) for(int x=0;x<256;++x) {
        const int distance=std::abs(y-origin);const bool stripe=distance<tick && distance%2==0;
        assert(BG_BMP_RAM(16)[y*256+x]==(black ? 0x8000 : stripe ? 0xffff : p.image[y*256+x]));
    }
    const size_t uploaded=test_dma_copy_bytes-p.uploads;p.uploads=test_dma_copy_bytes;
    if(tick>0 && tick<p.lines) {
        int rows=0;if(tick%2) {for(int y:{p.center-(tick-1)*2,p.center+(tick-1)*2}) if(y>=16 && y<400) ++rows;if(tick==1 && rows) rows=1;}
        assert(uploaded==size_t(rows*512));
    } else if(tick==p.lines) assert(uploaded==256*192*2);
    else if(black) assert(!uploaded);
    if(black) {
        assert(test_objs[0].size==SpriteSize_16x16 && !std::memcmp(test_objs[0].gfx,Miko_tiles,128));
        for(int slot=1;slot<48;++slot) assert(test_objs[slot].hidden);
    }
    test_keys_down=KEY_SELECT|KEY_START|KEY_A;test_keys_held=KEY_RIGHT|KEY_B; // blocking effect ignores input
}
}
void test_elis_defeat()
{
    timing_and_clear();const auto blocks=test_heap_count,bytes=test_heap_bytes,objects=test_obj_attempts;
    const int score=stage_score,bombs=rem_bombs,speed=pellet_speed;const auto timer=stage_timer;const bool inv=invincible;
    probe.image.resize(256*192);probe.bg.resize(65536);probe.palette.resize(256);probe.actors.resize(256);
    for(int bat=0;bat<2;++bat) {
        camera.left=bat*64;video_stage_fade_out();ElisView view;ElisPhase1 boss;boss.reset(true,true);
        auto &s=const_cast<ElisPhase1State&>(boss.state());s.phase=5;s.checkpoint=true;s.health.hp=0;
        s.image=6;s.attack_overlay=true;s.flash=true;s.shake=2;s.transforming=true;s.bat_form=bat;
        s.bat.left=s.bat_draw_x=552;s.bat.top=s.bat_draw_y=128;s.bat_shown=1;
        assert(!view.defeat(s,boss.missiles()) && view.begin());
        for(int guard=0;guard<3;++guard) {auto bad=s;if(guard==0) bad.health.hp=1;else if(guard==1) bad.phase=4;else bad.checkpoint=false;assert(!view.defeat(bad,boss.missiles()));}
        player_initialize();player_update(KEY_A,KEY_A);orb_initialize();orb_set_position(300,151+bat);shots_initialize();shots_add(240,220);
        Pellets.init();Pellets.add_single(80,180,64,32);Pellets.spawn_with_cloud=true;Pellets.add_single(80,180,64,32);Pellets.spawn_with_cloud=false;
        Pellets.reset_nonclouds();assert(!Pellets.at(0).moving && Pellets.at(1).cloud_frame);
        canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,boss.missiles()));
        canvas_playfield_clip(false);canvas_buffer_prepare();REG_VCOUNT=192;assert(view.present());
        probe.waits=0;probe.center=167+bat;probe.lines=std::max(probe.center,399-probe.center)/2+1;probe.cam=camera.left;
        probe.boss=&boss;probe.view=&view;probe.state=s;probe.missiles=boss.missiles();probe.player=player_get_state();probe.orb=orb_get_state();
        probe.shots=shots_get_state();probe.pellets=Pellets;probe.score=stage_score;probe.timer=stage_timer;probe.bombs=rem_bombs;probe.speed=pellet_speed;
        probe.heap=test_heap_attempts;probe.objects=test_obj_attempts;probe.uploads=test_dma_copy_bytes;probe.sfx=test_effects.size();probe.fades=test_mima_fades;
        probe.sub=REG_MASTER_BRIGHT_SUB;probe.blend=REG_BLDCNT;probe.alpha=REG_BLDALPHA;
        std::copy(BG_BMP_RAM(4),BG_BMP_RAM(4)+65536,probe.bg.begin());std::copy(BG_PALETTE,BG_PALETTE+256,probe.palette.begin());
        std::copy(SPRITE_PALETTE,SPRITE_PALETTE+256,probe.actors.begin());
        const u16 window=REG_WININ;initialize_irand(817);const int random=irand();initialize_irand(817);
        if(bat) {video_bomb_prepare(true,90,7);video_bomb_commit();assert(video_bomb_effect_active());}
        test_heap_fail_after=0;test_vblank_hook=observe;assert(view.defeat(s,boss.missiles()));
        test_vblank_hook=nullptr;test_heap_fail_after=-1;test_keys_down=test_keys_held=0;
        assert(probe.waits==probe.lines+79 && irand()==random && REG_MASTER_BRIGHT==0x8010 && REG_MASTER_BRIGHT_SUB==probe.sub);
        assert(REG_WININ==window && test_bgs[0].priority==0 && !view.defeat(s,boss.missiles()) && !view.present());
        video_stage_fade_out(0);view.end();video_stage_reveal();
        assert(!g_canvas_target && test_heap_count==blocks && test_heap_bytes==bytes && test_obj_attempts==objects && !REG_MASTER_BRIGHT);
    }
    stage_score=score;rem_bombs=bombs;pellet_speed=speed;stage_timer=timer;invincible=inv;
    player_initialize();player_is_hit=false;orb_initialize();shots_initialize();Pellets.init();camera.snap_center();
    std::puts("Elis defeat:400-center timing, girl/bat parity/camera edges, guarded missile/laser clear and pending clouds, sparse stripes/native7/5 fade to black/default pose, frozen world/RNG/palettes/resources, input/reentry/allocation guards and black-safe cleanup passed");
}
