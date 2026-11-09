#pragma once

#include <nds.h>

// Main-screen video setup: Mode 5 layer stack.
//
// Main BG VRAM (A+B at 0..256 KB, D at 256..384 KB):
//   0x00000  BG1 Text4bpp tile data   (tileBase 0)   stage-object tiles
//   0x04000  BG1 512x256 tile map      (mapBase 8, two screenblocks)
//   0x08000  BG0 Text4bpp tile data   (tileBase 2)   big text (later)
//   0x0F000  BG0 tile map             (mapBase 30)
//   0x10000  BG3 Bmp8 512x256         (mapBase 4)    resident background
//   0x40000  BG2 Bmp16 256x256        (mapBase 16)   effects canvas
//
// Layer order (front to back): BG0 text, BG2 canvas, gameplay OAM, BG1
// tiles, BG3 background. BG0 and BG2 both use priority 0; the DS resolves
// equal-priority BGs by BG number, keeping BG0 text in front of BG2.
//
// (mapBase is a 2 KB offset for tile maps and a 16 KB offset for
// bitmaps; tileBase is a 16 KB offset.)

// Background layer IDs, created by video_init().
extern int g_bg_id_background; // BG3, Bmp8
extern int g_bg_id_tiles;      // BG1, Text4bpp
extern int g_bg_id_canvas;     // BG2, Bmp16

// Bullets/effects are software-blitted to BG2 and must cover every gameplay
// sprite, matching the original's put order. Priority 1 places OAM behind the
// priority-0 canvas but ahead of priority-2 stage tiles.
static const int OAM_PRIORITY_GAMEPLAY = 1;

// Overlay OAM: ahead of BG2, separate from gameplay's existing palette banks.
constexpr int OAM_SLOT_ORB_ARROW = 11;
constexpr int OAM_SLOT_HUD_LIVES = 48; // 48..59 reserved; five icons or icon+count
constexpr int OAM_SLOT_HUD_BOMBS = 60; // 60..64
constexpr int OAM_SLOT_HUD_HP = 72;    // 72..74: eight original-size points per OBJ
constexpr int OAM_HUD_HP_COUNT = 3;    // room for all original bosses' <=18 HP
constexpr int OBJ_PAL_HUD_ICONS = 5;
constexpr int OBJ_PAL_OVERLAY = 6;     // 0 transparent, 1 red, 2 white

void video_init();
void video_apply_camera();

// Blocking, non-gameplay BG2 animations may cover BG0/HUD OAM as well.
// Nestable so the stage-title sequence can invoke a shared banner safely.
void video_canvas_foreground_begin();
void video_canvas_foreground_end();
class CanvasForegroundScope {
public:
    CanvasForegroundScope() { video_canvas_foreground_begin(); }
    ~CanvasForegroundScope() { video_canvas_foreground_end(); }
    CanvasForegroundScope(const CanvasForegroundScope &) = delete;
    CanvasForegroundScope &operator=(const CanvasForegroundScope &) = delete;
};
