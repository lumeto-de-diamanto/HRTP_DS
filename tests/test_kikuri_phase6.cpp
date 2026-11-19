// Literal b15j phase6 oracle: real approved prefix, intercepted pellet requests,
// real global laser pass. Every rank through32 pattern boundaries (8 cycles).
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "main/boss/kikuri_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/orb.hpp"
#include "main/player/player.hpp"
#include "main/player/shot.hpp"

rank_t rank=RANK_NORMAL;
int stage_score=0;
bool player_is_hit=false;
CPellets Pellets;
namespace {
PlayerState player{300,368,PLAYER,0};
OrbState orb{10000,10000,0};orb_velocity_x_t vx=OVX_0;
struct PelletRequest {int group,x,y,angle,speed,motion,fixed;};
std::vector<PelletRequest> requests;
std::vector<int> sounds;
int random_calls=0;
int random_value(int n) {return (n*137+17)&32767;}
}
const PlayerState &player_get_state() {return player;}
bool player_is_invincible() {return true;}
const OrbState &orb_get_state() {return orb;}
orb_velocity_x_t orb_get_velocity_x() {return vx;}
void orb_set_velocity_x(orb_velocity_x_t value) {vx=value;}
bool orb_hit_by_shot(int,int) {return false;}
int irand() {return random_value(++random_calls);}
void mdrv2_se_play(int id) {sounds.push_back(id);}
void CPellets::add_single(int x,int y,int angle,subpixel_t speed,pellet_motion_t motion,subpixel_t fixed) {
    assert(!spawn_with_cloud);requests.push_back({0,x,y,angle&255,speed,int(motion),fixed});
}
void CPellets::add_single(int x,int y,int angle,subpixel_t speed,pellet_motion_t motion,subpixel_t fixed,int,int) {
    add_single(x,y,angle,speed,motion,fixed);
}
void CPellets::add_group(int x,int y,pellet_group_t group,subpixel_t speed) {
    assert(!spawn_with_cloud);requests.push_back({int(group),x,y,0,speed,0,0});
}
static void enter(KikuriPhase1 &boss)
{
    orb={10000,10000,0};boss.reset(true,true,true);
    for(int i=0;i<4;++i) {boss.update(true);for(int f=0;f<41;++f) boss.update();}
    for(int f=0;f<701;++f) boss.update();
    assert(boss.state().phase==5);
    for(int f=0;f<433;++f) {boss.update();boss.update_lasers();}
    for(int i=0;i<4;++i) {
        boss.update(true);boss.update_lasers();
        for(int f=0;f<41;++f) {boss.update();boss.update_lasers();}
    }
    const auto &s=boss.state();
    assert(s.phase==6 && !s.checkpoint && s.frame==0 && s.pattern==0 && s.patterns_done==0);
    assert(s.health.hp==6 && s.hp_displayed==6 && s.entity.orb_active);
    assert(s.lasers[0].alive && s.lasers[1].alive);
    assert(std::any_of(s.tears,s.tears+10,[](const KikuriTear &t) {return t.frame>0;}));
    requests.clear();sounds.clear();stage_score=random_calls=0;player_is_hit=false;
}

static void cycles()
{
    for(int difficulty=0;difficulty<4;++difficulty) {
        rank=rank_t(difficulty);KikuriPhase1 boss;enter(boss);
        auto expected=boss.state();
        const int spiral_intervals[]={14,8,6,4},left_speeds[]={52,56,60,64},top_speeds[]={60,64,68,72},ranges[]={0,10,16,20};
        const int ends[]={602,608,606,604};
        int pattern=0,f=0,done=0,angle=32,interval=0,rng=0,ticks=0;
        std::vector<PelletRequest> want;
        std::vector<int> want_sounds;
        auto single=[&](int x,int y,int a,int speed,int motion=PM_REGULAR,int fixed=0) {
            want.push_back({0,x,y,a&255,speed,motion,fixed});
        };
        while(done<32) {
            ++f;++ticks;assert(ticks<13000);player.x_position=(ticks*7)%609;
            want.clear();want_sounds.clear();const size_t old_requests=requests.size(),old_sounds=sounds.size();
            if(f%200==0) for(const auto &soul:expected.souls)
                want.push_back({PG_1_AIMED,soul.left+12,soul.top+12,0,40,0,0});
            for(auto &t:expected.tears) if(t.frame) {
                if(t.top<=384) {
                    if(!t.lock) {t.left=std::clamp(t.left,32,576);t.top=std::min(t.top+8,400);t.visible=true;t.lock=1;}
                    else t.lock=0;
                } else {
                    ++t.frame;if(t.frame<5) want_sounds.push_back(7);
                    if(t.frame==75) {t.frame=0;t.visible=false;}
                }
            }
            for(int i=0;i<2;++i) {
                auto &s=expected.souls[i];
                if(!s.lock) {
                    s.left=std::clamp(s.left+(i ? -1 : 1)*((4*Cos8(f))>>8),32,576);
                    s.top=std::clamp(s.top+((2*Cos8(f))>>8),64,400);s.shown_image=s.image;s.lock=1;
                } else s.lock=0;
                if(f%12==0) s.image=(s.image+1)%3;
            }
            int fire=-1,x=0,y=0,tx=0,speed=0,width=0;
            const int old_pattern=pattern;
            if(pattern==0) {
                if(f==100) {angle=32;interval=spiral_intervals[difficulty];}
                if(f>=100 && f%interval==0) {
                    for(int duplicate=0;duplicate<2;++duplicate) for(int a=0;a<256;a+=64)
                        single(320+((90*Cos8(f*4))>>8),180+((90*Sin8(f*4))>>8),angle+a,36);
                    if(f<=600) angle=(angle-5)&255;
                    else {assert(f==ends[difficulty]);f=0;pattern=1;++done;}
                }
            } else if(pattern==1) {
                for(int i=0;i<6;++i) if(f==250+i*30) {
                    interval=left_speeds[difficulty];fire=i;x=307;y=147;speed=interval;width=4;
                    tx=player.x_position+random_value(++rng)%120-60;want_sounds.push_back(6);
                }
                if(f==501) {f=0;pattern=2;++done;}
            } else if(pattern==2) {
                assert(interval==left_speeds[difficulty]); // unreachable source assignment must NOT take effect
                if(f>100 && f%interval==0) for(const auto &soul:expected.souls) {
                    for(int i=0;i<6;++i) {
                        const int speeds[]={48,32,16,16,32,48};
                        single(soul.left+12,soul.top+12,i<3 ? 0 : 128,speeds[i],PM_GRAVITY,1);
                    }
                    want_sounds.push_back(7);
                }
                if(f==251) {f=0;pattern=3;++done;}
            } else {
                if(f==100) interval=top_speeds[difficulty];
                if(f>=100 && f<=190 && f%10==0) {
                    fire=(f-100)/10;const int range=ranges[difficulty];
                    x=fire*64+random_value(++rng)%(range*2+1)-range;
                    tx=x;y=64;speed=interval;width=8;want_sounds.push_back(6);
                }
                if(f==190) {f=0;pattern=0;++done;}
            }
            const auto before=boss.state();boss.update();const auto &s=boss.state();
            assert(s.phase==6 && s.frame==f && s.pattern==pattern && s.patterns_done==done && !s.checkpoint);
            assert(s.health.hp==(done>29 ? 1 : 6) && s.hp_displayed==6 && stage_score==0 && !player_is_hit);
            assert(random_calls==rng && requests.size()==old_requests+want.size() && sounds.size()==old_sounds+want_sounds.size());
            for(size_t i=0;i<want.size();++i) {
                const auto &a=requests[old_requests+i],&b=want[i];
                assert(!std::memcmp(&a,&b,sizeof(a)));
            }
            assert(std::equal(want_sounds.begin(),want_sounds.end(),sounds.begin()+old_sounds));
            for(int i=0;i<2;++i) assert(!std::memcmp(&s.souls[i],&expected.souls[i],sizeof(KikuriSoul)));
            for(int i=0;i<10;++i) {
                assert(!std::memcmp(&s.tears[i],&expected.tears[i],sizeof(KikuriTear)));
                const auto &laser=s.lasers[i];
                if(i==fire) {
                    assert(!before.lasers[i].alive && laser.alive && laser.age==0 && laser.length==0);
                    const int a=iatan2(400-y,tx-x);
                    assert(laser.start_x==x*256 && laser.start_y==y*256 && laser.step_x==Cos8(a) && laser.step_y==Sin8(a));
                    assert(laser.speed==speed/8 && laser.width==width && laser.moveout_age==20);
                } else assert(!std::memcmp(&laser,&before.lasers[i],sizeof(laser)));
            }
            boss.update_lasers();
            if(old_pattern!=pattern) assert(s.frame==0); // no global laser reset at any pattern boundary
        }
        assert(rng==8*16); // all six aimed offsets + ten top offsets, even on Easy
        orb={300,100,0};boss.update();
        assert(boss.state().checkpoint && boss.state().health.hp==0 && boss.state().hp_displayed==0 && stage_score==7000);
        const auto frozen=boss.state();const auto count=requests.size(),sfx=sounds.size();const int calls=random_calls;
        for(int i=0;i<100;++i) {boss.update(true);boss.update_lasers();}
        assert(!std::memcmp(&frozen,&boss.state(),sizeof(frozen)) && requests.size()==count && sounds.size()==sfx && random_calls==calls);
    }
    std::puts("Kikuri phase6: all ranks/every frame through8 complete four-pattern cycles, duplicated disc origin/8 volleys/rank intervals/wrapped angles, pre-move aimed soul shots/post-move gravity rain, inherited52/56/60/64 cadence, six4px random eye lasers/ten8px top lasers/exact128 RNG calls, persistent tears/lasers/locks/cels, 30-boundary hidden1HP without HUD update and frozen zeroHP without bonus passed");
}

static void hits()
{
    rank=RANK_NORMAL;KikuriPhase1 boss;enter(boss);shots_initialize();shots_add(300,140);
    boss.update();assert(boss.state().health.hp==6 && shots_get_state().alive[0]);
    for(int i=0;i<6;++i) {
        orb={300,100,0};boss.update();orb={10000,10000,0};
        assert(boss.state().health.hp==5-i && stage_score==(i+1)*7000);
        if(i<5) {
            for(int f=0;f<41;++f) boss.update();
            assert(!boss.state().checkpoint && !boss.state().health.invincible);
        }
    }
    assert(boss.state().checkpoint && stage_score==42000); // no defeat award
    // A live slot refuses the laser, but source RNG/SFX still occur.
    enter(boss);auto &s=const_cast<KikuriPhase1State&>(boss.state());s.pattern=1;s.frame=249;
    s.lasers[0]={};assert(shootout_laser_spawn(s.lasers[0],100,100,200,400,52,20,4));
    const auto occupied=s.lasers[0];boss.update();
    assert(!std::memcmp(&occupied,&s.lasers[0],sizeof(occupied)) && random_calls==1);
    assert(sounds.back()==6);
    // Zero HP carried from a preceding block freezes safely, with no extra hit.
    enter(boss);s.health.hp=0;boss.update();assert(s.checkpoint && s.health.hp==0 && stage_score==0);
    std::puts("Kikuri phase6 contacts: real sixHP continuation, original disc orb bug/reflection/7000 hits/all41-frame grace periods, shot immunity, immediate lethal freeze/no bonus, occupied laser refusal still consumes RNG/SFX and inherited zeroHP safety passed");
}
int main() {cycles();hits();}
