#include <nds.h>
#include <cstring>
#include "boss_render.hpp"
#include "boss_gfx.hpp"
#include "main/boss/singyoku.hpp"
#include "canvas.hpp"
#include "palette_fx.hpp"
#include "sound_effects.hpp"
#include "text_layer.hpp"
#include "video.hpp"
#include "orb_render.hpp"
#include "camera.hpp"
#include "sprites/SingyokuSphere.h"
#include "sprites/SingyokuFlash.h"
#include "sprites/SingyokuPerson.h"

namespace {
constexpr int boss_slot = BOSS_OAM_SLOT, boss_palette = 3, frame_bytes = 64 * 64 / 2;
bool initialized;
int last_image = -1;
u16 palette[16];
bool last_flash;
bool entrance_fade_active;

void route_render(RouteChoice route)
{
    canvas_clear();
    canvas_text_scaled(76, 24, "ROUND 1 CLEAR", 1, RGB15(31, 31, 31));
    canvas_text_scaled(60, 48, "Congratulations!!", 1, RGB15(31, 31, 31));
    canvas_text_scaled(60, 82, "Select next Round", 1, RGB15(31, 31, 31));
    // Original route strings, retained in the compact production JIS atlas.
    static const u16 labels[2][3] = {{0x4b62, 0x3326, 0x2458}, {0x434f, 0x3976, 0x2458}};
    for (int r = 0; r < 2; ++r) {
        const u16 color = r == (int)route ? RGB15(31, 31, 31) : RGB15(18, 18, 18);
        for (int i = 0; i < 3; ++i)
            canvas_jis_glyph_scaled(96 + 16 * i, 117 + r * 25, labels[r][i], 1, color);
    }
    canvas_text_scaled(64, 117 + (int)route * 25, ">", 1, RGB15(31, 31, 31));
}
} // namespace

bool boss_render_init()
{
    if (initialized) return true;
    static_assert(sizeof(SingyokuSphere_tiles) == 8 * frame_bytes);
    static_assert(sizeof(SingyokuFlash_tiles) == 3 * frame_bytes);
    static_assert(sizeof(SingyokuPerson_tiles) == 5 * frame_bytes);
    // One replaceable frame, not all 16 cels in the limited OBJ VRAM window.
    if (!boss_gfx_init()) return false;
    boss_render_hide();
    initialized = true;
    return true;
}

void boss_render_hide()
{
    oamSetHidden(&oamMain, boss_slot, true);
    last_image = -1;
}

void boss_render_frame()
{
    const auto &s = singyoku_state();
    const int image = (int)s.sheet * 8 + s.cel;
    if (last_image < 0) {
        for (int i = 0; i < 16; ++i)
            palette[i] = BG_PALETTE[(i - 1) & 15];
        std::memcpy(SPRITE_PALETTE + boss_palette * 16, palette, sizeof(palette));
        last_flash = false;
        entrance_fade_active = s.phase == SG_ENTRANCE;
    }
    if (image != last_image) {
        const unsigned int *sheets[] = {SingyokuSphere_tiles, SingyokuFlash_tiles, SingyokuPerson_tiles};
        const auto *bytes = reinterpret_cast<const u8 *>(sheets[s.sheet]);
        std::memcpy(boss_gfx(), bytes + s.cel * frame_bytes, frame_bytes);
        last_image = image;
    }
    if (s.health.flashing != last_flash) {
        // Only the boss's requested flash color changes, never a periodic reset
        // of global palettes while an unhit boss happens to be active.
        SPRITE_PALETTE[boss_palette * 16 + 14] = s.health.flashing ? RGB15(31, 31, 31) : palette[14];
        last_flash = s.health.flashing;
    }
    // b05.cpp entrance changes just source colors 5/9/15, one RGB4 step
    // every 20 frames, then restores their full values at the end. Averaged
    // background colors retain the existing converter's quantized palette.
    if (entrance_fade_active) {
        static const int entrance_colors[] = {5, 9, 15};
        for (int raw : entrance_colors) {
            const u16 target = palette[(raw + 1) & 15];
            u16 color = target;
            if (s.phase == SG_ENTRANCE) {
                color = 0;
                for (int shift = 0; shift <= 10; shift += 5) {
                    int component = (((target >> shift) & 31) * 15 + 15) / 31;
                    if (component > s.frame / 20) component = s.frame / 20;
                    color |= ((component * 17) >> 3) << shift;
                }
            }
            BG_PALETTE[raw] = color;
        }
        if (s.phase != SG_ENTRANCE) entrance_fade_active = false;
    }
    // 96x96 source ->48x48, padded to a 64x64 OBJ: 8px left /16px top.
    const int x = camera.x(s.entity.left) - 8, y = camera.y(s.entity.top) - 16;
    oamSet(&oamMain, boss_slot, x, y,
        OAM_PRIORITY_GAMEPLAY, boss_palette, SpriteSize_64x64, SpriteColorFormat_16Color,
        boss_gfx(), -1, false, !view_visible(x, y, 64, 64), false, false, false);
}

RouteChoice boss_defeat_and_select_route()
{
    CanvasForegroundScope foreground;
    canvas_playfield_clip(false);
    // First M5 implementation of defeat.cpp: expanding white lines and
    // 15-frame palette steps, adapted to the DS viewport. The original
    // nonblocking music fade and exact transition styling remain follow-ups.
    canvas_clear();
    palette_dim(100);
    const int center = singyoku_state().entity.top + 48;
    const int first_y = PLAYFIELD_TOP - UPPER_HUD_H * 2;
    for (int distance = 0; center - distance >= first_y || center + distance < PLAYFIELD_BOTTOM; ++distance) {
        if (distance % 15 == 0)
            palette_toward_white((distance / 15) * 2);
        if (center - distance >= first_y)
            canvas_hline(0, camera.y(center - distance), VIEW_W, RGB15(31, 31, 31));
        if (center + distance < PLAYFIELD_BOTTOM)
            canvas_hline(0, camera.y(center + distance), VIEW_W, RGB15(31, 31, 31));
        swiWaitForVBlank();
    }
    palette_toward_white(31);
    boss_render_hide(); orb_render_hide(); oamUpdate(&oamMain); canvas_clear();
    for (int step = 15; step >= 0; --step) {
        palette_toward_white(step * 2);
        for (int f = 0; f < 15; ++f) swiWaitForVBlank();
    }
    palette_restore();
    palette_dim(40);
    RouteChoice route = ROUTE_MAKAI;
    route_render(route);
    // Do not accept a shot held from the fight as an unseen route confirmation.
    for (int f = 0; f < 50; ++f) { swiWaitForVBlank(); scanKeys(); }
    bool released = false;
    while (true) {
        swiWaitForVBlank(); scanKeys();
        const u16 held = keysHeld(), down = keysDown();
        if (!(held & (KEY_A | KEY_B))) released = true;
        if (down & (KEY_UP | KEY_DOWN)) {
            route = route == ROUTE_MAKAI ? ROUTE_JIGOKU : ROUTE_MAKAI;
            route_render(route); // original route navigation is silent
        }
        if (released && (down & (KEY_A | KEY_B))) {
            mdrv2_se_play(SFX_ROUTE_CONFIRM);
            break;
        }
    }
    canvas_clear(); palette_restore(); text_layer_clear();
    return route;
}
