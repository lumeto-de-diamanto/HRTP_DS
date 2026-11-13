#pragma once
#include "main/player/death.hpp"

bool player_render_init();
void player_render_frame();
// BG2 blitter particles; pure rendering, no RNG/timer or allocation changes.
void player_invincibility_render_frame();
// Uses the already-resident MIKO cels; no per-miss OBJ or heap allocations.
void player_render_death(const PlayerDeathFrame &frame);
