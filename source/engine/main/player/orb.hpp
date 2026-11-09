#pragma once

#include "main/math/fixed.hpp"

static const int ORB_W = 32;
static const int ORB_H = 32;

static const int ORB_HITBOX_W = (ORB_W / 2);
static const int ORB_HITBOX_H = (ORB_W / 2);

static const int ORB_HITBOX_LEFT   = ((ORB_W / 2) - (ORB_HITBOX_W / 2));
static const int ORB_HITBOX_TOP    = ((ORB_H / 2) - (ORB_HITBOX_H / 2));
static const int ORB_HITBOX_RIGHT  = ((ORB_W / 2) + (ORB_HITBOX_W / 2));
static const int ORB_HITBOX_BOTTOM = ((ORB_H / 2) + (ORB_HITBOX_H / 2));

enum orb_repel_friction_t {
	OR_NONE = 0,
	OR_MAX = 26,

	// Wouldn't it be cool if those *exactly* matched the orb_velocity_x_t
	// order?
	OR_3_X_UNCHANGED = 100,
	OR_3_X_4_LEFT = 101,
	OR_3_X_4_RIGHT = 102,
	OR_3_X_8_RIGHT = 103,
	OR_3_X_8_LEFT = 104,
};

enum orb_velocity_x_t {
	OVX_0 = 0,
	OVX_4_LEFT = 1,
	OVX_4_RIGHT = 2,
	OVX_8_LEFT = 3,
	OVX_8_RIGHT = 4,
	OVX_COUNT,
};

enum orb_force_t {
	OF_BOUNCE_FROM_SURFACE = 0, // bottom of playfield, or bumper
	OF_BOUNCE_FROM_TOP = 1, // ignores [immediate]
	OF_SHOT = 2,
	OF_IMMEDIATE = 3, // new force passed directly in [immediate]
};

struct OrbState {
	int x_position;
	int y_position;
	int sprite_number;
};

void orb_initialize();
void orb_update();
void orb_hit_player(int repel_friction);
void orb_force_new(fixed immediate, orb_force_t force);

bool orb_hit_by_shot(int shot_left, int shot_top);
bool orb_hits_player(int player_left, int player_top, int repel_friction, bool player_invincible, bool player_invincible_against_orb);

const OrbState &orb_get_state();

// Stage-object interaction (ReC98's orb_velocity_reflect_x/y and the
// portal/bumper state changes).
orb_velocity_x_t orb_get_velocity_x();
void orb_set_velocity_x(orb_velocity_x_t velocity_x);
void orb_set_top(int top);
void orb_set_position(int left, int top);
void orb_velocity_reflect_x();
void orb_velocity_reflect_y();
void orb_set_in_portal(bool in_portal);
bool orb_get_in_portal();
