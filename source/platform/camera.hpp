#pragma once

#include "main/playfld.hpp"

// Render-only coordinates, in DS pixels. No simulation code depends on this.
constexpr int VIEW_W = 256;
constexpr int VIEW_H = 192;
constexpr int UPPER_HUD_H = 24;
constexpr int VIEW_PLAY_H = PLAYFIELD_H / 2;
constexpr int BACKGROUND_W = PLAYFIELD_W / 2;
constexpr int VIEW_MAX_LEFT = BACKGROUND_W - VIEW_W;
static_assert(UPPER_HUD_H + VIEW_PLAY_H == VIEW_H);

// Floor, including partially visible sprites left/above the viewport. Division
// toward zero would make those sprites jump by one pixel at the screen edge.
constexpr int half_floor(int value) { return value / 2 - (value < 0 && value % 2 != 0); }
constexpr int three_fifths(int value) { return (value * 3) / 5; }
constexpr int view_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

struct Camera {
    int left = VIEW_MAX_LEFT / 2; // DS pixels, always aligned to a whole pixel
    void snap_center() { left = VIEW_MAX_LEFT / 2; }
    void follow(int player_center) {
        left = view_clamp(half_floor(player_center) - VIEW_W / 2, 0, VIEW_MAX_LEFT);
    }
    int x(int world_x) const { return half_floor(world_x) - left; }
    int background_x(bool boss) const {
        const int center = VIEW_MAX_LEFT / 2;
        return boss ? left : center + three_fifths(left - center);
    }
    static constexpr int y(int world_y) { return half_floor(world_y) - PLAYFIELD_TOP / 2 + UPPER_HUD_H; }
};
extern Camera camera;

constexpr bool view_visible(int x, int y, int width, int height) {
    return x < VIEW_W && x + width > 0 && y < VIEW_H && y + height > UPPER_HUD_H;
}

struct OrbIndicator { int direction, x, y; }; // -1 left, +1 right, 0 hidden
inline OrbIndicator orb_indicator(const Camera &view, int left, int top, int width, int height) {
    const int x = view.x(left), w = width / 2;
    const int side = x + w <= 0 ? -1 : x >= VIEW_W ? 1 : 0;
    return {side, side < 0 ? 2 : VIEW_W - 3,
        view_clamp(Camera::y(top + height / 2), UPPER_HUD_H + 5, VIEW_H - 6)};
}
