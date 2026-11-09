#pragma once

// Trigonometry for 256-step angles (one full turn = 256 units), matching
// master.lib's Sin8/Cos8/iatan2 exactly. See trig.cpp.

int Sin8(unsigned char angle);
int Cos8(unsigned char angle);
int iatan2(int y, int x);

// ReC98 th01/math/polar.hpp: polar_x_fast/polar_y_fast. The `>> 8` on
// negative radii rounds away from zero; preserved deliberately.
static inline int polar_x_fast(int center, int radius, unsigned char angle)
{
    return ((radius * Cos8(angle)) >> 8) + center;
}

static inline int polar_y_fast(int center, int radius, unsigned char angle)
{
    return ((radius * Sin8(angle)) >> 8) + center;
}
