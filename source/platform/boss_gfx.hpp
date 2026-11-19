#pragma once
#include <nds.h>

// One ROM-lifetime 4KiB OBJ buffer, shared by mutually exclusive bosses.
// SinGyoku/Mima upload 4bpp; Konngara uploads 8bpp. Init is side-effect-free on reuse.
// SinGyoku's first 2KiB holds its frame; the unused tail holds its boss marker.
// Hide/invalidate the SinGyoku renderer before Konngara takes the entire cache.
// Mima's review uses3KiB for its64x64+64x32 pair and a marker at byte3072.
// Kikuri uses640 bytes: three16x16/4bpp soul cels plus two HUD-clipped variants.
// YuugenMagan uses3584 bytes: all seven padded32x32/4bpp eye cels, shared
// simultaneously by five OAM entries40..44; the final512 bytes stay untouched.
constexpr int BOSS_OAM_SLOT = 40;
bool boss_gfx_init();
u16 *boss_gfx();
