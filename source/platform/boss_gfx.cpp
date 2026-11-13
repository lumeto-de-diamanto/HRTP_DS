#include "boss_gfx.hpp"

namespace { u16 *gfx; }
bool boss_gfx_init()
{
    if (gfx) return true;
    gfx = oamAllocateGfx(&oamMain, SpriteSize_64x64, SpriteColorFormat_256Color);
    return gfx != nullptr;
}
u16 *boss_gfx() { return gfx; }
