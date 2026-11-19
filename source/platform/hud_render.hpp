#pragma once

// Bottom-screen HUD: score, high score, combo/max, stage/rank and world radar.
// Lives/bombs/timer/HP
// belong to upper_hud.hpp. HURRY UP is drawn on the playfield canvas.

void hud_render(int lives, int stage);
void hud_invalidate(); // only when the sub-screen bitmap has been replaced
constexpr int HUD_RADAR_LEFT=88, HUD_RADAR_TOP=19, HUD_RADAR_W=160, HUD_RADAR_H=84;
struct HudRadarBoss { int x=0,y=0;bool visible=false; }; // hitbox center in world units; fixed-size icon
struct BossEntity;
HudRadarBoss hud_radar_boss(const BossEntity &entity,bool visible=true);
// Read-only world overview: no projectiles, camera outline or cosmetic RNG.
void hud_radar_render(const HudRadarBoss &boss = {});

// Blocking two-phase banner over the playfield canvas (ReC98's TRAM
// 16x-scaled letters). Used by HURRY UP and the stage-start number.
void banner_two_phase(const char *line1, const char *line2, int pause_frames);

// Blocking banner when the stage timer runs out (ReC98 harryup_animate).
void harryup_animate();

// Giant "STAGE NN" at stage start (ReC98 stage_num_animate).
// `show_title` adds ReC98's 東方★靈異伝 flourish on scene-opening stages.
// `black_banner` hides BG3 AFTER the kanji, before STAGE/NN; the caller owns
// the subsequent background reveal. Other entrances keep their scenery.
void stage_num_animate(int stage_num, bool show_title, bool black_banner=false);
