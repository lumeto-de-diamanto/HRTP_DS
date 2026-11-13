#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
#include "main/boss/konngara.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/math/trig.hpp"

rank_t rank = RANK_NORMAL;
int stage_score;
bool player_is_hit;
CPellets Pellets;
namespace {
PlayerState player{304,368,PLAYER,0}; OrbState orb{10000,10000,0};
orb_velocity_x_t vx = OVX_4_LEFT;
bool invincible = true;
int roll, calls, clock_frame;
struct Shot { int frame,x,y,angle,speed,group,motion,fixed; };
std::vector<Shot> shots;
std::vector<int> sounds;
}
const PlayerState &player_get_state() { return player; }
const OrbState &orb_get_state() { return orb; }
bool player_is_invincible() { return invincible; }
bool player_is_sliding() { return false; }
orb_velocity_x_t orb_get_velocity_x() { return vx; }
void orb_set_velocity_x(orb_velocity_x_t value) { vx = value; }
bool orb_hit_by_shot(int,int) { return false; }
int irand() { ++calls; return roll; }
void mdrv2_se_play(int id) { sounds.push_back(id); }
void CPellets::add_group(int x,int y,pellet_group_t group,subpixel_t speed)
{ shots.push_back({clock_frame,x,y,-1,speed,group,PM_REGULAR,0}); }
void CPellets::add_single(int x,int y,int angle,subpixel_t speed,pellet_motion_t motion,subpixel_t fixed)
{ shots.push_back({clock_frame,x,y,angle&255,speed,0,motion,fixed}); }

static void begin(KonngaraCombat &boss,int pattern,int set = 2)
{
    boss.reset(set); orb = {10000,10000,0}; player.x_position = 304; invincible = true;
    shots_initialize(); shots.clear(); sounds.clear(); calls = 0; roll = pattern; player_is_hit = false;
    assert(boss.state().phase == set*2 && boss.state().health.hp == 18-set*3 && boss.state().health.total == 18);
    for (int i = 0; i < 121; ++i) boss.update();
    assert(boss.state().phase == set*2+1 && boss.state().frame == 0 && calls == 0 && shots.empty());
    for (int i = 0; i < (set==3 ? 5 : 120); ++i) boss.update();
    boss.update(); assert(boss.state().pattern == pattern && boss.state().frame == 1 && calls == 1);
}

static void slashes()
{
    const int first[] = {5,3,2,1}, last[] = {3,2,1,1}, speed[] = {32,48,64,72}, aimed_speed[]={64,80,88,96};
    for (int difficulty = 0; difficulty < 4; ++difficulty) for (int kind : {0,1,2}) {
        const bool triangular=kind==1,aimed=kind==2;
        rank = rank_t(difficulty); KonngaraCombat boss; begin(boss,aimed ? 11 : triangular ? 2 : 0,aimed ? 3 : 2);
        std::vector<Shot> expected; int x = 410,y = 70;
        for (int f = 2; f <= 201; ++f) {
            clock_frame = f; player.x_position = f % 2 ? 0 : 608;
            boss.update(); const auto &s = boss.state();
            if (f >= 10) assert(s.direction == KonngaraDirection::Center);
            if (f >= 10 && f < 201) assert(s.expression == (triangular || aimed ? KonngaraExpression::Aim : KonngaraExpression::Closed));
            int cel = -1; const int keys[] = {50,60,100,120,140,160,170};
            for (int i = 0; i < 7; ++i) if (f >= keys[i]) cel = i;
            assert(s.slash_cel == (f == 201 ? -1 : cel));
            assert(s.slash_cycles == (f >= 50 ? 1 : 0));
            bool fire = false; int dx = 0,dy = 0;
            if (f >= 100 && f < 140) {
                const int n = triangular || aimed ? 3 : first[difficulty];
                fire = f % n == 0; dx = 22 / (40/n); dy = 162 / (40/n);
            } else if (f >= 140 && f < 150) {
                if (f == 140) { x = 432; y = 232; }
                fire = true; dx = -23; dy = -3;
            } else if (f >= 150 && f < 170) {
                if (f == 150) { x = 198; y = 198; }
                const int n = triangular || aimed ? 2 : last[difficulty];
                fire = f % n == 0; dx = 212/(20/n); dy = -(128/(20/n));
            }
            if (fire) {
                for (int i = 0; i < (aimed ? 1 : 2); ++i) expected.push_back({f,x,y,aimed ? -1 : triangular ? (i ? 0x60 : 0x20) : roll%127,
                    aimed ? aimed_speed[difficulty] : triangular ? speed[difficulty] : 0,aimed ? PG_1_AIMED : 0,
                    triangular || aimed ? PM_REGULAR : PM_GRAVITY,triangular || aimed ? 0 : 1});
                x += dx; y += dy;
            }
        }
        assert(boss.state().frame == 0 && boss.state().pattern == 99 && boss.state().expression == KonngaraExpression::Neutral);
        assert(shots.size() == expected.size() && sounds == std::vector<int>{8});
        for (size_t i = 0; i < shots.size(); ++i) {
            const auto &a = shots[i], &b = expected[i];
            assert(a.frame == b.frame && a.x == b.x && a.y == b.y && a.angle == b.angle && a.speed == b.speed && a.group == b.group && a.motion == b.motion && a.fixed == b.fixed);
        }
        assert(calls == 1 + (triangular || aimed ? 0 : int(shots.size())));
        boss.update(); assert(boss.state().direction == KonngaraDirection::Left); // unlock only after201
    }
    std::puts("Konngara phases5/7: rain/triangular/aimed slashes, four ranks, artwork/lock/SFX8, source spawner steps, aimed groups/speeds and RNG passed");
}

static void laser_patterns()
{
    const int delta[] = {128,96,80,64}, count[] = {6,8,9,11};
    for (int difficulty = 0; difficulty < 4; ++difficulty) for (int right : {0,1}) {
        rank = rank_t(difficulty); KonngaraCombat boss; begin(boss,1); roll = right;
        int fired = 0;
        for (int f = 2; f <= 200; ++f) {
            clock_frame = f; boss.update();
            const auto &s = boss.state();
            if (f >= 100 && f % 10 == 0) {
                const auto &laser = s.lasers[(f/10)%10];
                const int target = right ? 640 - fired*delta[difficulty] : fired*delta[difficulty];
                const unsigned char angle = iatan2(330,target-410);
                assert(laser.alive && laser.age == 0 && laser.length == 0 && laser.speed == 8 && laser.moveout_age == 30 && laser.width == 5);
                assert(laser.start_x == 410*256 && laser.start_y == 70*256 && laser.step_x == Cos8(angle) && laser.step_y == Sin8(angle));
                ++fired;
            }
            boss.update_lasers();
            if (!s.frame) break;
        }
        assert(fired == count[difficulty] && calls == 2 && int(shots.size()) == fired && sounds == std::vector<int>(fired,6));
        for (const auto &s : shots) assert(s.x == 410 && s.y == 70 && s.speed == 72 && s.group == PG_3_SPREAD_WIDE_AIMED);
        // Laser motion continues through the idle selection interval.
        const auto old = boss.state().lasers[((100+(fired-1)*10)/10)%10].age;
        boss.update(); boss.update_lasers();
        assert(boss.state().lasers[((100+(fired-1)*10)/10)%10].age == old+1);
    }
    std::puts("Konngara phase5: both laser sweep directions/four ranks, 6/8/9/11 shots, wrapped10-slot ownership, source8.5->8 integer speed, paired3-spreads/SFX6 and idle persistence passed");
}

static void lasers()
{
    int total_hits=0;
    // Independent sample-and-flush oracle. Collision follows original byte/Y
    // groups, not a convenient geometric line hitbox or the displayed width.
    for (int target : {0,96,320,640,672}) for (bool invuln : {false,true}) {
        ShootoutLaser actual; assert(shootout_laser_spawn(actual,410,70,target,400));
        const unsigned char angle = iatan2(330,target-410);
        const int sx = Cos8(angle),sy = Sin8(angle);
        int x = 410*256,y = 70*256,length = 0,age = 0;
        invincible = invuln;
        auto initial = actual;
        assert(!shootout_laser_spawn(actual,0,0,0,1) && actual.start_x == initial.start_x);
        bool alive = true;
        for (int tick = 0; tick < 200 && alive; ++tick) {
            player.x_position = (tick * 7) % 609; player_is_hit = false;
            if (age < 30) length += 8;
            else {
                int left = x >> 8,top = y >> 8;
                for (int p = 0; p < 8; ++p) {
                    if (left/8 != (x>>8)/8 || top != y>>8) {
                        left = x>>8; top = y>>8;
                        if (left<0 || left>638 || top<64 || top>399) break;
                    }
                    x += sx; y += sy;
                }
                if (x<0 || x>=640*256 || y<64*256 || y>=400*256) alive = false;
            }
            bool hit = false;
            if (alive) {
                ++age;
                int ix=x,iy=y,left=x>>8,top=y>>8;
                for (int p=0;p<length;++p) {
                    if (left/8 != (ix>>8)/8 || top != iy>>8) {
                        hit |= !invuln && top>376 && player.x_position+4<=left && player.x_position+24>left;
                        left=ix>>8;top=iy>>8;
                        if (left<0 || left>638 || top<64 || top>399) break;
                    }
                    ix+=sx;iy+=sy;
                }
            }
            shootout_laser_update(actual);
            assert(actual.alive == alive && player_is_hit == hit);
            total_hits += player_is_hit;
            assert(actual.start_x==x && actual.start_y==y && actual.length==length && actual.age==age);
        }
        assert(!alive);
    }
    assert(total_hits>0);
    std::puts("Shootout lasers: independent Q8 growth/moveout/clipping/lifetime and grouped source player collision, invincibility, live-slot refusal and offscreen targets passed");
}

static void boundaries()
{
    for (int pattern=0;pattern<3;++pattern) {
        KonngaraCombat boss; begin(boss,pattern);
        int selections=1;
        for (int i=0;i<4000 && boss.state().phase==5;++i) {
            selections=std::max(selections,boss.state().patterns_done);boss.update();boss.update_lasers();
        }
        assert(!boss.state().complete && boss.state().phase==6 && selections==6 && boss.state().health.hp==12);
        begin(boss,pattern);stage_score=0;
        for(int i=0;i<90;++i) { orb={288,120,0};boss.update(); }
        assert(boss.state().health.hp==9 && !boss.state().complete && stage_score==30000);
        orb={10000,10000,0};
        for(int i=0;i<300 && boss.state().phase==5;++i) boss.update();
        assert(!boss.state().complete && boss.state().phase==6 && boss.state().frame==0 && boss.state().health.hp==9);
    }
    // A repeated triangular choice must reroll from TWO options, never3.
    KonngaraCombat boss;begin(boss,2);
    while(boss.state().frame) boss.update();
    for(int i=0;i<120;++i) boss.update();
    const int before=calls;boss.update();assert(calls==before+2 && boss.state().pattern==0);
    std::puts("Konngara phase5: real <10HP/6-selection idle handoff to6, hit grace, inherited HP/score and asymmetric reroll passed");
}

static void final_mix()
{
    // The ten earlier attacks must use their original controllers, with only
    // selection cadence/IDs changed. Compare complete event/visual timelines.
    rank=RANK_NORMAL;
    for(int pattern=0;pattern<10;++pattern) {
        KonngaraCombat boss,reference;KonngaraPhase1 first;
        if(pattern<3) {
            first.reset();orb={10000,10000,0};roll=pattern;
            for(int i=0;i<122;++i) first.update();
            assert(first.state().pattern==pattern && first.state().frame==1);
        } else begin(reference,pattern<7 ? pattern-3 : pattern-7,pattern<7 ? 1 : 2);
        begin(boss,pattern,3);
        bool finished=false;
        for(int f=2;f<2000 && !finished;++f) {
            clock_frame=f;player.x_position=(f*3)%609;
            shots.clear();sounds.clear();calls=0;
            if(pattern<3) first.update();else { reference.update();reference.update_lasers(); }
            const auto expected=shots;const auto effects=sounds;const int rng=calls;
            const auto &a=pattern<3 ? first.state() : reference.state();
            shots.clear();sounds.clear();calls=0;boss.update();boss.update_lasers();
            const auto &b=boss.state();
            assert(shots.size()==expected.size() && sounds==effects && calls==rng);
            for(size_t i=0;i<shots.size();++i) {
                const auto &x=shots[i],&y=expected[i];
                assert(x.frame==y.frame && x.x==y.x && x.y==y.y && x.angle==y.angle && x.speed==y.speed && x.group==y.group && x.motion==y.motion && x.fixed==y.fixed);
            }
            assert(a.frame==b.frame && a.direction==b.direction && a.expression==b.expression && a.visual_count==b.visual_count);
            assert(a.ray_active==b.ray_active && (!a.ray_active || (a.ray_left==b.ray_left && a.ray_top==b.ray_top)));
            assert(a.slash_cel==b.slash_cel && a.slash_cycles==b.slash_cycles);
            for(int i=0;i<a.visual_count;++i) assert(a.visuals[i].left==b.visuals[i].left && a.visuals[i].top==b.visuals[i].top && a.visuals[i].color==b.visuals[i].color);
            for(int i=0;i<SHOOTOUT_LASER_COUNT;++i) assert(a.lasers[i].alive==b.lasers[i].alive && a.lasers[i].age==b.lasers[i].age);
            assert(b.phase==7 && b.health.hp==9 && !b.complete);
            finished=!b.frame;
        }
        assert(finished);
    }
    for(int difficulty=0;difficulty<4;++difficulty) {
        rank=rank_t(difficulty);KonngaraCombat boss;begin(boss,10,3);
        for(int f=2;f<=300;++f) {
            clock_frame=f;boss.update();
            if(f>=10 && f<300) assert(boss.state().expression==KonngaraExpression::Aim);
        }
        assert(shots.size()==210 && sounds==std::vector<int>(21,7) && calls==1);
        for(size_t i=0;i<shots.size();++i) {
            const auto &s=shots[i];int volley=i/10;
            assert(s.frame==100+volley*10 && s.x==290 && s.y==200 && s.angle==((-int(i%10)*12)&255) && s.speed==32);
            assert(s.motion==(volley%2 ? PM_FALL_STRAIGHT_FROM_TOP_THEN_REGULAR : PM_GRAVITY) && s.fixed==(volley%2 ? 48 : 1));
        }
        assert(boss.state().frame==0 && boss.state().pattern==99 && !boss.state().complete);
    }
    // Five-frame wait, one asymmetric reroll, and zero HP ends immediately,
    // even mid-pattern. The lethal hit awards the bonus once; completion freezes.
    KonngaraCombat boss;begin(boss,11,3);
    while(boss.state().frame) boss.update();
    for(int i=0;i<5;++i) boss.update();
    int before=calls;boss.update();assert(calls==before+2 && boss.state().pattern==1);
    stage_score=0;orb={288,120,0};
    for(int i=0;i<400 && !boss.state().complete;++i) boss.update();
    assert(boss.state().complete && boss.state().phase==7 && boss.state().health.hp==0 && stage_score==140000);
    before=calls;int frame=boss.state().frame;boss.update();boss.update_lasers();
    assert(calls==before && boss.state().frame==frame && stage_score==140000);
    // Zero HP before phase7 must not strand the intervening hit-grace wait.
    boss.reset(2);stage_score=0;
    for(int i=0;i<12;++i) boss.update(true);
    assert(boss.state().phase==4 && boss.state().health.hp==0);
    assert(stage_score==170000); // bonus already awarded, before later phases/defeat
    for(int i=0;i<300 && !boss.state().complete;++i) boss.update();
    assert(boss.state().complete && boss.state().phase==7 && stage_score==170000);
    std::puts("Konngara phases6/7: reused attacks, sleeve rain/four ranks, selection/reroll, inherited HP and50000 bonus at lethal hit exactly once passed");
}
int main() { slashes();laser_patterns();lasers();boundaries();final_mix(); }
