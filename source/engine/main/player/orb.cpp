#include "main/playfld.hpp"
#include "main/math/overlap.hpp"
#include "main/math/fixed.hpp"
#include "main/stage/stageobj.hpp"
#include "orb.hpp"
#include "shot.hpp"
#include "player.hpp"

OrbState orb;

static const int ORB_CELS = 4;
static const int ORB_FRAMES_PER_CEL = 3;

static const int ORB_LEFT_MIN = (PLAYFIELD_LEFT);
static const int ORB_LEFT_MAX = (PLAYFIELD_RIGHT - ORB_W);
static const int ORB_TOP_MIN = (PLAYFIELD_TOP);
static const int ORB_TOP_MAX = (PLAYFIELD_BOTTOM - ORB_H);

static const int ORB_LEFT_START = (ORB_LEFT_MAX -  8);
static const int ORB_TOP_START = ( ORB_TOP_MAX - 88);
static const orb_velocity_x_t ORB_VELOCITY_X_START = OVX_4_LEFT;

#define ORB_FORCE_START fixed(-8.0)

int rotation_frame;

bool orb_in_portal;
fixed orb_force;
int orb_force_frame;

orb_velocity_x_t orb_velocity_x;
fixed orb_velocity_y;

#define COEFFICIENT_OF_RESTITUTION fixed(0.78)

void orb_initialize() {
    orb.x_position = ORB_LEFT_START;
    orb.y_position = ORB_TOP_START;
    orb.sprite_number = 0;
    rotation_frame = 0;

    orb_in_portal = false;
    orb_force = ORB_FORCE_START;
    orb_force_frame = 0;
    
    orb_velocity_x = ORB_VELOCITY_X_START;
    orb_velocity_y = 0.0;
}

inline fixed gravity_for(const fixed& force) {
	return (fixed(orb_force_frame / 5) + orb_force);
}

void orb_force_new(fixed immediate, orb_force_t force)
{
	if(force == OF_BOUNCE_FROM_SURFACE) {
		orb_force = (-orb_velocity_y * COEFFICIENT_OF_RESTITUTION);
        //orb_force = (-int(orb_velocity_y) * 200) >> 8;
		if(orb_velocity_x == OVX_0) {
            if(orb_force_frame < 17) {
                int val = irand() % 50;
                if (val == 0) {
                    orb_velocity_x = OVX_4_LEFT;
                } else if (val == 1) {
                    orb_velocity_x = OVX_4_RIGHT;
                }
            }
		}
	}
	if(force == OF_BOUNCE_FROM_TOP) {
		orb_force = ((-orb_velocity_y) - (orb_force_frame / 4));
	}
	if(force == OF_SHOT) {
		orb_force = (fixed(-10.0) + (orb_velocity_y / 2.0));
	}
	if(force == OF_IMMEDIATE) {
		orb_force = immediate;
	}
	orb_force_frame = 0;
}

void orb_update() {
    orb_force_frame++;
    if(!orb_in_portal) {
        // Horizontal movement
        switch(orb_velocity_x) {
        case OVX_0: break;
        case  OVX_4_LEFT:	orb.x_position -= 4;	break;
        case OVX_4_RIGHT:	orb.x_position += 4;	break;
        case  OVX_8_LEFT:	orb.x_position -= 8;	break;
        case OVX_8_RIGHT:	orb.x_position += 8;	break;
        }
        if(orb.x_position <= ORB_LEFT_MIN) {
            if(orb_velocity_x == OVX_4_LEFT) {
                orb_velocity_x = OVX_4_RIGHT;
            } else if(orb_velocity_x == OVX_8_LEFT) {
                orb_velocity_x = OVX_8_RIGHT;
            }
        }
        if(orb.x_position >= ORB_LEFT_MAX) {
            if(orb_velocity_x == OVX_4_RIGHT) {
                orb_velocity_x = OVX_4_LEFT;
            } else if(orb_velocity_x == OVX_8_RIGHT) {
                orb_velocity_x = OVX_8_LEFT;
            }
        }

        // Vertical movement
        orb_velocity_y = gravity_for(orb_force);
        if(orb_velocity_y > fixed(16.0f)) {
            orb_velocity_y = fixed(16.0f);
        } else if(orb_velocity_y < fixed(-16.0f)) {
            orb_velocity_y = fixed(-16.0f);
        }
        // ReC98 orb_velocity_y_update() returns pixel_t, truncating the
        // unclamped displacement before addition. Converting fixed to double
        // instead goes through its nearest-integer operator and can sustain
        // full-height ceiling/floor loops. Do not change fixed's global rounding.
        orb.y_position += gravity_for(orb_force).trunc_to_int();

        if (orb_velocity_x == OVX_4_LEFT || orb_velocity_x == OVX_8_LEFT)
            rotation_frame++;
        else if (orb_velocity_x == OVX_4_RIGHT || orb_velocity_x == OVX_8_RIGHT)
            rotation_frame--;

        if (rotation_frame >= ORB_CELS * ORB_FRAMES_PER_CEL)
            rotation_frame = 0;
        if (rotation_frame < 0)
            rotation_frame = ORB_CELS * ORB_FRAMES_PER_CEL - 1;

		if(orb.y_position > ORB_TOP_MAX) {
			orb_force_new(COEFFICIENT_OF_RESTITUTION, OF_BOUNCE_FROM_SURFACE);
			orb.y_position = ORB_TOP_MAX;
			stage_cardcombo_reset();
		}
		if(orb.y_position < ORB_TOP_MIN) {
			orb_force_new(0, OF_BOUNCE_FROM_TOP);
			orb.y_position = ORB_TOP_MIN;
		}
    }
    orb.sprite_number = rotation_frame / ORB_FRAMES_PER_CEL;
    
    // TODO: Handle card collision here?

    // Orb damage here
}

bool orb_hit_by_shot(int shot_left, int shot_top) {
	if(overlap_xywh_xywh_lt_gt(
		shot_left, shot_top, SHOT_W, SHOT_H,
		orb.x_position, orb.y_position, ORB_W, ORB_H
	)) {
		if((shot_left - orb.x_position) > (SHOT_W / 2)) {
			orb_velocity_x = OVX_4_LEFT;
		} else if((shot_left - orb.x_position) == (SHOT_W / 2)) {
			orb_velocity_x = OVX_0;
		} else if((shot_left - orb.x_position) > (-SHOT_W)) {
			orb_velocity_x = OVX_4_RIGHT;
		}
		orb_force_new(0, OF_SHOT);
		return true;
	}
	return false;
}

static const int ORB_FORCE_REPEL = -13;

template <class T> inline T delta_abs(const T p1, const T p2) {
	return ((p1 - p2) < 0) ? ((p1 - p2) * -1) : (p1 - p2);
}

#define orb_overlaps_player(hitbox_w, hitbox_h) ( \
	(delta_abs(orb.x_position, player_left) < hitbox_w) && \
	(delta_abs(orb.y_position,  player_top)  < hitbox_h) \
)

#define player_in_repel_range() \
	orb_overlaps_player((PLAYER_W + (ORB_W / 4)), PLAYER_H)

// Perhaps split this into three functions?
bool orb_hits_player(int player_left, int player_top, int repel_friction, bool player_invincible, bool player_invincible_against_orb) {
	if(repel_friction == OR_NONE) {
		if(
			!player_invincible &&
			!player_invincible_against_orb &&
			orb_overlaps_player((PLAYER_W - (ORB_W / 4)), (PLAYER_H - (ORB_H / 2)))
		) {
            // Player has been beaned!
			return true;
		}
	} else if(repel_friction < OR_3_X_UNCHANGED) {
		if(!player_in_repel_range()) {
			return false;
		}
		if((player_left - orb.x_position) > 0) {
			orb_velocity_x = OVX_4_LEFT;
		} else if((player_left - orb.x_position) == 0) {
			orb_velocity_x = OVX_0;
			if((irand() % 8) == 0) {
				orb_velocity_x = OVX_4_LEFT;
			}
			if((irand() % 8) == 4) {
				orb_velocity_x = OVX_4_RIGHT;
			}
		} else {
			orb_velocity_x = OVX_4_RIGHT;
		}
		orb_force_new(((repel_friction / 2) + ORB_FORCE_REPEL), OF_IMMEDIATE);
	} else if(player_in_repel_range()) {
		if(repel_friction == OR_3_X_4_LEFT) {
			orb_velocity_x = OVX_4_LEFT;
		} else if(repel_friction == OR_3_X_4_RIGHT) {
			orb_velocity_x = OVX_4_RIGHT;
		} else if(repel_friction == OR_3_X_8_RIGHT) {
			orb_velocity_x = OVX_8_RIGHT;
		} else if(repel_friction == OR_3_X_8_LEFT) {
			orb_velocity_x = OVX_8_LEFT;
		}
		orb_force_new(-10.0, OF_IMMEDIATE);
	}
    return false;
}

const OrbState &orb_get_state()
{
	return orb;
}

// Stage-object interaction
// ------------------------

orb_velocity_x_t orb_get_velocity_x()
{
	return orb_velocity_x;
}

void orb_set_velocity_x(orb_velocity_x_t velocity_x)
{
	orb_velocity_x = velocity_x;
}

void orb_set_top(int top)
{
	orb.y_position = top;
}

void orb_set_position(int left, int top)
{
	orb.x_position = left;
	orb.y_position = top;
}

// ReC98 th01/main/player/orb.hpp:93-107. The X reflection just flips the
// discrete speed enum and is not idempotent within a single frame.
void orb_velocity_reflect_x()
{
	switch (orb_velocity_x) {
	case OVX_4_LEFT:  orb_velocity_x = OVX_4_RIGHT; break;
	case OVX_4_RIGHT: orb_velocity_x = OVX_4_LEFT;  break;
	case OVX_8_LEFT:  orb_velocity_x = OVX_8_RIGHT; break;
	case OVX_8_RIGHT: orb_velocity_x = OVX_8_LEFT;  break;
	case OVX_0: break;
	}
}

void orb_velocity_reflect_y()
{
	orb_force_new(-orb_velocity_y, OF_IMMEDIATE);
}

void orb_set_in_portal(bool in_portal)
{
	orb_in_portal = in_portal;
}

bool orb_get_in_portal()
{
	return orb_in_portal;
}
