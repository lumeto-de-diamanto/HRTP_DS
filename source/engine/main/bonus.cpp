// Port of ReC98 th01/main/bonus.cpp (stagebonus_animate's math).

#include "bonus.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/item.hpp"
#include "main/stage/timer.hpp"

static int bonus_clamp(int val)
{
    return (val > 6553) ? 6553 : val;
}

StageBonus stagebonus_compute(int stage_num, int lives)
{
    StageBonus b;
    b.time = bonus_clamp(stage_timer * 3);
    b.combo = bonus_clamp(stage_cardcombo_max * 100);
    b.resources = bonus_clamp((lives * 200) + (rem_bombs * 100));
    b.stage = bonus_clamp((stage_num - 1) * 200);
    b.total = (b.time + b.combo + b.resources + b.stage) * 10;
    return b;
}
