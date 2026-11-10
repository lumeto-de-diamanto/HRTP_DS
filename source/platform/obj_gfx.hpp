#pragma once
#include <nds.h>

// Renderer graphics live for the OAM engine's lifetime. Publish a complete
// set, or return every allocation on failure so initialization can be retried.
// Do not reset/reinitialize OAM underneath these owners.
template <unsigned Capacity>
class ObjGfxTransaction {
    u16 **slots[Capacity];
    unsigned count = 0;
public:
    ObjGfxTransaction() = default;
    ObjGfxTransaction(const ObjGfxTransaction &) = delete;
    ObjGfxTransaction &operator=(const ObjGfxTransaction &) = delete;
    ~ObjGfxTransaction() {
        while (count) {
            u16 **slot = slots[--count];
            oamFreeGfx(&oamMain, *slot);
            *slot = nullptr;
        }
    }
    bool allocate(u16 *&slot, SpriteSize size) {
        if (count == Capacity) return false;
        slot = oamAllocateGfx(&oamMain, size, SpriteColorFormat_16Color);
        if (!slot) return false;
        slots[count++] = &slot;
        return true;
    }
    void commit() { count = 0; }
};

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
