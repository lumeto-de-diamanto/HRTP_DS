// Port of ReC98 th01/main/bullet/pellet.cpp. The spawn tables and aiming
// math follow the original exactly; the per-axis velocity clamp is
// symmetric (docs/ds-port-plan.md section 5 approves this bug fix; ZUN's
// original only clamped positive velocities).

#include <stdlib.h>

#include "pellet.hpp"
#include "main/math/vector.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/stage/stageobj.hpp"
#include "main/playfld.hpp"
#include "main/random.hpp"

CPellets Pellets;
int pellet_speed;

void pellet_speed_raise(float pixels)
{
    pellet_speed += (int)(pixels * PELLET_SPEED_MULTIPLIER);
    if (pellet_speed > 20)
        pellet_speed = 20;
}

void pellet_speed_lower(float pixels)
{
    pellet_speed -= (int)(pixels * PELLET_SPEED_MULTIPLIER);
    if (pellet_speed < -15)
        pellet_speed = -15;
}

// ReC98 speed_set(): rank base, then the global pellet_speed offset.
static subpixel_t speed_set(subpixel_t speed)
{
    speed += base_speed_for_rank();
    speed += (pellet_speed * speed) / PELLET_SPEED_MULTIPLIER;
    if (speed < TO_SP(1))
        speed = TO_SP(1);
    return speed;
}

// ReC98 th01/main/bullet/pellet.cpp: vector2_to_player_from().
static void vector2_to_player_from(int pellet_left, int pellet_top,
                                   int &ret_x, int &ret_y,
                                   subpixel_t speed, int plus_angle)
{
    unsigned char angle =
        iatan2(player_center_y() - pellet_top,
               (player_center_x() - (PELLET_W / 2)) - pellet_left)
        + plus_angle;
    vector2(ret_x, ret_y, speed, angle);
}

// ReC98 group_velocity_set(), verbatim control flow (including the PG_NONE
// quirk where an invalid group fills the array). Returns true when the
// group is complete; `i` is the pellet index within the group.
static bool group_velocity_set(Subpixel &ret_x, Subpixel &ret_y,
                               pellet_group_t group, subpixel_t speed,
                               int &i, int pellet_left, int pellet_top)
{
    bool done = false;
    unsigned char angle = 0x00;
    unsigned char spread_delta = 0x00;

    #define to_aim_or_not_to_aim() \
        if (group >= PG_AIMED_SPREADS) { \
            goto aim; \
        } \
        /* Static group; add a 90 degree angle, so that 0 points down */ \
        angle += 0x40; \
        goto no_aim;

    switch (group) {
    case PG_NONE:
        // ReC98 quirk: invalid groups fall through with uninitialized
        // velocities (see the original comment); kept identical.
        break;
    case PG_1:
        ret_y.v = speed;
        ret_x.set(0.0f);
        done = true;
        break;
    case PG_1_AIMED:
        vector2_between(
            pellet_left, pellet_top,
            (player_center_x() - PELLET_W), player_center_y(),
            ret_x.v, ret_y.v, speed
        );
        done = true;
        break;

    case PG_2_SPREAD_WIDE:
    case PG_2_SPREAD_WIDE_AIMED:  spread_delta = 0x08;  // fallthrough
    case PG_2_SPREAD_NARROW:
    case PG_2_SPREAD_NARROW_AIMED:
        /**/ if (i == 0) { angle = (+0x04 + spread_delta); }
        else if (i == 1) { angle = (-0x04 - spread_delta); done = true; }
        to_aim_or_not_to_aim();

    case PG_3_SPREAD_WIDE:
    case PG_3_SPREAD_WIDE_AIMED:  spread_delta = 0x05;  // fallthrough
    case PG_3_SPREAD_NARROW:
    case PG_3_SPREAD_NARROW_AIMED:
        /**/ if (i == 0) { angle =   0x00; }
        else if (i == 1) { angle = (+0x04 + spread_delta); }
        else if (i == 2) { angle = (-0x04 - spread_delta); done = true; }
        to_aim_or_not_to_aim();

    case PG_4_SPREAD_WIDE:
    case PG_4_SPREAD_WIDE_AIMED:  spread_delta = 0x04;  // fallthrough
    case PG_4_SPREAD_NARROW:
    case PG_4_SPREAD_NARROW_AIMED:
        /**/ if (i == 0) { angle = (+0x04 +  spread_delta); }
        else if (i == 1) { angle = (-0x04 -  spread_delta); }
        else if (i == 2) { angle = (+0x0C + (spread_delta * 3)); }
        else if (i == 3) { angle = (-0x0C - (spread_delta * 3)); done = true; }
        to_aim_or_not_to_aim();

    case PG_5_SPREAD_WIDE:
    case PG_5_SPREAD_WIDE_AIMED:  spread_delta = 0x04;  // fallthrough
    case PG_5_SPREAD_NARROW:
    case PG_5_SPREAD_NARROW_AIMED:
        /**/ if (i == 0) { angle =   0x00; }
        else if (i == 1) { angle = (+0x04 +  spread_delta); }
        else if (i == 2) { angle = (-0x04 -  spread_delta); }
        else if (i == 3) { angle = (+0x08 + (spread_delta * 2)); }
        else if (i == 4) { angle = (-0x08 - (spread_delta * 2)); done = true; }
        to_aim_or_not_to_aim();

    case PG_1_RANDOM_NARROW_AIMED:
        angle = ((irand() & 0x0F) - 0x07);
        done = true;
        goto aim;

    case PG_1_RANDOM_WIDE:
        angle = ((irand() & 0x3F) + 0x20);
        done = true;
        goto no_aim;

    aim:
        vector2_to_player_from(
            pellet_left, pellet_top, ret_x.v, ret_y.v, speed, angle
        );
        break;

    no_aim:
        vector2(ret_x.v, ret_y.v, speed, angle);
        break;
    }
    i++;
    return done;

    #undef to_aim_or_not_to_aim
}

void CPellets::init()
{
    for (int i = 0; i < PELLET_COUNT; i++) {
        p[i].moving = false;
        p[i].motion_type = PM_REGULAR;
        p[i].decay_frame = 0;
        p[i].from_group = PG_NONE;
        p[i].angle = 0;
        p[i].sling_direction = 0;
    }
}

void CPellets::add_group(int left, int top, pellet_group_t group,
                         subpixel_t speed)
{
    subpixel_t final_speed = speed_set(speed);
    int i = 0;

    for (int slot = 0; slot < PELLET_COUNT; slot++) {
        if (p[slot].moving)
            continue;
        Pellet &pel = p[slot];
        pel.moving = true;
        pel.motion_type = PM_REGULAR;
        pel.cur_left.set(left);
        pel.cur_top.set(top);
        pel.prev_left = pel.cur_left;
        pel.prev_top = pel.cur_top;
        pel.from_group = group;
        pel.speed.v = final_speed;
        pel.decay_frame = 0;

        Subpixel vx, vy;
        if (group_velocity_set(vx, vy, group, final_speed, i, left, top)) {
            pel.velocity.x = vx;
            pel.velocity.y = vy;
            return;
        }
        pel.velocity.x = vx;
        pel.velocity.y = vy;
    }
}

void CPellets::add_single(int left, int top, int angle, subpixel_t speed,
                          pellet_motion_t motion, subpixel_t fixed_motion_speed)
{
    for (int slot = 0; slot < PELLET_COUNT; slot++) {
        if (p[slot].moving)
            continue;
        Pellet &pel = p[slot];
        pel.moving = true;
        pel.motion_type = motion;
        pel.cur_left.set(left);
        pel.cur_top.set(top);
        pel.prev_left = pel.cur_left;
        pel.prev_top = pel.cur_top;
        pel.from_group = PG_NONE;
        const subpixel_t initial_speed = speed_set(speed);
        pel.speed.v = fixed_motion_speed; // source motion speed is not rank-tuned
        pel.decay_frame = 0;
        pel.angle = angle;
        vector2(pel.velocity.x.v, pel.velocity.y.v, initial_speed, angle);
        return;
    }
}

int CPellets::alive_count() const
{
    int n = 0;
    for (int i = 0; i < PELLET_COUNT; i++)
        if (p[i].moving)
            n++;
    return n;
}

void CPellets::decay()
{
    // Bomb effect (ReC98 Pellets.decay): every pellet enters its
    // destruction animation and slows to two thirds of its velocity.
    for (int i = 0; i < PELLET_COUNT; i++) {
        if (!p[i].moving || p[i].decay_frame)
            continue;
        p[i].decay_frame = 1;
        p[i].velocity.x.v = (p[i].velocity.x.v * 2) / 3;
        p[i].velocity.y.v = (p[i].velocity.y.v * 2) / 3;
    }
}

bool CPellets::update()
{
    bool player_hit = false;
    const OrbState &orb = orb_get_state();

    for (int i = 0; i < PELLET_COUNT; i++) {
        Pellet &pel = p[i];
        if (!pel.moving)
            continue;

        // ReC98 motion_type_apply_for_cur. A completed sling marks itself as
        // an aimed group, so motion automation stops on subsequent frames.
        if (pel.from_group == PG_NONE) {
            if (pel.motion_type == PM_GRAVITY) {
                pel.velocity.y.v += pel.speed;
            } else if (pel.motion_type == PM_CHASE) {
                int vx, vy;
                vector2_between(TO_PIXEL(pel.cur_left.v), TO_PIXEL(pel.cur_top.v),
                    player_center_x() - PELLET_W, player_center_y(), vx, vy, pel.speed.v);
                if (pel.cur_top.v < (PLAYFIELD_BOTTOM - 80) * 16) {
                    pel.velocity.x.v += (vx > pel.velocity.x.v) - (vx < pel.velocity.x.v);
                    pel.velocity.y.v += (vy > pel.velocity.y.v) - (vy < pel.velocity.y.v);
                }
            } else if (pel.motion_type == PM_SLING_AIMED) {
                if (!pel.sling_direction)
                    pel.sling_direction = (irand() & 1) + 1;
                if (pel.sling_direction == 1)
                    vector2(pel.velocity.x.v, pel.velocity.y.v, pel.speed.v, (unsigned char)pel.angle);
                else
                    vector2(pel.velocity.y.v, pel.velocity.x.v, pel.speed.v, (unsigned char)pel.angle);
                pel.angle += 8;
                if (pel.angle > 256) {
                    vector2_between(TO_PIXEL(pel.cur_left.v), TO_PIXEL(pel.cur_top.v),
                        player_center_x() - PELLET_W, player_center_y(),
                        pel.velocity.x.v, pel.velocity.y.v, pel.speed.v);
                    pel.from_group = PG_1_AIMED;
                    pel.angle = 0;
                }
            }
        }

        // Symmetric per-axis clamp (bug fix; the original only clamped the
        // positive side).
        if (pel.velocity.x.v > PELLET_VELOCITY_MAX)
            pel.velocity.x.v = PELLET_VELOCITY_MAX;
        if (pel.velocity.x.v < -PELLET_VELOCITY_MAX)
            pel.velocity.x.v = -PELLET_VELOCITY_MAX;
        if (pel.velocity.y.v > PELLET_VELOCITY_MAX)
            pel.velocity.y.v = PELLET_VELOCITY_MAX;
        if (pel.velocity.y.v < -PELLET_VELOCITY_MAX)
            pel.velocity.y.v = -PELLET_VELOCITY_MAX;

        pel.prev_left = pel.cur_left;
        pel.prev_top = pel.cur_top;
        pel.cur_left.v += pel.velocity.x.v;
        pel.cur_top.v += pel.velocity.y.v;

        // Despawn outside the playfield.
        if (pel.cur_left.v < -PELLET_W * SUBPIXEL_FACTOR ||
            pel.cur_left.v > TO_SP(PLAYFIELD_RIGHT) ||
            pel.cur_top.v < (PLAYFIELD_TOP - PELLET_H) * SUBPIXEL_FACTOR ||
            pel.cur_top.v > TO_SP(PLAYFIELD_BOTTOM)) {
            pel.moving = false;
            continue;
        }

        // Orb: halves the velocity and starts the destruction animation
        // (+10). Decaying pellets ignore all hit tests (ReC98).
        if (pel.decay_frame == 0) {
            if ((pel.cur_left.v > (orb.x_position - PELLET_W) * 16) &&
                (pel.cur_left.v < (orb.x_position + PELLET_W + 8) * 16) &&
                (pel.cur_top.v > (orb.y_position - PELLET_H) * 16) &&
                (pel.cur_top.v < (orb.y_position + PELLET_H) * 16)) {
                pel.velocity.x.v >>= 1;
                pel.velocity.y.v >>= 1;
                pel.decay_frame = 1;
                stage_score += 10;
                continue;
            }

            // A deflecting player repels pellets horizontally.
            if (player_is_deflecting()) {
                int player_left = player_get_state().x_position;
                int player_top = player_get_state().y_position;
                if ((pel.cur_left.v > TO_SP(player_left - PELLET_W)) &&
                    (pel.cur_left.v < TO_SP(player_left + PLAYER_W + PELLET_W)) &&
                    (pel.cur_top.v > TO_SP(player_top - 2 * PELLET_H)) &&
                    (pel.cur_top.v < TO_SP(player_top + PLAYER_H + 2 * PELLET_H))) {
                    int deflect_angle =
                        (TO_PIXEL(pel.cur_left.v) < (player_left + 12)) ? 0x80 : 0x00;
                    vector2(pel.velocity.x.v, pel.velocity.y.v, TO_SP(8),
                            deflect_angle);
                }
            }

            // Player kill box (ReC98 hittest_player_for_cur).
            {
                int player_left = player_get_state().x_position;
                int player_top = player_get_state().y_position;
                int px = TO_PIXEL(pel.cur_left.v);
                int py = TO_PIXEL(pel.cur_top.v);
                if (!player_is_invincible() && (px >= player_left + 4) && (px < player_left + 20) &&
                    (py >= player_top + 8 * player_is_sliding()) &&
                    (py < player_top + 24)) {
                    player_hit = true;
                }
            }

            // Shots start the destruction animation.
            if (shots_hit_pellet(TO_PIXEL(pel.cur_left.v),
                                 TO_PIXEL(pel.cur_top.v))) {
                pel.decay_frame = 1;
            }
        } else {
            // Decay tick (ReC98 decay_tick_for_cur).
            pel.decay_frame++;
            if (pel.decay_frame > (PELLET_DECAY_FRAMES + 1)) {
                pel.decay_frame = 0;
                pel.moving = false;
            }
        }
    }
    return player_hit;
}
