// Literal b15j.cpp spiral/handoff oracle, before shared pellet speed tuning.
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>
#include "main/boss/kikuri_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/player/player.hpp"

rank_t rank=RANK_NORMAL;
int stage_score=0;
bool player_is_hit=false;
bool player_is_invincible() {return true;}
CPellets Pellets;
namespace {
OrbState orb{10000,10000,0};orb_velocity_x_t velocity=OVX_0;
struct Spawn {int frame,x,y,angle,speed,motion,fixed;bool cloud;};
struct Spin {int x,y,angle,speed,motion,fixed,cx,cy;};
struct Group {int x,y,group,speed;};
std::vector<Spawn> spawns;
std::vector<Spin> spins;
std::vector<Group> groups;
PlayerState player{100,368,PLAYER,0};
std::vector<int> sounds;
int frame=0,random_calls=0;
}
const OrbState &orb_get_state() {return orb;}
const PlayerState &player_get_state() {return player;}
orb_velocity_x_t orb_get_velocity_x() {return velocity;}
void orb_set_velocity_x(orb_velocity_x_t value) {velocity=value;}
bool orb_hit_by_shot(int,int) {return false;}
int irand() {++random_calls;return 17;}
void mdrv2_se_play(int id) {sounds.push_back(id);}
void CPellets::add_single(int x,int y,int angle,subpixel_t speed,pellet_motion_t motion,subpixel_t fixed) {
    spawns.push_back({frame,x,y,angle&255,speed,int(motion),fixed,spawn_with_cloud});
}
void CPellets::add_single(int x,int y,int angle,subpixel_t speed,pellet_motion_t motion,subpixel_t fixed,int cx,int cy) {
    spins.push_back({x,y,angle,speed,int(motion),fixed,cx,cy});
}
void CPellets::add_group(int x,int y,pellet_group_t group,subpixel_t speed) {
    groups.push_back({x,y,int(group),speed});
}

static void enter_phase4(KikuriPhase1 &boss)
{
    orb={10000,10000,0};boss.reset(true);
    for(int hit=0;hit<4;++hit) {boss.update(true);for(int f=0;f<41;++f) boss.update();}
    assert(boss.state().phase==3);
    for(int f=0;f<100;++f) boss.update(true);
    assert(boss.state().phase==4 && !boss.state().checkpoint && boss.state().frame==0);
    assert(boss.state().health.hp==10 && boss.state().entity.orb_active);
    spins.clear();groups.clear();sounds.clear();stage_score=0;
}

static void phase4()
{
    for(int difficulty=0;difficulty<4;++difficulty) {
        rank=rank_t(difficulty);KikuriPhase1 boss;enter_phase4(boss);
        int image=0,shown=0,lock=0,moves=0;
        const int intervals[]={80,60,55,50};
        const int types[]={PG_2_SPREAD_WIDE_AIMED,PG_3_SPREAD_WIDE_AIMED,PG_5_SPREAD_WIDE_AIMED,PG_5_SPREAD_WIDE};
        for(int f=1;f<=601;++f) {
            player.x_position=(f*7)%600;player.y_position=368;
            const auto old_spins=spins.size(),old_groups=groups.size();
            boss.update();const auto &s=boss.state();
            if(f%intervals[difficulty]==0) {
                assert(spins.size()==old_spins+8);
                const int aim=iatan2(368+PLAYER_H/2+4-224,player.x_position+PLAYER_W/2+4-320);
                for(int i=0;i<8;++i) {
                    const auto &p=spins[old_spins+i];
                    assert(p.x==316+((16*Cos8(i*32))>>8) && p.y==224+((16*Sin8(i*32))>>8));
                    assert(p.angle==aim && p.speed==48 && p.motion==PM_SPIN && p.fixed==32 && p.cx==312 && p.cy==224);
                }
            } else assert(spins.size()==old_spins);
            if(f==400 || f==480 || f==560) {
                assert(groups.size()==old_groups+2);
                for(int i=0;i<2;++i) {
                    const auto &g=groups[old_groups+i];
                    assert(g.x==(i ? 468 : 164) && g.y==102 && g.group==types[difficulty] && g.speed==55);
                }
            } else assert(groups.size()==old_groups);
            // Independent literal entity-lock/blit oracle: even calls during
            // ascent, every call after350; cel advancement follows publication.
            if((f>200 && f<=350 && f%2==0) || f>350) {
                if(!lock) {if(f<=350) ++moves;shown=image;lock=1;} else lock=0;
                if(f%12==0) image=(image+1)%3;
            }
            for(int i=0;i<2;++i) {
                const auto &soul=s.souls[i];
                assert(soul.visible==(f>=202) && soul.image==image && soul.shown_image==shown && soul.lock==lock);
                assert(soul.left==(f<200 ? 0 : 304+(i ? 4 : -4)*moves));
                assert(soul.top==(f<200 ? 0 : 208-3*moves));
            }
            assert(s.soul_phase==(f<350 ? 0 : f<=600 ? 1 : 2));
            assert(s.health.hp==10 && s.hp_displayed==10 && s.lightball_step==5 && !s.flash);
            assert(s.phase==(f<=600 ? 4 : 5) && s.frame==(f<=600 ? f : 0) && s.checkpoint==(f==601));
            assert(stage_score==0 && sounds.empty()); // no soul contact, RNG, SFX or bonus
        }
        const auto frozen=boss.state();const auto count=spins.size(),group_count=groups.size();
        for(int f=0;f<1000;++f) boss.update(true);
        assert(!std::memcmp(&frozen,&boss.state(),sizeof(frozen)) && spins.size()==count && groups.size()==group_count);
        boss.reset();assert(!boss.state().souls[0].visible && boss.state().soul_phase==0);
    }
    rank=RANK_NORMAL;KikuriPhase1 boss;enter_phase4(boss);
    for(int f=1;f<=641;++f) {
        orb=f==600 ? OrbState{300,100,0} : OrbState{10000,10000,0};boss.update();
        assert(boss.state().phase==(f<641 ? 4 : 5));
    }
    assert(boss.state().checkpoint && boss.state().health.hp==9 && stage_score==7000 && sounds==std::vector<int>{3});
    assert(groups.size()==8); // grace delays handoff; original spreads still fire at640
    enter_phase4(boss);
    for(int f=1;f<=601;++f) boss.update(f<=10);
    assert(boss.state().phase==5 && boss.state().health.hp==0 && stage_score==70000 && sounds.size()==10);
    assert(random_calls==0); // zero HP here is still a checkpoint, NOT defeat
    std::puts("Kikuri phase4: all-rank aimed spin rings/source offsets/speeds, two souls' alternating-call locks/pre-advance cels/byte alignment, frame350 ascent and400/480/560 spreads including static Lunatic, carried timer, hit-grace-delayed641 handoff/640 spread, zeroHP safety and frozen phase5 without bonus passed");
}

int main()
{
    for(int difficulty=0;difficulty<4;++difficulty) {
        rank=rank_t(difficulty);orb={10000,10000,0};spawns.clear();sounds.clear();stage_score=0;
        KikuriPhase1 boss;boss.reset();
        const int intervals[]={4,3,2,1},distances[]={90,90,90,50},counts[]={402,534,802,1602};
        int angle=0,drift=0;size_t emitted=0;
        for(frame=1;frame<=5400;++frame) {
            const int f=(frame-1)%900+1;
            if(f==100) angle=drift=0;
            boss.update();
            if(f>=100 && f%intervals[difficulty]==0) {
                assert(spawns.size()==emitted+2);
                for(int side=0;side<2;++side) {
                    const unsigned char a=angle+side*128;
                    const auto &actual=spawns[emitted++];
                    assert(actual.frame==frame && actual.x==320+((distances[difficulty]*Cos8(a))>>8));
                    assert(actual.y==180+((distances[difficulty]*Sin8(a))>>8));
                    assert(actual.angle==((a+drift)&255) && actual.speed==50 && actual.motion==PM_REGULAR);
                    assert(!actual.cloud && actual.fixed==0);
                }
                angle=(angle-8)&255;drift=(drift+1)&255;
            } else assert(spawns.size()==emitted);
            if(frame<5400) {
                assert(boss.state().phase==2 && boss.state().frame==frame%900);
                assert(boss.state().patterns_done==frame/900);
            }
            assert(boss.state().hp_displayed==(frame<14 ? frame : 14));
            assert(boss.state().health.hp==14 && !boss.state().flash && stage_score==0 && sounds.empty());
        }
        assert(spawns.size()==size_t(counts[difficulty]*6) && boss.state().phase==3);
        assert(boss.state().frame==0 && !boss.state().checkpoint);
    }
    std::puts("Kikuri spiral: all ranks,100-frame startup, source interval/radius/negative polar rounding, byte-wrapped angle/drift, every paired emission through six900-frame cycles, HP fill and zero RNG/SFX passed");

    for(int x=250;x<=355;++x) for(int y:{INT_MIN,-1000,0,64,131,132,179,180,224}) {
        KikuriPhase1 boss;boss.reset();orb={x,y,0};stage_score=0;sounds.clear();spawns.clear();
        boss.update();const bool hit=x>256 && x<352 && y<132 && y<180;
        assert(kikuri_hittest_orb(x,y)==hit && boss.state().health.hp==14-int(hit));
        assert(stage_score==(hit ? 7000 : 0));
        assert(sounds==(hit ? std::vector<int>{3} : std::vector<int>{}));
    }
    for(auto old:{OVX_0,OVX_4_LEFT,OVX_8_LEFT,OVX_4_RIGHT,OVX_8_RIGHT}) {
        KikuriPhase1 boss;boss.reset();orb={300,100,0};velocity=old;boss.update();
        assert(velocity==((old==OVX_4_LEFT || old==OVX_8_LEFT || old==OVX_0) ? OVX_4_RIGHT : OVX_4_LEFT));
        assert(!boss.state().flash);
        orb={10000,10000,0};
        for(int f=1;f<=41;++f) {
            boss.update();assert(boss.state().health.hp==13);
            assert(boss.state().health.invincible==(f<=40));
            assert(boss.state().flash==((f<40 && f%4>=2) || f==40));
        }
    }
    shots_initialize();shots_add(300,140);sounds.clear();orb={10000,10000,0};
    KikuriPhase1 boss;boss.reset();boss.update();
    assert(boss.state().health.hp==14 && shots_get_state().alive[0] && shots_get_state().sprite_number[0]==0 && sounds.empty());
    boss.reset();stage_score=0;sounds.clear();
    for(int hit=0;hit<4;++hit) {
        boss.update(true);assert(boss.state().health.hp==13-hit && boss.state().phase==2);
        for(int grace=1;grace<=40;++grace) {boss.update();assert(boss.state().phase==2);}
        boss.update();assert(boss.state().phase==(hit==3 ? 3 : 2));
    }
    assert(stage_score==28000 && sounds==std::vector<int>({3,3,3,3}));
    assert(!boss.state().health.invincible && boss.state().hp_displayed==10 && boss.state().frame==0);
    const size_t count=spawns.size();
    for(int f=1;f<=100;++f) {
        boss.update(true);
        assert(boss.state().lightball_step==(f+1)/20 && boss.state().health.hp==10);
        assert(boss.state().phase==(f==100 ? 4 : 3));
    }
    assert(boss.state().checkpoint && boss.state().frame==0 && spawns.size()==count && stage_score==28000);
    const auto frozen=boss.state();
    for(int f=0;f<1000;++f) boss.update(true);
    assert(!std::memcmp(&frozen,&boss.state(),sizeof(frozen)) && spawns.size()==count && stage_score==28000);
    assert(!boss_bomb_damage_allowed(true,1,50) && !boss_bomb_damage_allowed(true,0,49) && boss_bomb_damage_allowed(true,0,50));
    assert(random_calls==0);
    std::puts("Kikuri contacts: user-selected original above-disc sign bug/all strict edges, prior-orb reflection/7000 score, shot immunity, bomb policy, exact41-frame grace/flashing,10HP handoff/100-frame lightball fade and guarded phase4 checkpoint without defeat bonus passed");
    phase4();
}
