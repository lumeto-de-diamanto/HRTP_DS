#include "video.hpp"
#include "camera.hpp"
#include "background.hpp"

int g_bg_id_background;
int g_bg_id_tiles;
int g_bg_id_canvas;
static unsigned canvas_foreground_depth;
static u16 saved_window_in;

void video_init()
{
    canvas_foreground_depth = 0;
    videoSetMode(MODE_5_2D);
    // Main sprite VRAM is only available on banks A, B and E; A+B hold the
    // main BGs; D adds a separate effects canvas, E supplies 64KB OBJ VRAM.
    vramSetPrimaryBanks(VRAM_A_MAIN_BG_0x06000000, VRAM_B_MAIN_BG_0x06020000,
                        VRAM_C_LCD, VRAM_D_LCD);
    vramSetBankE(VRAM_E_MAIN_SPRITE);
    vramSetBankD(VRAM_D_MAIN_BG_0x06040000);

    g_bg_id_tiles = bgInit(1, BgType_Text4bpp, BgSize_T_512x256, 8, 0);
    g_bg_id_canvas = bgInit(2, BgType_Bmp16, BgSize_B16_256x256, 16, 0);
    g_bg_id_background = bgInitHidden(3, BgType_Bmp8, BgSize_B8_512x256, 4, 0);

    // Front to back: BG0 text (later), BG2 canvas, priority-1 gameplay OAM,
    // BG1 tiles, BG3 bitmap. BG0 wins the equal-priority BG tie over BG2.
    bgSetPriority(g_bg_id_canvas, 0);
    bgSetPriority(g_bg_id_tiles, 2);
    bgSetPriority(g_bg_id_background, 3);

    // Only BG0 text, HUD OAM and scenery belong in the upper strip. Gameplay
    // sprites use world bounds (their visible ink starts at/below y24);
    // software effects also clip below y24. BG2/BG1 are excluded here.
    // Two half-width windows avoid the empty x=0..0 interval produced by
    // encoding a 256px right edge in the hardware's 8-bit window register.
    REG_WIN0H = 128;         // [0,128)
    REG_WIN1H = 128 << 8;    // [128,256), wrapped right edge 0
    REG_WIN0V = UPPER_HUD_H; // [0,24)
    REG_WIN1V = UPPER_HUD_H;
    REG_WININ = 0x1919;     // both HUD windows: BG0, BG3, OBJ
    REG_WINOUT = 0x001F;    // gameplay: all four BGs and OBJ
    REG_DISPCNT |= DISPLAY_WIN0_ON | DISPLAY_WIN1_ON;
}

void video_apply_camera()
{
    bgSetScroll(g_bg_id_tiles, camera.left, 0);
    bgSetScroll(g_bg_id_background, background_scroll_x(), 0);
    bgUpdate();
}

void video_apply_death_shake(int down_pixels, bool konngara)
{
    bgSetScroll(g_bg_id_tiles, camera.left, -down_pixels);
    bgSetScroll(g_bg_id_background, konngara ? camera.left : background_scroll_x(), -down_pixels);
    bgSetScroll(g_bg_id_canvas, 0, -down_pixels);
    if (down_pixels) for (int slot = 0; slot < 128; ++slot) {
        if (slot == OAM_SLOT_ORB_ARROW || (slot >= 48 && slot < 75)) continue;
        const auto &entry = oamMain.oamMemory[slot];
        oamSetXY(&oamMain, slot, entry.x, entry.y + down_pixels);
    }
}

static void hud_oam_priority(int priority)
{
    oamSetPriority(&oamMain, OAM_SLOT_ORB_ARROW, priority);
    for (int slot = OAM_SLOT_HUD_LIVES; slot < OAM_SLOT_HUD_LIVES + 12; ++slot)
        oamSetPriority(&oamMain, slot, priority);
    for (int slot = OAM_SLOT_HUD_BOMBS; slot < OAM_SLOT_HUD_BOMBS + 5; ++slot)
        oamSetPriority(&oamMain, slot, priority);
    for (int slot = OAM_SLOT_HUD_HP; slot < OAM_SLOT_HUD_HP + OAM_HUD_HP_COUNT; ++slot)
        oamSetPriority(&oamMain, slot, priority);
    oamUpdate(&oamMain);
}

void video_canvas_foreground_begin()
{
    if (canvas_foreground_depth++ != 0) return;
    saved_window_in = REG_WININ;
    REG_WININ = saved_window_in | 0x0404; // admit BG2 in both upper HUD windows
    bgSetPriority(0, 1);                 // below priority-0 BG2, including ties
    hud_oam_priority(1);
}

void video_canvas_foreground_end()
{
    if (!canvas_foreground_depth || --canvas_foreground_depth != 0) return;
    REG_WININ = saved_window_in;
    bgSetPriority(0, 0);
    hud_oam_priority(0);
}
