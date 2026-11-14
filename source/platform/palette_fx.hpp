#pragma once

// Native scene/OBJ darkening (nearest BLDY step), including both HUDs.
// BG0 menu text stays bright; its shared HUD layer uses three private ink
// adjustments. The sub-engine HUD uses hardware master brightness.
// Unlike palette scaling, white gameplay pixels darken with their layer.
// Only a cleared foreground-only BG2 (route labels) may stay bright too.
void palette_dim(int tone, bool keep_canvas_bright = false); // 0..100; pause=40
void palette_restore();
// Separate palette-only snapshot for SinGyoku's approved defeat transition.
void palette_white_begin();
// Palette-only white blend against the most recent palette_white_begin().
// Direct-color white-line/canvas animation is owned by the caller.
void palette_toward_white(int amount); // 0=original,31=white

// Applies the current round palette (BG_PALETTE[0..15], preserved from
// the stage's GRP by grp_to_bin.py) to the stage-object tiles and the
// item sprites. This is how the original works: PTN colors are raw
// indices into the hardware palette (ptn.hpp: "retaining the current
// hardware palette"), so cards/items recolor with every round. The point
// orb is pinned to round 1's palette per the project head's request.
// Call after load_background().
void palette_apply_round();
