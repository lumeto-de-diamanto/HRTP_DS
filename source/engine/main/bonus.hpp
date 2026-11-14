#pragma once

// ReC98 th01/main/bonus.cpp tallies. Metrics are stored before the x10 award;
// card-stage and non-final boss/round results use different source factors.

struct StageBonus {
    int time;       // timer *3 (card) / *5 (round), clamped6553
    int combo;      // max combo *100 / *200, clamped6553
    int resources;  // lives*200+bombs*100 / lives*500+bombs*200, clamped6553
    int stage;      // (stage-1)*200 / stage*1000 (round clamp65530)
    int total;      // (sum of the clamped metrics) * 10
};

StageBonus stagebonus_compute(int stage_num, int lives);
StageBonus roundbonus_compute(int stage_num, int lives);
