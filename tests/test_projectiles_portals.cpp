// Real projectile, portal and orb code; only player/audio/RNG services are fake.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>
#include "main/bullet/pellet.hpp"
#include "main/player/orb.hpp"
#include "main/player/player.hpp"
#include "main/player/shot.hpp"
#include "main/playfld.hpp"
#include "main/stage/stageobj.hpp"
#include "sound_effects.hpp"

rank_t rank = RANK_NORMAL;
bool player_is_hit;
static PlayerState player{PLAYER_LEFT_START, player_top, PLAYER, 0};
const PlayerState &player_get_state() { return player; }
static bool invincible = true;
bool player_is_invincible() { return invincible; }
bool player_is_deflecting() { return false; }
bool player_is_sliding() { return false; }
static std::vector<int> effects;
void mdrv2_se_play(int id) { effects.push_back(id); }

static int portal_frame, exit_roll, exit_force = 9;
static bool fallback;
int irand() {
    if (portal_frame == 40) return exit_roll++ == 0 ? OVX_4_RIGHT : exit_force + 9;
    return fallback ? 1 : 0; // select a peer, or force the existing self fallback
}
extern fixed orb_force;

static void orb_ceiling_bounces()
{
    // ReC98 orb_velocity_y_update() returns pixel_t (int), so truncate each
    // displacement BEFORE adding it to the integer position. The fixed class's
    // ordinary int conversion intentionally still rounds to nearest.
    for (double force : {-20.0, -1.75, -1.5, -0.75, -0.5, 0.5, 0.75, 1.5, 1.75, 20.0}) {
        orb_initialize(); orb_set_position(300, 200); orb_set_velocity_x(OVX_0);
        orb_force_new(force, OF_IMMEDIATE); orb_update();
        assert(orb_get_state().y_position == 200 + int(force));
    }
    assert(int(fixed(0.75)) == 1 && int(fixed(-0.75)) == -1);

    // Literal double/integer source oracle; no player, shots or stage obstacles
    // inject new force. -17.5 used to settle into a perpetual 73-frame full-height
    // orbit instead of losing energy after the first ceiling contact.
    for (double initial : {-18.0, -17.5, -16.0, -13.0, -10.0, -8.0}) {
        orb_initialize(); orb_set_position(300, 368); orb_set_velocity_x(OVX_0);
        orb_force_new(initial, OF_IMMEDIATE);
        int y = 368, age = 0, ceiling_contacts = 0, first_floor = 0;
        double force = initial;
        for (int frame = 1; frame <= 2000; ++frame) {
            ++age;
            const double gravity = age / 5 + force;
            const double velocity = gravity > 16 ? 16 : gravity < -16 ? -16 : gravity;
            y += int(gravity); // source's unclamped pixel_t return
            if (y > PLAYFIELD_BOTTOM - ORB_H) {
                force = -velocity * 0.78; age = 0; y = PLAYFIELD_BOTTOM - ORB_H;
                if (!first_floor) first_floor = frame;
            }
            if (y < PLAYFIELD_TOP) {
                force = -velocity - age / 4; age = 0; y = PLAYFIELD_TOP;
                ++ceiling_contacts;
            }
            orb_update();
            assert(orb_get_state().y_position == y);
        }
        if (initial == -17.5) assert(ceiling_contacts == 1 && first_floor == 49);
    }
    std::puts("Orb: source truncation/unclamped movement, unchanged fixed rounding, ceiling/floor trajectories and no self-sustaining -17.5 loop passed");
}

static void pellet_orb_contacts()
{
    orb_initialize(); orb_set_position(300, 180);
    shots_initialize(); pellet_speed = 0;
    for (int difficulty = RANK_EASY; difficulty <= RANK_LUNATIC; ++difficulty) {
        rank = static_cast<rank_t>(difficulty);
        for (int dx = -18; dx <= 34; ++dx) {
            for (int dy = -10; dy <= 34; ++dy) {
                for (int fraction : {0, 1, 15}) {
                    for (int direction : {-1, 1}) {
                        Pellets.init(); stage_score = 0;
                        Pellets.add_group(300, 180, PG_1, 4 * 16);
                        auto &p = Pellets.at(0);
                        const int vx = direction * 31, vy = -direction * 17;
                        const int end_x = (300 + dx) * 16 + fraction;
                        const int end_y = (180 + dy) * 16 + fraction;
                        p.cur_left.v = end_x - vx; p.cur_top.v = end_y - vy;
                        p.velocity.x.v = vx; p.velocity.y.v = vy;
                        // Independent expansion of ReC98's strict overlap:
                        // pellet16x8 versus orb32x32, integer-pixel positions.
                        const bool hit = dx > -16 && dx < 32 && dy > -8 && dy < 32;
                        assert(!Pellets.update());
                        assert(p.cur_left.v == end_x && p.cur_top.v == end_y);
                        assert(p.velocity.x.v == (hit ? vx >> 1 : vx));
                        assert(p.velocity.y.v == (hit ? vy >> 1 : vy));
                        assert(p.decay_frame == int(hit));
                        assert(stage_score == (hit ? 10 : 0));
                    }
                }
            }
        }
    }
    // Decay cannot award the same contact again or repeatedly halve velocity.
    Pellets.init(); stage_score = 0;
    Pellets.add_group(320, 200, PG_1, 4 * 16);
    auto &p = Pellets.at(0);
    p.velocity.x.v = -33; p.velocity.y.v = 65;
    assert(!Pellets.update() && p.decay_frame == 1 && stage_score == 10);
    assert(p.velocity.x.v == -17 && p.velocity.y.v == 32);
    orb_set_position(10000, 10000);
    for (int frame = 1; frame <= PELLET_DECAY_FRAMES + 1; ++frame) {
        const int x = p.cur_left.v, y = p.cur_top.v;
        assert(!Pellets.update());
        assert(p.cur_left.v == x - 17 && p.cur_top.v == y + 32);
        assert(p.velocity.x.v == -17 && p.velocity.y.v == 32 && stage_score == 10);
        assert(p.moving == (frame <= PELLET_DECAY_FRAMES));
    }
    std::puts("Projectiles: source overlap boundaries/subpixels/all ranks, signed half-speed, single scoring and decay passed");
}

static void ofuda_contacts()
{
    for (int difficulty = RANK_EASY; difficulty <= RANK_LUNATIC; ++difficulty) {
        rank = static_cast<rank_t>(difficulty);
        // Plain contact, simultaneous orb contact, and a player-threatening
        // pellet must all resolve to immediate interception by the ofuda.
        for (int mode = 0; mode < 3; ++mode) {
            Pellets.init(); shots_initialize(); effects.clear(); stage_score = 0;
            const int x = mode == 2 ? player.x_position + 8 : 300;
            const int y = mode == 2 ? player_top : 160;
            invincible = mode != 2;
            orb_set_position(mode == 1 ? x : 10000, mode == 1 ? y : 10000);
            shots_add(x, y);
            Pellets.add_group(x, y, PG_1, 3 * 16);
            auto &p = Pellets.at(0);
            p.velocity.x.v = p.velocity.y.v = 0;
            assert(!Pellets.update());
            assert(!p.moving && p.decay_frame == 0 && Pellets.alive_count() == 0);
            assert(stage_score == 0); // only orb destruction awards10
            assert(effects == (std::vector<int>{SFX_SHOT, SFX_SHOT_HIT}));
            assert(shots_get_state().alive[0] && shots_get_state().sprite_number[0] == 1);
            assert(!shots_hit_pellet(x, y) && !shots_hit_boss(x, y, 96, 96));
            // A second pellet is not erased by the non-moving impact sprite.
            invincible = true; orb_set_position(10000, 10000);
            Pellets.add_group(x, y, PG_1, 3 * 16);
            p.velocity.x.v = p.velocity.y.v = 0;
            assert(!Pellets.update() && p.moving && p.decay_frame == 0);
            assert(effects.size() == 2);
            for (int frame = 1; frame <= 7; ++frame) {
                shots_update();
                assert(shots_get_state().x_position[0] == x && shots_get_state().y_position[0] == y);
                assert(shots_get_state().alive[0] == (frame < 7));
                if (frame < 7) assert(shots_get_state().sprite_number[0] == (frame + 1) / 4 + 1);
            }
            shots_add(x + 40, y);
            assert(shots_get_state().alive[0] && shots_get_state().sprite_number[0] == 0);
            assert(shots_get_state().x_position[0] == x + 40); // slot reusable after animation
        }
    }
    // Pellets already shattering from the orb remain untouched by ofuda.
    Pellets.init(); shots_initialize(); orb_set_position(10000, 10000);
    Pellets.add_group(300, 160, PG_1, 3 * 16);
    auto &p = Pellets.at(0);
    p.velocity.x.v = p.velocity.y.v = 0; p.decay_frame = 1;
    shots_add(300, 160); effects.clear();
    assert(!Pellets.update() && p.moving && p.decay_frame == 2);
    assert(shots_get_state().alive[0] && shots_get_state().sprite_number[0] == 0 && effects.empty());
    std::puts("Ofuda: immediate pellet erase, stationary impact cels/lifetime, single SFX/contact, hit priority and slot reuse passed");
}

static int expected_portal_cel(int frame, bool entered, bool destination)
{
    if (entered && frame < 10) return 0;
    if (entered && frame < 20) return 1;
    if (destination && frame >= 20 && frame < 30) return 1;
    if (destination && frame >= 30 && frame < 40) return 0;
    return -1;
}

static void portals()
{
    for (int count : {1, 2}) for (int entry = 0; entry < count; ++entry) {
        for (bool self : {false, true}) for (int force = -9; force <= 9; ++force) {
            uint8_t obstacles[STAGE_ROWS][STAGE_COLS] = {};
            const uint8_t cards[STAGE_ROWS][STAGE_COLS] = {};
            obstacles[0][0] = OT_BUMPER; // entered portal need not be slot0
            for (int i = 0; i < count; ++i) obstacles[2][6 + i * 4] = OT_PORTAL;
            stage_init(obstacles, cards); stage_reset(); orb_initialize();
            const int source = entry + 1;
            const int destination = count == 1 || self ? source : 2 - entry;
            fallback = self; exit_force = force; exit_roll = 0;
            orb_set_position(stage_obstacle(source).left, stage_obstacle(source).top);
            for (int frame = 1; frame <= 60; ++frame) {
                portal_frame = frame;
                if (frame > 40) {
                    // Hold the orb on the other portal while blocked; move it
                    // clear at60 so legitimate re-entry does not start a new trip.
                    const int other = count == 2 ? 2 - entry : source;
                    orb_set_position(frame == 60 ? 10000 : stage_obstacle(other).left,
                                     frame == 60 ? 10000 : stage_obstacle(other).top);
                }
                stage_obstacles_update();
                assert(orb_get_in_portal() == (frame < 40));
                for (int slot = 0; slot < stage_obstacle_count(); ++slot)
                    assert(stage_portal_anim(slot) == expected_portal_cel(
                        frame, slot == source, slot == destination));
                if (frame == 40) {
                    assert(orb_get_state().x_position == stage_obstacle(destination).left);
                    assert(orb_get_state().y_position == stage_obstacle(destination).top);
                    assert(orb_get_velocity_x() == OVX_4_RIGHT && int(orb_force) == force);
                    assert(exit_roll == 2);
                }
            }
            assert(stage_obstacle(source).since_collision == 0);
            orb_set_position(stage_obstacle(destination).left, stage_obstacle(destination).top);
            portal_frame = 61; stage_obstacles_update();
            assert(orb_get_in_portal() && stage_portal_anim(destination) == 0);
            stage_reset();
            assert(!orb_get_in_portal());
            for (int slot = 0; slot < stage_obstacle_count(); ++slot)
                assert(stage_portal_anim(slot) == -1 && stage_obstacle(slot).since_collision == 0);
        }
    }
    std::puts("Portals: single/paired/self fallback, both slot orders, all 60 frames/19 exit forces, cooldown/reset/re-entry passed");
}

int main(int argc, char **argv)
{
    if (argc == 1 || std::strcmp(argv[1], "orb") == 0) orb_ceiling_bounces();
    if (argc == 1 || std::strcmp(argv[1], "pellets") == 0) pellet_orb_contacts();
    if (argc == 1 || std::strcmp(argv[1], "ofuda") == 0) ofuda_contacts();
    if (argc == 1 || std::strcmp(argv[1], "portals") == 0) portals();
}
