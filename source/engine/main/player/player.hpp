#pragma once

#include "main/playfld.hpp"
#include "main/player/invincibility.hpp"

static const int PLAYER_W = 32;
static const int PLAYER_H = 32;

static const int PLAYER_LEFT_MIN = (PLAYFIELD_LEFT);
static const int PLAYER_LEFT_MAX = (PLAYFIELD_RIGHT - PLAYER_W);

static const int PLAYER_LEFT_START = (PLAYFIELD_CENTER_X - (PLAYER_W / 2));

static const int PLAYER_MISS_INVINCIBILITY_FRAMES = 150;

enum PlayerSpritesheet {
	PLAYER,
	PLAYER_32,
	PLAYER_48
};

enum PlayerSpriteNumber {
	SPRITE_IDLE_LEFT = 0,
	SPRITE_WALK_LEFT0 = 1,
	SPRITE_SHOOT_LEFT = 3,
	SPRITE_WALK_SHOOT_LEFT0 = 4,
	SPRITE_IDLE_RIGHT = 10,
	SPRITE_WALK_RIGHT0 = 11,
	SPRITE_SHOOT_RIGHT = 13,
	SPRITE_WALK_SHOOT_RIGHT0 = 14,
};

enum Player32SpriteNumber {
	SPRITE_SLIDE_LEFT0 = 0,
	SPRITE_SLIDE_RIGHT0 = 2,
	SPRITE_SLIDEKICK_LEFT = 4,
	SPRITE_SLIDEKICK_RIGHT = 5
};

enum Player48SpriteNumber {
	SPRITE_SWING0 = 0,
	SPRITE_FLIPKICK_LEFT0 = 8,
	SPRITE_FLIPKICK_RIGHT0 = 14,
	SPRITE_SHOTCOMBO_LEFT0 = 20,
	SPRITE_SHOTCOMBO_RIGHT0 = 24,
};

static const int player_top = (PLAYFIELD_BOTTOM - PLAYER_H);

struct PlayerState {
	int x_position;
	int y_position;
	PlayerSpritesheet spritesheet;
	int sprite_number;
};

void player_initialize();
void player_update(uint16_t keys_held, uint16_t keys_down);
const PlayerState &player_get_state();

// Collision helpers and the shared "player died" signal (ReC98's
// player_is_hit, set by every damage source and consumed by the game loop).
extern bool player_is_hit;
static inline int player_center_x()
{
    return player_get_state().x_position + (PLAYER_W / 2);
}
static inline int player_center_y()
{
    return player_get_state().y_position + (PLAYER_H / 2);
}
bool player_is_sliding();
bool player_is_deflecting();

// Post-animation recovery: reset attack/deflection state, respawn at center,
// extend the current timer and grant PLAYER_MISS_INVINCIBILITY_FRAMES grace.
void player_miss();
bool player_is_invincible();
bool player_is_invincible_to_orb();
const InvincibilitySparkles &player_invincibility_sparkles();
