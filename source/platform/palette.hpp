#pragma once

// Shared BG palette budget for the main engine.
//
// The Bmp8 background bitmap and the tiled layers share the single
// 256-color BG palette (docs/ds-port-plan.md, landmine #1), so the
// background converter (tools/grp_to_bin.py) caps background images at
// BG_PAL_BG_COUNT colors and everything else stays out of that range.
//
//   0..207  Bmp8 background. Indices 0..15 keep the original GRP
//           colors at fixed slots so boss palette effects (e.g.
//           Sariel's recolors) keep working.
//   208..223  BG0 text layer, palette bank 13 (red ink).
//   224..239  BG0 text layer, palette bank 14 (white ink).
//   240..255  Stage-object tiles (BG1), palette bank 15.
static const int BG_PAL_BG_COUNT = 208;
static const int BG_PAL_FONT_BANK = 14;
static const int BG_PAL_STGOBJ_BANK = 15;
static const int BG_PAL_STGOBJ_BASE = BG_PAL_STGOBJ_BANK * 16;
