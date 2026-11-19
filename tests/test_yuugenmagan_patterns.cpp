// Production phase2..9 timing/requests, without obsolete practice checkpoints.
#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "yuugenmagan_fixture.hpp"
#include "main/math/trig.hpp"
#include "main/random.hpp"
#include "main/stage/stageobj.hpp"

extern PlayerState player;
namespace {
void transitions()
{
    rank=RANK_NORMAL;
    for(int phase:{2,4,6,8}) {
        YuugenMaganPhase1 boss;yuugenmagan_enter(boss,phase);
        const auto initial=boss.state();const int score=stage_score;
        const unsigned closing=phase==4 || phase==8 ? 12 : 3,opening=phase==8 ? 16 : closing^15;
        auto &pool=const_cast<MissilePool&>(boss.missiles());pool.add(300,240,0.25,0.125);
        for(int tick=1;tick<=70;++tick) {
            boss.update(true);const auto &s=boss.state();const int changes=tick-tick/10;
            int rgb[3];std::copy(initial.visual.color15,initial.visual.color15+3,rgb);
            const int dec=phase==4 ? 1 : phase==8 ? 2 : 0,inc=phase==2 ? 2 : phase==4 ? 0 : 1;
            rgb[dec]=std::max(0,rgb[dec]-changes);if(phase!=6) rgb[inc]+=changes;
            assert(std::equal(rgb,rgb+3,s.visual.color15));
            const unsigned mask=tick<20 ? closing : tick<40 ? closing|opening : opening;
            for(int i=0;i<5;++i) assert(s.eyes[i].orb_active==bool(mask&(1u<<i)));
            assert(s.health.hp==initial.health.hp && stage_score==score && !s.checkpoint);
            assert(tick<70 ? s.phase==phase && s.frame==tick : s.phase==phase+1 && !s.frame);
            assert(pool.at(0).x==4800+tick*4 && pool.at(0).y==3840+tick*2);
        }
    }
}
void missiles()
{
    for(int phase:{3,7}) for(int difficulty=0;difficulty<4;++difficulty) for(int first=0;first<(phase==3 ? 3 : 2);++first) {
        rank=static_cast<rank_t>(difficulty);YuugenMaganPhase1 boss;yuugenmagan_enter(boss,phase);
        const int variants=phase==3 ? 3 : 2;
        unsigned seed=0;for(;;++seed) {initialize_irand(seed);if(irand()%variants==first) break;}
        int variation[5];initialize_irand(seed);for(auto &v:variation) v=irand()%variants;
        const int random=irand();initialize_irand(seed);int pairs=0;
        for(int cycle=0;cycle<5;++cycle) {
            unsigned angles[2]{};const int aim=cycle%2 ? 608 : 0;
            for(int tick=1;tick<=369;++tick) {
                player.x_position=tick<=100 ? aim : 304;
                const_cast<MissilePool&>(boss.missiles()).reset();
                if(tick==100) {angles[0]=(iatan2(252,aim-172)-16)&255;angles[1]=(iatan2(252,aim-364)+16)&255;}
                bool emission=tick>=104 && tick<=200 && tick%8==0;
                if(tick>=201 && tick<=281 && tick%8==1) {
                    angles[0]=(angles[0]+(phase==3 || !variation[cycle] ? -2 : 2))&255;
                    angles[1]=(angles[1]+(phase==3 || variation[cycle] ? 2 : -2))&255;emission=true;
                } else if(tick>=289 && tick%8==1) {
                    if(phase==7) for(auto &a:angles) a=(a+(variation[cycle] ? -3 : 3))&255;
                    else if(variation[cycle]==0) angles[0]=(angles[0]+4)&255;
                    else if(variation[cycle]==1) angles[1]=(angles[1]-4)&255;
                    else {angles[0]=(angles[0]+2)&255;angles[1]=(angles[1]-2)&255;}
                    emission=true;
                }
                boss.update();const auto &s=boss.state();
                assert(boss.missiles().alive_count()==(emission ? 2 : 0));
                if(emission) {
                    ++pairs;
                    for(int i=0;i<2;++i) {
                        const auto &m=boss.missiles().at(i);
                        assert(m.flag==1 && !m.visible && m.x==(i ? 412 : 220)*16 && m.y==204*16);
                        assert(m.vx==((8*Cos8(angles[i]))>>8)*16 && m.vy==((8*Sin8(angles[i]))>>8)*16);
                    }
                }
                if(tick>=100) assert(s.target_left==aim-8 && s.missile_angles[0]==angles[0] && s.missile_angles[1]==angles[1]);
                assert(s.iterations==cycle+(tick==369) && !s.checkpoint && s.frame==(tick==369 ? 0 : tick));
            }
        }
        assert(pairs==175 && boss.state().phase==phase+1 && irand()==random);
    }
}
void spray()
{
    for(int difficulty=0;difficulty<4;++difficulty) for(int speed:{-4,20}) {
        rank=static_cast<rank_t>(difficulty);pellet_speed=speed;YuugenMaganPhase1 boss;yuugenmagan_enter(boss,5);
        const int intervals[]={12,8,4,2},totals[]={26,37,75,149};int pairs=0;
        for(int cycle=0;cycle<5;++cycle) for(int tick=1;tick<=446;++tick) {
            Pellets.init();
            const int counter=tick<100 ? (cycle ? 100 : 0)+tick : tick<=330 ? tick-100 : tick-346;
            const unsigned angle=tick<331 ? unsigned(-tick)&255 : unsigned(459-tick)&255;
            const bool second=tick>=331,emission=((tick>=100 && tick<=280) || second) && counter%intervals[difficulty]==0;
            boss.update();const auto &s=boss.state();assert(Pellets.alive_count()==(emission ? 2 : 0));
            if(emission) {
                ++pairs;int tuned=64+difficulty*6;tuned+=speed*tuned/40;tuned=std::max(16,tuned);
                for(int i=0;i<2;++i) {
                    const auto &p=Pellets.p[i];const unsigned a=i ? angle : (128-angle)&255;
                    assert(p.cur_left.v==(i ? 540 : 92)*16 && p.cur_top.v==160*16 && p.angle==int(a));
                    assert(p.motion_type==(second ? PM_FALL_STRAIGHT_FROM_TOP_THEN_REGULAR : PM_REGULAR));
                    assert(p.speed.v==(second ? 64 : 0));
                    assert(p.velocity.x.v==((tuned*Cos8(a))>>8) && p.velocity.y.v==((tuned*Sin8(a))>>8));
                }
            }
            assert(s.spray_frame==counter && s.pellet_angle==(tick==446 ? 0 : angle));
            assert(s.iterations==cycle+(tick==446) && !s.checkpoint);
        }
        assert(pairs==totals[difficulty]*5 && boss.state().phase==6);
    }
}
void lasers()
{
    for(int difficulty=0;difficulty<4;++difficulty) {
        rank=static_cast<rank_t>(difficulty);YuugenMaganPhase1 boss;yuugenmagan_enter(boss,9);
        for(int tick=1;tick<=491;++tick) {
            const int iteration=tick<=189 ? 0 : tick<=378 ? 1 : 2;
            const int f=tick-(iteration==2 ? 378 : iteration*189),end=iteration==2 ? 113 : 189;
            const bool fire=f>=41 && f%4==1,done=f==end;
            const int offset=f>=41 ? (f-41)/4*16 : 0;
            const int targets[]={iteration==1 ? 640-offset : offset,640-offset};
            // Strict endpoint-only collisions, including invisible terminal
            // steps and idle ink. Sample every boundary, not every player X.
            if(difficulty==1) for(int endpoint:targets) for(int left:{endpoint-32,endpoint-31,endpoint-1,endpoint,304}) {
                auto probe=boss;player.x_position=left;invincible=false;player_is_hit=false;Pellets.init();probe.update();
                const bool a=fire && targets[0]>left && targets[0]<left+32;
                const bool b=fire && iteration==2 && targets[1]>left && targets[1]<left+32;
                assert(player_is_hit==(a || b));
            }
            invincible=true;player.x_position=304;player_is_hit=false;Pellets.init();boss.update();const auto &s=boss.state();
            const unsigned count=f>=41 && !done ? (iteration==2 ? 2 : 1) : 0;
            assert(s.laser_count==count);if(count) for(unsigned i=0;i<count;++i) assert(s.laser_targets[i]==targets[i]);
            assert(s.laser_subphase==(done ? (iteration+1)*2 : iteration*2+(f>=40)) && s.frame==(done ? 0 : f));
            const int pellets=done && difficulty>=2 ? (iteration==2 && difficulty==3 ? 5 : 3) : 0;
            assert(Pellets.alive_count()==pellets);
            if(pellets) assert(Pellets.p[0].from_group==(pellets==5 ? PG_5_SPREAD_NARROW_AIMED : PG_3_SPREAD_NARROW_AIMED));
        }
        assert(boss.state().phase==10 && !boss.state().checkpoint);
    }
}
}

void test_yuugenmagan_patterns()
{
    const auto difficulty=rank;const bool inv=invincible;const int score=stage_score,speed=pellet_speed;
    pellet_speed=-4;transitions();missiles();spray();lasers();
    rank=difficulty;stage_score=score;pellet_speed=speed;player_initialize();orb_initialize();shots_initialize();Pellets.init();invincible=inv;player_is_hit=false;
    std::puts("YuugenMagan patterns: palette/hitbox shifts, both five-cycle missile variants/PLUS-offset aim/RNG, signed spray counter/speed tuning,491-update endpoint-only laser sequence and rank bursts passed");
}
