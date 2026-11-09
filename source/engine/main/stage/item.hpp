#pragma once

// Port of ReC98 th01/main/stage/item.{hpp,cpp}: the bomb/point items that
// drop from every 10th card flip. Lifecycle: splash ring -> fall ->
// bounce -> collect (or drop off the playfield).

#include <stdint.h>

static const int ITEM_W = 32;
static const int ITEM_H = 32;

static const int ITEM_BOMB_COUNT = 4;
static const int ITEM_POINT_COUNT = 10;

static const int POINT_VALUE_STEP = 1000;
static const int POINT_VALUE_MAX = 10000;

enum item_flag_t {
    IF_FREE = 0,
    IF_SPLASH = 1,
    IF_FALL = 2,
    IF_BOUNCE = 3,
    IF_COLLECTED = 99,
    IF_COLLECTED_OVER_CAP = 100,
};

struct Item {
    int left, top;
    int velocity_y;
    uint8_t flag;        // item_flag_t
    uint8_t splash_radius; // IF_SPLASH
    uint8_t collect_time;  // >= IF_COLLECTED
};

struct CItems {
    Item bomb[ITEM_BOMB_COUNT];
    Item point[ITEM_POINT_COUNT];

    void init();
    void add_bomb(int left, int top);
    void add_point(int left, int top);
    void update(); // movement + collection effects

    // Queried by the renderer.
    static bool is_visible(const Item &item) {
        return (item.flag > IF_SPLASH) && (item.flag < IF_COLLECTED);
    }
};

extern CItems Items;

// The escalating point-item value (ReC98 resident->point_value).
extern int point_value;

// Collected bomb items (ReC98 resident->rem_bombs).
extern int rem_bombs;
