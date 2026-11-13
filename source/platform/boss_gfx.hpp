#pragma once
#include <nds.h>

// One ROM-lifetime 4KiB OBJ buffer, shared by mutually exclusive bosses.
// SinGyoku uploads 4bpp; Konngara uploads 8bpp. Init is side-effect-free on reuse.
constexpr int BOSS_OAM_SLOT = 40;
bool boss_gfx_init();
u16 *boss_gfx();
