#include <nds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "konngara_intro_render.hpp"
#include "main/boss/konngara_intro.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "music.hpp"
#include "sound_effects.hpp"
#include "video.hpp"

namespace {
constexpr int width = 320, height = 400, page_pixels = width * height;
constexpr size_t fixed_size = 16 + 48;
unsigned le16(const u8 *p) { return p[0] | (p[1] << 8); }
unsigned le32(const u8 *p) { return le16(p) | (le16(p + 2) << 16); }

class Artwork {
public:
    const u8 *palette;
    const u16 *signatures, *pages;
    int count;
    explicit Artwork(const u8 *data) : palette(data + 16),
        signatures(reinterpret_cast<const u16 *>(data + fixed_size)),
        pages(signatures + le16(data + 10)), count(le16(data + 10)) {}

    void colors(u16 *out, int ramp, int step) const {
        int components[16][3];
        for (int color = 0; color < 16; ++color)
            for (int c = 0; c < 3; ++c) {
                int value = palette[color * 3 + c];
                if (value > ramp) value = ramp;
                if (step >= 0 && color == 9) {
                    const int targets[] = {0, 9, 10};
                    value = 14 - step; // first decrement precedes first 10-frame hold
                    if (value < targets[c]) value = targets[c];
                }
                // Same channel averaging as the existing background converter,
                // but after the original palette effect rather than before it.
                components[color][c] = value * 17 / 4;
            }
        for (int i = 0; i < count; ++i) {
            const unsigned signature = signatures[i];
            int sums[3] = {};
            for (int pixel = 0; pixel < 4; ++pixel)
                for (int c = 0; c < 3; ++c)
                    sums[c] += components[(signature >> (pixel * 4)) & 15][c];
            out[i] = 0x8000 | RGB15(sums[0] >> 3, sums[1] >> 3, sums[2] >> 3);
        }
    }

    void draw(const KonngaraIntroFrame &frame, const u16 *colors) const {
        for (int y = 0; y < VIEW_H; ++y) {
            // Same centered 50% world projection as gameplay, including odd
            // source scroll lines and wrap across source row399 -> row0.
            const int row = (16 + y * 2 + frame.line_on_top) % height;
            const int page = frame.syllable_step >= 0 ? 2 : row >= frame.arena_from ? 1 : 0;
            const u16 *src = pages + page * page_pixels + row * width + VIEW_MAX_LEFT / 2;
            u16 *dst = canvas_at(0, y);
            // CPU halfword writes: no cache-unsafe DMA or byte stores to VRAM.
            for (int x = 0; x < VIEW_W; ++x) dst[x] = colors[src[x]];
        }
    }
};
}

bool konngara_intro_asset_valid(const void *data, size_t size)
{
    if (!data || reinterpret_cast<uintptr_t>(data) % 2 || size < fixed_size) return false;
    const auto *bytes = static_cast<const u8 *>(data);
    if (std::memcmp(bytes, "KGI1", 4) || le16(bytes + 4) != width ||
        le16(bytes + 6) != height || le16(bytes + 8) != 3 || le32(bytes + 12) != size) return false;
    const unsigned count = le16(bytes + 10);
    if (!count || count > 3876 || size != fixed_size + count * 2 + page_pixels * 3 * 2) return false;
    for (size_t i = 16; i < fixed_size; ++i) if (bytes[i] > 15) return false;
    const Artwork art(bytes);
    for (int i = 0; i < page_pixels * 3; ++i) if (art.pages[i] >= count) return false;
    return true;
}

bool konngara_intro_preview()
{
    // Bound the read/allocation even for a damaged input file. Do not disturb
    // the current screen or song until all data and allocation checks pass.
    FILE *input = std::fopen("nitro:/konngara_intro.bin", "rb");
    if (!input) return false;
    u8 header[16];
    if (std::fread(header, 1, sizeof(header), input) != sizeof(header)) {
        std::fclose(input); return false;
    }
    const size_t size = le32(header + 12);
    if (size < fixed_size || size > fixed_size + 3876 * 2 + page_pixels * 3 * 2) {
        std::fclose(input); return false;
    }
    auto *data = static_cast<u8 *>(std::malloc(size));
    if (!data) { std::fclose(input); return false; }
    std::memcpy(data, header, sizeof(header));
    const bool loaded = std::fread(data + sizeof(header), 1, size - sizeof(header), input) == size - sizeof(header) &&
        std::fgetc(input) == EOF && !std::ferror(input);
    std::fclose(input);
    if (!loaded || !konngara_intro_asset_valid(data, size)) { std::free(data); return false; }
    const Artwork art(data);
    auto *colors = static_cast<u16 *>(std::malloc(art.count * sizeof(u16)));
    if (!colors) { std::free(data); return false; }
    {
        CanvasForegroundScope foreground;
        canvas_playfield_clip(false);
        canvas_fill_rect(0, 0, VIEW_W, VIEW_H, 0);
        sfx_stop();
        music_stop();
        const bool playing = music_play_track(NATIVE_ALICE);
        if (!playing) {
            canvas_clear(); std::free(colors); std::free(data); return false;
        }
        KonngaraIntro timeline;
        KonngaraIntroFrame frame{}, previous{KI_DONE, -1, -1, -1, -2, false};
        while (timeline.next(frame)) {
            if (frame.ramp != previous.ramp || frame.syllable_step != previous.syllable_step)
                art.colors(colors, frame.ramp, frame.syllable_step);
            if (frame.line_on_top != previous.line_on_top || frame.arena_from != previous.arena_from ||
                frame.ramp != previous.ramp || frame.syllable_step != previous.syllable_step)
                art.draw(frame, colors);
            if (frame.shake_sound) mdrv2_se_play(9);
            previous = frame;
            swiWaitForVBlank();
        }
        // Preview-only review hold; NOT part of the source entrance timeline.
        for (int f = 0; f < 120; ++f) swiWaitForVBlank();
        canvas_clear();
    }
    std::free(colors);
    std::free(data);
    return true;
}
