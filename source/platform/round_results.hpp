#pragma once
#include <cstdint>

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
// A boss may finish at native black. Capture that ending, but restore its
// normal gameplay brightness (not black) at the next scene's first mask.
void round_results_run(const StageBonus &bonus,int stage,uint16_t restore_master);
