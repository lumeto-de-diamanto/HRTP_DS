#pragma once
#include <cstddef>
#include <nds.h>
#include "main/boss/konngara_visual.hpp"

bool konngara_art_asset_valid(const void *data, size_t size);

// Owns one source-resolution indexed page and its immutable patch asset.
// BOS/GRZ changes persist in command order, matching the original VRAM pages.
// No OBJ allocations; load is idempotent and failed loads leave it empty.
class KonngaraArtwork {
public:
    KonngaraArtwork() = default;
    ~KonngaraArtwork() { release(); }
    KonngaraArtwork(const KonngaraArtwork &) = delete;
    KonngaraArtwork &operator=(const KonngaraArtwork &) = delete;
    bool load();
    void release();
    bool reset();
    bool set_direction(KonngaraDirection direction);
    bool set_expression(KonngaraExpression expression);
    bool slash_put(int image);
    bool draw(int camera_left = 32) const; // opaque full viewport on BG2
    bool draw_to(u16 *destination, int stride, int camera_left = 32) const;
    bool prepare_hit_flash(); // optional upfront combat lookup; no per-frame allocation
    bool set_hit_flash(bool flashing);
    u16 color(unsigned index) const;
    unsigned revision() const { return revision_; }
    bool loaded() const { return data_ != nullptr; }
private:
    u8 *data_ = nullptr;
    u32 *page_ = nullptr;
    u16 *colors_ = nullptr;
    u16 *flash_colors_ = nullptr;
    bool flashing_ = false;
    unsigned revision_ = 0;
    KonngaraDirection direction_ = KonngaraDirection::Center;
    KonngaraExpression expression_ = KonngaraExpression::Neutral;
    void apply(int entry);
    void face_put();
};

// Development-only gallery, NOT stage 20/combat. Leaves the intro untouched.
bool konngara_art_preview();
