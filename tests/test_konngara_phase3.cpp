// Source-constant oracles for the second attack set and phase1->2->3 handoff.
// Actual controller/shared hit code; record emissions BEFORE rank adjustment.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
#include "main/boss/konngara.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/math/vector.hpp"

rank_t rank = RANK_NORMAL;
int stage_score;
bool player_is_hit;
CPellets Pellets;
namespace {
PlayerState player{304, player_top, PLAYER, 0};
OrbState orb{10000, 10000, 0};
orb_velocity_x_t vx = OVX_4_LEFT;
int roll, calls, frame_now;
bool invincible = true, sliding;
struct Spawn { int frame, x, y, angle, speed, group, motion, fixed; };
std::vector<Spawn> spawns;
std::vector<int> sounds;
}
const PlayerState &player_get_state() { return player; }
const OrbState &orb_get_state() { return orb; }
bool player_is_invincible() { return invincible; }
bool player_is_sliding() { return sliding; }
orb_velocity_x_t orb_get_velocity_x() { return vx; }
void orb_set_velocity_x(orb_velocity_x_t v) { vx = v; }
bool orb_hit_by_shot(int, int) { return false; }
int irand() { ++calls; return roll; }
void mdrv2_se_play(int id) { sounds.push_back(id); }
void CPellets::add_group(int x, int y, pellet_group_t group, subpixel_t speed)
{ spawns.push_back({frame_now, x, y, -1, speed, group, PM_REGULAR, 0}); }
void CPellets::add_single(int x, int y, int angle, subpixel_t speed, pellet_motion_t motion, subpixel_t fixed)
{ spawns.push_back({frame_now, x, y, angle & 255, speed, 0, motion, fixed}); }

static void begin(KonngaraCombat &boss, int pattern)
{
    orb = {10000, 10000, 0}; player.x_position = 304; player_is_hit = false;
    invincible = true; sliding = false;
    shots_initialize(); stage_score = 0; roll = pattern; calls = 0;
    spawns.clear(); sounds.clear();
    boss.reset(true);
    assert(boss.state().phase == 2 && boss.state().health.hp == 15 && boss.state().health.total == 18);
    for (int i = 1; i <= 121; ++i) boss.update();
    assert(boss.state().phase == 3 && boss.state().frame == 0 && calls == 0 && spawns.empty());
    for (int i = 1; i <= 120; ++i) boss.update();
    assert(boss.state().pattern == 99 && boss.state().patterns_done == 0);
    boss.update();
    assert(boss.state().pattern == pattern && boss.state().frame == 1 && boss.state().patterns_done == 1 && calls == 1);
}

static void transitions()
{
    KonngaraCombat boss;
    orb = {10000, 10000, 0}; shots_initialize();
    boss.reset(true);
    for (int i = 1; i <= 120; ++i) {
        player.x_position = i % 3 == 0 ? 0 : i % 3 == 1 ? 304 : 608;
        boss.update(); const auto &s = boss.state();
        assert(s.phase == 2 && s.frame == i && s.patterns_done == 0);
        const int t = i < 50 ? 0 : i < 52 ? 15 : std::max(0, 15 - (i / 4 - 12));
        assert(s.siddham_white == t && s.bos_head && s.visual_count == 0 && !s.ray_active);
        assert(int(s.direction) == (player.x_position < 198 ? 1 : player.x_position > 396 ? 0 : 2));
    }
    boss.update(); assert(boss.state().phase == 3 && boss.state().frame == 0 && boss.state().siddham_white == 0);
    // Damage and ofuda contact remain active during the palette interlude;
    // recent orb hits delay the transition until the original >40 expiry.
    boss.reset(true); stage_score = 0;
    for (int i = 1; i <= 141; ++i) {
        orb = i == 100 ? OrbState{288,120,0} : OrbState{10000,10000,0};
        if (i == 110) shots_add(300,130);
        boss.update();
        assert(boss.state().health.hp == (i < 100 ? 15 : 14));
        if (i <= 140) assert(boss.state().phase == 2);
        if (i >= 110) assert(shots_get_state().sprite_number[0] == 1);
    }
    assert(boss.state().phase == 3 && stage_score == 10000);
    // The approved phase1 controller's state/emission prefix is unchanged.
    KonngaraPhase1 first;
    first.reset(); boss.reset(); orb = {10000,10000,0}; roll = 1;
    for (int i = 0; i < 9000 && boss.state().phase == 1; ++i) {
        spawns.clear(); first.update(); const auto expected = first.state();
        const auto emitted = spawns;
        spawns.clear(); boss.update();
        const auto &actual = boss.state();
        assert(actual.health.hp == expected.health.hp && actual.frame == expected.frame && actual.pattern == expected.pattern);
        assert(actual.direction == expected.direction && actual.expression == expected.expression);
        assert(spawns.size() == emitted.size());
        for (size_t j = 0; j < emitted.size(); ++j) {
            const auto &a = spawns[j], &b = emitted[j];
            assert(a.x == b.x && a.y == b.y && a.angle == b.angle && a.speed == b.speed && a.group == b.group);
        }
    }
    assert(boss.state().phase == 2 && boss.state().health.hp == 18); // count path never invents HP loss
    // Fast, real first-set HP boundary: three hits before the first choice.
    boss.reset(); shots_initialize(); stage_score = 0; calls = 0; roll = 1;
    for (int i = 0; i < 1000 && boss.state().phase == 1; ++i) {
        orb = i < 90 ? OrbState{288,120,0} : OrbState{10000,10000,0}; boss.update();
    }
    assert(boss.state().phase == 2 && boss.state().frame == 0 && boss.state().health.hp == 15 && stage_score == 30000);
    for (int i = 0; i < 241; ++i) boss.update();
    assert(boss.state().phase == 3 && boss.state().pattern == 99 && boss.state().frame == 120);
    const int before = calls; boss.update();
    assert(boss.state().pattern == 1 && calls == before + 2); // previous-ID reroll crosses phases
    std::puts("Konngara phases1->2->3: original controller prefix, retained HP/previous pattern, exact Siddham fade, live shots/orb hits, tracking and hit-grace transition passed");
}

static void patterns()
{
    for (int difficulty = 0; difficulty < 4; ++difficulty) for (int pattern = 0; pattern < 4; ++pattern) {
        rank = rank_t(difficulty); KonngaraCombat boss; begin(boss, pattern);
        int end = 0, rain_x = 0, rain_y = 264, row_x = 290, row_y = 200, row_dir = 99;
        int row_vx, row_vy, row_speed = 48;
        vector2_between(290,200,64,64,row_vx,row_vy,16);
        std::vector<Spawn> expected;
        const int row_interval[] = {12,10,8,6}, spray_interval[] = {5,4,3,2}, rain_interval[] = {5,3,2,2};
        int spray_offset = 32, spray_delta = -8, spray_count = 0;
        for (int frame = 2; frame <= 1000; ++frame) {
            frame_now = frame; boss.update(); const auto &s = boss.state();
            if (frame == 10) assert(int(s.expression) == pattern);
            assert(s.health.hp == 15 && !s.complete && !player_is_hit && s.visual_count <= 16);
            if (pattern == 0 && frame > 100) {
                if (row_dir == 99) {
                    row_x += row_vx; row_y += row_vy;
                    if (row_y <= 64) { row_x = 64; row_y = 64; row_dir = 0; assert(s.visual_count == 0); continue; }
                } else {
                    if (row_dir < 2) row_x += row_dir == 0 ? 8 : -8;
                    else { row_y += 8; ++row_dir; if (row_dir == 10) row_dir = row_x > 320 ? 1 : 0; }
                    if (row_x > 576 || row_x < 64) { row_x = row_x > 576 ? 576 : 64; row_dir = 2; }
                    if (row_y < 256 && row_dir < 2 && frame % row_interval[difficulty] == 0) {
                        expected.push_back({frame,row_x,row_y,-1,row_speed,PG_1_AIMED,PM_REGULAR,0}); row_speed += 2;
                    }
                }
                if (row_y < 256) {
                    assert(s.visual_count == 1 && s.visuals[0].left == row_x && s.visuals[0].top == row_y);
                    assert(s.visuals[0].color == (row_dir == 99 ? 9 : 6));
                }
            }
            if (pattern == 1 && frame >= 100 && frame % spray_interval[difficulty] == 0) {
                const int angle = (iatan2(184,26) + spray_offset) & 255;
                spray_offset = (spray_offset + spray_delta) & 255;
                if (++spray_count == 9) { spray_count = 0; spray_delta = -spray_delta; }
                expected.push_back({frame,312,188,angle,48,0,PM_REGULAR,0});
            }
            if (pattern == 2 && (frame == 101 || frame == 151 || frame == 201 || frame == 251)) {
                const int index = (frame - 101) / 50, count = s.visual_count;
                assert(count >= 4 && count <= (index + 1) * 4);
                for (int j = count - 4; j < count; ++j) {
                    assert(s.visuals[j].left == (index < 2 ? 312 : 290) && s.visuals[j].top == (index < 2 ? 188 : 200));
                    assert(s.visuals[j].color == (j == count - 1 ? 7 : 9));
                }
            }
            if (pattern == 3 && frame >= 100) {
                if (frame == 125) { rain_x = 0; rain_y = 64; }
                if (frame == 205 || frame == 255) { rain_x = 639; rain_y = 64; }
                if (frame == 230) { rain_x = 639; rain_y = 264; }
                if (frame == 335) { rain_x = 0; rain_y = 64; }
                if (frame < 360) {
                    assert(s.ray_active && s.ray_left == rain_x && s.ray_top == rain_y);
                    if (frame < 125 || (frame >= 230 && frame < 255)) rain_y -= 8;
                    else if (frame < 205) rain_x += 8;
                    else if (frame < 230 || frame >= 335) rain_y += 8;
                    else rain_x -= 8;
                } else { rain_y -= 8; assert(!s.ray_active); }
                if (frame % rain_interval[difficulty] == 0 || frame == 360)
                    for (int i = 0; i < 2; ++i) expected.push_back({frame,rain_x,rain_y,pattern,32,0,PM_GRAVITY,1});
            }
            if (!s.frame) { end = frame; break; }
        }
        assert(end && boss.state().pattern == 99 && boss.state().expression == KonngaraExpression::Neutral);
        if (pattern != 2) {
            assert(spawns.size() == expected.size());
            for (size_t i = 0; i < expected.size(); ++i) {
                const auto &a = spawns[i], &b = expected[i];
                assert(a.frame == b.frame && a.x == b.x && a.y == b.y && a.angle == b.angle && a.speed == b.speed);
                assert(a.group == b.group && a.motion == b.motion && a.fixed == b.fixed);
            }
        }
        if (pattern == 0) { assert(calls == 1 && sounds == std::vector<int>{12}); }
        if (pattern == 1) { assert(end == 700 && calls == 1 && sounds.empty()); }
        if (pattern == 2) {
            assert(end == 400 && spawns.empty() && calls == 1 + 4 + 299 + 249 + 199 + 149);
            assert(sounds == std::vector<int>({12,12,12,12}));
        }
        if (pattern == 3) {
            assert(end == 360 && calls == 1 + int(spawns.size()) && spawns.back().x == 0 && spawns.back().y == 256);
            assert(sounds == std::vector<int>(27,6));
        }
    }
    std::puts("Konngara phase3: all four patterns/four ranks, exact origins/timing/speeds/angles/motions/RNG, row paths, four snake spawn/render order and pre-move rain ray/final emissions passed");
}

static void boundaries()
{
    for (int pattern = 0; pattern < 4; ++pattern) {
        KonngaraCombat boss; begin(boss, pattern);
        int selections = 1;
        for (int i = 0; i < 9000 && boss.state().phase == 3; ++i) {
            selections = std::max(selections,boss.state().patterns_done); boss.update();
        }
        assert(!boss.state().complete && selections == 9 && boss.state().health.hp == 15 && boss.state().phase == 4);
        const int before = calls; boss.update(); assert(calls == before);
        begin(boss, pattern); stage_score = 0;
        for (int i = 0; i < 90; ++i) { orb = {288,120,0}; boss.update(); }
        assert(boss.state().health.hp == 12 && !boss.state().complete && stage_score == 30000);
        orb = {10000,10000,0};
        for (int i = 0; i < 1000 && boss.state().phase == 3; ++i) boss.update();
        assert(!boss.state().complete && boss.state().frame == 0 && boss.state().phase == 4 && boss.state().health.hp == 12);
    }
    std::puts("Konngara phase3: real <13HP/9-selection idle boundaries, hit grace and retained health/score into phase4 without premature defeat passed");
}

static void snake_collision()
{
    int hits = 0;
    for (int mode = 0; mode < 3; ++mode) {
        KonngaraCombat boss; begin(boss, 2);
        invincible = mode == 2; sliding = mode == 1;
        struct Ref { int x[5]{}, y[5]{}, target = 0; bool active = false, locked = false; } snakes[4];
        auto spawn = [&](int i) {
            snakes[i].active = true;
            for (int j = 0; j < 5; ++j) { snakes[i].x[j] = i < 2 ? 312 : 290; snakes[i].y[j] = i < 2 ? 188 : 200; }
        };
        for (int frame = 2; frame < 400; ++frame) {
            roll = frame % 2; player_is_hit = false;
            if (mode == 2 && frame > 200) player.x_position = frame % 40 < 20 ? 0 : 608;
            std::vector<KonngaraDiamond> drawn;
            bool expected_hit = false;
            if (frame == 100) spawn(0);
            else if (frame > 100) {
                for (auto &s : snakes) if (s.active) {
                    for (int j = 3; j >= 0; --j) if (s.x[j] >= 0 && s.x[j] < 640 && s.y[j] >= 0 && s.y[j] < 400)
                        drawn.push_back({s.x[j],s.y[j],j ? 9 : 7});
                    if (!s.locked) s.target = player.x_position;
                    const unsigned char a = iatan2(384 - s.y[0], s.target + 12 - s.x[0]) + (roll ? -40 : 40);
                    if (s.y[0] > 300) s.locked = true;
                    for (int j = 4; j >= 1; --j) { s.x[j] = s.x[j-1]; s.y[j] = s.y[j-1]; }
                    s.x[0] += (7 * Cos8(a)) >> 8; s.y[0] += (7 * Sin8(a)) >> 8;
                    expected_hit |= !invincible && s.x[0] > player.x_position && s.x[0] < player.x_position + 24 &&
                        s.y[0] > (sliding ? 378 : 368) && s.y[0] < 392;
                }
                if (frame == 150 || frame == 200 || frame == 250) spawn((frame - 100) / 50);
            }
            boss.update();
            assert(player_is_hit == expected_hit);
            hits += player_is_hit;
            const auto &s = boss.state(); assert(s.visual_count == int(drawn.size()));
            for (size_t j = 0; j < drawn.size(); ++j) assert(s.visuals[j].left == drawn[j].left &&
                s.visuals[j].top == drawn[j].top && s.visuals[j].color == drawn[j].color);
        }
    }
    assert(hits > 0);
    std::puts("Konngara phase3 snakes: independent four-snake motion/target-lock/pre-move pixels and actual updated-head collisions, including slide/invincibility, passed");
}

int main() { transitions(); patterns(); boundaries(); snake_collision(); }
