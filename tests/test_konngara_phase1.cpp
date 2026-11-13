// Actual phase1 controller, hit code and shot lifecycle. Pellet spawn requests
// are recorded before rank/speed adjustment; original pattern constants are the
// oracle. Orb/player services are fixed so camera/rendering cannot affect AI.
#include <cassert>
#include <cstdio>
#include <vector>
#include "main/boss/konngara_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/orb.hpp"
#include "main/player/player.hpp"
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
bool invincible = true, sliding;
int roll, calls;
struct Spawn { int frame, x, y, angle, speed, group; };
int current_frame;
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
{ spawns.push_back({current_frame, x, y, -1, speed, group}); }
void CPellets::add_single(int x, int y, int angle, subpixel_t speed, pellet_motion_t, subpixel_t)
{ spawns.push_back({current_frame, x, y, angle & 255, speed, 0}); }

static void begin(KonngaraPhase1 &boss, int pattern)
{
    boss.reset(); orb = {10000, 10000, 0}; shots_initialize();
    player.x_position = 304; player_is_hit = false;
    calls = 0; roll = pattern; stage_score = 0; spawns.clear(); sounds.clear();
    boss.update(); // source phase0 setup only
    for (int frame = 1; frame <= 120; ++frame) {
        boss.update();
        assert(boss.state().frame == frame && boss.state().pattern == 99 && boss.state().patterns_done == 0);
    }
    assert(spawns.empty() && sounds.empty() && calls == 0 && boss.state().hp_displayed == 18);
    boss.update();
    assert(boss.state().pattern == pattern && boss.state().frame == 1 && boss.state().patterns_done == 1 && calls == 1);
}

static void patterns()
{
    for (int difficulty = 0; difficulty < 4; ++difficulty) {
        rank = rank_t(difficulty);
        for (int pattern = 0; pattern < 3; ++pattern) {
            KonngaraPhase1 boss;
            begin(boss, pattern);
            int end = 0;
            for (int frame = 2; frame < 1000; ++frame) {
                current_frame = frame;
                boss.update();
                const auto &state = boss.state();
                if (frame == 10) assert(int(state.expression) == pattern);
                assert(state.health.hp == 18 && !state.complete && !player_is_hit);
                if (pattern == 2 && frame == 101) {
                    assert(state.visual_count == 4);
                    for (int i = 0; i < 4; ++i)
                        assert(state.visuals[i].left == 312 && state.visuals[i].top == 188 && state.visuals[i].color == (i == 3 ? 7 : 9));
                }
                // With a deliberately constant wobble roll, snake0 can leave
                // source VRAM; shape8x8_put then omits those origins. Snake1's
                // four pre-move segments are still at its visible cup spawn.
                if (pattern == 2 && frame == 151) assert(state.visual_count >= 4 && state.visual_count <= 8);
                if (state.frame == 0) { end = frame; break; }
            }
            assert(end && boss.state().pattern == 99 && boss.state().expression == KonngaraExpression::Neutral);
            if (pattern == 0) {
                int bx, by, tx, ty;
                vector2_between(316, 236, 0, 384, bx, by, 7);
                vector2_between(316, 236, 0, 64, tx, ty, 7);
                const int x_steps = (316 - bx - 1) / -bx;
                const int y_steps = (236 + x_steps * by - 64 + 2) / 3;
                assert(end == 100 + x_steps + y_steps + 201);
                const int intervals[] = {18, 16, 14, 12};
                const int groups[] = {PG_2_SPREAD_NARROW_AIMED, PG_3_SPREAD_NARROW_AIMED,
                                      PG_5_SPREAD_WIDE_AIMED, PG_5_SPREAD_NARROW_AIMED};
                assert(spawns.size() == size_t(1 + 200 / intervals[difficulty] * 5));
                assert(spawns[0].frame == 100 && spawns[0].x == 312 && spawns[0].y == 188 && spawns[0].speed == 48);
                assert(spawns[0].group == groups[difficulty] && sounds == std::vector<int>{12});
                for (size_t i = 1; i < spawns.size(); i += 5) {
                    assert(spawns[i].x == 0 && spawns[i + 1].x == 632);
                    assert(spawns[i + 2].y == 64 && spawns[i + 3].y == 64 && spawns[i + 4].group == PG_1_AIMED);
                    for (int n = 0; n < 5; ++n) assert(spawns[i + n].speed == 40);
                }
                assert(calls == 1 + (200 / intervals[difficulty]) * 9);
            } else if (pattern == 1) {
                assert(end == 300 && spawns.size() == 90 && sounds.empty());
                const int speeds[] = {80, 80, 96, 112};
                for (size_t i = 0; i < spawns.size(); i += 2) {
                    assert(spawns[i].x == 328 && spawns[i + 1].x == 296 && spawns[i].y == 188);
                    assert(spawns[i].speed == speeds[difficulty] && spawns[i + 1].speed == speeds[difficulty]);
                    assert(spawns[i].angle == spawns[i + 1].angle && spawns[i].frame % 8 == 0);
                }
                assert(spawns.front().frame == 104 && spawns.front().angle == 64);
                assert(spawns[10].frame == 144 && spawns[10].angle == 69 && spawns[12].angle == 59);
                assert(spawns.back().frame == 296 && spawns[86].angle == 250 && spawns[88].angle == 134);
                assert(calls == 1);
            } else {
                assert(end == 500 && spawns.size() == size_t((6 + difficulty) * 20));
                assert(sounds.size() == size_t(2 + 6 + difficulty) && sounds[0] == 12 && sounds[1] == 12);
                const int first[] = {280, 240, 200, 160};
                assert(spawns.front().frame == first[difficulty] && spawns.back().frame == 480);
                for (size_t i = 0; i < spawns.size(); i += 20) {
                    const int frame = spawns[i].frame;
                    const int x = frame % 120 == 0 ? 410 : frame % 120 == 40 ? 316 : 312;
                    const int y = frame % 120 == 0 ? 70 : frame % 120 == 40 ? 140 : 188;
                    for (int n = 0; n < 20; ++n) {
                        assert(spawns[i + n].x == x && spawns[i + n].y == y);
                        const int cel = n / 2;
                        assert(spawns[i + n].speed == 40 + (cel % 4 == 1 ? 16 : cel % 4 == 3 ? 32 : 0));
                    }
                }
                assert(calls == 1 + 2 + 399 + 349 + 6 + difficulty);
            }
        }
    }
    std::puts("Konngara phase1: all three source patterns/four ranks, exact spawn timing/counts/origins/speeds, face changes, pre-move snake visuals and RNG call counts passed");
}

static void hits_and_phase_boundary()
{
    rank = RANK_NORMAL;
    for (int x = 287; x <= 353; ++x) for (int y : {119, 120, 121, 159, 160, 161}) {
        KonngaraPhase1 boss; boss.reset(); boss.update(); shots_initialize();
        orb = {x, y, 0}; stage_score = 0; vx = OVX_8_LEFT; sounds.clear();
        boss.update();
        const bool hit = x >= 288 && x <= 352 && y >= 120 && y <= 160;
        assert(boss.state().health.hp == (hit ? 17 : 18));
        assert(stage_score == (hit ? 10000 : 0) && vx == (hit ? OVX_4_RIGHT : OVX_8_LEFT));
        assert(sounds == std::vector<int>(hit ? 1 : 0, 3));
    }
    KonngaraPhase1 boss; boss.reset(); boss.update();
    orb = {10000, 10000, 0}; stage_score = 0;
    shots_initialize(); shots_add(300, 130); sounds.clear();
    boss.update();
    assert(boss.state().health.hp == 18 && stage_score == 0 && sounds == std::vector<int>{16});
    assert(shots_get_state().alive[0] && shots_get_state().sprite_number[0] == 1);
    for (int frame = 0; frame < 7; ++frame) shots_update();
    assert(!shots_get_state().alive[0]);
    boss.reset(); boss.update(); orb = {288, 120, 0};
    boss.update(); assert(boss.state().health.hp == 17);
    for (int i = 1; i <= 41; ++i) { boss.update(); assert(boss.state().health.hp == 17); }
    boss.update(); assert(boss.state().health.hp == 16);
    for (int i = 0; i < 42; ++i) boss.update();
    assert(boss.state().health.hp == 15);
    orb = {10000, 10000, 0}; roll = 1;
    while (boss.state().frame < 120) boss.update();
    assert(!boss.state().complete);
    boss.update(); assert(boss.state().pattern == 1 && !boss.state().complete);
    for (int i = 0; i < 300 && !boss.state().complete; ++i) boss.update();
    assert(boss.state().complete && boss.state().frame == 0 && boss.state().health.hp == 15);
    for (int pattern = 0; pattern < 3; ++pattern) {
        begin(boss, pattern);
        for (int frame = 0; frame < 10000 && !boss.state().complete; ++frame) boss.update();
        assert(boss.state().complete && boss.state().patterns_done == 7 && boss.state().health.hp == 18);
        const int before = calls;
        for (int i = 0; i < 100; ++i) boss.update();
        assert(calls == before && boss.state().frame == 0);
    }
    for (int left : {197, 198, 396, 397}) {
        boss.reset(); boss.update(); player.x_position = left; boss.update();
        assert(int(boss.state().direction) == (left < 198 ? 1 : left > 396 ? 0 : 2));
    }
    for (bool slide : {false, true}) for (bool invuln : {false, true})
        for (int x = 99; x <= 125; ++x) for (int y = 367; y <= 393; ++y)
            assert(konngara_snake_hits_player(x, y, 100, 368, slide, invuln) ==
                (!invuln && x >= 101 && x <= 123 && y >= (slide ? 379 : 369) && y <= 391));
    std::puts("Konngara phase1: inclusive orb and shot-only contact boxes, score/reflection/i-frames, 18HP safe bar, delayed HP/7-pattern boundary, direction thresholds and strict sliding/invincible snake hitboxes passed");
}

int main() { patterns(); hits_and_phase_boundary(); }
