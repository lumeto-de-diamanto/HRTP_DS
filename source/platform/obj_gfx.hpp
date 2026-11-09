#pragma once
#include <nds.h>

// One-time / changed-frame 4bpp OBJ tile uploads. Always use halfword writes;
// neither these helpers nor the HUD use the software framebuffer blitter.
inline void obj4_clear(u16 *gfx, int bytes)
{
    for (int i = 0; i < bytes / 2; ++i) gfx[i] = 0;
}

inline void obj4_pixel(u16 *gfx, int width, int x, int y, unsigned color)
{
    const int word = ((y / 8) * (width / 8) + x / 8) * 16 + (y % 8) * 2 + (x % 8) / 4;
    const int shift = (x % 4) * 4;
    gfx[word] = (gfx[word] & ~(0xFu << shift)) | ((color & 15) << shift);
}
