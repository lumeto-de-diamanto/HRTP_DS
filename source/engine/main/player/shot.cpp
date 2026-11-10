#include "shot.hpp"
#include "orb.hpp"
#include "main/playfld.hpp"
#include "sound_effects.hpp"

static const int SHOT_DECAY_FRAMES = 7;

ShotsState shots;
int decay_frame[SHOT_COUNT];
bool moving[SHOT_COUNT];

void shots_initialize() {
    for (int i = 0; i < SHOT_COUNT; i++) {
        shots.x_position[i] = 0;
        shots.y_position[i] = 0;
        shots.alive[i] = false;
        shots.sprite_number[i] = 0;

        decay_frame[i] = 0;
        moving[i] = false;
    }
}

void shots_add(int new_left, int new_top) {
	if(new_left < PLAYFIELD_LEFT || new_left > (PLAYFIELD_RIGHT - 1)) {
		return;
	}
	for(int i = 0; i < SHOT_COUNT; i++) {
		if(shots.alive[i] == true) {
			continue;
		}
		if(decay_frame[i] != 0) {
			continue;
		}
		shots.x_position[i] = new_left;
		shots.y_position[i] = new_top;
		shots.alive[i] = true;
        shots.sprite_number[i] = 0;

		decay_frame[i] = 0;
        moving[i] = true;

        mdrv2_se_play(SFX_SHOT);
		return;
	}
}

void shots_update() {
    for(int i = 0; i < SHOT_COUNT; i++) {
        if (!shots.alive[i]) continue;
        if (moving[i] == true) { 
            if (orb_hit_by_shot(shots.x_position[i], shots.y_position[i])) {
                moving[i] = false;
                continue;
            }
            shots.y_position[i] -= 12;
            if(shots.y_position[i] <= PLAYFIELD_TOP) { shots.alive[i] = false; }
        } else {
            decay_frame[i]++;
            if(decay_frame[i] > SHOT_DECAY_FRAMES) {
				decay_frame[i] = 0;
                shots.alive[i] = false;
			} else {
                shots.sprite_number[i] = decay_frame[i] / 4 + 1;
            }
        }
    }
}

bool shots_hit_pellet(int pellet_left, int pellet_top)
{
    for (int i = 0; i < SHOT_COUNT; i++) {
        if (!shots.alive[i] || !moving[i])
            continue;
        int dx = pellet_left - shots.x_position[i];
        int dy = pellet_top - shots.y_position[i];
        // ReC98 tests the pellet against the shot box shrunk by 2px.
        if ((dx > -6) && (dx < (SHOT_W - 2)) &&
            (dy > -6) && (dy < (SHOT_H - 2))) {
            moving[i] = false;
            decay_frame[i] = 1;
            shots.sprite_number[i] = 1;
            mdrv2_se_play(SFX_SHOT_HIT);
            return true;
        }
    }
    return false;
}

const ShotsState &shots_get_state()
{
	return shots;
}

bool shots_hit_boss(int left, int top, int width, int height)
{
    if (width < SHOT_W || height < SHOT_H)
        return false;
    for (int i = 0; i < SHOT_COUNT; ++i) {
        if (!shots.alive[i] || !moving[i])
            continue;
        if (shots.x_position[i] >= left && shots.x_position[i] <= left + width - SHOT_W &&
            shots.y_position[i] >= top && shots.y_position[i] <= top + height - SHOT_H) {
            moving[i] = false;
            decay_frame[i] = 1;
            shots.sprite_number[i] = 1;
            mdrv2_se_play(SFX_SHOT_HIT);
            return true;
        }
    }
    return false;
}
