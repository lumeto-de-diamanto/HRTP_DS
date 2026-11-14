#include "video.hpp"
#include "camera.hpp"
#include "background.hpp"

int g_bg_id_background;
int g_bg_id_tiles;
int g_bg_id_canvas;
static unsigned canvas_foreground_depth;
static u16 saved_window_in;
static bool bomb_effect_owned,bomb_paused,bomb_palette_dirty,bomb_palette_inverted;
static u16 bomb_saved_control,bomb_saved_alpha,bomb_saved_y,bomb_saved_window,bomb_saved_master;
static u16 bomb_normal[208],bomb_indigo[208]; // BG3 only; BG0/BG1 use208..255
static u16 bomb_obj_normal[144],bomb_obj_indigo[144];
static bool bomb_obj_owned,bomb_obj_dirty,bomb_obj_inverted;
static int bomb_obj_brightness;
static unsigned bomb_recovery_start;
static int bomb_phase,bomb_brightness,bomb_shake_y,bomb_master;
static bool bomb_inverted;

void video_init()
{
    canvas_foreground_depth = 0;
    bomb_effect_owned=bomb_paused=false;bomb_phase=bomb_brightness=bomb_shake_y=0;
    bomb_obj_owned=false;bomb_master=0;
    REG_BLDCNT=0;REG_BLDALPHA=0;REG_BLDY=0;REG_MASTER_BRIGHT=0;
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

u16 video_bomb_indigo(u16 color)
{
    // Inverse luminance, colourised white -> source flash palette's (6,6,F).
    // This deliberately is neither a component-wise negative nor index XOR.
    const int light=(77*(color&31)+150*((color>>5)&31)+29*((color>>10)&31)+128)>>8;
    const int component=31-(19*light+15)/31;
    return RGB15(component,component,31);
}

void video_bomb_palette_changed()
{
    if(!bomb_effect_owned) return;
    for(unsigned i=0;i<208;++i) {
        bomb_normal[i]=BG_PALETTE[i];bomb_indigo[i]=video_bomb_indigo(bomb_normal[i]);
    }
    bomb_palette_dirty=true;
}

void video_bomb_obj_palette_changed()
{
    if(!bomb_obj_owned) return;
    for(unsigned i=0;i<144;++i) {
        bomb_obj_normal[i]=SPRITE_PALETTE[112+i];
        bomb_obj_indigo[i]=video_bomb_indigo(bomb_obj_normal[i]);
    }
    bomb_obj_dirty=true;
}

void video_bomb_prepare(bool active,int local_frame,unsigned gameplay_frame)
{
    bomb_shake_y=0;bomb_brightness=0;bomb_inverted=false;bomb_master=0;
    if(active) {
        // A second bomb can interrupt cosmetic recovery without extending or
        // retriggering gameplay damage. Its approved kuji intro is unmodified.
        bomb_phase=local_frame>=50 ? 1 : 0;
        bomb_inverted=bomb_phase==1;
        if(bomb_phase && ((local_frame-50)/5)%2) bomb_master=(1<<14)|5;
        if(bomb_phase && local_frame>=52) {
            static const int shake[]={4,0,8,0};
            bomb_shake_y=shake[(local_frame/4)%4];
        }
    } else {
        if(bomb_phase==1) { bomb_phase=2;bomb_recovery_start=gameplay_frame; }
        if(bomb_phase==2) {
            const unsigned age=gameplay_frame-bomb_recovery_start;
            if(age>=32) bomb_phase=0;
            else {
                bomb_inverted=age<16;
                bomb_brightness=age<=16 ? int(age) : 32-int(age);
            }
        }
    }
}

bool video_bomb_effect_active() { return bomb_phase!=0 || bomb_effect_owned; }
int video_bomb_shake() { return bomb_shake_y; }

void video_bomb_reset()
{
    if(bomb_effect_owned) {
        for(unsigned i=0;i<208;++i) BG_PALETTE[i]=bomb_normal[i];
        if(bomb_obj_owned) for(unsigned i=0;i<144;++i) SPRITE_PALETTE[112+i]=bomb_obj_normal[i];
        REG_BLDCNT=bomb_saved_control;REG_BLDALPHA=bomb_saved_alpha;
        REG_BLDY=bomb_saved_y;REG_WINOUT=bomb_saved_window;
        REG_MASTER_BRIGHT=bomb_saved_master;
    }
    bomb_effect_owned=bomb_paused=bomb_obj_owned=false;
    bomb_phase=bomb_brightness=bomb_shake_y=0;bomb_inverted=false;
}

void video_canvas_effect_commit()
{
    if(!bomb_effect_owned) return;
    // BG3's transparent index0 reveals the backdrop, so fade that too. No
    // foreground layer is a brightness target; HUD windows keep effects off.
    REG_BLDCNT=(!bomb_paused && !canvas_foreground_depth && bomb_brightness) ?
        BLEND_FADE_WHITE|BLEND_SRC_BG3|BLEND_SRC_BACKDROP : 0;
    REG_BLDY=bomb_brightness;
    // Master brightness is a final-output effect: intentionally includes HUD
    // and foreground ink. No pulse in the approved intro or cosmetic recovery.
    REG_MASTER_BRIGHT=(!bomb_paused && !canvas_foreground_depth && bomb_phase==1) ? bomb_master : bomb_saved_master;
}

void video_bomb_commit(bool konngara)
{
    if(!bomb_phase) { video_bomb_reset();return; }
    if(!bomb_effect_owned) {
        bomb_saved_control=REG_BLDCNT;bomb_saved_alpha=REG_BLDALPHA;
        bomb_saved_y=REG_BLDY;bomb_saved_window=REG_WINOUT;
        bomb_saved_master=REG_MASTER_BRIGHT;
        bomb_effect_owned=true;video_bomb_palette_changed();
    }
    if(konngara && !bomb_obj_owned) {
        bomb_obj_owned=true;video_bomb_obj_palette_changed();
    }
    if(bomb_palette_dirty || bomb_palette_inverted!=bomb_inverted) {
        const u16 *palette=bomb_inverted ? bomb_indigo : bomb_normal;
        for(unsigned i=0;i<208;++i) BG_PALETTE[i]=palette[i];
        bomb_palette_dirty=false;bomb_palette_inverted=bomb_inverted;
    }
    if(bomb_obj_owned && (bomb_obj_dirty || bomb_obj_inverted!=bomb_inverted || bomb_obj_brightness!=bomb_brightness)) {
        const u16 *palette=bomb_inverted ? bomb_obj_indigo : bomb_obj_normal;
        // Fade only the boss's private OBJ palette through white alongside BG3.
        // Selecting OBJ in BLDY would also whiten Reimu, items and the HUD.
        for(unsigned i=0;i<144;++i) {
            const u16 c=palette[i];
            const int r=c&31,g=(c>>5)&31,b=(c>>10)&31,t=bomb_brightness;
            SPRITE_PALETTE[112+i]=RGB15(r+((31-r)*t)/16,g+((31-g)*t)/16,b+((31-b)*t)/16);
        }
        bomb_obj_dirty=false;bomb_obj_inverted=bomb_inverted;bomb_obj_brightness=bomb_brightness;
    }
    REG_WINOUT=bomb_saved_window|0x20;
    video_canvas_effect_commit();
}

void video_bomb_pause(bool paused)
{
    bomb_paused=paused;
    video_canvas_effect_commit();
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
