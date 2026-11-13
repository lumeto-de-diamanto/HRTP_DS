#pragma once

#include "main/bonus.hpp"

// Blocking BG0 menu screens over the top-screen playfield.

// Returns true if the player confirmed QUIT (after a second confirmation
// screen, like ReC98's pause_menu()).
bool pause_menu_run();

// Post-stage tally; waits for a key press.
void stagebonus_run(const StageBonus &bonus, int stage_num);

// Temporary user-approved prompt after the source game-over burst. True retries
// this stage, false starts a new run; original artwork/continue UI comes later.
bool gameover_menu_run();
