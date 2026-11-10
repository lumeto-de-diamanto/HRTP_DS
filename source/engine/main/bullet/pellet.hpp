#pragma once

// Port of ReC98 th01/main/bullet/pellet.{hpp,cpp}: the main bullet type.
// 8x8 pellets with Q12.4 subpixel positions; velocity is computed once at
// spawn from (angle, speed) using the original 256-step angle math.

#include "main/math/subpixel.hpp"
#include "main/math/trig.hpp"
#include "rank.h"

static const int PELLET_W = 8;
static const int PELLET_H = 8;
static const int PELLET_COUNT = 100;

static const subpixel_t PELLET_VELOCITY_MAX = TO_SP(8);

// Destruction animation (ReC98 pellet.cpp): a hit pellet keeps moving for
// 20 frames while cycling through the two shatter cels, then despawns.
static const int PELLET_DECAY_FRAMES = 20;
static const int PELLET_DECAY_CELS = 2;
static const int PELLET_DECAY_FRAMES_PER_CEL = PELLET_DECAY_FRAMES / PELLET_DECAY_CELS;

// Speed tuning: pellet_speed is the global difficulty speed offset,
// premultiplied by PELLET_SPEED_MULTIPLIER (ReC98 th01/main/bullet/
// pellet_s.hpp).
static const int PELLET_SPEED_MULTIPLIER = 40;

enum pellet_motion_t {
    PM_REGULAR = 0,
    PM_GRAVITY = 1,
    PM_SLING_AIMED = 2,
    PM_BOUNCE_FROM_TOP_THEN_GRAVITY = 3,
    PM_FALL_STRAIGHT_FROM_TOP_THEN_REGULAR = 4,
    PM_SPIN = 5,
    PM_CHASE = 6,
};

// ReC98 pellet_group_t, values preserved (group_velocity_set dispatches on
// >= PG_AIMED_SPREADS). See the original header for the symmetry rules.
enum pellet_group_t {
    PG_NONE = 0,

    PG_1 = 1,
    PG_1_AIMED = 12,

    PG_2_SPREAD_WIDE,
    PG_2_SPREAD_NARROW,
    PG_3_SPREAD_WIDE,
    PG_3_SPREAD_NARROW,
    PG_4_SPREAD_WIDE,
    PG_4_SPREAD_NARROW,
    PG_5_SPREAD_WIDE,
    PG_5_SPREAD_NARROW,

    PG_AIMED_SPREADS,

    PG_2_SPREAD_WIDE_AIMED = PG_AIMED_SPREADS,
    PG_2_SPREAD_NARROW_AIMED,
    PG_3_SPREAD_WIDE_AIMED,
    PG_3_SPREAD_NARROW_AIMED,
    PG_4_SPREAD_WIDE_AIMED,
    PG_4_SPREAD_NARROW_AIMED,
    PG_5_SPREAD_WIDE_AIMED,
    PG_5_SPREAD_NARROW_AIMED,

    // -11.25 to +11.25 degrees around the player
    PG_1_RANDOM_NARROW_AIMED = 29,
    // -45 to +45 degrees, facing down
    PG_1_RANDOM_WIDE = 30,
};

struct Pellet {
    bool moving;
    unsigned char motion_type;
    Subpixel cur_left;
    Subpixel cur_top;
    Subpixel prev_left;
    Subpixel prev_top;
    pellet_group_t from_group;
    SPPoint velocity;
    Subpixel speed;
    int decay_frame;
    int angle;
    int sling_direction; // retained when a slot is reused, as in the original
};

struct CPellets {
    Pellet p[PELLET_COUNT];

    void init();
    // Spawns the pellets of a predefined group at the given position.
    void add_group(int left, int top, pellet_group_t group, subpixel_t speed);
    void add_single(int left, int top, int angle, subpixel_t speed,
                    pellet_motion_t motion = PM_REGULAR,
                    subpixel_t fixed_motion_speed = 0);

    // Per-frame: motion, clipping, and hit tests (orb, player, shots).
    // Returns true if the player was hit.
    bool update();

    int alive_count() const;

    // Bomb effect (ReC98 Pellets.decay): reduces velocity to two thirds.
    void decay();

    // Access for the renderer and hit tests.
    Pellet &at(int i) { return p[i]; }
};

extern CPellets Pellets;

// Global bullet-speed offset (ReC98 pellet_speed_t), premultiplied by 40.
extern int pellet_speed;

// ReC98 pellet.hpp: rank-dependent base speed added to every pellet.
static inline subpixel_t base_speed_for_rank()
{
    return (rank == RANK_EASY) ? TO_SP(0) :
           (rank == RANK_NORMAL) ? 6 : // to_sp(0.375f)
           (rank == RANK_HARD) ? 12 :  // to_sp(0.75f)
           18;                        // to_sp(1.125f)
}

// ReC98 pellet_s.hpp: global bullet-speed knob, clamped to [-0.375, 0.5]
// pixels (premultiplied by 40: [-15, 20]).
void pellet_speed_raise(float pixels);
void pellet_speed_lower(float pixels);
