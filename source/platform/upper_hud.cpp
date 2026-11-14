#include "upper_hud.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "obj_gfx.hpp"
#include "text_layer.hpp"
#include "sprites/HudIcons.h"

namespace {
constexpr int regular_bombs_x = 104, boss_bombs_x = 32, boss_hp_x = 64;
static_assert(UPPER_HUD_MAX_LIVES * 16 + 8 <= regular_bombs_x, "life/bomb icon rows overlap");
static_assert(regular_bombs_x + UPPER_HUD_MAX_BOMBS * 16 <= TEXT_HUD_TIMER_X, "bomb row overlaps timer");
static_assert(boss_hp_x + UPPER_HUD_MAX_BOSS_HP * 8 <= TEXT_HUD_TIMER_X, "maximum boss HP overlaps timer");
static_assert(UPPER_HUD_MAX_BOSS_HP <= OAM_HUD_HP_COUNT * 8, "insufficient HP strips");
u16 *icons[2];
u16 *health[OAM_HUD_HP_COUNT];
int last_lives = -1, last_bombs = -1;
int last_hp = -1, last_redwhite = -1, last_white = -1;
bool health_visible;
UpperHudMode last_mode = UpperHudMode::Regular;
bool mode_valid;

void icon_row(int slot, int x, int count, int maximum, int capacity, u16 *gfx)
{
    const int visible = count > capacity ? 1 : count;
    for (int i = 0; i < maximum; ++i)
        oamSet(&oamMain, slot + i, x + (i % capacity) * 16, 0,
            0, OBJ_PAL_HUD_ICONS, SpriteSize_16x16, SpriteColorFormat_16Color,
            gfx, -1, false, i >= visible, false, false, false);
}
}

bool upper_hud_init()
{
    if (icons[0]) return true;
    ObjGfxTransaction<2 + OAM_HUD_HP_COUNT> allocation;
    for (int icon = 0; icon < 2; ++icon)
        if (!allocation.allocate(icons[icon], SpriteSize_16x16)) return false;
    for (int i = 0; i < OAM_HUD_HP_COUNT; ++i)
        if (!allocation.allocate(health[i], SpriteSize_64x32)) return false;

    // Do not publish OAM pointers or palettes until all allocations succeed.
    for (int icon = 0; icon < 2; ++icon) {
        obj4_clear(icons[icon], 128);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                obj4_pixel(icons[icon], 16, x, y, HudIcons_pixels[icon][y * 16 + x]);
    }
    for (int i = 0; i < 16; ++i)
        SPRITE_PALETTE[OBJ_PAL_HUD_ICONS * 16 + i] = HudIcons_palette[i];
    SPRITE_PALETTE[OBJ_PAL_OVERLAY * 16 + 1] = RGB15(31, 0, 0);
    SPRITE_PALETTE[OBJ_PAL_OVERLAY * 16 + 2] = RGB15(31, 31, 31);
    // Compact boss resources leave144px for all original bosses' <=18 HP,
    // plus a16px gap before the halfwidth timer.
    // Eight points per64px OBJ keeps the scanline budget comfortably small.
    for (int i = 0; i < OAM_HUD_HP_COUNT; ++i) {
        obj4_clear(health[i], 1024);
        oamSet(&oamMain, OAM_SLOT_HUD_HP + i, boss_hp_x + i * 64, 0,
            0, OBJ_PAL_OVERLAY, SpriteSize_64x32, SpriteColorFormat_16Color,
            health[i], -1, false, true, false, false, false);
    }
    last_lives = last_bombs = last_hp = -1;
    health_visible = false;
    mode_valid = false;
    allocation.commit();
    return true;
}

void upper_hud_invalidate()
{
    mode_valid=false;
    last_hp=last_redwhite=last_white=-1;
}

void upper_hud_render(int lives, int bombs, unsigned timer, UpperHudMode mode)
{
    lives = view_clamp(lives, 0, 12);
    bombs = view_clamp(bombs, 0, UPPER_HUD_MAX_BOMBS);
    if (!mode_valid || mode != last_mode) {
        text_layer_hud_resources_clear();
        upper_hud_boss_hide();
        last_lives = last_bombs = -1;
        last_mode = mode;
        mode_valid = true;
        if (mode == UpperHudMode::Boss) {
            // Icons remain visible at zero; their adjacent numbers carry the
            // actual resource state. Only a mode change needs to position them.
            icon_row(OAM_SLOT_HUD_LIVES, 0, 1, 12, 1, icons[0]);
            icon_row(OAM_SLOT_HUD_BOMBS, boss_bombs_x, 1, UPPER_HUD_MAX_BOMBS, 1, icons[1]);
        }
    }
    if (lives != last_lives) {
        if (mode == UpperHudMode::Regular) {
            icon_row(OAM_SLOT_HUD_LIVES, 0, lives, 12, UPPER_HUD_MAX_LIVES, icons[0]);
            text_layer_hud_lives_count(lives);
        } else text_layer_hud_resource_number(2, lives);
        last_lives = lives;
    }
    if (bombs != last_bombs) {
        if (mode == UpperHudMode::Regular)
            icon_row(OAM_SLOT_HUD_BOMBS, regular_bombs_x, bombs, UPPER_HUD_MAX_BOMBS, UPPER_HUD_MAX_BOMBS, icons[1]);
        else text_layer_hud_resource_number(boss_bombs_x / 8 + 2, bombs);
        last_bombs = bombs;
    }
    text_layer_hud_timer(timer);
}

void upper_hud_boss_hide()
{
    if (!health_visible) return;
    for (int i = 0; i < OAM_HUD_HP_COUNT; ++i)
        oamSetHidden(&oamMain, OAM_SLOT_HUD_HP + i, true);
    health_visible = false;
}

void upper_hud_boss(int hp, int displayed, int first_redwhite, int first_white)
{
    if (!mode_valid || last_mode != UpperHudMode::Boss) return;
    const int points = view_clamp(hp < displayed ? hp : displayed, 0, UPPER_HUD_MAX_BOSS_HP);
    if (points != last_hp || first_redwhite != last_redwhite || first_white != last_white) {
        // Source hp.cpp: 8px pitch, 7px FE mask, 15px height. Its A7 pattern
        // alternates red/white columns, not checkerboard rows.
        for (int block = 0; block < OAM_HUD_HP_COUNT; ++block)
            obj4_clear(health[block], 1024);
        for (int i = 0; i < points; ++i)
            for (int y = 0; y < 15; ++y)
                for (int x = 0; x < 7; ++x) {
                    const bool white = i >= first_white || (i >= first_redwhite && (x & 1));
                    obj4_pixel(health[i / 8], 64, (i % 8) * 8 + x, y, white ? 2 : 1);
                }
        last_hp = points;
        last_redwhite = first_redwhite;
        last_white = first_white;
    } else if (health_visible) return;
    for (int i = 0; i < OAM_HUD_HP_COUNT; ++i)
        oamSetHidden(&oamMain, OAM_SLOT_HUD_HP + i, i * 8 >= points);
    health_visible = true;
}
