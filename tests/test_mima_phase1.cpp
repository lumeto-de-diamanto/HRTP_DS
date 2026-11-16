// Portable actual Mima/missile controllers. Record pellet requests BEFORE the
// shared rank/global-speed adjustment; the oracle is source pattern arithmetic.
#include <cassert>
#include <cstdio>
#include <vector>
#include "main/boss/mima_phase1.hpp"
#include "main/boss/mima_defeat.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/math/vector.hpp"
#include "main/math/trig.hpp"

rank_t rank=RANK_NORMAL;
int stage_score=0;
bool player_is_hit=false;
CPellets Pellets;
namespace {
PlayerState player{304,player_top,PLAYER,0};
OrbState orb{10000,10000,0};
orb_velocity_x_t vx=OVX_4_LEFT;
bool invincible=true;
int roll=0,calls=0,tick=0;
std::vector<int> rolls;
size_t roll_index=0;
struct Spawn { int tick,x,y,angle,speed,motion,fixed;bool cloud; };
std::vector<Spawn> spawns;
struct Group { int tick,x,y,speed;pellet_group_t kind; };
std::vector<Group> groups;
std::vector<int> sounds;
}
const PlayerState &player_get_state() { return player; }
const OrbState &orb_get_state() { return orb; }
bool player_is_invincible() { return invincible; }
orb_velocity_x_t orb_get_velocity_x() { return vx; }
void orb_set_velocity_x(orb_velocity_x_t value) { vx=value; }
bool orb_hit_by_shot(int,int) { return false; }
int irand() { ++calls;return roll_index<rolls.size() ? rolls[roll_index++] : roll; }
void mdrv2_se_play(int id) { sounds.push_back(id); }
void CPellets::add_single(int x,int y,int angle,subpixel_t speed,pellet_motion_t motion,subpixel_t fixed)
{ spawns.push_back({tick,x,y,angle&255,speed,int(motion),fixed,spawn_with_cloud}); }
void CPellets::add_group(int x,int y,pellet_group_t kind,subpixel_t speed)
{ groups.push_back({tick,x,y,speed,kind}); }

static void missiles()
{
    MissilePool pool;
    for(int i=0;i<50;++i) assert(pool.add(300,100,0,0));
    assert(pool.alive_count()==50 && !pool.add(300,100,1,1));
    pool.reset();assert(!pool.alive_count());
    assert(pool.add(300,100,0.1,-0.1));pool.update(0);
    assert(pool.at(0).x==4801 && pool.at(0).y==1599 && pool.at(0).visible);
    const int old_y=pool.at(0).draw_y;pool.update(1);
    assert(pool.at(0).y==1598 && pool.at(0).draw_y==old_y);
    for(int angle=0;angle<256;++angle) {
        pool.reset();int dx,dy;vector2(dx,dy,64,angle);
        assert(pool.add(300,200,dx/16.0,dy/16.0));pool.update(0);
        assert(pool.at(0).cel==iatan2(dy,dx)/16 && pool.at(0).visible);
    }
    // Crossing in either parity has different visible explosion cels. The
    // unselected field retains its old position, not a flicker/half-speed move.
    for(int parity:{0,1}) {
        pool.reset();assert(pool.add(624,100,0,0));pool.update(0);
        auto &m=const_cast<Missile&>(pool.at(0));m.vx=16;sounds.clear();
        pool.update(parity);
        assert(m.flag==2 && m.x==625*16 && m.previous_x==624*16 && m.previous_y==100*16);
        assert(m.visible==(parity==0));
        if(parity==0) assert(m.cel==16);
        pool.update(2);assert(m.flag==3 && m.visible && m.cel==17 && m.draw_x==624 && m.draw_y==100);
        pool.update(3);assert(m.flag==3 && m.visible);
        pool.update(4);assert(!m.flag && !m.visible && sounds==std::vector<int>{7});
    }
    for(int side=0;side<4;++side) {
        pool.reset();assert(pool.add(side==0?0:side==1?624:300,side==2?64:side==3?399:200,0,0));pool.update(0);
        auto &m=const_cast<Missile&>(pool.at(0));
        if(side<2) m.vx=side==0?-16:16;else m.vy=side==2?-16:16;
        pool.update(2);assert(m.flag==2 && m.visible && m.cel==16);
        assert(m.draw_x==(side==0?0:side==1?624:300));
        assert(m.draw_y==(side==2?64:side==3?392:200));
    }
    invincible=false;
    for(int x:{296,297,327,328}) for(int y:{360,361,399}) {
        pool.reset();assert(pool.add(x,y,0,0));
        assert(pool.update(0)==(x>296 && x<328 && y>360 && y<400));
    }
    pool.reset();assert(pool.add(310,380,0,0));
    auto &explosion=const_cast<Missile&>(pool.at(0));explosion.flag=2;
    assert(pool.update(1)); // explosions retain source current-point collision
    invincible=true;assert(!pool.update(1));
    std::puts("Missiles: all50 slots, Q4 fractions/16 direction cels, both parity crossings/retained ink, four edge anchors, explosion lifetime/single SFX7 and strict player/explosion/invincibility collision passed");
}

static void particles()
{
    rolls={1,4,123,7,42};roll_index=0;calls=0;
    MimaPhase1 boss;boss.reset();assert(calls==1);
    boss.update();const auto &p=boss.state().particles;
    assert(calls==3 && p[1].alive && p[1].x==639*16-65 && p[1].y==123*16+40);
    boss.update();assert(calls==3 && p[1].x==639*16-130);
    boss.update();assert(calls==5 && p[2].alive && p[2].x==42*16-104 && p[2].y==64);
    assert(p[1].x==639*16-195 && p[1].y==123*16+120);
    rolls.clear();roll_index=0;roll=0;
    std::puts("Mima particles: post-entrance direction roll, first-spawn latency, alternating right/top origins, velocity-before-position RNG order and Q4 movement passed");
}

static void patterns()
{
    for(int difficulty=0;difficulty<4;++difficulty) for(int pattern=0;pattern<4;++pattern) {
        rank=rank_t(difficulty);roll=0;calls=0;spawns.clear();sounds.clear();orb={10000,10000,0};
        MimaPhase1 boss;boss.reset();auto &state=const_cast<MimaPhase1State&>(boss.state());state.pattern=pattern;
        assert(calls==1 && state.health.hp==12 && state.image==41);
        int end=0,angle=0,radius=32,locked=0;size_t emitted=0;int missile_emissions=0;
        const int speeds[]={64,72,80,88},missile_speeds[]={96,104,112,118};
        for(tick=1;tick<=1000;++tick) {
            player.x_position=100+(tick*3)%400;
            const_cast<MissilePool&>(boss.missiles()).reset();
            const size_t before=spawns.size();boss.update();
            assert(state.health.hp==12 && !state.checkpoint && !player_is_hit && !Pellets.spawn_with_cloud);
            if(pattern<3 && tick>=100 && tick%8==0) {
                MimaPoint old[2][4];
                for(int square=0;square<(pattern==2?2:1);++square) for(int i=0;i<4;++i) {
                    const unsigned char a=(square?-angle:angle)+i*64;
                    old[square][i]={polar_x_fast(320,radius,a),polar_y_fast(200,radius,a)};
                }
                angle=(angle+(pattern==0 ? (tick<260?12:-12) : pattern==1?-12:-6))&255;
                if(radius<(pattern==1?80:112)) radius+=8;
                else if(pattern==1) {
                    if(tick==224) locked=player.x_position;
                    else if(tick>240) {
                        assert(boss.missiles().alive_count()==4);++missile_emissions;
                        int dx,dy;vector2_between(320,200,locked+8,384,dx,dy,missile_speeds[difficulty]);
                        for(int i=0;i<4;++i) {
                            const auto &m=boss.missiles().at(i);
                            assert(m.x==old[0][i].x*16 && m.y==old[0][i].y*16);
                            assert(m.vx==(dx>>4)*16 && m.vy==(dy>>4)*16 && m.previous_x==-1);
                        }
                    }
                } else {
                    const int count=pattern==2?8:4;assert(spawns.size()==before+count);
                    for(int i=0;i<count;++i) {
                        const auto &s=spawns[before+i];const auto &p=old[pattern==2?i%2:0][pattern==2?i/2:i];
                        const int aim=pattern==0 && tick<=280 ? iatan2(384-p.y,player.x_position+12-p.x) : iatan2(p.y-200,p.x-320);
                        assert(s.x==p.x && s.y==p.y && s.angle==aim && s.cloud==(pattern==2));
                        assert(s.speed==(pattern==0 && tick<=280 ? speeds[difficulty]/2 : speeds[difficulty]));
                    }
                    emitted+=count;
                }
                for(int square=0;square<(pattern==2?2:1);++square) for(int i=0;i<4;++i) {
                    const unsigned char a=(square?-angle:angle)+i*64;
                    assert(state.corners[square][i].x==polar_x_fast(320,radius,a));
                    assert(state.corners[square][i].y==polar_y_fast(200,radius,a));
                }
            }
            if(pattern==2 && tick==50) assert(state.image==42);
            if(pattern==3 && spawns.size()>before) {
                const int group=int(before)/(difficulty==0?5:difficulty==1?8:difficulty==2?10:12);
                const int counts[]={5,8,10,12},hop_speeds[]={30,35,40,45};
                assert(spawns.size()==before+counts[difficulty]);
                assert(tick==83+31*group);
                for(int i=0;i<counts[difficulty];++i) {
                    const auto &s=spawns[before+i];assert(s.x==group*128+64 && s.y==176);
                    assert(s.angle==i*(256/counts[difficulty]) && s.speed==hop_speeds[difficulty]);
                    assert(s.motion==PM_CHASE && s.fixed==s.speed && !s.cloud);
                }
            }
            if(state.frame==0) { end=tick;break; }
        }
        assert(end==(pattern==0?361:pattern==3?176:321));
        assert(state.pattern==(pattern+1)%4 && !state.squares_visible);
        if(pattern==0) assert(emitted==92 && sounds.size()==93);
        if(pattern==1) assert(missile_emissions==10 && sounds.size()==11);
        if(pattern==2) assert(emitted==144 && sounds.size()==73 && state.image==43);
        if(pattern==3) assert(state.entity.left==384 && state.image==41 && state.darkness==0 && state.entity.orb_active);
    }
    // Alternating direction: source X_LEFT is1, with its asymmetric positions.
    roll=1;MimaPhase1 left;left.reset();const_cast<MimaPhase1State&>(left.state()).pattern=3;
    spawns.clear();for(tick=1;tick<=176;++tick) left.update();
    assert(left.state().entity.left==128 && spawns.front().x==576);
    std::puts("Mima first block: all four patterns/four ranks, exact old-corner origins/new-square geometry, aimed/static timing, locked-target truncated missiles, clouds, all four hops/chase speeds/directions and pattern-end ordering passed");
}

static void health_and_handoff()
{
    player.x_position=304;roll=0;orb={10000,10000,0};sounds.clear();stage_score=0;
    MimaPhase1 boss;boss.reset();
    for(int i=0;i<6;++i) boss.update(true); // explicit bomb policy is the caller's
    assert(boss.state().health.hp==6 && stage_score==30000 && boss.state().phase==1);
    for(int i=6;i<361;++i) boss.update();
    assert(boss.state().phase==2 && boss.state().frame==0 && boss.state().image==0 && !boss.state().health.invincible);
    for(int f=1;f<=52;++f) {
        boss.update();
        if(f<52) assert(boss.state().phase==2 && !boss.state().checkpoint);
        if(f>=12 && f<52 && f%4==0) assert(boss.state().image==f-10);
    }
    assert(boss.state().checkpoint && boss.state().phase==3 && boss.state().health.hp==6 && boss.state().image==41);
    const auto snapshot=boss.state();const int old_calls=calls,old_score=stage_score;
    boss.update(true);assert(boss.state().frame==snapshot.frame && calls==old_calls && stage_score==old_score);
    boss.reset();auto &s=const_cast<MimaPhase1State&>(boss.state());s.health.hp=7;
    for(int i=1;i<361;++i) boss.update();
    boss.update(true);
    assert(s.health.hp==6 && s.phase==1 && s.pattern==1); // hit AFTER boundary selection
    for(int i=1;i<=321;++i) boss.update();
    assert(s.phase==2);
    boss.reset();s.health.hp=7;
    for(int i=1;i<350;++i) boss.update();
    boss.update(true);
    for(int i=351;i<=361;++i) boss.update();
    assert(s.pattern==99 && s.phase==1 && !s.squares_visible && s.health.invincible);
    int idle=0;while(s.phase==1) { boss.update();assert(++idle<42); }
    assert(idle==30 && !s.health.invincible);
    boss.reset();stage_score=0;orb={288,152,0};vx=OVX_8_LEFT;boss.update();
    assert(s.health.hp==11 && stage_score==5000 && vx==OVX_4_RIGHT && !s.flash);
    for(int i=0;i<40;++i) boss.update();
    assert(s.health.hp==11);
    boss.update();assert(!s.health.invincible && s.health.hp==11);
    boss.update();assert(s.health.hp==10);
    orb={10000,10000,0};
    boss.reset();s.pattern=3;
    for(int i=1;i<=52;++i) boss.update(i==49);
    assert(s.flash && s.flash_darkness==4 && s.darkness==4);
    boss.update();assert(!s.flash && !s.flash_darkness && !s.darkness);
    boss.reset();for(int i=0;i<12;++i) boss.update(true);
    for(int i=0;i<500 && !s.checkpoint;++i) boss.update(true);
    assert(s.checkpoint && s.health.hp==0 && stage_score<50000+12*5000); // no fake clear bonus
    std::puts("Mima handoff: prior-orb/reflection/single5000 hit, hit flash/grace, six-HP finish-current-pattern, boundary hit deferral,99 idle, exact52-frame spread-in, zero-HP safety and frozen phase3 checkpoint without defeat bonus passed");
}

static void second_block()
{
    const int first[]={104,121,146,163,188,205,222,247};
    for(int difficulty=0;difficulty<4;++difficulty) {
        rank=rank_t(difficulty);roll=17+difficulty*77;orb={10000,10000,0};invincible=true;
        MimaPhase1 boss;boss.reset(true);auto &s=const_cast<MimaPhase1State&>(boss.state());
        s.phase=3;s.health.hp=s.hp_displayed=6;groups.clear();sounds.clear();
        const int samples[]={roll,roll+47,roll+223,roll+109,roll+159,roll+23,roll+37,roll+83};
        const int centers[]={16+samples[0]%48,624-samples[1]%48,64+samples[2]%224,336+samples[3]%224,
            128+samples[4]%160,336+samples[5]%160,64+samples[6]%224,336+samples[7]%224};
        for(tick=1;tick<=351;++tick) {
            if(tick==100) { rolls.assign(std::begin(samples),std::end(samples));roll_index=0; }
            const int old_calls=calls;boss.update();boss.update_lasers();
            if(tick==50) assert(s.image==42);
            if(tick==100) { assert(calls==old_calls+8 && roll_index==8);rolls.clear();roll_index=0; }
            if(tick==240) assert(s.image==43);
            for(int i=0;i<8;++i) {
                const auto &p=s.pillars[i];
                if(tick>=100) assert(p.center_x==centers[i]);
                const int age=(tick-first[i])/8;
                if(tick<first[i]) assert(!p.radius && !p.ray && !p.height);
                else if(age<11) assert(p.radius==120-age*8 && p.ray && !p.height);
                else if(age<13) assert(!p.radius && !p.ray && p.height==(age==11?32:160));
                else assert(p.time==-9999 && !p.radius && !p.ray && !p.height);
            }
            assert(s.frame==(tick==351?0:tick) && !s.checkpoint && s.health.hp==6);
        }
        assert(s.pattern==1 && groups.size()==8);
        for(int i=0;i<8;++i) {
            const auto &g=groups[i];assert(g.tick==first[i]+104 && g.x==320 && g.y==200);
            assert(g.kind==PG_3_SPREAD_WIDE_AIMED && g.speed==36);
        }
        assert(sounds.size()==17); // cast setup8, eight circles12, eight pillars7
        for(int x:{259,260,261,307,308,309}) for(bool safe:{false,true}) for(int frame:{191,192}) {
            boss.reset(true);s.phase=3;s.frame=frame;
            for(auto &p:s.pillars) p.time=-9999;
            s.pillars[0].time=32;s.pillars[0].center_x=300;
            invincible=safe;player.x_position=x;player_is_hit=false;boss.update();
            assert(player_is_hit==(!safe && frame==191 && x>260 && x<308));
        }
        invincible=true;player_is_hit=false;
        for(int pattern=1;pattern<=3;++pattern) {
            boss.reset(true);s.phase=3;s.pattern=pattern;s.health.hp=s.hp_displayed=6;
            spawns.clear();sounds.clear();int angle=0,radius=32,fire=pattern==2?128:0;
            int emitted=0,laser_spawns=0;ShootoutLaser expected[10]{};
            const int end=pattern==1?341:pattern==2?371:301;
            for(tick=1;tick<=end;++tick) {
                player.x_position=100+(tick*3)%400;
                const_cast<MissilePool&>(boss.missiles()).reset();
                const size_t before=spawns.size();boss.update();
                if(pattern==3 && tick==50) assert(s.image==42);
                if(tick>=100 && tick%8==0) {
                    MimaPoint old[4];
                    for(int i=0;i<4;++i) old[i]={polar_x_fast(320,radius,angle+i*64),polar_y_fast(200,radius,angle+i*64)};
                    angle=(angle+(pattern==3?3:pattern==2 && tick>270?12:-12))&255;
                    if(radius<80) radius+=8;
                    else if(pattern==1 && tick>180 && tick%16==8) {
                        ++emitted;const int speed=difficulty<2?4:5;
                        assert(boss.missiles().alive_count()==4);
                        for(int i=0;i<4;++i) {
                            const auto &m=boss.missiles().at(i);
                            assert(m.x==old[i].x*16 && m.y==old[i].y*16);
                            assert(m.vx==((speed*Cos8(fire))>>8)*16 && m.vy==((speed*Sin8(fire))>>8)*16);
                        }
                        fire=(fire+13)&255;
                    } else if(pattern==2 && tick>180) {
                        ++emitted;fire=(fire+(tick>270?12:-12))&255;
                        assert(spawns.size()==before+4);
                        for(int i=0;i<4;++i) {
                            const auto &p=spawns[before+i];assert(p.x==old[i].x && p.y==old[i].y);
                            assert(p.angle==fire && p.speed==32+difficulty*8 && !p.cloud && p.motion==PM_REGULAR);
                        }
                    }
                    for(int i=0;i<4;++i) {
                        assert(s.corners[0][i].x==polar_x_fast(320,radius,angle+i*64));
                        assert(s.corners[0][i].y==polar_y_fast(200,radius,angle+i*64));
                    }
                }
                if(pattern==3 && tick>180 && tick<300) {
                    const int i=tick%4;
                    if(!expected[i].alive) {
                        ++laser_spawns;auto &l=expected[i];l={};
                        l.start_x=s.corners[0][i].x*256;l.start_y=s.corners[0][i].y*256;
                        const int a=iatan2(400-s.corners[0][i].y,player.x_position+14-s.corners[0][i].x);
                        l.step_x=Cos8(a);l.step_y=Sin8(a);l.speed=difficulty<2?6:7;
                        l.moveout_age=20;l.width=4;l.alive=l.damaging=true;
                    }
                }
                for(int i=0;i<10;++i) {
                    const auto &a=s.lasers[i],&e=expected[i];
                    assert(a.alive==e.alive && a.start_x==e.start_x && a.start_y==e.start_y);
                    assert(a.step_x==e.step_x && a.step_y==e.step_y && a.speed==e.speed);
                    assert(a.age==e.age && a.length==e.length && a.width==e.width && a.moveout_age==e.moveout_age);
                }
                boss.update_lasers();for(auto &l:expected) shootout_laser_update(l);
                assert(s.frame==(tick==end?0:tick) && !s.checkpoint && s.health.hp==6);
            }
            assert(s.pattern==(pattern+1)%4 && !s.squares_visible && !s.square_count);
            if(pattern==1) assert(emitted==10 && sounds.size()==11);
            if(pattern==2) assert(emitted==24 && spawns.size()==96 && sounds.size()==25);
            if(pattern==3) { assert(laser_spawns>=8 && sounds.size()==1);for(int i=4;i<10;++i) assert(!s.lasers[i].alive); }
        }
    }
    // Actual approved prefix reaches a live second block with the same HP,
    // then zero HP freezes combat without a manufactured clear/bonus event.
    MimaPhase1 boss;boss.reset(true);orb={10000,10000,0};stage_score=0;
    for(int i=0;i<413;++i) boss.update(i<6);
    assert(boss.state().phase==3 && boss.state().health.hp==6 && !boss.state().checkpoint);
    for(int i=0;i<6;++i) boss.update(true);
    assert(boss.state().checkpoint && boss.state().health.hp==0 && stage_score==60000);
    const int old_calls=calls;const auto snapshot=boss.state();boss.update(true);boss.update_lasers();
    assert(boss.state().frame==snapshot.frame && stage_score==60000 && calls==old_calls);
    std::puts("Mima second block: four patterns/all ranks, eight exact RNG ranges/staggered circles/rays,32/160px byte-aligned pillars and strict cadence-only collisions, aimed spreads, old-corner semicircle missiles/spray, new-corner four-slot lasers/truncated speeds/live refusal/persistence, real six-HP continuation and zero-HP review stop passed");
}

static void defeat_timing()
{
    for(int center=0;center<400;++center) {
        int top=center,bottom=center,lines=0;
        do { top-=2;bottom+=2;++lines; } while(top>=0 || bottom<=399);
        MimaDefeat animation(center);MimaDefeatFrame f;int waits=0;
        while(animation.next(f)) {
            if(waits==0) assert(f.phase==MD_SETTLE && !f.line_steps && !f.white_steps);
            else if(waits<lines) assert(f.phase==MD_LINES && f.line_steps==waits && f.white_steps==waits/7);
            else {
                const int frame=waits-lines+1;
                assert(f.phase==MD_FADE && !f.line_steps && f.fade_steps==frame/5);
            }
            ++waits;
        }
        assert(waits==lines+79 && f.phase==MD_SETTLE); // next() clears exhausted output
    }
    MimaPhase1 boss;boss.reset(true);auto &s=const_cast<MimaPhase1State&>(boss.state());
    auto &pool=const_cast<MissilePool&>(boss.missiles());
    assert(pool.add(300,100,0,0));s.lasers[0].alive=true;
    boss.clear_defeat_projectiles();assert(pool.alive_count()==1 && s.lasers[0].alive);
    s.phase=3;s.checkpoint=true;s.health.hp=0;boss.clear_defeat_projectiles();
    assert(!pool.alive_count() && !s.lasers[0].alive);
    std::puts("Mima defeat timing: all400 orb heights, initial scroll-reset wait, two-pixel line expansion, seven-frame RGB4 whiteout,79-frame five-frame white-in cadence, no final-iteration extra waits, guarded projectile cleanup and frozen checkpoint passed");
}

int main() { missiles();particles();patterns();health_and_handoff();second_block();defeat_timing(); }
