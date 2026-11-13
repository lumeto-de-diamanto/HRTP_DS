#include "konngara_art_render.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "music.hpp"
#include "sound_effects.hpp"

namespace {
constexpr size_t entries = 19, page_size = 640 * 400 / 2;
constexpr size_t base_offset = 64 + entries * 8, patches_offset = base_offset + page_size;
constexpr size_t max_size = patches_offset + (3 * 800 + 9 * 320 + 7 * 32000) * 8;
unsigned le16(const u8 *p) { return p[0] | (p[1] << 8); }
u32 le32(const u8 *p) { return le16(p) | (u32(le16(p + 2)) << 16); }

void build_colors(const u8 *data, u16 *colors, bool flash)
{
    u32 pairs[256];
    for (unsigned pair = 0; pair < 256; ++pair) {
        pairs[pair] = 0;
        for (int c = 0; c < 3; ++c) {
            unsigned sum = 0;
            for (unsigned pixel = 0; pixel < 2; ++pixel) {
                const unsigned index = (pair >> (pixel * 4)) & 15;
                const unsigned component = flash && index >= 3 && index <= 5 ? 15 : data[16 + index * 3 + c];
                sum += component * 17 / 4;
            }
            pairs[pair] |= sum << (c * 8);
        }
    }
    for (unsigned signature = 0; signature < 65536; ++signature) {
        const u32 sum = pairs[signature & 255] + pairs[signature >> 8];
        colors[signature] = 0x8000 | RGB15((sum & 255) >> 3,
            ((sum >> 8) & 255) >> 3, ((sum >> 16) & 255) >> 3);
    }
}
}

bool konngara_art_asset_valid(const void *data, size_t size)
{
    if (!data || size < patches_offset || size > max_size) return false;
    const auto *bytes = static_cast<const u8 *>(data);
    if (std::memcmp(bytes, "KGA1", 4) || le16(bytes + 4) != 640 || le16(bytes + 6) != 400 ||
        le16(bytes + 8) != entries || le16(bytes + 10) || le32(bytes + 12) != size) return false;
    for (size_t i = 16; i < 64; ++i) if (bytes[i] > 15) return false;
    size_t offset = patches_offset;
    for (size_t entry = 0; entry < entries; ++entry) {
        const u8 *index = bytes + 64 + entry * 8;
        const size_t count = le32(index + 4), limit = entry < 3 ? 800 : entry < 12 ? 320 : 32000;
        if (le32(index) != offset || count > limit || count > (size - offset) / 8) return false;
        for (size_t i = 0; i < count; ++i) {
            const u8 *record = bytes + offset + i * 8;
            if (le16(record) >= 32000 || !record[2] || record[3]) return false;
        }
        offset += count * 8;
    }
    return offset == size;
}

bool KonngaraArtwork::load()
{
    if (data_) return true;
    FILE *file = std::fopen("nitro:/konngara_art.bin", "rb");
    if (!file) return false;
    u8 header[16];
    if (std::fread(header, 1, sizeof(header), file) != sizeof(header)) {
        std::fclose(file); return false;
    }
    const size_t size = le32(header + 12);
    if (size < patches_offset || size > max_size) { std::fclose(file); return false; }
    auto *data = static_cast<u8 *>(std::malloc(size));
    if (!data) { std::fclose(file); return false; }
    std::memcpy(data, header, sizeof(header));
    const bool read = std::fread(data + 16, 1, size - 16, file) == size - 16 &&
        std::fgetc(file) == EOF && !std::ferror(file);
    std::fclose(file);
    if (!read || !konngara_art_asset_valid(data, size)) { std::free(data); return false; }
    auto *page = static_cast<u32 *>(std::malloc(page_size));
    if (!page) { std::free(data); return false; }
    auto *colors = static_cast<u16 *>(std::malloc(65536 * sizeof(u16)));
    if (!colors) { std::free(page); std::free(data); return false; }
    // Horizontal source-pixel pair sums occupy separate 8-bit component lanes.
    // Adding two pairs cannot carry across lanes (maximum sum 252).
    build_colors(data, colors, false);
    data_ = data; page_ = page; colors_ = colors;
    return reset();
}

void KonngaraArtwork::release()
{
    std::free(flash_colors_); std::free(colors_); std::free(page_); std::free(data_);
    colors_ = nullptr; page_ = nullptr; data_ = nullptr;
    flash_colors_ = nullptr; flashing_ = false;
    ++revision_;
    direction_ = KonngaraDirection::Center;
    expression_ = KonngaraExpression::Neutral;
}

bool KonngaraArtwork::reset()
{
    if (!data_) return false;
    std::memcpy(page_, data_ + base_offset, page_size);
    direction_ = KonngaraDirection::Center;
    expression_ = KonngaraExpression::Neutral;
    flashing_ = false;
    ++revision_;
    return true;
}

void KonngaraArtwork::apply(int entry)
{
    ++revision_;
    const u8 *index = data_ + 64 + entry * 8;
    const u8 *patch = data_ + le32(index);
    for (unsigned i = 0; i < le32(index + 4); ++i, patch += 8) {
        u32 mask = 0;
        for (int x = 0; x < 8; ++x) if (patch[2] & (0x80 >> x)) mask |= u32(15) << (x * 4);
        u32 &word = page_[le16(patch)];
        word = (word & ~mask) | (le32(patch + 4) & mask);
    }
}

void KonngaraArtwork::face_put()
{
    if (expression_ == KonngaraExpression::Aim) apply(9 + int(direction_));
    else if (expression_ != KonngaraExpression::Neutral)
        apply(3 + (int(expression_) - 1) * 3 + int(direction_));
}

bool KonngaraArtwork::set_direction(KonngaraDirection direction)
{
    if (!data_ || unsigned(direction) >= 3) return false;
    if (direction_ == direction) return true;
    direction_ = direction;
    apply(int(direction_)); face_put();
    return true;
}

bool KonngaraArtwork::set_expression(KonngaraExpression expression)
{
    if (!data_ || unsigned(expression) >= 4) return false;
    if (expression_ == expression) return true;
    expression_ = expression;
    if (expression_ == KonngaraExpression::Neutral) apply(int(direction_));
    else face_put();
    return true;
}

bool KonngaraArtwork::slash_put(int image)
{
    if (!data_ || unsigned(image) >= 7) return false;
    apply(12 + image);
    return true;
}

bool KonngaraArtwork::draw(int camera_left) const
{
    return draw_to(canvas_at(0, 0), CANVAS_W, camera_left);
}

bool KonngaraArtwork::draw_to(u16 *destination, int stride, int camera_left) const
{
    if (!data_ || !destination || stride < VIEW_W || camera_left < 0 || camera_left > VIEW_MAX_LEFT) return false;
    const auto *page = reinterpret_cast<const u8 *>(page_);
    const auto *colors = flashing_ ? flash_colors_ : colors_;
    for (int y = 0; y < VIEW_H; ++y) {
        const u8 *above = page + (16 + y * 2) * 320 + camera_left;
        const u8 *below = above + 320;
        u16 *out = destination + y * stride;
        for (int x = 0; x < VIEW_W; ++x) out[x] = colors[above[x] | (below[x] << 8)];
    }
    return true;
}

bool KonngaraArtwork::prepare_hit_flash()
{
    if (!data_) return false;
    if (flash_colors_) return true;
    flash_colors_ = static_cast<u16 *>(std::malloc(65536 * sizeof(u16)));
    if (!flash_colors_) return false;
    build_colors(data_, flash_colors_, true);
    return true;
}

bool KonngaraArtwork::set_hit_flash(bool flashing)
{
    if (!data_ || (flashing && !flash_colors_)) return false;
    if (flashing_ != flashing) { flashing_ = flashing; ++revision_; }
    return true;
}

u16 KonngaraArtwork::color(unsigned index) const
{
    if (!data_ || index >= 16) return 0;
    if (flashing_ && index >= 3 && index <= 5) return RGB15(31, 31, 31);
    return RGB15(data_[16 + index * 3] * 17 >> 3, data_[17 + index * 3] * 17 >> 3,
                 data_[18 + index * 3] * 17 >> 3);
}

bool konngara_art_preview()
{
    KonngaraArtwork art;
    if (!art.load()) return false;
    CanvasForegroundScope foreground;
    canvas_playfield_clip(false);
    canvas_fill_rect(0, 0, VIEW_W, VIEW_H, 0);
    sfx_stop(); music_stop();
    if (!music_play_track(NATIVE_ALICE)) { canvas_clear(); return false; }
    // Gallery holds are review-only; no implied boss AI or combat dispatch.
    for (int direction = 0; direction < 3; ++direction) {
        art.set_direction(KonngaraDirection(direction));
        for (int expression = 0; expression < 4; ++expression) {
            art.set_expression(KonngaraExpression(expression));
            art.draw();
            for (int frame = 0; frame < 30; ++frame) swiWaitForVBlank();
        }
    }
    art.reset(); art.draw();
    for (int frame = 0; frame <= 200; ++frame) {
        if (frame == 10) {
            art.set_expression(KonngaraExpression::Closed);
            art.draw();
        }
        const int cel = konngara_slash_cel_on_frame(frame);
        if (cel >= 0) {
            art.slash_put(cel); art.draw();
            if (frame == 50) mdrv2_se_play(8);
        }
        swiWaitForVBlank();
    }
    for (int frame = 0; frame < 120; ++frame) swiWaitForVBlank();
    canvas_clear();
    return true;
}
