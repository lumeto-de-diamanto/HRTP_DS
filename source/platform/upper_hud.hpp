#pragma once

// Original-size BG0 timer and OAM icons/HP. All HUD drawing is BG0/OAM.
enum class UpperHudMode { Regular, Boss };
bool upper_hud_init();
// After an external fullscreen OAM hide, republish cached icons/HP next draw.
void upper_hud_invalidate();
void upper_hud_render(int lives, int bombs, unsigned timer, UpperHudMode mode = UpperHudMode::Regular);
void upper_hud_boss_hide();
void upper_hud_boss(int hp, int displayed, int first_redwhite = 2, int first_white = 6);
