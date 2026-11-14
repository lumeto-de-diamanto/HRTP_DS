#pragma once
#include "boss.hpp"

enum SinGyokuPhase { SG_ENTRANCE, SG_SPHERE, SG_TRANSFORM, SG_DEFEATED };
enum SinGyokuSheet { SG_SHEET_SPHERE, SG_SHEET_FLASH, SG_SHEET_PERSON };
struct SinGyokuState {
    BossEntity entity;
    BossHealth health;
    SinGyokuPhase phase;
    SinGyokuSheet sheet;
    int frame, pattern, cel, sphere_cel;
    int hp_displayed;
};

void singyoku_init();
void singyoku_update(bool bomb_damage = false);
const SinGyokuState &singyoku_state();
