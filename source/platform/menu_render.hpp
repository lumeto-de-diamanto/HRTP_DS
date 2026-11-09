#pragma once

#include "main/bonus.hpp"

// Blocking menu screens on the bottom screen (the originals dim the
// palette and wait for input too).

// Returns true if the player confirmed QUIT (after a second confirmation
// screen, like ReC98's pause_menu()).
bool pause_menu_run();

// Post-stage tally; waits for a key press.
void stagebonus_run(const StageBonus &bonus, int stage_num);
