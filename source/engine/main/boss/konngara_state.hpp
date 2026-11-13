#pragma once
#include "boss.hpp"
#include "konngara_visual.hpp"
#include "main/bullet/shootout_laser.hpp"

struct KonngaraDiamond { int left, top, color; };
struct KonngaraCombatState {
    BossHealth health;
    int frame, pattern, patterns_done, hp_displayed;
    KonngaraDirection direction;
    KonngaraExpression expression;
    KonngaraDiamond visuals[16]; // source positions BEFORE snake movement
    int visual_count;
    bool complete; // combat finished: platform plays defeat; ending handoff is separate
    int phase = 1; // original source IDs: 2 is a transition, 3 the second set
    int siddham_white = 0; // source color9: (t,max(9,t),max(10,t)), RGB4
    bool bos_head = false;
    bool ray_active = false;
    int ray_left = 0, ray_top = 0; // drawn endpoint, BEFORE emitter movement
    int slash_cel = -1, slash_cycles = 0;
    ShootoutLaser lasers[SHOOTOUT_LASER_COUNT];
};
