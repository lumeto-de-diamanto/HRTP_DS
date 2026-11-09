// Port of ReC98 th01/main/stage/stageobj.cpp (stage-object state and
// obstacle collision: bumpers, bumper bars, portals) for the DS backend.
// Physics constants, hitboxes and quirks follow the ReC98 anniversary
// sources -- including the bumper's orb teleport (the pinball mechanic;
// see docs/ds-port-plan.md section 5).

#include <stdlib.h>

#include "main/stage/stageobj.hpp"
#include "main/player/orb.hpp"
#include "main/random.hpp"

int stage_score;
int hiscore;
int stage_cardcombo_cur;
int stage_cardcombo_max;

StageCard stage_cards[STAGE_MAX_CELLS];
int stage_card_total;
int stage_cards_removed_count;

StageObstacle stage_obstacles[STAGE_MAX_CELLS];
int stage_obstacle_total;

// Which portal the orb entered; other portals stay blocked until the
// sequence completes (frame 60 in ReC98's portal_enter_update...).
static int entered_portal_slot;
static int entered_portal_dst;
static bool portals_blocked;

void stage_init(const uint8_t obstacle[STAGE_ROWS][STAGE_COLS],
                const uint8_t card[STAGE_ROWS][STAGE_COLS])
{
    stage_card_total = 0;
    stage_obstacle_total = 0;
    stage_cards_removed_count = 0;
    stage_score = 0;
    stage_cardcombo_cur = 0;
    stage_cardcombo_max = 0;

    for (int cy = 0; cy < STAGE_ROWS; cy++) {
        for (int cx = 0; cx < STAGE_COLS; cx++) {
            uint8_t type = obstacle[cy][cx];
            bool has_card_bit = (card[cy][cx] != 0);
            int left = cx * STAGEOBJ_W;
            int top = STAGE_GRID_TOP + ((cy - STAGE_CELL_FIRST_ROW) * STAGEOBJ_H);

            // HP-bearing cards live in the obstacle array (ReC98's
            // OT_ACTUALLY_A_*FLIP_CARD smuggle, preserved by the stagedat
            // converter); plain cards use the card bitfield. hp is stored
            // minus 1: 0 = one more flip to remove.
            if (type == OT_CARD_2FLIP || type == OT_CARD_3FLIP ||
                type == OT_CARD_4FLIP || has_card_bit) {
                StageCard &c = stage_cards[stage_card_total++];
                c.left = left;
                c.top = top;
                c.flag = CARD_ALIVE;
                c.flip_frame = 0;
                switch (type) {
                case OT_CARD_2FLIP: c.hp = 1; break;
                case OT_CARD_3FLIP: c.hp = 2; break;
                case OT_CARD_4FLIP: c.hp = 3; break;
                default:            c.hp = 0; break;
                }
                continue;
            }
            if (type != OT_NONE) {
                StageObstacle &o = stage_obstacles[stage_obstacle_total++];
                o.left = left;
                o.top = top;
                o.type = type;
                o.since_collision = 0;
                o.fire_cycle = 0;
                o.turret_flag = TF_READY;
            }
        }
    }
}

void stage_reset()
{
    for (int i = 0; i < stage_obstacle_total; i++) {
        stage_obstacles[i].since_collision = 0;
        stage_obstacles[i].fire_cycle = 0;
        stage_obstacles[i].turret_flag = TF_READY;
    }
    entered_portal_slot = 0;
    entered_portal_dst = 0;
    portals_blocked = false;
    orb_set_in_portal(false);
}

void stage_cardcombo_reset()
{
    stage_cardcombo_cur = 0;
}

// Portal entry animation (ReC98 portal_enter_update_and_render_or_reset's
// keyframes: entered portal animates on frames 1-19, the destination on
// frames 20-39).
int stage_portal_anim(int i)
{
    if (!portals_blocked)
        return -1;
    if (i == entered_portal_slot) {
        int t = stage_obstacles[i].since_collision;
        if (t >= 1 && t < 10)
            return 0;
        if (t >= 10 && t < 20)
            return 1;
        return -1;
    }
    if (i == entered_portal_dst) {
        int t = stage_obstacles[entered_portal_slot].since_collision;
        if (t >= 20 && t < 30)
            return 1;
        if (t >= 30 && t < 40)
            return 0;
        return -1;
    }
    return -1;
}

bool stage_is_cleared()
{
    return stage_card_total > 0 && stage_cards_removed_count == stage_card_total;
}

int stage_card_count() { return stage_card_total; }
int stage_cards_removed() { return stage_cards_removed_count; }
const StageCard &stage_card(int i) { return stage_cards[i]; }
int stage_obstacle_count() { return stage_obstacle_total; }
const StageObstacle &stage_obstacle(int i) { return stage_obstacles[i]; }

// Obstacles
// ---------
// ReC98 th01/main/stage/stageobj.cpp: obstacles_update_and_render(). Hit
// boxes are exactly ReC98's: bumpers use the STAGEOBJ_ORB_DISTANCE margin
// around the tile top-left, bars use tile bands over the orb's top-left.

#include "main/bullet/pellet.hpp"

static const int BLOCK_FRAMES = 8;

// ReC98 turret_fire_update_and_render_or_reset(): the pellet-volley state
// machine. Types 2-7 fire every 200 frames, types 8-13 every 100.
static void turret_fire_update(StageObstacle &o)
{
    switch (o.turret_flag) {
    case TF_READY:
        o.turret_flag = TF_WARMUP;
        break;

    case TF_WARMUP:
        if ((o.fire_cycle % 10) == 9)
            o.turret_flag = TF_FIRE;
        break;

    case TF_FIRE: {
        pellet_group_t group;
        switch (o.type) {
        case OT_TURRET_SLOW_1_AIMED:
        case OT_TURRET_QUICK_1_AIMED:
            group = PG_1_AIMED;
            break;
        case OT_TURRET_SLOW_1_RANDOM_NARROW_AIMED:
        case OT_TURRET_QUICK_1_RANDOM_NARROW_AIMED:
            group = PG_1_RANDOM_NARROW_AIMED;
            break;
        case OT_TURRET_SLOW_2_SPREAD_WIDE_AIMED:
        case OT_TURRET_QUICK_2_SPREAD_WIDE_AIMED:
            group = PG_2_SPREAD_WIDE_AIMED;
            break;
        case OT_TURRET_SLOW_3_SPREAD_WIDE_AIMED:
        case OT_TURRET_QUICK_3_SPREAD_WIDE_AIMED:
            group = PG_3_SPREAD_WIDE_AIMED;
            break;
        case OT_TURRET_SLOW_4_SPREAD_WIDE_AIMED:
        case OT_TURRET_QUICK_4_SPREAD_WIDE_AIMED:
            group = PG_4_SPREAD_WIDE_AIMED;
            break;
        default:
            group = PG_5_SPREAD_WIDE_AIMED;
            break;
        }
        Pellets.add_group(
            (o.left + (STAGEOBJ_W / 2) - (PELLET_W / 2)),
            (o.top + (STAGEOBJ_H / 2) - (PELLET_H / 2)),
            group,
            TO_SP(7) / 2 // to_sp(3.5f)
        );
        o.turret_flag = TF_COOLDOWN;
        break;
    }

    default:
        o.turret_flag++;
        if (o.turret_flag >= TF_DONE) {
            o.turret_flag = TF_READY;
            o.fire_cycle = 0;
        }
        break;
    }
}

bool stage_turret_firing(int i)
{
    return stage_obstacles[i].turret_flag != TF_READY;
}

void stage_obstacles_update()
{
    static bool vertical_bars_blocked = false;

    const OrbState &orb = orb_get_state();

    for (int i = 0; i < stage_obstacle_total; i++) {
        StageObstacle &o = stage_obstacles[i];
        int delta_abs_x = abs(o.left - orb.x_position);
        int delta_abs_y = abs(o.top - orb.y_position);
        int delta_x = orb.x_position - o.left;
        int delta_y = orb.y_position - o.top;

        switch (o.type) {
        case OT_BUMPER:
            if (delta_abs_x < STAGEOBJ_ORB_DISTANCE_X &&
                delta_abs_y < STAGEOBJ_ORB_DISTANCE_Y &&
                o.since_collision == 0) {
                o.since_collision++;
                orb_velocity_x_t velocity_x = orb_get_velocity_x();

                // Immediate teleport above or below the bumper, bypassing
                // the regular physics system. Preserved deliberately: this
                // is what enables infinite bumper loops (ReC98 comment).
                orb_set_top((delta_y <= 0)
                    ? (o.top - STAGEOBJ_ORB_DISTANCE_Y)
                    : (o.top + STAGEOBJ_ORB_DISTANCE_Y));

                // Only OVX_4_* is reflected away from the bumper, and only
                // when moving into it. OVX_8_* passes through.
                if (velocity_x == OVX_4_RIGHT && delta_x < 0) {
                    orb_set_velocity_x(OVX_4_LEFT);
                } else if (velocity_x == OVX_4_LEFT && delta_x > 0) {
                    orb_set_velocity_x(OVX_4_RIGHT);
                }
                orb_force_new(1.5, OF_BOUNCE_FROM_SURFACE);
            }
            if (o.since_collision != 0) {
                o.since_collision++;
                if (o.since_collision >= BLOCK_FRAMES)
                    o.since_collision = 0;
            }
            continue;

        case OT_BAR_TOP:
            if ((delta_abs_x < STAGEOBJ_W) &&
                (delta_y < 0) && (delta_y >= -STAGEOBJ_H)) {
                if (o.since_collision == 0) {
                    o.since_collision++;
                    orb_velocity_reflect_y();
                }
            }
            if (o.since_collision != 0)
                goto bar_cooldown;
            continue;

        case OT_BAR_BOTTOM:
            if ((delta_abs_x < STAGEOBJ_W) &&
                (delta_y <= STAGEOBJ_H) && (delta_y > 0)) {
                if (o.since_collision == 0) {
                    o.since_collision++;
                    orb_velocity_reflect_y();
                }
            }
            if (o.since_collision != 0)
                goto bar_cooldown;
            continue;

        case OT_BAR_LEFT:
            if ((delta_abs_y < STAGEOBJ_H) &&
                (-delta_x <= STAGEOBJ_W) && (-delta_x >= 0)) {
                if (o.since_collision == 0 && !vertical_bars_blocked) {
                    o.since_collision++;
                    vertical_bars_blocked = true;
                    if (orb_get_velocity_x() == OVX_0)
                        orb_velocity_reflect_y();
                    else
                        orb_velocity_reflect_x();
                }
            }
            if (o.since_collision != 0)
                goto bar_cooldown;
            continue;

        case OT_BAR_RIGHT:
            if ((delta_abs_y < STAGEOBJ_H) &&
                (-delta_x <= 0) && (-delta_x >= -STAGEOBJ_W)) {
                if (o.since_collision == 0 && !vertical_bars_blocked) {
                    o.since_collision++;
                    vertical_bars_blocked = true;
                    if (orb_get_velocity_x() == OVX_0)
                        orb_velocity_reflect_y();
                    else
                        orb_velocity_reflect_x();
                }
            }
            if (o.since_collision != 0)
                goto bar_cooldown;
            continue;

        case OT_PORTAL:
            if ((delta_abs_x < STAGEOBJ_ORB_DISTANCE_X &&
                 delta_abs_y < STAGEOBJ_ORB_DISTANCE_Y) ||
                o.since_collision != 0) {
                if (portals_blocked && entered_portal_slot != i) {
                    continue;
                }
                if (o.since_collision == 0) {
                    o.since_collision = 1;
                    entered_portal_slot = i;
                    portals_blocked = true;
                    orb_set_in_portal(true);
                } else {
                    o.since_collision++;
                    if (o.since_collision == 20) {
                        // Destination: ReC98 repeatedly loops over all
                        // obstacles, rolling a die per non-entered portal,
                        // giving up after 16 completed loops (failure
                        // chance (0.75^(portal_count-1))^16). The orb only
                        // warps at frame 40.
                        entered_portal_dst = i;
                        int completed_loops = 0;
                        int candidate = 0;
                        while (completed_loops <= 15) {
                            if (stage_obstacles[candidate].type == OT_PORTAL &&
                                candidate != i && (irand() % 4) == 0) {
                                entered_portal_dst = candidate;
                                break;
                            }
                            candidate++;
                            if (candidate >= stage_obstacle_total) {
                                candidate = 0;
                                completed_loops++;
                            }
                        }
                    } else if (o.since_collision == 40) {
                        orb_set_position(stage_obstacles[entered_portal_dst].left,
                                         stage_obstacles[entered_portal_dst].top);
                        orb_set_velocity_x(
                            (orb_velocity_x_t)(irand() % OVX_COUNT));
                        orb_force_new((irand() % 19) - 9, OF_IMMEDIATE);
                        orb_set_in_portal(false);
                    } else if (o.since_collision == 60) {
                        o.since_collision = 0;
                        portals_blocked = false;
                    }
                }
            }
            continue;

        default:
            if (o.type >= OT_TURRET_FIRST && o.type <= OT_TURRET_LAST) {
                // ReC98 turret_update(): nothing fires on Easy.
                if (rank != RANK_EASY)
                    o.fire_cycle++;
                int interval = (o.type < OT_TURRET_QUICK_1_AIMED) ? 200 : 100;
                if (o.fire_cycle >= interval)
                    turret_fire_update(o);
            }
            continue;
        }

        // Common to all bumper bars (ReC98's shared tail).
        bar_cooldown:
        if (o.since_collision != 0) {
            o.since_collision++;
            if (o.since_collision >= BLOCK_FRAMES) {
                o.since_collision = 0;
                vertical_bars_blocked = false;
            }
        }
    }
}
