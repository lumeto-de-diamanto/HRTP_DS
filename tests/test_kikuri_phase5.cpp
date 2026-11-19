// Independent b15j phase5 oracle with real health/lasers, intercepted pellets.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "main/boss/kikuri_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/orb.hpp"
#include "main/player/player.hpp"

rank_t rank=RANK_NORMAL;
int stage_score=0;
bool player_is_hit=false;
CPellets Pellets;
namespace {
PlayerState player{300,368,PLAYER,0};
OrbState orb{10000,10000,0};orb_velocity_x_t vx=OVX_0;
bool invincible=true;
int random_calls=0,pellet_calls=0;
std::vector<int> sounds;
}
const PlayerState &player_get_state() {return player;}
bool player_is_invincible() {return invincible;}
const OrbState &orb_get_state() {return orb;}
orb_velocity_x_t orb_get_velocity_x() {return vx;}
void orb_set_velocity_x(orb_velocity_x_t value) {vx=value;}
bool orb_hit_by_shot(int,int) {return false;}
int irand() {++random_calls;return 17;}
void mdrv2_se_play(int id) {sounds.push_back(id);}
void CPellets::add_single(int,int,int,subpixel_t,pellet_motion_t,subpixel_t) {++pellet_calls;}
void CPellets::add_single(int,int,int,subpixel_t,pellet_motion_t,subpixel_t,int,int) {++pellet_calls;}
void CPellets::add_group(int,int,pellet_group_t,subpixel_t) {++pellet_calls;}

static void enter(KikuriPhase1 &boss)
{
    invincible=true;orb={10000,10000,0};boss.reset(true,true);
    for(int i=0;i<4;++i) {boss.update(true);for(int f=0;f<41;++f) boss.update();}
    for(int f=0;f<701;++f) boss.update();
    assert(boss.state().phase==5 && boss.state().frame==0 && !boss.state().checkpoint && boss.state().health.hp==10);
    stage_score=pellet_calls=0;sounds.clear();player_is_hit=false;
}

struct Tear {int x=0,y=0,age=0,lock=0,generation=0;bool visible=false;};
static bool hit(int x,int age,int px) {
    if(age<2 || age>=60) return false;
    const int left=age<10 ? x : age<20 ? x-16 : x-32;
    const int extra=age<10 ? 0 : age<20 ? 32 : 64;
    return px>=left-16 && px<=left+extra;
}
static void attacks()
{
    for(int difficulty=0;difficulty<4;++difficulty) {
        rank=rank_t(difficulty);KikuriPhase1 boss;enter(boss);
        auto souls=boss.state();Tear tears[10]{};int spawned=0,ended=0,bursts=0;
        const int intervals[]={200,160,140,120};
        for(int f=1;f<=1601;++f) {
            player.x_position=f%601;player_is_hit=false;
            if(f%intervals[difficulty]==0) for(int i=0;i<2;++i) {
                for(auto &t:tears) if(!t.age) {
                    t.x=souls.souls[i].left+12;t.y=souls.souls[i].top+8;t.age=1;t.visible=false;++t.generation;++spawned;break;
                }
            }
            for(auto &t:tears) if(t.age) {
                if(t.y<=384) {
                    if(t.lock==0) {t.x=std::clamp(t.x,32,576);t.y=std::min(t.y+8,400);t.visible=true;t.lock=1;}
                    else t.lock=0;
                } else {
                    ++t.age;if(t.age<5) ++bursts;
                    if(t.age==75) {t.age=0;t.visible=false;++ended;}
                }
            }
            const int dx=(6*Cos8(f))>>8,dy=(2*Cos8(f))>>8;
            for(int i=0;i<2;++i) {
                auto &s=souls.souls[i];
                if(!s.lock) {
                    s.left=std::clamp(s.left+(i ? -dx : dx),32,576);s.top=std::clamp(s.top+dy,64,400);
                    s.shown_image=s.image;s.lock=1;
                } else s.lock=0;
                if(f%12==0) s.image=(s.image+1)%3;
            }
            boss.update();const auto &actual=boss.state();
            for(int i=0;i<2;++i) assert(!std::memcmp(&souls.souls[i],&actual.souls[i],sizeof(KikuriSoul)));
            for(int i=0;i<10;++i) {
                const auto &a=actual.tears[i];const auto &e=tears[i];
                assert(a.left==e.x && a.top==e.y && a.frame==e.age && a.lock==e.lock && a.generation==e.generation && a.visible==e.visible);
            }
            if(f>=400 && f%200==0) {
                for(int i=0;i<2;++i) {
                    const auto &laser=actual.lasers[i];const int x=i ? 336 : 307;
                    const int target=player.x_position+(i ? -60 : 60),angle=iatan2(253,target-x);
                    assert(laser.alive && laser.age==0 && laser.length==0 && laser.speed==6 && laser.moveout_age==20 && laser.width==8);
                    assert(laser.start_x==x*256 && laser.start_y==147*256 && laser.step_x==Cos8(angle) && laser.step_y==Sin8(angle));
                }
            }
            if(f<400) for(const auto &laser:actual.lasers) assert(!laser.alive);
            const auto before=actual.lasers[0];boss.update_lasers();
            if(before.alive && f<=1600) assert(actual.lasers[0].age==before.age+1 || !actual.lasers[0].alive);
            if(f==1601) assert(!std::memcmp(&before,&actual.lasers[0],sizeof(before)));
            assert(actual.phase==(f<=1600 ? 5 : 6) && actual.frame==(f<=1600 ? f : 0));
            assert(actual.checkpoint==(f==1601) && actual.health.hp==10 && stage_score==0 && !player_is_hit);
            assert(std::count(sounds.begin(),sounds.end(),7)==bursts);
            assert(std::count(sounds.begin(),sounds.end(),6)==(f<400 ? 0 : (f-200)/200));
            assert(pellet_calls==0); // old rings/spreads stop at phase5
        }
        assert(spawned>=16 && ended>0); // slot reuse retains lock, no allocation
        const auto frozen=boss.state();for(int f=0;f<100;++f) {boss.update(true);boss.update_lasers();}
        assert(!std::memcmp(&frozen,&boss.state(),sizeof(frozen)));
    }
    assert(random_calls==0);
    std::puts("Kikuri phase5: all ranks/every1601 frames, inherited soul locks/cosine/clamps/pre-blit cels, pre-move paired tear origins/ten-slot reuse/alternating8px fall/overshoot/ripple age75 reset/repeated burst SFX, first400 then200-frame crossed6px lasers/offsets/slots/global pass, no pellets/RNG/bonus and frozen phase6 timeout passed");
}

static void contacts()
{
    for(int x=32;x<=576;x+=17) for(int age=0;age<=75;++age) for(int px=x-50;px<=x+34;++px)
        assert(kikuri_ripple_hits(x,age,px)==hit(x,age,px));
    rank=RANK_NORMAL;KikuriPhase1 boss;enter(boss);
    bool checked=false;
    for(int f=1;f<=600;++f) {
        const auto &t=boss.state().tears[0];
        if(t.frame==1 && t.top>384) {
            player.x_position=t.left;invincible=false;player_is_hit=false;boss.update();
            assert(player_is_hit);checked=true;
            invincible=true;player_is_hit=false;boss.update();assert(!player_is_hit);break;
        }
        boss.update();
    }
    assert(checked);
    // Four actual orb hits, all41-frame grace periods, no premature handoff.
    enter(boss);
    for(int i=0;i<4;++i) {
        orb={300,100,0};boss.update();orb={10000,10000,0};
        assert(boss.state().health.hp==9-i && boss.state().phase==5);
        for(int f=0;f<40;++f) {boss.update();assert(boss.state().phase==5);}
        boss.update();assert(boss.state().phase==(i==3 ? 6 : 5));
    }
    assert(boss.state().checkpoint && boss.state().health.hp==6 && stage_score==28000);
    enter(boss);for(int f=1;f<=11;++f) boss.update(f<=10);
    assert(boss.state().health.hp==0 && boss.state().phase==5);
    for(int f=0;f<40;++f) boss.update();
    assert(boss.state().phase==6 && boss.state().health.hp==0 && stage_score==70000);
    std::puts("Kikuri phase5 contacts: every ripple age/internal-X inclusive edge, real damaging ripple/invincibility, original disc orb bug/7000 hits/four41-frame grace periods/sixHP handoff, zeroHP safety without defeat bonus passed");
}
static void edges()
{
    rank=RANK_NORMAL;KikuriPhase1 boss;enter(boss);boss.update();
    auto &s=const_cast<KikuriPhase1State&>(boss.state());
    for(auto &t:s.tears) t={100,100,1,0,20,true};
    s.frame=159;boss.update();
    for(const auto &t:s.tears) assert(t.generation==20 && t.top==108 && t.lock==1);
    s.tears[3].frame=0;s.souls[0].left=576;s.souls[0].top=64;s.frame=319;
    boss.update();
    assert(s.tears[3].generation==21 && s.tears[3].left==588 && s.tears[3].top==72);
    assert(s.tears[3].lock==0 && !s.tears[3].visible); // retained lock delays first move
    boss.update();assert(s.tears[3].left==576 && s.tears[3].top==80 && s.tears[3].visible);
    for(int age=1;age<=74;++age) for(int px:{252,253,268,269,284,285,300,301,302,317,318,333,334})
    for(bool grace:{false,true}) {
        for(auto &t:s.tears) t={};
        s.tears[0]={301,392,age,0,1,true};s.frame=10;
        invincible=grace;player.x_position=px;player_is_hit=false;boss.update();
        assert(player_is_hit==(!grace && hit(301,age+1,px)));
    }
    invincible=true;enter(boss);s.frame=399;
    assert(shootout_laser_spawn(s.lasers[0],100,100,200,400,50,20,8));
    const auto occupied=s.lasers[0];boss.update();
    assert(!std::memcmp(&occupied,&s.lasers[0],sizeof(occupied)) && s.lasers[1].alive);
    enter(boss);s.frame=1599;orb={300,100,0};boss.update();orb={10000,10000,0};
    for(int f=0;f<40;++f) {boss.update();assert(s.phase==5 && !s.checkpoint);}
    boss.update();assert(s.phase==6 && s.health.hp==9 && stage_score==7000);
    std::puts("Kikuri phase5 edge injection: full ten-slot refusal/single-slot reuse/retained lock/first-move clamp, actual ripple damage at every age/internal-X edge with invincibility, occupied laser refusal and hit-grace-delayed1641 timeout passed");
}
int main() {attacks();contacts();edges();}
