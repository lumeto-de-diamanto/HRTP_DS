#include "player_render.hpp"
#include "main/player/player.hpp"
#include "video.hpp"
#include "camera.hpp"
#include "obj_gfx.hpp"
#include "canvas.hpp"
#include <nds.h>

#include "sprites/Miko.h"
#include "sprites/Miko32.h"
#include "sprites/Miko48.h"
#include "sprites/Shape8x8.h"

const int REGULAR_FRAME_SIZE = 16;
const int REGULAR_FRAME_COUNT = 28;

const int REGULAR_FRAME_BYTES = (REGULAR_FRAME_SIZE * REGULAR_FRAME_SIZE * 4) / 8;

const int MIKO_32_FRAME_COUNT = 6;
const int MIKO_32_FRAME_BYTES = (16 * 32 * 4) / 8;

const int MIKO_48_FRAME_COUNT = 28;
const int MIKO_48_FRAME_BYTES = (32 * 32 * 4) / 8;

u16 *g_regular_gfx[REGULAR_FRAME_COUNT];
u16 *g_32_gfx[MIKO_32_FRAME_COUNT];
u16 *g_48_gfx[MIKO_48_FRAME_COUNT];

bool player_render_init() {
    if (g_regular_gfx[0]) return true;
    ObjGfxTransaction<REGULAR_FRAME_COUNT + MIKO_32_FRAME_COUNT + MIKO_48_FRAME_COUNT> allocation;
    // Load Reimu's sprites into memory
    
    const uint8_t *regular_bytes = reinterpret_cast<const uint8_t *>(Miko_tiles);
    for (int i = 0; i < REGULAR_FRAME_COUNT; i++)
    {
        if (!allocation.allocate(g_regular_gfx[i], SpriteSize_16x16)) return false;
        memcpy(g_regular_gfx[i], regular_bytes + (i * REGULAR_FRAME_BYTES),
               REGULAR_FRAME_BYTES);
    }

    const uint8_t *miko_32_bytes = reinterpret_cast<const uint8_t *>(Miko32_tiles);
    for (int i = 0; i < MIKO_32_FRAME_COUNT; i++)
    {
        if (!allocation.allocate(g_32_gfx[i], SpriteSize_32x16)) return false;
        memcpy(g_32_gfx[i], miko_32_bytes + (i * MIKO_32_FRAME_BYTES),
               MIKO_32_FRAME_BYTES);
    }

    const uint8_t *miko_48_bytes = reinterpret_cast<const uint8_t *>(Miko48_tiles);
    for (int i = 0; i < MIKO_48_FRAME_COUNT; i++)
    {
        if (!allocation.allocate(g_48_gfx[i], SpriteSize_32x32)) return false;
        memcpy(g_48_gfx[i], miko_48_bytes + (i * MIKO_48_FRAME_BYTES),
               MIKO_48_FRAME_BYTES);
    }

    memcpy(SPRITE_PALETTE, Miko_palette, sizeof(Miko_palette));
    allocation.commit();
    return true;
}

void player_render_frame() {
    for (int i = 0; i < 5; ++i) oamSetHidden(&oamMain, OAM_SLOT_DEATH_EFFECT + i, true);
    const PlayerState &state = player_get_state();

    int oam_x, oam_y;
    SpriteSize size;
    u16 *gfx;

    // Defaults for any unexpected spritesheet value.
    oam_x = camera.x(state.x_position);
    oam_y = camera.y(state.y_position);
    size = SpriteSize_16x16;
    gfx = g_regular_gfx[state.sprite_number];

    switch (state.spritesheet) {
    case PLAYER:
        oam_x = camera.x(state.x_position);
        oam_y = camera.y(state.y_position);
        size = SpriteSize_16x16;
        gfx = g_regular_gfx[state.sprite_number];
    break;
    case PLAYER_32:
        oam_x = camera.x(state.x_position) - 8;
        oam_y = camera.y(state.y_position);
        size = SpriteSize_32x16;
        gfx = g_32_gfx[state.sprite_number];
    break;
    case PLAYER_48:
        oam_x = camera.x(state.x_position) - 8;
        oam_y = camera.y(state.y_position) - 16;
        size = SpriteSize_32x32;
        gfx = g_48_gfx[state.sprite_number];
    break;
    }

    oamSet(&oamMain, 0,
           oam_x, oam_y,
           OAM_PRIORITY_GAMEPLAY,
           0,
           size, SpriteColorFormat_16Color,
           gfx,
           -1,
           false,
           false,
           false, false,
           false);
}

void player_invincibility_render_frame()
{
    const auto &sparkles = player_invincibility_sparkles();
    if (!sparkles.active()) return;
    static const u16 colors[2] = {0, RGB15(31,0,0)};
    for (int i = 0; i < InvincibilitySparkles::count; ++i) {
        const auto &p = sparkles.particles()[i];
        canvas_put_indexed(camera.x(p.left), Camera::y(p.top), 4, 4,
                           Shape8x8_cels[3 + p.cel], colors);
    }
}

static void death_puff_put(int left, int top, int cel)
{
    const auto *tiles = reinterpret_cast<const u8 *>(Miko_tiles) + cel * REGULAR_FRAME_BYTES;
    const int x = camera.x(left), y = Camera::y(top);
    for (int row = 0; row < 16; ++row) {
        for (int col = 0; col < 16;) {
            const auto pixel = [&](int px) {
                const int offset = ((row / 8) * 2 + px / 8) * 32 + (row % 8) * 4 + (px % 8) / 2;
                return (tiles[offset] >> ((px % 2) * 4)) & 15;
            };
            const int color = pixel(col), first = col++;
            while (col < 16 && pixel(col) == color) ++col;
            if (color) canvas_hline(x + first, y + row, col - first, SPRITE_PALETTE[color]);
        }
    }
}

void player_render_death(const PlayerDeathFrame &frame)
{
    const int x = camera.x(frame.pose_left), y = Camera::y(frame.pose_top);
    oamSet(&oamMain, 0, x, y, OAM_PRIORITY_GAMEPLAY, 0,
           SpriteSize_16x16, SpriteColorFormat_16Color, g_regular_gfx[frame.pose],
           -1, false, !frame.show_pose || !view_visible(x,y,16,16), false, false, false);
    for (int i = 0; i < 5; ++i) {
        const int ex = camera.x(frame.left[i]), ey = Camera::y(frame.top[i]);
        // Source miss effects are put last, above the orb/player and pellets.
        oamSet(&oamMain, OAM_SLOT_DEATH_EFFECT + i, ex, ey, 0, 0,
               SpriteSize_16x16, SpriteColorFormat_16Color, g_regular_gfx[frame.cel],
               -1, false, i >= frame.count || !frame.visible[i] || !view_visible(ex,ey,16,16), false, false, false);
    }
    if (frame.burst_step < 0) return;
    // User-requested deviation from the source's foot-band erasure: preserve
    // whole puffs in all five trails, not only the portions above Reimu. Rebuild
    // them on BG2 after scenery/effects so frame clearing cannot erase the sides.
    for (int step = 0; step < frame.burst_step; ++step) {
        const int distance = (step + 1) * 8;
        const int diagonal = ((step + 1) * 370728) / 65536;
        const int dx[5] = {-distance, -diagonal, 0, diagonal, distance};
        const int dy[5] = {0, -diagonal, -distance, -diagonal, 0};
        for (int spoke = 0; spoke < 5; ++spoke) {
            const int left = frame.pose_left + dx[spoke];
            if (left < PLAYFIELD_LEFT || left >= PLAYFIELD_RIGHT - PLAYER_W) continue;
            death_puff_put(left,frame.pose_top + dy[spoke],20 + step % 2);
        }
    }
}
