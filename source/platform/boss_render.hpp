#pragma once
#include "main/progression.hpp"

struct BossEntity;

bool boss_render_init();
void boss_render_hide();
void boss_render_frame();
// Uses actual source-frame bounds, not padded OAM size or orb hitbox insets.
// Uses SinGyoku's resident cache tail; hidden before activation/entrances/defeat.
void boss_indicator_render(const BossEntity &boss,bool visible=true);
RouteChoice boss_defeat_and_select_route();
