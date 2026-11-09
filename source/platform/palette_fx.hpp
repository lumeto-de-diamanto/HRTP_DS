#pragma once

// ReC98's z_palette_settone_but_keep_white(): dims the whole screen to a
// percentage while keeping pure white pixels untouched. Used by the pause
// and bonus screens.

void palette_dim(int tone); // 0..100, 40 = ReC98's pause tone
void palette_restore();
// Palette-only white blend against the most recent palette_dim() snapshot.
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
