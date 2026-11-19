// Actual controller, real pellets/shots/orb, source-arithmetic request oracles.
#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "main/boss/yuugenmagan_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/stage/stageobj.hpp"
#include "main/random.hpp"

extern PlayerState player;
extern bool invincible;

namespace {
void opening(YuugenMaganPhase1 &boss,int ticks=331)
{
    boss.reset();orb_set_position(10000,10000);
    for(int tick=0;tick<ticks;++tick) {Pellets.init();boss.update();}
}
void compare_pellets(const CPellets &expected)
{
    for(int i=0;i<PELLET_COUNT;++i) {
        const auto &a=Pellets.p[i],&b=expected.p[i];
        assert(a.moving==b.moving);
        if(!a.moving) continue;
        assert(a.cur_left.v==b.cur_left.v && a.cur_top.v==b.cur_top.v && a.angle==b.angle && a.speed.v==b.speed.v);
        assert(a.velocity.x.v==b.velocity.x.v && a.velocity.y.v==b.velocity.y.v && a.from_group==b.from_group);
        assert(!a.cloud_frame && a.motion_type==PM_REGULAR);
    }
}
void schedules()
{
    initialize_irand(167);const int random=irand();initialize_irand(167);
    for(int difficulty=0;difficulty<4;++difficulty) {
        rank=static_cast<rank_t>(difficulty);YuugenMaganPhase1 boss;boss.reset();
        const int intervals[]={350,300,200,130};
        int image[]={4,3},shown[]={2,2},emitted=0;
        orb_set_position(10000,10000);shots_initialize();player_is_hit=false;
        for(int tick=1;tick<=1432;++tick) {
            player.x_position=(tick*17)%609;Pellets.init();CPellets expected;expected.init();
            if(tick<=331) {
                if((difficulty==2 && tick==240) || (difficulty==3 && tick==260)) {
                    const int order[]={4,3,2,0,1},lefts[]={64,512,192,384,288},tops[]={128,128,176,176,64};
                    for(int n=0;n<(difficulty==2 ? 3 : 5);++n)
                        expected.add_group(lefts[order[n]]+28,tops[order[n]]+28,PG_5_SPREAD_WIDE_AIMED,54);
                }
            } else {
                const int f=tick-331;
                for(int eye=0;eye<2;++eye) {
                    if(f%4==2) shown[eye]=image[eye];
                    const int center=eye ? 544 : 96;
                    image[eye]=player.x_position<center-32 ? 4 : player.x_position>center+32 ? 5 : 3;
                }
                const int times[]={10,25,40,60,80,110},speeds[]={24,28,32,44,50,68};
                for(int n=0;n<6;++n) if(f%intervals[difficulty]==times[n]) {
                    expected.add_group(92,160,n==5 ? PG_1_AIMED : PG_2_SPREAD_WIDE_AIMED,speeds[n]);
                    expected.add_group(540,160,n==5 ? PG_1_AIMED : PG_2_SPREAD_WIDE_AIMED,speeds[n]);
                    emitted+=n==5 ? 2 : 4;
                }
            }
            boss.update(true && tick<=331); // even qualifying opening bombs cannot hurt it
            const auto &s=boss.state();compare_pellets(expected);
            assert(s.health.hp==16 && s.hp_displayed==std::min(16,tick) && !s.health.invincible && !s.flash);
            assert(!player_is_hit && s.checkpoint==(tick==1432));
            if(tick<331) assert(s.phase==0 && s.frame==tick);
            else if(tick<1432) assert(s.phase==1 && s.frame==tick-331 && s.interval==intervals[difficulty]);
            else assert(s.phase==2 && !s.frame && s.timed_out);
            if(tick>331) for(int i=0;i<2;++i) {
                assert(s.visual.image[i]==image[i] && s.visual.shown[i]==shown[i]);
                assert(s.eyes[i].orb_active);
            }
            if(tick>=240) for(int i=2;i<5;++i) assert(!s.eyes[i].orb_active);
        }
        assert(emitted>0);
        const auto frozen=boss.state();Pellets.init();
        for(int i=0;i<50;++i) boss.update(true);
        assert(!std::memcmp(&frozen,&boss.state(),sizeof(frozen)) && !Pellets.alive_count());
    }
    assert(irand()==random);
}
void lasers()
{
    rank=RANK_NORMAL;YuugenMaganPhase1 base;base.reset();orb_set_position(10000,10000);
    for(int frame=1;frame<=331;++frame) {
        const auto before=base;
        invincible=false;
        for(int x:{0,68,69,83,84,196,197,211,212,292,293,307,308,388,389,403,404,516,517,555,556,608}) {
            player.x_position=x;player_is_hit=false;auto probe=before;probe.update();
            bool expected=false;
            const int centers[]={92,540,220,412,316};
            for(int i=0;i<5;++i) {
                const bool active=i<2 ? frame>=161 && frame<=299 : i<4 ? frame>=181 && frame<=319 : frame>=201;
                const int width=i==1 ? 8 : 32;
                if(active && x>centers[i]-24 && x<centers[i]+24-width) expected=true;
            }
            assert(player_is_hit==expected);
            if(frame==331 && x==300) assert(player_is_hit && !probe.state().visual.lasers);
        }
        invincible=true;player_is_hit=true;base.update();assert(!player_is_hit);
    }
    // Swing/slide orb protection is NOT invincibility against these beams.
    invincible=true;opening(base,209);invincible=false;
    player_initialize();player.x_position=300;player_update(KEY_A,KEY_A);
    assert(player_is_invincible_to_orb() && !player_is_invincible());
    player_is_hit=false;base.update();assert(player_is_hit);
    player_initialize();invincible=true;player_is_hit=false;
}
void damage()
{
    rank=RANK_NORMAL;YuugenMaganPhase1 base;opening(base);
    const int score=stage_score;
    for(int eye=0;eye<5;++eye) for(int dx:{-13,-12,0,44,45}) for(int dy:{-13,-12,0,24,25}) {
        auto probe=base;auto &s=const_cast<YuugenMaganPhase1State&>(probe.state());
        for(auto &e:s.eyes) e.orb_active=false;
        s.eyes[eye].orb_active=true;s.visual.image[eye]=YMG_DOWN;
        orb_set_position(s.eyes[eye].left+dx,s.eyes[eye].top+dy);orb_set_velocity_x(OVX_8_LEFT);
        stage_score=score;probe.update();
        const bool hit=dx>=-12 && dx<=44 && dy>=-12 && dy<=24;
        assert(s.health.hp==(hit ? 15 : 16) && s.checkpoint==hit && !s.timed_out);
        assert(stage_score==score+(hit ? 5000 : 0));
        if(hit) assert(orb_get_velocity_x()==OVX_4_RIGHT && s.phase==2 && !s.frame);
        // Model uses logical image for hidden filtering, not its old shown cel.
        probe=base;auto &hidden=const_cast<YuugenMaganPhase1State&>(probe.state());
        hidden.visual.image[0]=hidden.visual.image[1]=YMG_HIDDEN;
        hidden.visual.shown[0]=hidden.visual.shown[1]=YMG_DOWN;
        hidden.eyes[eye].orb_active=false;stage_score=score;probe.update();
        assert(hidden.health.hp==16 && stage_score==score);
    }
    orb_set_position(10000,10000);shots_initialize();shots_add(92,156);
    auto probe=base;probe.update();assert(shots_get_state().alive[0] && probe.state().health.hp==16);
    // Keep an extra HP only in this oracle to observe source hit grace/flash
    // rather than stopping at the actual16->15 implementation checkpoint.
    probe=base;auto &s=const_cast<YuugenMaganPhase1State&>(probe.state());s.health.hp=17;
    orb_set_position(80,128);stage_score=score;probe.update();assert(s.health.hp==16 && !s.flash);
    for(int frame=1;frame<=41;++frame) {
        probe.update();assert(s.health.hp==16 && s.health.invincible==(frame<=40));
        assert(s.flash==(frame==40 || (frame<40 && frame%4>=2)));
    }
    probe.update();assert(s.health.hp==15 && s.checkpoint && stage_score==score+10000);
    probe=base;orb_set_position(10000,10000);stage_score=score;
    probe.update(true);assert(probe.state().health.hp==15 && probe.state().checkpoint && stage_score==score+5000);
}
}

void test_yuugenmagan_phase1()
{
    const auto saved_rank=rank;const auto old_invincible=invincible;const int score=stage_score,speed=pellet_speed;
    player_initialize();invincible=true;pellet_speed=-4;stage_score=0;
    schedules();lasers();damage();
    rank=saved_rank;stage_score=score;pellet_speed=speed;
    player_initialize();orb_initialize();shots_initialize();Pellets.init();player_is_hit=false;
    invincible=old_invincible;
    std::puts("YuugenMagan opening: all-rank pellet schedules/eye locks/HP fill, laser boundary samples/invisible331/east asymmetry, five eye hitbox edges/hidden filtering, shot pass-through and orb/bomb scoring/grace passed");
}
