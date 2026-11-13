#include "player.hpp"
#include "shot.hpp"
#include "orb.hpp"
#include "main/stage/timer.hpp"
#include "rank.h"
#include "nds/input.h"
#include "sound_effects.hpp"

static const int RUN_CELS = 2;
static const int RUN_FRAMES_PER_CEL = 4;
static const int RUN_FRAMES = (RUN_FRAMES_PER_CEL * RUN_CELS);

#define SWING_FRAMES_PER_CEL 3
#define SWING_FRAMES 23

static const int SLIDE_FRAMES = 12;
static const int SLIDE_CELS = 2;

#define SHOTCOMBO_FRAMES_PER_CEL 6
#define SHOTCOMBO_FRAMES 19

PlayerState player;

rank_t rank = RANK_NORMAL;

void player_move_and_clamp(int delta)
{
	player.x_position += delta;
	if (player.x_position  < PLAYER_LEFT_MIN) {
		player.x_position  = PLAYER_LEFT_MIN;
	} else if (player.x_position  >= PLAYER_LEFT_MAX) {
		player.x_position  = PLAYER_LEFT_MAX;
	}
}

inline void player_shoot(int distance_x_from_center = 0) {
	shots_add(
		(player.x_position + (PLAYER_W / 4) + distance_x_from_center), player_top
	);
}

enum PlayerMode {
	M_REGULAR = 0,

	// Regular shot while standing still or dashing. Quickly switches back
	// into M_REGULAR.
	M_SHOOT = 1,

	// Stationary swing, or sliding left or right. Not interruptible, but can
	// launch into a special attack combo in the end.
	M_SWING_OR_SLIDE = 2,

	// First, second, and third special attacks in a potential combo.
	M_SPECIAL_FIRST = 3,
	M_SPECIAL_SECOND = 4,
	M_SPECIAL_THIRD = 5,
};

enum Submode {
	STATIONARY,
	RUN_LEFT,
	RUN_RIGHT
};

enum FacingDirection {
	FACE_LEFT,
	FACE_RIGHT
};

enum SpecialSubmode {
	SS_FLIPKICK_MOVING_LEFT = 0,
	SS_FLIPKICK_MOVING_RIGHT = 1,
	SS_FLIPKICK_STATIONARY_LEFT = 2,
	SS_FLIPKICK_STATIONARY_RIGHT = 3,
	SS_SLIDEKICK_LEFT = 4,
	SS_SLIDEKICK_RIGHT = 5,
	SS_SHOTCOMBO_LEFT = 6,
	SS_SHOTCOMBO_RIGHT = 7,
};

int run_cycle;
int mode_timer;

bool deflecting;
bool sliding;
bool combo_enabled;
bool invincible_to_orb;
bool invincible;
int miss_invincibility_frames;
static InvincibilitySparkles invincibility_sparkles;

bool player_is_hit = false;

int swing_deflection_frames;
int special_frames;

FacingDirection facing_dir;
PlayerMode mode;
Submode submode;
SpecialSubmode special_submode;

void inline increment_mode() {
	if (mode == M_SPECIAL_FIRST) mode = M_SPECIAL_SECOND;
	if (mode == M_SPECIAL_SECOND) mode = M_SPECIAL_THIRD;
}

void player_initialize() {
	run_cycle = 0;
	mode_timer = 0;

	facing_dir = FACE_LEFT;
	mode = M_REGULAR;
	submode = STATIONARY;

	deflecting = false;
	sliding = false;
	combo_enabled = false;
	invincible_to_orb = false;
	invincible = false;
	miss_invincibility_frames = 0;
	invincibility_sparkles.reset();

	player.x_position = PLAYER_LEFT_START;
	player.y_position = player_top;
	player.spritesheet = PLAYER;
	player.sprite_number = SPRITE_IDLE_LEFT;
	player.spritesheet = PLAYER;

	switch (rank) {
        case RANK_EASY:
		swing_deflection_frames = 15;
          break;
        case RANK_NORMAL:
		swing_deflection_frames = 12;
          break;
        case RANK_HARD:
		swing_deflection_frames = 10;
          break;
        case RANK_LUNATIC:
		swing_deflection_frames = 8;
          break;
    }
}

static void player_update_controls(uint16_t keys_held, uint16_t keys_down) {
	if (miss_invincibility_frames > 0)
		miss_invincibility_frames--;
	bool left_held = (keys_held & KEY_LEFT) != 0;
	bool right_held = (keys_held & KEY_RIGHT) != 0;
	bool a_held = (keys_held & KEY_A) != 0;
	bool b_held = (keys_held & KEY_B) != 0;

	bool a_down = (keys_down & KEY_A) != 0;
	bool b_down = (keys_down & KEY_B) != 0;

	run_cycle++;
	if (run_cycle >= RUN_FRAMES) run_cycle = 0;

	if (mode == M_REGULAR) {
		if (left_held && !right_held) {
			// Go left
			facing_dir = FACE_LEFT;
			submode = RUN_LEFT;
			player_move_and_clamp(-4);
			player.sprite_number = SPRITE_WALK_LEFT0 + run_cycle / RUN_FRAMES_PER_CEL;
		} else if (right_held && !left_held) {
			// Go right
			facing_dir = FACE_RIGHT;
			submode = RUN_RIGHT;
			player_move_and_clamp(+4);
			player.sprite_number = SPRITE_WALK_RIGHT0 + run_cycle / RUN_FRAMES_PER_CEL;
		} else {
			// Stand still
			submode = STATIONARY;
			player.sprite_number = (facing_dir == FACE_LEFT) ? SPRITE_IDLE_LEFT : SPRITE_IDLE_RIGHT;
		}

		// Shot detection here
		if (b_down) {
			mode = M_SHOOT;
			mode_timer = 0;
			switch (submode) {
				case STATIONARY:
					player.sprite_number = (facing_dir == FACE_LEFT) ? SPRITE_SHOOT_LEFT : SPRITE_SHOOT_RIGHT;
				break;
				case RUN_LEFT:
					player.sprite_number = SPRITE_WALK_SHOOT_LEFT0;
				break;
				case RUN_RIGHT:
					player.sprite_number = SPRITE_WALK_SHOOT_RIGHT0;
				break;
			}
		}

		// Swing detection here
		if (a_down) {
			invincible_to_orb = true;
			if (!left_held && !right_held) {
				// swing
				mode = M_SWING_OR_SLIDE;
				mode_timer = 0;
				submode = STATIONARY;
				deflecting = true;
				player.spritesheet = PLAYER_48;
				player.sprite_number = SPRITE_SWING0;
			} else {
				// slide
				mode = M_SWING_OR_SLIDE;
				mode_timer = 0;
				submode = left_held ? RUN_LEFT : RUN_RIGHT;
				sliding = true;
				combo_enabled = false;
				mdrv2_se_play(SFX_SLIDE);
				player.spritesheet = PLAYER_32;
				player.sprite_number = left_held ? SPRITE_SLIDE_LEFT0 : SPRITE_SLIDE_RIGHT0;
			}
		}
	} else if (mode == M_SHOOT) {
		switch (submode) {
			case STATIONARY:
				if (mode_timer == 0) {player_shoot(0);}
				player.sprite_number = (facing_dir == FACE_LEFT) ? SPRITE_SHOOT_LEFT : SPRITE_SHOOT_RIGHT;
				if (mode_timer >= 2) {
					mode = M_REGULAR;
					b_down = false;
				}
			break;
			case RUN_LEFT:
				player_move_and_clamp(-4);
				player.sprite_number = SPRITE_WALK_SHOOT_LEFT0 + (mode_timer > 0 ? 1 : 0);
				if (mode_timer >= 1) {
					player_shoot();
					mode = M_REGULAR;
					b_down = false;
				}
			break;
			case RUN_RIGHT:
				player_move_and_clamp(+4);
				player.sprite_number = SPRITE_WALK_SHOOT_RIGHT0 + (mode_timer > 0 ? 1 : 0);
				if (mode_timer >= 1) {
					player_shoot();
					mode = M_REGULAR;
					b_down = false;
				}
			break;
		}
		mode_timer++;
	} else if(mode == M_SWING_OR_SLIDE) {
		if (submode == STATIONARY) {
			// 1 frame grace period where you can slide
			if (mode_timer < 1) {
				if (left_held) submode = RUN_LEFT;
				if (right_held) submode = RUN_RIGHT;
				if (submode != STATIONARY) {
					mdrv2_se_play(SFX_SLIDE);
					deflecting = false;
					sliding = true;
					player.spritesheet = PLAYER_32;
					player.sprite_number = left_held ? SPRITE_SLIDE_LEFT0 : SPRITE_SLIDE_RIGHT0;
				}
			} else {
				// The swing itself
				player.spritesheet = PLAYER_48;
				player.sprite_number = SPRITE_SWING0 + mode_timer / SWING_FRAMES_PER_CEL;
				if(mode_timer == swing_deflection_frames) {
					deflecting = false;
				}
				if(mode_timer >= SWING_FRAMES) {
					mode = M_REGULAR;
					submode = STATIONARY;
					player.spritesheet = PLAYER;
					mode_timer = 0;
					a_down = false;
					invincible_to_orb = false;
				}
			}
		} else if (submode == RUN_LEFT) {
			// Slide left
			player_move_and_clamp(-6);
			player.spritesheet = PLAYER_32;
			player.sprite_number = SPRITE_SLIDE_LEFT0 + mode_timer % SLIDE_CELS;
			if((mode_timer >= 5) && !a_held && !b_held) {
				combo_enabled = true;
			}
			if(mode_timer >= SLIDE_FRAMES) {
				mode = M_REGULAR;
				submode = STATIONARY;
				player.spritesheet = PLAYER;
				mode_timer = 0;
				sliding = false;
				// TODO: Understand and implement the combo
				if((combo_enabled == true) && (a_held == true)) {
					special_submode =
							right_held ? (SS_FLIPKICK_STATIONARY_LEFT) :
							left_held  ? (SS_SLIDEKICK_LEFT) :
							/*  no direction  */  (SS_FLIPKICK_MOVING_LEFT);

					mode = M_SPECIAL_FIRST;
					if(special_submode != SS_SLIDEKICK_LEFT) {
						deflecting = true;
					}
					combo_enabled = false;
					mdrv2_se_play(SFX_SLIDE);
					return;
				} else if((combo_enabled == true) && (b_held == true)) {
					special_submode = SS_SHOTCOMBO_LEFT;
					mode = M_SPECIAL_FIRST;
					deflecting = true;
					mdrv2_se_play(SFX_SLIDE);
					combo_enabled = false;
					return;
				}
				invincible_to_orb = false;
			}
		} else if (submode == RUN_RIGHT) {
			// Slide right
			player_move_and_clamp(6);
			player.spritesheet = PLAYER_32;
			player.sprite_number = SPRITE_SLIDE_RIGHT0 + mode_timer % SLIDE_CELS;
			if((mode_timer >= 5) && !a_held && !b_held) {
				combo_enabled = true;
			}
			if(mode_timer >= SLIDE_FRAMES) {
				mode = M_REGULAR;
				submode = STATIONARY;
				player.spritesheet = PLAYER;
				mode_timer = 0;
				sliding = false;
				// TODO: Understand and implement the combo
				if((combo_enabled == true) && (a_held == true)) {
					special_submode =
							left_held ? (SS_FLIPKICK_STATIONARY_RIGHT) :
							right_held  ? (SS_SLIDEKICK_RIGHT) :
							/*  no direction  */  (SS_FLIPKICK_MOVING_RIGHT);

					mode = M_SPECIAL_FIRST;
					if(special_submode != SS_SLIDEKICK_RIGHT) {
						deflecting = true;
					}
					combo_enabled = false;
					mdrv2_se_play(SFX_SLIDE);
					return;
				} else if((combo_enabled == true) && (b_held == true)) {
					special_submode = SS_SHOTCOMBO_RIGHT;
					mode = M_SPECIAL_FIRST;
					deflecting = true;
					mdrv2_se_play(SFX_SLIDE);
					combo_enabled = false;
					return;
				}
				invincible_to_orb = false;
			}
		}
		mode_timer++;
		// Orb hit tests go here
		if(submode == STATIONARY) {
			if(mode_timer < 21) {
				orb_hits_player(player.x_position, player.y_position, mode_timer, invincible, invincible_to_orb);
			}
		} else {
			if(submode == RUN_RIGHT) {
				orb_hits_player(player.x_position, player.y_position, OR_3_X_4_RIGHT, invincible, invincible_to_orb);
			}
			if(submode == RUN_LEFT) {
				orb_hits_player(player.x_position, player.y_position, OR_3_X_4_LEFT, invincible, invincible_to_orb);
			}
		}
	} else if (
		(mode == M_SPECIAL_FIRST) ||
		(mode == M_SPECIAL_SECOND) ||
		(mode == M_SPECIAL_THIRD)
	) {
		// display special
		switch(special_submode) {
            case SS_FLIPKICK_MOVING_LEFT:
				player.spritesheet = PLAYER_48;
				player.sprite_number = SPRITE_FLIPKICK_LEFT0 + (mode_timer < 10 ? 0 : ((mode_timer - 6) / 4));
				player_move_and_clamp(-2);
				break;
            case SS_FLIPKICK_MOVING_RIGHT:
				player.spritesheet = PLAYER_48;
				player.sprite_number = SPRITE_FLIPKICK_RIGHT0 + (mode_timer < 10 ? 0 : ((mode_timer - 6) / 4));
				player_move_and_clamp(2);
				break;
            case SS_FLIPKICK_STATIONARY_LEFT:
				player.spritesheet = PLAYER_48;
				player.sprite_number = SPRITE_FLIPKICK_LEFT0 + (mode_timer < 10 ? 0 : ((mode_timer - 6) / 4));
				break;
            case SS_FLIPKICK_STATIONARY_RIGHT:
				player.spritesheet = PLAYER_48;
				player.sprite_number = SPRITE_FLIPKICK_RIGHT0 + (mode_timer < 10 ? 0 : ((mode_timer - 6) / 4));
				break;
            case SS_SLIDEKICK_LEFT:
				player.spritesheet = PLAYER_32;
				player.sprite_number = SPRITE_SLIDEKICK_LEFT;
				player_move_and_clamp(-6);
				break;
            case SS_SLIDEKICK_RIGHT:
				player.spritesheet = PLAYER_32;
				player.sprite_number = SPRITE_SLIDEKICK_RIGHT;
				player_move_and_clamp(6);
				break;
            case SS_SHOTCOMBO_LEFT:
				player.spritesheet = PLAYER_48;
				player.sprite_number = SPRITE_SHOTCOMBO_LEFT0 + (mode_timer / SHOTCOMBO_FRAMES_PER_CEL);
				break;
            case SS_SHOTCOMBO_RIGHT:
				player.spritesheet = PLAYER_48;
				player.sprite_number = SPRITE_SHOTCOMBO_RIGHT0 + (mode_timer / SHOTCOMBO_FRAMES_PER_CEL);
				break;
            }
			// Then, update special state
			switch(special_submode) {
                case SS_FLIPKICK_MOVING_LEFT:
                case SS_FLIPKICK_MOVING_RIGHT:
                case SS_FLIPKICK_STATIONARY_LEFT:
                case SS_FLIPKICK_STATIONARY_RIGHT:
					if(mode_timer == 20) {
						deflecting = false;
					}
					if((mode_timer >= 20) && !a_held && !b_held)  {
						combo_enabled = true;
					}
					special_frames = 28;
					break;
                case SS_SLIDEKICK_LEFT:
                case SS_SLIDEKICK_RIGHT:
					special_frames = 13;
					break;
                case SS_SHOTCOMBO_LEFT:
                case SS_SHOTCOMBO_RIGHT:
					if(mode_timer ==  1) { player_shoot( -8); }
					if(mode_timer ==  4) { player_shoot( +8); }
					if(mode_timer ==  7) { player_shoot(  0); }
					if(mode_timer == 10) { player_shoot(-16); }
					if(mode_timer == 13) { player_shoot(  0); }
					if(mode_timer == 16) { player_shoot(+16); }
					if(mode_timer == 19) { player_shoot(  0); }
					if(mode_timer == 4) {
						deflecting = false;
					}
					special_frames = SHOTCOMBO_FRAMES;
                    break;
            }
			mode_timer++;
			if(mode_timer > special_frames) {
				mode_timer = 0;
				sliding = false;
				deflecting = false;
				if(mode < M_SPECIAL_THIRD) {
					// Combo continuation
					if((combo_enabled == true) && (a_held == true)) {
						if(special_submode == SS_FLIPKICK_MOVING_LEFT 
							|| special_submode == SS_FLIPKICK_STATIONARY_LEFT) {
							special_submode = 
								right_held ? SS_FLIPKICK_STATIONARY_LEFT :
								left_held ? SS_SLIDEKICK_LEFT :
									SS_FLIPKICK_MOVING_LEFT;
						} else if(special_submode == SS_FLIPKICK_MOVING_RIGHT 
							|| special_submode == SS_FLIPKICK_STATIONARY_RIGHT) {
							special_submode = 
								left_held ? SS_FLIPKICK_STATIONARY_RIGHT :
								right_held ? SS_SLIDEKICK_RIGHT :
									SS_FLIPKICK_MOVING_RIGHT;
						}
						increment_mode();
						if(special_submode < SS_SLIDEKICK_LEFT) {
							deflecting = true;
						}
						mdrv2_se_play(SFX_SLIDE);
						combo_enabled = false;
						return;
					} else if((combo_enabled == true) && (b_held == true)) {
						if(special_submode == SS_FLIPKICK_MOVING_LEFT 
							|| special_submode == SS_FLIPKICK_STATIONARY_LEFT) {
							special_submode = SS_SHOTCOMBO_LEFT;
						} else if(special_submode == SS_FLIPKICK_MOVING_RIGHT 
							|| special_submode == SS_FLIPKICK_STATIONARY_RIGHT) {
							special_submode = SS_SHOTCOMBO_RIGHT;
						}
						increment_mode();
						deflecting = true;
						mdrv2_se_play(SFX_SLIDE);
						combo_enabled = false;
						return;
					}
				}
				invincible_to_orb = false;
				mode = M_REGULAR;
				submode = STATIONARY;
				player.spritesheet = PLAYER;
				player.sprite_number = SPRITE_IDLE_LEFT;

			} else {
				// Orb hit tests go here
				switch(special_submode) {
				case (SS_FLIPKICK_MOVING_RIGHT):
				case (SS_FLIPKICK_MOVING_LEFT):
				case (SS_FLIPKICK_STATIONARY_RIGHT):
				case (SS_FLIPKICK_STATIONARY_LEFT):
					orb_hits_player(player.x_position, player.y_position, mode_timer / 2, invincible, invincible_to_orb);
					break;
				case (SS_SLIDEKICK_RIGHT):
					orb_hits_player(player.x_position, player.y_position, OR_3_X_8_RIGHT, invincible, invincible_to_orb);
					break;
				case (SS_SLIDEKICK_LEFT):
					orb_hits_player(player.x_position, player.y_position, OR_3_X_8_LEFT, invincible, invincible_to_orb);
					break;
				case (SS_SHOTCOMBO_RIGHT):
				case (SS_SHOTCOMBO_LEFT):
					if(mode_timer < 16) {
						orb_hits_player(player.x_position, player.y_position, mode_timer, invincible, invincible_to_orb);
					}
					break;
				}
			}
        }
}

void player_update(uint16_t keys_held, uint16_t keys_down)
{
    // The control routine has early returns during special attacks. Keep the
    // cosmetic tick outside it so all invincible movement advances once.
    player_update_controls(keys_held, keys_down);
    invincibility_sparkles.update(player_is_invincible(), player.x_position, player.y_position);
}

const InvincibilitySparkles &player_invincibility_sparkles()
{
    return invincibility_sparkles;
}

const PlayerState &player_get_state()
{
	return player;
}

void player_miss()
{
	// Cancel every swing/slide/special and its deflection flags, not merely X/Y.
	player_initialize();
	miss_invincibility_frames = PLAYER_MISS_INVINCIBILITY_FRAMES;
	invincibility_sparkles.update(true, player.x_position, player.y_position);
	player_is_hit = false;
	// ReC98 player_miss_animate_and_update(): surviving a miss extends the
	// current timer, canceling HURRY UP rather than restarting the stage.
	timer_extend();
}

bool player_is_invincible()
{
	return invincible || (miss_invincibility_frames > 0);
}

bool player_is_invincible_to_orb()
{
	return invincible_to_orb;
}

bool player_is_sliding()
{
	return sliding;
}

bool player_is_deflecting()
{
	return deflecting;
}
