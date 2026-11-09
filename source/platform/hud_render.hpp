#pragma once

// Bottom-screen HUD: score, combo, stage and rank. Lives/bombs/timer/HP
// belong to upper_hud.hpp. HURRY UP is drawn on the playfield canvas.

void hud_render(int lives, int stage);
void hud_invalidate(); // only when the sub-screen bitmap has been replaced

// Blocking two-phase banner over the playfield canvas (ReC98's TRAM
// 16x-scaled letters). Used by HURRY UP and the stage-start number.
void banner_two_phase(const char *line1, const char *line2, int pause_frames);

// Blocking banner when the stage timer runs out (ReC98 harryup_animate).
void harryup_animate();

// Giant "STAGE NN" at stage start (ReC98 stage_num_animate).
// `show_title` adds ReC98's 東方★靈異伝 flourish on scene-opening stages.
void stage_num_animate(int stage_num, bool show_title);
