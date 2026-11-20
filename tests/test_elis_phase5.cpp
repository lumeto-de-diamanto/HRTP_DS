#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "main/boss/elis_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/random.hpp"

extern bool invincible;
namespace {
ElisPhase1State &state(ElisPhase1 &boss) {return const_cast<ElisPhase1State&>(boss.state());}
void start(ElisPhase1 &boss,bool bat,int pattern)
{
    boss.reset(true,true);auto &s=state(boss);s.phase=5;s.health.hp=s.hp_displayed=6;
    s.bat_form=bat;s.pattern=pattern;s.attack_started=!bat;s.image=6;
    player_initialize();player_is_hit=false;shots_initialize();orb_set_position(608,350);Pellets.init();
}
void rng_matches(const IrandState &expected) {const auto actual=irand_state();assert(!std::memcmp(&actual,&expected,sizeof(actual)));}
void transform_model(ElisPhase1 &boss,bool to_girl)
{
    auto &s=state(boss);ElisRift rifts[5]{};int shake=0;
    const auto body=to_girl ? s.bat : s.entity;
    for(int f=1;f<=101;++f) {
        const auto before=irand_state();
        if(f>=20 && f<=100 && f%4==0) for(int i=0;i<5;++i) {
            if(f==20 || f%16==(i*4)%16) {rifts[i].x=body.left+irand()%192-(to_girl ? 72 : 32);rifts[i].y=body.top+irand()%128-(to_girl ? 48 : 32);}
            if(f<100) {rifts[i].cel=4+irand()%2;rifts[i].color=irand()%16;}
        }
        const auto after=irand_state();irand_restore(before);boss.update(!to_girl);rng_matches(after);
        assert(s.health.hp==6 && !s.checkpoint); //phase4 ignores even qualifying bomb damage
        if(f>=20 && f<=100 && f%2==0) shake=f%4;
        assert(s.shake==(f==101 ? 0 : shake));
        if(f>=20) assert(!std::memcmp(s.rifts,rifts,sizeof(rifts)));
        assert(s.rifts_visible==(f>=20 && f<100));
    }
    assert(!s.frame && s.bat_form==!to_girl && !s.transforming);
    if(to_girl) assert(s.image==6 && s.bat_shown==-1 && s.entity.left==std::clamp(body.left-40,0,512) && s.entity.top==std::max(64,body.top-32));
    else assert(s.image==-1 && s.bat_shown==-1 && s.bat.left==296 && s.bat.top==128);
}
void transformation()
{
    ElisPhase1 boss;boss.reset(true,true);auto &s=state(boss);s.pattern=0;s.frame=80;s.health.hp=6;
    boss.update();assert(s.phase==2); // first block still must return through phase2
    // Enter the already source-tested phase3 end boundary without replaying its
    // pattern matrix. This slice starts with a fresh fixed-center return.
    s.phase=3;s.frame=80;s.teleport_done=false;boss.update();assert(s.phase==4 && !s.frame);
    for(int i=0;i<81;++i) boss.update(true);
    assert(s.phase==4 && s.teleport_done && s.health.hp==6 && !s.frame);
    initialize_irand(641);transform_model(boss,false);assert(s.phase==5);
}
void bats()
{
    // Rank-dependent slow spreads on all ranks; the other three patterns have
    // rank-independent cadence, so test each once rather than a practice matrix.
    for(int trial=0;trial<7;++trial) {
        const int pattern=trial<4 ? 1 : trial-2;rank=static_cast<rank_t>(trial<4 ? trial : 1);
        ElisPhase1 boss;start(boss,true,pattern);auto &s=state(boss);initialize_irand(73+trial);
        int x=296,y=128,vx=0,vy=0,lock=0,cel=0,shown=-1,dx=x,dy=y,until=0;
        for(int f=1;f<1200;++f) {
            if(!lock) {x=std::clamp(x+vx,0,736);y=std::clamp(y+vy,64,304);dx=x;dy=y;shown=cel;lock=1;}
            else lock=lock==2 ? 0 : lock+1;
            if(f%9==0) cel=0;else if(f%9==3) cel=1;else if(f%9==6) cel=2;
            const auto before=irand_state();bool arrived=false;int clock=f;
            if(f<3) vx=vy=0;
            else {
                if(f==3) {
                    int tx,ty;do {tx=irand()%512;ty=96+irand()%136;} while(std::abs(tx-x)<128 && std::abs(ty-y)<64);
                    const int angle=iatan2(ty-y,tx-x);vx=polar_x_fast(0,2,angle);vy=polar_y_fast(0,2,angle);until=std::abs(tx-x)/2;
                }
                irand(); // source's zero-valued gravity still advances RNG
                if(f/3>=until) {vx=vy=0;clock=0;arrived=true;}
            }
            int count=0,rain_angle=-1;pellet_group_t group=PG_NONE;
            if(pattern==1 && clock%16==0) {
                const pellet_group_t groups[]={PG_3_SPREAD_WIDE_AIMED,PG_5_SPREAD_WIDE_AIMED,PG_5_SPREAD_NARROW_AIMED,PG_5_SPREAD_NARROW};
                group=groups[trial];count=trial==0 ? 3 : 5;
            } else if(pattern==2 && clock%4==0) {group=irand()&1 ? PG_2_SPREAD_NARROW_AIMED : PG_2_SPREAD_WIDE_AIMED;count=2;}
            else if(pattern==3 && clock%4==0) {rain_angle=irand();count=1;}
            else if(pattern==4 && clock%4==0) {
                if(clock%50<10) {group=PG_2_SPREAD_NARROW_AIMED;count=2;}
                else if(clock%50>=25 && clock%50<35) {group=PG_3_SPREAD_NARROW_AIMED;count=3;}
            }
            const auto after=irand_state();irand_restore(before);Pellets.init();boss.update();rng_matches(after);
            assert(s.bat.left==x && s.bat.top==y && s.bat_draw_x==dx && s.bat_draw_y==dy && s.bat_shown==shown && s.bat_image==cel);
            assert(s.frame==clock && s.transforming==arrived && Pellets.alive_count()==count);
            for(int i=0;i<count;++i) {
                const auto &p=Pellets.at(i);assert(p.cur_left.to_pixel()==x+20 && p.cur_top.to_pixel()==y+12);
                if(rain_angle>=0) assert(p.angle==rain_angle && p.motion_type==PM_GRAVITY && p.speed.v==1);
                else assert(p.from_group==group);
            }
            if(arrived) {
                assert(count>0); // terminal frame0 attack must not be skipped
                if(trial==0) {initialize_irand(991);transform_model(boss,true);}
                break;
            }
            assert(f<1199);
        }
    }
    rank=RANK_NORMAL;
}
void girls()
{
    for(int r=0;r<4;++r) {
        rank=static_cast<rank_t>(r);ElisPhase1 boss;start(boss,false,1);auto &s=state(boss);
        int count=0;const int base=r==0 ? 2 : r==3 ? 4 : 3;
        for(int f=1;f<=120;++f) {
            boss.update();
            if(f==50 || f==60 || f==70 || f==80) {
                const int pairs=f==80 ? 10 : 4,angle=f==50 ? 64 : f==60 ? 48 : 24;
                for(int i=0;i<pairs;++i) for(int side=0;side<2;++side) {
                    const auto &p=Pellets.at(count++);const int a=f==80 ? i*6 : angle;
                    const int speed=(f==80 ? base+2 : base+i)*16+base_speed_for_rank(),direction=side ? a : 128-a;
                    assert(p.cur_left.to_pixel()==(side ? 320 : 312) && p.cur_top.to_pixel()==136);
                    assert(p.angle==direction && p.velocity.x.v==polar_x_fast(0,speed,direction) && p.velocity.y.v==polar_y_fast(0,speed,direction));
                }
            }
            assert(Pellets.alive_count()==count);
        }
        assert(count==44 && s.transforming && !s.frame && !s.pattern);
        start(boss,false,3);s.entity.left=r==0 ? 0 : 256;const int interval=r==0 ? 40 : r==1 ? 30 : r==2 ? 20 : 15;
        for(int f=1;f<=162;++f) {
            Pellets.init();boss.update();
            if(f<162) {
                const int n=f>=50 && f%interval==0 ? 10 : 0;assert(Pellets.alive_count()==n);
                for(int i=0;i<n;++i) assert(Pellets.at(i).from_group==PG_5_SPREAD_WIDE_AIMED && Pellets.at(i).cur_left.to_pixel()==s.entity.left+(i<5 ? 22 : 100));
                if(f>=50 && f%(interval*2)==0) {const auto &laser=s.lasers[(f/10)%10];assert(laser.alive && laser.speed==6 && laser.width==8 && laser.moveout_age==20);}
            } else {
                int n=0;bool negative=false;
                for(int i=0;i<32;++i) {
                    const int x=polar_x_fast(s.entity.left+64,128,i*8),y=polar_y_fast(124,128,i*8);
                    if(x< -8 || x>=640 || y<56 || y>400) continue;
                    const auto &p=Pellets.at(n++);assert(p.cur_left.v==x*16 && p.cur_top.v==y*16 && p.from_group==PG_1);negative|=x<0;
                }
                assert(Pellets.alive_count()==n && (r!=0 || negative));
            }
        }
        assert(s.transforming && !s.frame && s.circle==ElisPhase1State::None);
    }
    rank=RANK_NORMAL;
}
void safety_and_hits()
{
    ElisPhase1 boss;start(boss,false,2);auto &s=state(boss);s.bat.left=400;s.bat.top=160;
    int rain=0,late=0,post=0;
    for(int f=1;f<=382;++f) {
        Pellets.init();boss.update();
        if(f>=123 && f<=321) {
            const int counter=f-121;rain+=f%2==0;late+=counter>180 && counter%4==0;
            assert(Pellets.alive_count()==(f%2==0)+(counter>180 && counter%4==0));
        }
        if(f>=322 && f<=381 && (f-121)%8==0) {
            ++post;assert(Pellets.alive_count()==1 && Pellets.at(0).cur_left.to_pixel()==460 && Pellets.at(0).cur_top.to_pixel()==204);
        }
        assert(!player_is_hit && s.hit_pause==0);
    }
    assert(rain==99 && late==5 && post==7 && s.transforming && !s.frame);
    for(int edge:{176,177,407,408}) {
        start(boss,false,2);for(int f=1;f<=122;++f) boss.update();
        const_cast<PlayerState&>(player_get_state()).x_position=edge;boss.update();
        const bool hit=edge==176 || edge==408;assert(player_is_hit==hit && s.hit_pause==(hit ? 6 : 0));
        if(hit) {player_is_hit=false;boss.update();assert(!s.circle_override && s.circle==ElisPhase1State::None);}
    }
    start(boss,true,1);s.bat.left=100;s.bat.top=200;shots_add(100,100);shots_add(100,200);boss.update();
    assert(shots_get_state().sprite_number[0]==1 && shots_get_state().sprite_number[1]==0 && s.health.hp==6); //bat shot-top typo
    orb_set_position(84,184);boss.update();assert(s.health.hp==5); //expanded raw orb boundary
    start(boss,true,1);s.health.hp=1;player_is_hit=true;boss.update(true);
    assert(s.checkpoint && s.health.hp==0 && s.health.invincible);const auto frozen=s;
    boss.update(true);boss.update_lasers();assert(!std::memcmp(&frozen,&s,sizeof(s)));
}
}
void test_elis_phase5()
{
    pellet_speed=0;rank=RANK_NORMAL;orb_initialize();transformation();bats();girls();safety_and_hits();
    player_initialize();player_is_hit=false;orb_initialize();shots_initialize();Pellets.init();invincible=false;
    std::puts("Elis final block:101-update colored transformations/shared frame wait, locked bat movement/cel/RNG/terminal shots, all bat/girl patterns, safety bounds/stall/bat-anchor, source ring clipping/negative Q4, bat hitboxes and immediate0HP checkpoint passed");
}
