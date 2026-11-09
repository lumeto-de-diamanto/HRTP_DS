#pragma once

// All 320 halved source columns are uploaded once to a 512x256 BG3 bitmap.
// Camera movement only changes hardware scroll registers; no per-frame copy.
// Each generated palette keeps the original 16 GRP colors at indices
// 0..15 -- the round palette that the stage tiles and items render
// through (palette_fx.cpp).

void load_background(int scene); // 0..6
void load_boss_background(); // current M5 boss arena: SinGyoku / BOSS1.GRP
int background_scroll_x(); // regular scenery: center-anchored 1/2 speed; bosses: 1:1
