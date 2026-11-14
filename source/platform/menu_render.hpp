#pragma once

#include "main/bonus.hpp"

// Source-style blocking BG0 modal screens; fixed upper HUD is preserved.

// Returns true if the player confirmed QUIT (after a second confirmation
// screen, like ReC98's pause_menu()). Left/right select; B/A confirm; START
// resumes. QUIT still restarts stage1 until the title/menu flow exists.
bool pause_menu_run();

// Solid translucent BG0 Stage Complete tally, native8x16 ANK (not TOTLE).
// Displays the source x10 components; B/A acknowledge. Caller awards it once;
// drawing and input polling never mutate the score.
void stagebonus_run(const StageBonus &bonus, int stage_num);

// Temporary user-approved prompt after the source game-over burst. True retries
// this stage, false starts a new run; original artwork/continue UI comes later.
bool gameover_menu_run();
