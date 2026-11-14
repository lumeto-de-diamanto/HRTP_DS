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
struct GuardedSubVram { u32 before=0xC0FFEE11;alignas(4) u16 pixels[128*1024/2]{};u32 after=0xC0FFEE11; };
inline GuardedSubVram test_sub_vram;
inline u16 BG_PALETTE_SUB[256]{};
inline u16 *BG_MAP_RAM(int n) { return test_vram.pixels + n * 1024; }
inline u16 *BG_TILE_RAM(int n) { return test_vram.pixels + n * 8192; }
inline u16 *BG_BMP_RAM(int n) { return test_vram.pixels + n * 8192; }
inline u16 *BG_BMP_RAM_SUB(int n) { return test_sub_vram.pixels+n*8192; }
inline size_t test_dma_fill_bytes{}, test_dma_copy_calls{};
inline size_t test_dma_copy_bytes{};
inline void (*test_dma_fill_hook)() = nullptr;
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
    if (test_dma_fill_hook) test_dma_fill_hook();
}
inline void dmaCopyHalfWords(int, const void *in, void *out, size_t size) {
    ++test_dma_copy_calls;
    test_dma_copy_bytes+=size;
    // Do not silently emulate cache coherence that ARM9 hardware lacks. The
    // This checks cache handling only: host pointers cannot identify the DS's
    // DTCM stack. Renderer checks must separately bound DMA sources/transfers;
    // flushing a DTCM buffer does NOT make it DMA-readable on hardware.
    const auto address = reinterpret_cast<uintptr_t>(in);
    assert(address >= test_dma_clean_start && address + size <= test_dma_clean_end);
    assert(!(reinterpret_cast<uintptr_t>(in) & 1) && !(reinterpret_cast<uintptr_t>(out) & 1) && !(size & 1));
    std::memcpy(out, in, size);
}
enum { MODE_5_2D, VRAM_A_MAIN_BG_0x06000000, VRAM_B_MAIN_BG_0x06020000, VRAM_C_LCD, VRAM_D_LCD, VRAM_E_MAIN_SPRITE, VRAM_D_MAIN_BG_0x06040000, VRAM_C_SUB_BG };
enum { BgType_Text4bpp, BgType_Bmp16, BgType_Bmp8, BgSize_T_512x256, BgSize_T_256x256, BgSize_B16_256x256, BgSize_B8_256x256, BgSize_B8_512x256, BgSize_B8_128x128 };
inline constexpr int DISPLAY_WIN0_ON = 1 << 13, DISPLAY_WIN1_ON = 1 << 14;
inline u32 REG_DISPCNT{};
inline u32 REG_DISPCAPCNT{};
inline constexpr u32 DCAP_ENABLE=1u<<31;
inline constexpr int DCAP_MODE_A=0,DCAP_SRC_A_COMPOSITED=0,DCAP_SIZE_256x192=3,DCAP_BANK_VRAM_D=3;
inline constexpr u32 DCAP_MODE(int n) {return (n&3u)<<29;}
inline constexpr u32 DCAP_SRC_A(int n) {return (n&1u)<<24;}
inline constexpr u32 DCAP_SIZE(int n) {return (n&3u)<<20;}
inline constexpr u32 DCAP_BANK(int n) {return (n&3u)<<16;}
inline u16 REG_VCOUNT = 192;
inline u16 REG_WIN0H{}, REG_WIN1H{}, REG_WIN0V{}, REG_WIN1V{}, REG_WININ{}, REG_WINOUT{};
inline u16 REG_BLDCNT{},REG_BLDALPHA{},REG_BLDY{};
inline u16 REG_MASTER_BRIGHT{},REG_MASTER_BRIGHT_SUB{};
inline constexpr int BLEND_ALPHA=1<<6,BLEND_SRC_BG0=1,BLEND_SRC_BG2=1<<2,
    BLEND_DST_BG1=1<<9,BLEND_DST_BG2=1<<10,BLEND_DST_BG3=1<<11,BLEND_DST_SPRITE=1<<12,BLEND_DST_BACKDROP=1<<13;
inline constexpr int BLEND_FADE_WHITE=2<<6,BLEND_SRC_BG3=1<<3,BLEND_SRC_BACKDROP=1<<5;
struct TestBg { int type{}, size{}, map{}, tile{}, x{}, y{}, priority{}; int sx = 256, sy = 256; bool visible = true,wrap=false; };
inline TestBg test_bgs[8];
inline void videoSetMode(int) {}
inline void videoSetModeSub(int) {}
inline void consoleDemoInit() { assert(false && "unexpected fatal game console"); }
inline void vramSetPrimaryBanks(int, int, int, int) {}
inline void vramSetBankE(int) {}
inline void vramSetBankC(int) {}
inline int test_vram_d_mode;
inline void vramSetBankD(int mode) { test_vram_d_mode = mode; }
inline int bgInit(int id, int type, int size, int map, int tile) {
    if(id<4) REG_DISPCNT|=1<<(8+id);
    test_bgs[id] = {type, size, map, tile}; return id;
}
inline int bgInitHidden(int id, int type, int size, int map, int tile) { bgInit(id,type,size,map,tile);test_bgs[id].visible=false;if(id<4) REG_DISPCNT&=~(1<<(8+id));return id; }
inline int bgInitSub(int id,int type,int size,int map,int tile) { return bgInit(id+4,type,size,map,tile); }
inline void bgWrapOn(int id) { test_bgs[id].wrap=true; }
inline void bgSetPriority(int id, int priority) { test_bgs[id].priority = priority; }
inline void bgSetScroll(int id, int x, int y) { test_bgs[id].x = x; test_bgs[id].y = y; }
inline void bgSetScale(int id, int sx, int sy) { test_bgs[id].sx = sx; test_bgs[id].sy = sy; }
inline void (*test_bg_update_hook)() = nullptr;
inline void bgUpdate() { if (test_bg_update_hook) test_bg_update_hook(); }
inline void bgShow(int id) { test_bgs[id].visible=true;if(id<4) REG_DISPCNT|=1<<(8+id); }
inline void bgHide(int id) { test_bgs[id].visible=false;if(id<4) REG_DISPCNT&=~(1<<(8+id)); }
inline void *bgGetGfxPtr(int id) { return id>=4 ? BG_BMP_RAM_SUB(test_bgs[id].map) : BG_BMP_RAM(test_bgs[id].map); }
#define TILE_PALETTE(n) ((n) << 12)

enum SpriteSize { SpriteSize_8x8, SpriteSize_16x16, SpriteSize_32x16, SpriteSize_32x32, SpriteSize_64x64, SpriteSize_8x16, SpriteSize_32x8, SpriteSize_64x32,
                  SpriteSize_8x32, SpriteSize_16x8, SpriteSize_16x32, SpriteSize_32x64 };
enum { SpriteColorFormat_16Color, SpriteMapping_1D_32, SpriteColorFormat_256Color };
struct TestObj { int x{}, y{}, priority{}, palette{}; SpriteSize size{}; bool hidden = true; const u16 *gfx{}; bool hflip{}; int format{}; };
inline TestObj test_objs[128];
struct OamState { TestObj *oamMemory; };
inline OamState oamMain{test_objs};
alignas(2048) inline u16 test_obj_vram[32768]{};
// SpriteMapping_1D_32: 1024 addressable 32-byte units, NOT the entire 64KiB
// physical bank. Model libnds' first-fit, block-size-aligned allocation and
// coalescing free space, but not its heap-backed allocator metadata.
inline constexpr size_t test_obj_capacity = 1024 * 32;
inline bool test_obj_busy[1024]{};
inline size_t test_obj_lengths[1024]{};
inline size_t test_obj_used{};
inline size_t test_obj_count{}, test_obj_attempts{}, test_obj_frees{};
inline int test_obj_fail_after = -1;
inline size_t test_oam_changes{};
inline void oamInit(OamState *, int mapping, bool) {
    assert(mapping == SpriteMapping_1D_32);
    std::memset(test_obj_busy, 0, sizeof(test_obj_busy));
    std::memset(test_obj_lengths, 0, sizeof(test_obj_lengths));
    test_obj_used = test_obj_count = test_obj_attempts = test_obj_frees = 0;
    test_obj_fail_after = -1;
    for (auto &obj : test_objs) obj = TestObj{};
}
inline int sprite_bytes(SpriteSize size) {
    const int bytes[] = {32, 128, 256, 512, 2048, 64, 128, 1024, 128, 64, 256, 1024}; return bytes[size];
}
inline u16 *oamAllocateGfx(OamState *, SpriteSize size, int format) {
    ++test_obj_attempts;
    if (test_obj_fail_after == 0) return nullptr;
    if (test_obj_fail_after > 0) --test_obj_fail_after;
    assert(format == SpriteColorFormat_16Color || format == SpriteColorFormat_256Color);
    const size_t bytes = sprite_bytes(size) * (format == SpriteColorFormat_256Color ? 2 : 1);
    const size_t units = bytes / 32;
    for (size_t start = 0; start + units <= 1024; start += units) {
        bool free = true;
        for (size_t i = start; i < start + units; ++i) free &= !test_obj_busy[i];
        if (!free) continue;
        for (size_t i = start; i < start + units; ++i) test_obj_busy[i] = true;
        test_obj_lengths[start] = bytes;
        test_obj_used += bytes;
        ++test_obj_count;
        return test_obj_vram + start * 16;
    }
    return nullptr;
}
inline void oamFreeGfx(OamState *, const void *gfx) {
    const auto address = reinterpret_cast<uintptr_t>(gfx);
    const auto base = reinterpret_cast<uintptr_t>(test_obj_vram);
    assert(address >= base && address < base + test_obj_capacity && (address - base) % 32 == 0);
    const size_t start = (address - base) / 32, bytes = test_obj_lengths[start];
    assert(bytes); // reject invalid/double frees
    for (size_t i = start; i < start + bytes / 32; ++i) {
        assert(test_obj_busy[i]); test_obj_busy[i] = false;
    }
    std::memset(test_obj_vram + start * 16, 0xDD, bytes);
    test_obj_lengths[start] = 0;
    test_obj_used -= bytes;
    --test_obj_count; ++test_obj_frees;
}
inline void oamSet(OamState *, int slot, int x, int y, int priority, int palette,
                   SpriteSize size, int format, const void *gfx, int, bool, bool hidden, bool hflip, bool, bool) {
    ++test_oam_changes;
    test_objs[slot] = {x, y, priority, palette, size, hidden, static_cast<const u16 *>(gfx), hflip, format};
}
inline void oamSetHidden(OamState *, int slot, bool hidden) { ++test_oam_changes; test_objs[slot].hidden = hidden; }
inline void oamSetPriority(OamState *, int slot, int priority) { ++test_oam_changes; test_objs[slot].priority = priority; }
inline void oamSetXY(OamState *, int slot, int x, int y) { ++test_oam_changes; test_objs[slot].x=x; test_objs[slot].y=y; }
inline void oamUpdate(OamState *) {}
inline void oamEnable(OamState *) {}
inline void oamDisable(OamState *) {}
inline void (*test_vblank_hook)() = nullptr;
inline size_t test_capture_count;
inline u16 (*test_capture_source)(int,int)=nullptr;
inline void swiWaitForVBlank() {
    REG_VCOUNT=192;
    if(REG_DISPCAPCNT&DCAP_ENABLE) {
        assert(test_vram_d_mode==VRAM_D_LCD && !(REG_DISPCNT&(1<<10)));
        assert(REG_DISPCAPCNT==(DCAP_ENABLE|DCAP_SIZE(DCAP_SIZE_256x192)|DCAP_BANK(DCAP_BANK_VRAM_D)));
        for(int y=0;y<192;++y) for(int x=0;x<256;++x)
            BG_BMP_RAM(16)[y*256+x]=test_capture_source ? test_capture_source(x,y) : 0x8000;
        REG_DISPCAPCNT&=~DCAP_ENABLE;++test_capture_count;
    }
    if (test_vblank_hook) test_vblank_hook();
}
inline void scanKeys() {}
inline u16 test_keys_held{}, test_keys_down{};
inline u16 keysHeld() { return test_keys_held; }
inline u16 keysDown() { return test_keys_down; }
enum { KEY_A=1, KEY_B=2, KEY_SELECT=4, KEY_START=8, KEY_RIGHT=16, KEY_LEFT=32,
       KEY_UP=64, KEY_DOWN=128, KEY_R=256, KEY_L=512, KEY_X=1024, KEY_Y=2048 };
