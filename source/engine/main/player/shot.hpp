#pragma once

static const int SHOT_COUNT = 8;
static const int SHOT_W = 16;
static const int SHOT_H = 16;

struct ShotsState {
	bool alive[SHOT_COUNT];
    int x_position[SHOT_COUNT];
    int y_position[SHOT_COUNT];
	int sprite_number[SHOT_COUNT];
};

void shots_initialize();
void shots_add(int new_left, int new_top);
void shots_update();

// Destroys the first live shot overlapping the given pellet position
// (ReC98's CShots::hittest_pellet). Returns true if a shot was hit.
bool shots_hit_pellet(int pellet_left, int pellet_top);
// Contact/decay only, never HP damage. Width/height are visual hitbox sizes;
// the original tests the shot top-left against (width-SHOT_W,height-SHOT_H).
bool shots_hit_boss(int boss_left, int boss_top, int boss_width, int boss_height);
const ShotsState &shots_get_state();
