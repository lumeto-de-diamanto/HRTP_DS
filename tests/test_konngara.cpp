// Actual portable timeline + actual DS intro renderer on host register/VRAM
// models. Pixel oracle interprets the packaged signature dictionary separately.
#include <nds.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "heap_probe.hpp"
#include "main/boss/konngara_intro.hpp"
#include "konngara_intro_render.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "music.hpp"
#include "video.hpp"

int background_scroll_x() { return 32; }
static bool playing, allow_play = true;
static int starts, stops, sfx_stops, sounds, ticks;
static std::vector<std::array<int, 6>> trace;
static std::vector<u8> asset;
bool music_play_track(NativeTrackId track) {
    assert(track == NATIVE_ALICE);
    assert(REG_WININ == 0x1D1D && *canvas_at(0, 0) == 0x8000);
    ++starts;
    playing = allow_play;
    return playing;
}
void music_stop() { ++stops; playing = false; }
void sfx_stop() { ++sfx_stops; }
void mdrv2_se_play(int id) { assert(id == 9 && playing); ++sounds; }

static std::vector<u8> read(const char *path) {
    std::ifstream input(path, std::ios::binary); assert(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
static unsigned word(size_t offset) { return asset[offset] | (asset[offset + 1] << 8); }

static u16 expected_pixel(const std::array<int, 6> &f, int x, int y) {
    const int row = (y * 2 + 16 + f[1]) % 400;
    const int page = f[4] >= 0 ? 2 : row >= f[2] ? 1 : 0;
    const size_t start = 64 + word(10) * 2;
    const unsigned id = word(start + ((page * 400 + row) * 320 + 32 + x) * 2);
    const unsigned signature = word(64 + id * 2);
    unsigned color = 0x8000;
    for (int component = 0; component < 3; ++component) {
        int sum = 0;
        for (int pixel = 0; pixel < 4; ++pixel) {
            const unsigned index = (signature >> (pixel * 4)) & 15;
            int raw = std::min<int>(asset[16 + index * 3 + component], f[3]);
            if (index == 9 && f[4] >= 0) {
                const int target[] = {0, 9, 10};
                raw = std::max(14 - f[4], target[component]);
            }
            sum += raw * 17 / 4;
        }
        color |= (sum >> 3) << (component * 5);
    }
    return color;
}

static void observe() {
    assert(playing && ++ticks <= 831);
    const auto &f = trace[std::min<int>(ticks - 1, trace.size() - 1)];
    assert(REG_WININ == 0x1D1D && test_bgs[0].priority == 1 && test_bgs[2].priority == 0);
    for (int slot : {OAM_SLOT_ORB_ARROW, OAM_SLOT_HUD_LIVES, OAM_SLOT_HUD_BOMBS, OAM_SLOT_HUD_HP})
        assert(test_objs[slot].priority == 1);
    for (int y : {0, 23, 24, 47, 96, 191})
        for (int x : {0, 17, 127, 128, 255})
            assert(*canvas_at(x, y) == expected_pixel(f, x, y));
    int expected_sounds = 0;
    for (int i = 0; i < std::min<int>(ticks, trace.size()); ++i) expected_sounds += trace[i][5];
    assert(sounds == expected_sounds);
    if (ticks == 711) {
        // Complete final picture, including transparency-composited syllables.
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 256; ++x) assert(*canvas_at(x, y) == expected_pixel(f, x, y));
    }
}

int main() {
    asset = read("nitro:/konngara_intro.bin");
    const auto raw_trace = read("trace.bin");
    assert(raw_trace.size() == 711 * 6 * 4);
    for (size_t p = 0; p < raw_trace.size(); p += 24) {
        std::array<int, 6> row;
        for (int i = 0; i < 6; ++i) {
            const u8 *v = raw_trace.data() + p + i * 4;
            row[i] = static_cast<int32_t>(u32(v[0]) | (u32(v[1]) << 8) | (u32(v[2]) << 16) | (u32(v[3]) << 24));
        }
        trace.push_back(row);
    }
    KonngaraIntro timeline;
    KonngaraIntroFrame frame;
    for (const auto &expected : trace) {
        assert(timeline.next(frame));
        assert(frame.phase == expected[0] && frame.line_on_top == expected[1] && frame.arena_from == expected[2]);
        assert(frame.ramp == expected[3] && frame.syllable_step == expected[4] && frame.shake_sound == bool(expected[5]));
    }
    assert(!timeline.next(frame) && !timeline.next(frame));

    assert(konngara_intro_asset_valid(asset.data(), asset.size()));
    for (size_t length : {size_t(0), size_t(16), size_t(63), asset.size() - 1})
        assert(!konngara_intro_asset_valid(asset.data(), length));
    assert(!konngara_intro_asset_valid(nullptr, asset.size()));
    assert(!konngara_intro_asset_valid(asset.data() + 1, asset.size() - 1));
    auto bad = asset;
    for (size_t offset : {size_t(0), size_t(4), size_t(6), size_t(8), size_t(10), size_t(12), size_t(16)}) {
        bad[offset] = 255;
        assert(!konngara_intro_asset_valid(bad.data(), bad.size()));
        bad[offset] = asset[offset];
    }
    bad[bad.size() - 1] = 255;
    assert(!konngara_intro_asset_valid(bad.data(), bad.size()));

    video_init(); canvas_init();
    camera.left = 64; // preview projection must be centered, independent of old camera
    for (int i = 0; i < 256; ++i) { BG_PALETTE[i] = i; SPRITE_PALETTE[i] = i ^ 0x7fff; }
    const std::vector<u16> resident(test_vram.pixels, test_vram.pixels + 384 * 1024 / 2);
    const std::vector<u16> bgpal(BG_PALETTE, BG_PALETTE + 256), objpal(SPRITE_PALETTE, SPRITE_PALETTE + 256);
    test_vblank_hook = observe;
    const auto obj_attempts = test_obj_attempts;
    assert(test_heap_count == 0 && test_heap_bytes == 0);
    for (int fail = 0; fail < 2; ++fail) {
        test_heap_fail_after = fail;
        assert(!konngara_intro_preview());
        assert(test_heap_count == 0 && test_heap_bytes == 0 && starts == 0);
    }
    test_heap_fail_after = -1;
    for (int repeat = 0; repeat < 20; ++repeat) {
        ticks = sounds = 0;
        assert(konngara_intro_preview());
        assert(ticks == 831 && sounds == 4);
        assert(starts == repeat + 1 && stops == repeat + 1 && sfx_stops == repeat + 1);
        assert(test_heap_count == 0 && test_heap_bytes == 0 && test_obj_attempts == obj_attempts);
        assert(REG_WININ == 0x1919 && test_bgs[0].priority == 0 && camera.left == 64);
        for (int slot : {OAM_SLOT_ORB_ARROW, OAM_SLOT_HUD_LIVES, OAM_SLOT_HUD_BOMBS, OAM_SLOT_HUD_HP})
            assert(test_objs[slot].priority == 0);
        for (int i = 0; i < 256 * 192; ++i) assert(canvas_at(0, 0)[i] == 0);
        assert(std::equal(bgpal.begin(), bgpal.end(), BG_PALETTE));
        assert(std::equal(objpal.begin(), objpal.end(), SPRITE_PALETTE));
        // Nothing outside the effects canvas may be uploaded/overwritten.
        for (size_t i = 0; i < resident.size(); ++i)
            if (i < 0x40000 / 2 || i >= 0x40000 / 2 + 256 * 192) assert(resident[i] == test_vram.pixels[i]);
    }
    {
        CanvasForegroundScope outer;
        ticks = sounds = 0;
        assert(konngara_intro_preview());
        assert(test_heap_count == 0 && test_heap_bytes == 0);
        assert(ticks == 831 && REG_WININ == 0x1D1D && test_bgs[0].priority == 1);
    }
    assert(REG_WININ == 0x1919 && test_bgs[0].priority == 0);
    test_vblank_hook = nullptr;
    allow_play = false;
    assert(!konngara_intro_preview());
    assert(test_heap_count == 0 && test_heap_bytes == 0);
    assert(REG_WININ == 0x1919 && test_bgs[0].priority == 0);
    // A failed open must not enter a foreground scope or touch audio/video.
    assert(std::rename("nitro:/konngara_intro.bin", "nitro:/konngara_intro.saved") == 0);
    const int before = starts;
    assert(!konngara_intro_preview() && starts == before);
    assert(test_heap_count == 0 && test_heap_bytes == 0);
    // Truncated and malformed files must also fail before any audio/video work.
    auto write_bad = [](const std::vector<u8> &bytes) {
        std::ofstream output("nitro:/konngara_intro.bin", std::ios::binary);
        output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    };
    write_bad({asset.begin(), asset.begin() + 15});
    assert(!konngara_intro_preview() && starts == before);
    assert(test_heap_count == 0 && test_heap_bytes == 0);
    write_bad({asset.begin(), asset.end() - 1});
    assert(!konngara_intro_preview() && starts == before);
    assert(test_heap_count == 0 && test_heap_bytes == 0);
    write_bad(bad);
    assert(!konngara_intro_preview() && starts == before);
    assert(REG_WININ == 0x1919 && test_bgs[0].priority == 0);
    assert(std::rename("nitro:/konngara_intro.saved", "nitro:/konngara_intro.bin") == 0);
    assert(test_heap_count == 0 && test_heap_bytes == 0 && test_obj_attempts == obj_attempts);
    std::puts("Konngara allocations: 20 full replays, both malloc failures, audio/open/invalid-file failures return to zero live heap allocations; zero OBJ requests");
    assert(test_vram.before == 0xDEADBEEF && test_vram.after == 0xDEADBEEF);
    std::puts("Konngara: all 711 source frames, odd-line/wrapped scroll, strip pan, four SFX9 cues, palette composition, replay/failure cleanup and fullscreen priority restoration passed");
}
