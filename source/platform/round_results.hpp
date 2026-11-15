#pragma once

#include "main/bonus.hpp"

// Blocking non-final-boss results, after route selection. The caller awards
// this immutable tally once. No gameplay/RNG updates or runtime asset reads.
// One joined-screen zipper shifts out the composed HUD/boss image and reveals
// the pre-drawn labels/artwork before the numbers roll. No heap/OBJ allocation.
// Requires the normal, unbuffered canvas; restores its empty state on exit.
// After acknowledgment, fade BOTH result screens and leave them black through
// cleanup. The caller must load the next scene; its first kanji mask releases
// black and restores both original brightness settings.
void round_results_run(const StageBonus &bonus, int stage);
