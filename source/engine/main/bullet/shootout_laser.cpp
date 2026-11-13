#include "shootout_laser.hpp"
#include "main/math/trig.hpp"
#include "main/player/player.hpp"

bool shootout_laser_spawn(ShootoutLaser &s, int x, int y, int tx, int ty, int speed, int moveout, int width)
{
    if (s.alive || speed < 8 || moveout < 0 || width < 1 || width > 8) return false;
    const unsigned char a = iatan2(ty - y, tx - x);
    s = {}; s.start_x = x * 256; s.start_y = y * 256;
    s.step_x = Cos8(a); s.step_y = Sin8(a); s.speed = speed / 8;
    s.moveout_age = moveout; s.width = width; s.alive = s.damaging = true;
    return true;
}

void shootout_laser_visit(const ShootoutLaser &s, LaserVisitor visit, void *context)
{
    if (!s.alive || !visit) return;
    int32_t x = s.start_x, y = s.start_y;
    int left = x >> 8, top = y >> 8;
    uint16_t dots = 0;
    for (int i = 0; i < s.length; ++i) {
        if ((left >> 3) != (x >> 11) || top != (y >> 8)) {
            visit(left, top, dots, context);
            dots = 0; left = x >> 8; top = y >> 8;
            if (left > 638 || left < 0 || top < 64 || top > 399) break;
        }
        dots |= uint16_t(((0xffffu << (16 - s.width)) & 0xffffu) >> ((x >> 8) & 7));
        x += s.step_x; y += s.step_y;
    }
}

namespace {
void hit(int left, int y, uint16_t, void *)
{
    const int player_left = player_get_state().x_position;
    if (y > player_top + 8 && player_left + 4 <= left && player_left + 24 > left) player_is_hit = true;
}
}
void shootout_laser_update(ShootoutLaser &s)
{
    if (!s.alive) return;
    if (s.age >= s.moveout_age) {
        // The source's unput iterator can clip before all speed steps. Preserve
        // that advance order instead of extending/retracting by screen pixels.
        int left = s.start_x >> 8, top = s.start_y >> 8;
        for (int i = 0; i < s.speed; ++i) {
            if ((left >> 3) != (s.start_x >> 11) || top != (s.start_y >> 8)) {
                left = s.start_x >> 8; top = s.start_y >> 8;
                if (left > 638 || left < 0 || top < 64 || top > 399) break;
            }
            s.start_x += s.step_x; s.start_y += s.step_y;
        }
        if (s.start_x >= 640 * 256 || s.start_x < 0 || s.start_y < 64 * 256 || s.start_y >= 400 * 256) {
            s.alive = false; return;
        }
    } else s.length += s.speed;
    ++s.age;
    if (s.damaging && !player_is_invincible()) shootout_laser_visit(s, hit, nullptr);
}
