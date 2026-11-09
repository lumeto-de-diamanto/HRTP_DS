// Port of ReC98 th01/main/stage/item.cpp. The splash ring and the
// collection popups are cosmetic (platform layer); the movement and
// effects follow the original.

#include <stdlib.h>

#include "item.hpp"
#include "main/stage/stageobj.hpp"
#include "main/player/player.hpp"
#include "main/playfld.hpp"
#include "popup.hpp"
#include "sound_effects.hpp"

CItems Items;
int point_value;

static const int BOMBS_MAX = 5;

// ReC98 resident->rem_bombs: collected bomb items.
int rem_bombs;

void CItems::init()
{
    for (int i = 0; i < ITEM_BOMB_COUNT; i++)
        bomb[i].flag = IF_FREE;
    for (int i = 0; i < ITEM_POINT_COUNT; i++)
        point[i].flag = IF_FREE;
}

static void item_add(Item &item, int left, int top)
{
    item.flag = IF_SPLASH;
    item.left = left;
    item.top = top;
    item.velocity_y = 2;
    item.splash_radius = 4;
}

void CItems::add_bomb(int left, int top)
{
    for (int i = 0; i < ITEM_BOMB_COUNT; i++) {
        if (bomb[i].flag == IF_FREE) {
            item_add(bomb[i], left, top);
            return;
        }
    }
}

void CItems::add_point(int left, int top)
{
    for (int i = 0; i < ITEM_POINT_COUNT; i++) {
        if (point[i].flag == IF_FREE) {
            item_add(point[i], left, top);
            return;
        }
    }
}

// ReC98 item_player_hittest: a box symmetric around the player's center.
static bool item_player_hittest(const Item &item)
{
    int player_left = player_get_state().x_position;
    int player_top = player_get_state().y_position;
    return (item.left > (player_left - (PLAYER_W - (ITEM_W / 4)))) &&
           (item.left < (player_left + (PLAYER_W - (ITEM_W / 4)))) &&
           (item.top > (player_top - (PLAYER_H - (ITEM_H / 4)))) &&
           CItems::is_visible(item);
}

static void item_update(Item &item, bool is_point)
{
    if (item.flag == IF_FREE)
        return;

    // Collection test (ReC98 runs this before the movement states).
    if (item_player_hittest(item)) {
        if (is_point) {
            item.flag = IF_COLLECTED;
        } else if (rem_bombs < BOMBS_MAX) {
            item.flag = IF_COLLECTED;
        } else {
            item.flag = IF_COLLECTED_OVER_CAP;
        }
        item.velocity_y = -2;
        item.collect_time = 16;
        if (item.top > (PLAYFIELD_BOTTOM - ITEM_H))
            item.top = PLAYFIELD_BOTTOM - ITEM_H;

        if (is_point) {
            // The point value escalates with every pickup and resets when
            // an item is dropped (ReC98 point_hittest / point_drop).
            point_value += POINT_VALUE_STEP;
            if (point_value > POINT_VALUE_MAX)
                point_value = POINT_VALUE_MAX;
            stage_score += point_value;
            popup_add(item.left + ITEM_W / 2, item.top, point_value);
        } else if (item.flag == IF_COLLECTED) {
            rem_bombs++;
        } else {
            stage_score += 10000;
            popup_add(item.left + ITEM_W / 2, item.top, 10000);
        }
        mdrv2_se_play(SFX_PICKUP);
        return;
    }

    if (item.flag == IF_SPLASH) {
        item.splash_radius += 4;
        if (item.splash_radius >= 48)
            item.flag = IF_FALL;
        return;
    }

    if (item.flag == IF_FALL) {
        item.top += item.velocity_y;
        if (item.top >= (PLAYFIELD_BOTTOM - ITEM_H)) {
            item.velocity_y = -8;
            item.flag = IF_BOUNCE;
            // falls through to the bounce step this frame (ReC98 goto)
        } else {
            return;
        }
    }

    if (item.flag == IF_BOUNCE) {
        item.top += item.velocity_y;
        item.velocity_y++;
        if (item.top >= PLAYFIELD_BOTTOM) {
            item.flag = IF_FREE;
            if (is_point)
                point_value = 0;
        }
        return;
    }

    if (item.flag >= IF_COLLECTED) {
        // Pop upward for 16 frames, then free.
        item.top += item.velocity_y;
        if (--item.collect_time == 0)
            item.flag = IF_FREE;
    }
}

void CItems::update()
{
    for (int i = 0; i < ITEM_BOMB_COUNT; i++)
        item_update(bomb[i], false);
    for (int i = 0; i < ITEM_POINT_COUNT; i++)
        item_update(point[i], true);
}
