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
constexpr int OAM_SLOT_DEATH_EFFECT = 12; // 12..16; reuse resident MIKO graphics
constexpr int OAM_SLOT_BOSS_ARROW = 17; // distinct red/purple notched boss marker
constexpr int OAM_SLOT_HUD_LIVES = 48; // 48..59 reserved; five icons or icon+count
constexpr int OAM_SLOT_HUD_BOMBS = 60; // 60..64
constexpr int OAM_SLOT_HUD_HP = 72;    // 72..74: eight original-size points per OBJ
constexpr int OAM_HUD_HP_COUNT = 3;    // room for all original bosses' <=18 HP
constexpr int OBJ_PAL_HUD_ICONS = 5;
constexpr int OBJ_PAL_OVERLAY = 6;     // 0 transparent, 1 red, 2 white, 3 purple

void video_init();
void video_apply_camera();
// BLDY is write-only. Keep its last written coefficient for effect handoffs;
// never calculate or restore an effect by reading the hardware register.
void video_set_fade(u16 amount);
u16 video_fade();
// Fade the outgoing stage/results top screen to black and retain it through scene
// loading. No pixels, palettes, simulation or bottom-screen brightness change.
void video_stage_fade_out();
// Results temporarily use neutral brightness; restore the caller's original
// gameplay brightness only when the next scene's first mask/image is ready.
void video_stage_fade_out(u16 restore_master);
// Results exit fades both screens together; retain both at black until the
// next scene's first mask/image, then restore their original brightness.
void video_stage_fade_out(u16 restore_master, u16 restore_sub_master);
// Commit a prepared first kanji mask/intro image, then release the fade in the
// same VBlank. Renderers with their own first-frame wait may reuse that VBlank.
// Without a pending fade, this only commits background settings.
void video_stage_reveal(bool already_in_vblank = false);
// Apply after rebuilding gameplay shadow OAM, before its VBlank commit. The
// fixed HUD stays put; scenery, canvas, boss and player shake together.
void video_apply_death_shake(int down_pixels, bool konngara);
// Cosmetic bomb timeline, independent of damage/protection. Preparation uses
// gameplay time (pause/repeated rendering cannot advance the recovery).
void video_bomb_prepare(bool active, int bomb_frame, unsigned gameplay_frame);
bool video_bomb_effect_active();
int video_bomb_shake();
u16 video_bomb_indigo(u16 color);
// Called after a renderer replaces BG3's normal palette, before the VBlank
// commit. Keep current boss palette animation underneath the bomb treatment.
void video_bomb_palette_changed();
// Konngara owns OBJ entries112..255. No other gameplay/HUD OBJ palette changes.
void video_bomb_obj_palette_changed();
void video_bomb_commit(bool konngara = false);
void video_bomb_reset();
void video_bomb_pause(bool paused);
void video_canvas_effect_commit();
// Blocking menus own the blend/window registers, suppressing bomb commits and
// main master brightness until exact restoration of both engines. No gameplay
// time advances here; the caller may darken the sub-engine HUD independently.
void video_modal_effect_begin();
void video_modal_effect_end();

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
