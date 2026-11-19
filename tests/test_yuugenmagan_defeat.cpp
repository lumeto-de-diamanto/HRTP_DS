#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "yuugenmagan_fixture.hpp"
#include "yuugenmagan_combat_render.hpp"
#include "main/boss/yuugenmagan_defeat.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/timer.hpp"
#include "main/random.hpp"
#include "heap_probe.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "boss_render.hpp"
#include "background.hpp"
#include "sprites/Miko.h"

extern int test_mima_fades;
namespace {
int coefficient(int n) {return std::min(16,(n*16+7)/15);}
void timing()
{
    for(int center=0;center<400;++center) {
        YuugenMaganDefeat clock(center);YuugenMaganDefeatFrame f;
        assert(clock.next(f) && f.phase==YMD_SETTLE && !f.brightness);
        int top=center,bottom=center,tick=0;
        while(true) {
            top-=2;bottom+=2;++tick;
            if(top<0 && bottom>399) break; // final source line has no wait
            assert(clock.next(f) && f.phase==YMD_LINES && f.line_steps==tick);
            assert(f.brightness==coefficient(tick/7));
        }
        for(int fade=1;fade<=79;++fade) {
            assert(clock.next(f) && f.phase==YMD_ARENA && f.fade_steps==fade/5);
            assert(f.brightness==coefficient(15-fade/5));
        }
        assert(!clock.next(f) && !clock.next(f));
    }
    YuugenMaganPhase1 boss;yuugenmagan_enter(boss,13);
    auto &s=const_cast<YuugenMaganPhase1State&>(boss.state());int ticks=0;
    while(s.phase!=13 || s.pentagram.attack_phase!=6 || s.pentagram.center_y<417 || s.frame%4!=3) {
        assert(++ticks<3000);Pellets.init();boss.update();
    }
    s.health.hp=1;orb_set_position(80,128);boss.update();
    assert(s.phase==10 && s.defeat_phase==13 && s.checkpoint && !s.health.hp); // same-update slam return
}
struct Probe {
    int waits=0,center=0,lines=0,cam=0,score=0,fades=0;
    unsigned timer=0;u16 sub=0;
    size_t heap=0,objects=0,uploads=0;
    OrbState orb{};PlayerState player{};CPellets pellets{};
    YuugenMaganPhase1State state{};MissilePool missiles{};
    const YuugenMaganPhase1 *boss=nullptr;YuugenMaganCombatView *view=nullptr;
    std::vector<u16> image,actors;
} probe;
void observe()
{
    auto &p=probe;++p.waits;assert(p.waits<=p.lines+79);
    assert(!std::memcmp(&p.state,&p.boss->state(),sizeof(p.state)) && !std::memcmp(&p.missiles,&p.boss->missiles(),sizeof(p.missiles)));
    assert(!std::memcmp(&p.orb,&orb_get_state(),sizeof(p.orb)) && !std::memcmp(&p.player,&player_get_state(),sizeof(p.player)));
    assert(!std::memcmp(&p.pellets,&Pellets,sizeof(Pellets)) && stage_timer==p.timer && stage_score==p.score);
    assert(camera.left==p.cam && test_heap_attempts==p.heap && test_obj_attempts==p.objects);
    assert(REG_MASTER_BRIGHT_SUB==p.sub && test_mima_fades==p.fades+1);
    assert(std::equal(p.actors.begin(),p.actors.end(),SPRITE_PALETTE));
    assert(!p.view->defeat(p.boss->state(),p.boss->missiles()) && !p.view->compose(p.boss->state()));
    if(p.waits==1) {std::copy(g_canvas_target,g_canvas_target+256*192,p.image.begin());return;}
    const int tick=p.waits-2;const bool arena=tick>=p.lines;
    const int amount=arena ? coefficient(15-(tick-p.lines+1)/5) : coefficient(tick/7);
    assert(REG_MASTER_BRIGHT==(amount ? 0x4000|amount : 0));
    for(int y=0;y<192;++y) for(int x=0;x<256;++x) {
        const int distance=std::abs(y-(p.center/2-8));const bool stripe=distance<tick && distance%2==0;
        assert(BG_BMP_RAM(16)[y*256+x]==(arena ? 0 : stripe ? 0xffff : p.image[y*256+x]));
    }
    const size_t bytes=test_dma_copy_bytes-p.uploads;p.uploads=test_dma_copy_bytes;
    if(tick>0 && tick<p.lines) {
        int rows=0;if(tick%2) {for(int y:{p.center-(tick-1)*2,p.center+(tick-1)*2}) if(y>=16 && y<400) ++rows;if(tick==1 && rows) rows=1;}
        assert(bytes==size_t(rows*512));
    } else if(tick==p.lines) assert(bytes==256*192*2);
    else if(arena) assert(!bytes);
    if(arena) {
        assert(test_objs[0].size==SpriteSize_16x16 && !std::memcmp(test_objs[0].gfx,Miko_tiles,128));
        for(int slot=1;slot<48;++slot) assert(test_objs[slot].hidden);
    }
    test_keys_down=KEY_SELECT|KEY_START|KEY_A; // animation must ignore input
}
}

void test_yuugenmagan_defeat()
{
    const auto difficulty=rank;const bool inv=invincible;const int score=stage_score;
    rank=RANK_NORMAL;timing();
    const auto blocks=test_heap_count,bytes=test_heap_bytes;probe.image.resize(256*192);
    for(int cam:{0,64}) for(int center:{167,168}) {
        camera.left=cam;video_stage_fade_out();YuugenMaganCombatView view;assert(view.begin());
        YuugenMaganPhase1 boss;boss.reset();auto &s=const_cast<YuugenMaganPhase1State&>(boss.state());
        s.phase=s.defeat_phase=center%2 ? 11 : 13;s.checkpoint=true;s.health.hp=0;
        s.visual.color15_override=true;s.visual.color15[0]=-67;s.visual.color15[2]=5;
        auto &pool=const_cast<MissilePool&>(boss.missiles());pool.add(300,240,0,0);pool.update(0);boss.clear_defeat_projectiles();
        assert(pool.alive_count()==(s.phase==11 ? 1 : 0));
        player_initialize();orb_initialize();orb_set_position(300,center-16);Pellets.init();
        Pellets.spawn_with_cloud=true;Pellets.add_single(100,140,0,32);Pellets.spawn_with_cloud=false;
        probe.waits=0;probe.center=center;probe.lines=std::max(center,399-center)/2+1;probe.cam=cam;
        probe.state=s;probe.missiles=pool;probe.orb=orb_get_state();probe.player=player_get_state();probe.pellets=Pellets;
        probe.boss=&boss;probe.view=&view;probe.timer=stage_timer;probe.score=stage_score;
        probe.heap=test_heap_attempts;probe.objects=test_obj_attempts;probe.uploads=test_dma_copy_bytes;
        probe.sub=REG_MASTER_BRIGHT_SUB;probe.fades=test_mima_fades;probe.actors.assign(SPRITE_PALETTE,SPRITE_PALETTE+48);
        initialize_irand(817);const int random=irand();initialize_irand(817);
        test_heap_fail_after=0;test_vblank_hook=observe;assert(view.defeat(s,pool));
        test_vblank_hook=nullptr;test_heap_fail_after=-1;test_keys_down=test_keys_held=0;
        assert(probe.waits==probe.lines+79 && irand()==random && !REG_MASTER_BRIGHT);
        assert(BG_PALETTE[15]==RGB15(10,10,10) && !view.begin() && !view.present());
        video_stage_fade_out();view.end();video_stage_reveal();
        assert(!g_canvas_target && test_heap_count==blocks && test_heap_bytes==bytes);
    }
    rank=difficulty;stage_score=score;player_initialize();orb_initialize();shots_initialize();Pellets.init();invincible=inv;player_is_hit=false;
    camera.snap_center();load_boss_background();boss_render_hide();boss_render_frame();
    std::puts("YuugenMagan defeat:400-center source timing, parity/camera edges, sparse stripes/native7/5 fade/default arena pose, frozen state/RNG/clouds, phase11/13 missiles and same-update return, replay/input/allocation guards passed");
}
