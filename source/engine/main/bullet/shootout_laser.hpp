#pragma once
#include <cstdint>

constexpr int SHOOTOUT_LASER_COUNT = 10;
struct ShootoutLaser {
    int32_t start_x = 0, start_y = 0; // Q24.8, original precision
    int step_x = 0, step_y = 0, length = 0, speed = 0, age = 0, moveout_age = 0;
    int width = 5;
    bool alive = false, damaging = true;
};
// Source ignores a spawn into a live slot. Speed loses its fractional part
// after division by8, exactly as laser_s.cpp (68 ->8 pixels per frame).
bool shootout_laser_spawn(ShootoutLaser &laser, int x, int y, int target_x, int target_y,
                         int speed_times8 = 68, int moveout_age = 30, int width = 5);
void shootout_laser_update(ShootoutLaser &laser);
using LaserVisitor = void (*)(int left, int y, uint16_t dots, void *context);
// Same byte/y grouping as the source's GRCG writes, including its omitted
// final flush. left is the first point in the group; dots start at(left &~7).
void shootout_laser_visit(const ShootoutLaser &laser, LaserVisitor visitor, void *context);
