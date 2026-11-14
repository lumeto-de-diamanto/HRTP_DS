#pragma once

#include "main/bonus.hpp"

// Blocking non-final-boss results, after route selection. The caller awards
// this immutable tally once. No gameplay/RNG updates or runtime asset reads.
// Requires the normal, unbuffered canvas; restores its empty state on exit.
void round_results_run(const StageBonus &bonus, int stage);
