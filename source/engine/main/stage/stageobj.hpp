#pragma once

// Port of ReC98 th01/main/stage/stageobj.hpp: stage-object collision state
// (cards, bumpers, bumper bars, portals, turrets) for the DS backend.
//
// Original PC-98 coordinates, independent of the DS camera. Each cell (cx,cy)
// covers (cx*32, PLAYFIELD_TOP+cy*32,32,32), on the full 20x10 source grid.

#include <stdint.h>

#include "main/math/fixed.hpp"
#include "main/playfld.hpp"

static const int STAGEOBJ_W = 32;
static const int STAGEOBJ_H = 32;

// == ((STAGEOBJ_W * 3) / 8) + (ORB_VISUAL_W / 2), orb visual size 25:
// integer arithmetic and truncation kept verbatim from ReC98.
static const int STAGEOBJ_ORB_DISTANCE_X = ((STAGEOBJ_W * 3) / 8) + (25 / 2);
static const int STAGEOBJ_ORB_DISTANCE_Y = ((STAGEOBJ_W * 3) / 8) + (25 / 2);

static const int STAGE_COLS = 20;
static const int STAGE_ROWS = 10;
static const int STAGE_CELL_FIRST_ROW = 0;

// Original grid leaves a 16px gap between its last row and the floor.
static const int STAGE_GRID_TOP = PLAYFIELD_TOP;

static inline int stage_cell_x(int left)
{
    return left / STAGEOBJ_W;
}

static inline int stage_cell_y(int top)
{
    return ((top - STAGE_GRID_TOP) / STAGEOBJ_H) + STAGE_CELL_FIRST_ROW;
}

// Cards
// -----

enum card_flag_t {
    CARD_ALIVE = 0,
    CARD_FLIPPING = 1,
    CARD_REMOVED = 2,
};

static const int CARD_FRAMES_PER_CEL = 6;

enum card_cel_t {
    CARD_CEL_NOT_FLIPPING = 0,
    CARD_CEL_FRONT_HALF = 1,
    CARD_CEL_EDGE = 2,
    CARD_CEL_BACK_HALF = 3,
    CARD_CEL_FLIPPED = 4,
    CARD_CELS,
};

static inline int card_first_frame_of(card_cel_t cel) { return cel * CARD_FRAMES_PER_CEL; }

static const int CARD_HP_MAX = 5;

static const int CARD_SCORE_CAP = 25600;
static const int CARD_FLIP_CYCLE_MAX = 140;

struct StageCard {
    int left, top;
    uint8_t flag;       // card_flag_t
    int flip_frame;     // frame within the CARD_FLIPPING animation
    int8_t hp;          // stored minus 1: 0 = one more flip to remove
};

// Obstacles
// ---------

// Raw stagedat obstacle_type_t values (th01/formats/stagedat.hpp).
enum obstacle_type_t {
    OT_NONE = 0,
    OT_BUMPER = 1,
    OT_TURRET_SLOW_1_AIMED = 2,
    OT_TURRET_SLOW_1_RANDOM_NARROW_AIMED = 3,
    OT_TURRET_SLOW_2_SPREAD_WIDE_AIMED = 4,
    OT_TURRET_SLOW_3_SPREAD_WIDE_AIMED = 5,
    OT_TURRET_SLOW_4_SPREAD_WIDE_AIMED = 6,
    OT_TURRET_SLOW_5_SPREAD_WIDE_AIMED = 7,
    OT_TURRET_QUICK_1_AIMED = 8,
    OT_TURRET_QUICK_1_RANDOM_NARROW_AIMED = 9,
    OT_TURRET_QUICK_2_SPREAD_WIDE_AIMED = 10,
    OT_TURRET_QUICK_3_SPREAD_WIDE_AIMED = 11,
    OT_TURRET_QUICK_4_SPREAD_WIDE_AIMED = 12,
    OT_TURRET_QUICK_5_SPREAD_WIDE_AIMED = 13,
    OT_TURRET_FIRST = OT_TURRET_SLOW_1_AIMED,
    OT_TURRET_LAST = OT_TURRET_QUICK_5_SPREAD_WIDE_AIMED,
    OT_CARD_2FLIP = 14,
    OT_CARD_3FLIP = 15,
    OT_CARD_4FLIP = 16,
    OT_PORTAL = 17,
    OT_BAR_TOP = 18,
    OT_BAR_BOTTOM = 19,
    OT_BAR_LEFT = 20,
    OT_BAR_RIGHT = 21,
};

struct StageObstacle {
    int left, top;
    uint8_t type;          // obstacle_type_t
    int since_collision;   // BLOCK_FRAMES cooldown (portals use it as a timer)
    int fire_cycle;        // turrets: frames until the next volley
    uint8_t turret_flag;   // turret_flag_t
};

// Turret firing state machine (ReC98 th01/main/stage/stageobj.cpp).
enum turret_flag_t {
    TF_READY = 0,
    TF_WARMUP = 1,
    TF_FIRE = 2,
    TF_COOLDOWN = 3,
    TF_DONE = 10, // TF_COOLDOWN + TURRET_COOLDOWN_FRAMES (7)
};

static const int STAGE_MAX_CELLS = STAGE_COLS * STAGE_ROWS;

// Stage-object state (owned by stageobj.cpp; card.cpp mutates the cards).
extern StageCard stage_cards[STAGE_MAX_CELLS];
extern int stage_card_total;
extern int stage_cards_removed_count;
extern StageObstacle stage_obstacles[STAGE_MAX_CELLS];
extern int stage_obstacle_total;

// Game state (score/combo live here until the HUD arrives in M3)
extern int stage_score;
extern int hiscore; // session high score (save data comes with M6)
extern int stage_cardcombo_cur;
extern int stage_cardcombo_max;

void stage_init(const uint8_t obstacle[STAGE_ROWS][STAGE_COLS],
                const uint8_t card[STAGE_ROWS][STAGE_COLS]);
void stage_reset(); // kicks the orb out of portals; zeroes cooldowns

// Per-frame updates (call order: cards_hittest -> stage_obstacles_update
// -> cards_update; the card functions live in card.hpp/.cpp)
void stage_obstacles_update();

bool stage_is_cleared();

int stage_card_count();
int stage_cards_removed();
const StageCard &stage_card(int i);
int stage_obstacle_count();
const StageObstacle &stage_obstacle(int i);

// Portal entry animation for rendering: -1 = show the base portal sprite,
// otherwise the PTN_PORTAL_ANIM cel (0 or 1) for this obstacle's phase.
int stage_portal_anim(int i);

// Turrets show their "firing" sprite while the state machine is active.
bool stage_turret_firing(int i);

void stage_cardcombo_reset();
