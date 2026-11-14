#pragma once

// Halfwidth8x16 BG0 timer; original-size OAM icons/HP. No software canvas ink.
// Source caps, not the larger diagnostic count range accepted by the renderer.
constexpr int UPPER_HUD_MAX_LIVES = 6;
constexpr int UPPER_HUD_MAX_BOMBS = 5;
constexpr int UPPER_HUD_MAX_BOSS_HP = 18;
enum class UpperHudMode { Regular, Boss };
bool upper_hud_init();
// After an external fullscreen OAM hide, republish cached icons/HP next draw.
void upper_hud_invalidate();
void upper_hud_render(int lives, int bombs, unsigned timer, UpperHudMode mode = UpperHudMode::Regular);
void upper_hud_boss_hide();
void upper_hud_boss(int hp, int displayed, int first_redwhite = 2, int first_white = 6);
