#pragma once

#include "main/bonus.hpp"

// Blocking non-final-boss results, after route selection. The caller awards
// this immutable tally once. No gameplay/RNG updates or runtime asset reads.
// One joined-screen zipper shifts out the composed HUD/boss image and reveals
// the pre-drawn labels/artwork before the numbers roll. No heap/OBJ allocation.
// Requires the normal, unbuffered canvas; restores its empty state on exit.
void round_results_run(const StageBonus &bonus, int stage);
