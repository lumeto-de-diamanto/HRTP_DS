#pragma once

// ReC98 th01/math/vector.hpp: velocity computation from angles.

void vector2(int &ret_x, int &ret_y, int length, unsigned char angle);
void vector2_between(int x1, int y1, int x2, int y2,
                     int &ret_x, int &ret_y, int length);
