#pragma once

// Port of ReC98 th01/main/bonus.cpp: the per-stage tally shown after a
// card stage is cleared. Each metric is clamped to 6553 and counted into
// the bonus ten times (ZUN's clamp_and_add_x10_to_score_bonus).

struct StageBonus {
    int time;       // stage_timer * 3
    int combo;      // cardcombo_max * 100
    int resources;  // lives * 200 + bombs * 100
    int stage;      // (stage_num - 1) * 200
    int total;      // (sum of the clamped metrics) * 10
};

StageBonus stagebonus_compute(int stage_num, int lives);
