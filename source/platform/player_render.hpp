#pragma once
#include "main/player/death.hpp"

bool player_render_init();
void player_render_frame();
// Uses the already-resident MIKO cels; no per-miss OBJ or heap allocations.
void player_render_death(const PlayerDeathFrame &frame);
