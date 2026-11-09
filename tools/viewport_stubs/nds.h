#pragma once
// Host register/VRAM model for actual viewport renderers, not DS emulation.
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstddef>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;

inline u16 RGB15(int r, int g, int b) { return r | (g << 5) | (b << 10); }
struct GuardedVram { u32 before = 0xDEADBEEF; alignas(4) u16 pixels[384 * 1024 / 2]{}; u32 after = 0xDEADBEEF; };
inline GuardedVram test_vram;
inline u16 BG_PALETTE[256]{}, SPRITE_PALETTE[256]{};
inline u16 *BG_MAP_RAM(int n) { return test_vram.pixels + n * 1024; }
inline u16 *BG_TILE_RAM(int n) { return test_vram.pixels + n * 8192; }
inline u16 *BG_BMP_RAM(int n) { return test_vram.pixels + n * 8192; }
inline size_t test_dma_fill_bytes{}, test_dma_copy_calls{};
inline uintptr_t test_dma_clean_start{}, test_dma_clean_end{};
inline void DC_FlushRange(const void *in, size_t size) {
    test_dma_clean_start = reinterpret_cast<uintptr_t>(in);
    test_dma_clean_end = test_dma_clean_start + size;
}
inline void dmaFillHalfWords(u16 v, void *out, size_t size) {
    test_dma_fill_bytes += size;
    assert(!(reinterpret_cast<uintptr_t>(out) & 1) && !(size & 1));
    auto *p = static_cast<u16 *>(out);
    for (size_t i = 0; i < size / 2; ++i) p[i] = v;
}
inline void dmaCopyHalfWords(int, const void *in, void *out, size_t size) {
    ++test_dma_copy_calls;
    // Do not silently emulate cache coherence that ARM9 hardware lacks. The
    // previous stack-DMA background path fails this model unless flushed.
    const auto address = reinterpret_cast<uintptr_t>(in);
    assert(address >= test_dma_clean_start && address + size <= test_dma_clean_end);
    assert(!(reinterpret_cast<uintptr_t>(in) & 1) && !(reinterpret_cast<uintptr_t>(out) & 1) && !(size & 1));
    std::memcpy(out, in, size);
}
enum { MODE_5_2D, VRAM_A_MAIN_BG_0x06000000, VRAM_B_MAIN_BG_0x06020000, VRAM_C_LCD, VRAM_D_LCD, VRAM_E_MAIN_SPRITE, VRAM_D_MAIN_BG_0x06040000 };
enum { BgType_Text4bpp, BgType_Bmp16, BgType_Bmp8, BgSize_T_512x256, BgSize_T_256x256, BgSize_B16_256x256, BgSize_B8_256x256, BgSize_B8_512x256 };
inline constexpr int DISPLAY_WIN0_ON = 1 << 13, DISPLAY_WIN1_ON = 1 << 14;
inline u32 REG_DISPCNT{};
inline u16 REG_WIN0H{}, REG_WIN1H{}, REG_WIN0V{}, REG_WIN1V{}, REG_WININ{}, REG_WINOUT{};
struct TestBg { int type{}, size{}, map{}, tile{}, x{}, y{}, priority{}; };
inline TestBg test_bgs[4];
inline void videoSetMode(int) {}
inline void vramSetPrimaryBanks(int, int, int, int) {}
inline void vramSetBankE(int) {}
inline int test_vram_d_mode;
inline void vramSetBankD(int mode) { test_vram_d_mode = mode; }
inline int bgInit(int id, int type, int size, int map, int tile) {
    test_bgs[id] = {type, size, map, tile}; return id;
}
inline int bgInitHidden(int id, int type, int size, int map, int tile) { return bgInit(id, type, size, map, tile); }
inline void bgSetPriority(int id, int priority) { test_bgs[id].priority = priority; }
inline void bgSetScroll(int id, int x, int y) { test_bgs[id].x = x; test_bgs[id].y = y; }
inline void bgUpdate() {}
inline void bgShow(int) {}
inline void *bgGetGfxPtr(int id) { return BG_BMP_RAM(test_bgs[id].map); }
#define TILE_PALETTE(n) ((n) << 12)

enum SpriteSize { SpriteSize_8x8, SpriteSize_16x16, SpriteSize_32x16, SpriteSize_32x32, SpriteSize_64x64, SpriteSize_8x16, SpriteSize_32x8, SpriteSize_64x32 };
enum { SpriteColorFormat_16Color, SpriteMapping_1D_32 };
struct OamState {};
inline OamState oamMain;
struct TestObj { int x{}, y{}, priority{}, palette{}; SpriteSize size{}; bool hidden = true; const u16 *gfx{}; bool hflip{}; };
inline TestObj test_objs[128];
inline u16 test_obj_vram[32768]{};
inline size_t test_obj_used{};
inline size_t test_oam_changes{};
inline void oamInit(OamState *, int, bool) {}
inline int sprite_bytes(SpriteSize size) {
    const int bytes[] = {32, 128, 256, 512, 2048, 64, 128, 1024}; return bytes[size];
}
inline u16 *oamAllocateGfx(OamState *, SpriteSize size, int) {
    const size_t bytes = sprite_bytes(size);
    if (test_obj_used + bytes > sizeof(test_obj_vram)) return nullptr;
    auto *p = test_obj_vram + test_obj_used / 2; test_obj_used += bytes; return p;
}
inline void oamSet(OamState *, int slot, int x, int y, int priority, int palette,
                   SpriteSize size, int, const void *gfx, int, bool, bool hidden, bool hflip, bool, bool) {
    ++test_oam_changes;
    test_objs[slot] = {x, y, priority, palette, size, hidden, static_cast<const u16 *>(gfx), hflip};
}
inline void oamSetHidden(OamState *, int slot, bool hidden) { ++test_oam_changes; test_objs[slot].hidden = hidden; }
inline void oamSetPriority(OamState *, int slot, int priority) { ++test_oam_changes; test_objs[slot].priority = priority; }
inline void oamUpdate(OamState *) {}
inline void oamEnable(OamState *) {}
inline void oamDisable(OamState *) {}
inline void (*test_vblank_hook)() = nullptr;
inline void swiWaitForVBlank() { if (test_vblank_hook) test_vblank_hook(); }
inline void scanKeys() {}
inline u16 test_keys_held{}, test_keys_down{};
inline u16 keysHeld() { return test_keys_held; }
inline u16 keysDown() { return test_keys_down; }
enum { KEY_A=1, KEY_B=2, KEY_SELECT=4, KEY_START=8, KEY_RIGHT=16, KEY_LEFT=32,
       KEY_UP=64, KEY_DOWN=128, KEY_R=256, KEY_L=512, KEY_X=1024, KEY_Y=2048 };
