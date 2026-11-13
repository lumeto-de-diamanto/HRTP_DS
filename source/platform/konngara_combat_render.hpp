#pragma once
#include <cstddef>
#include <nds.h>
#include "main/boss/konngara_state.hpp"

bool konngara_combat_asset_valid(const void *data, size_t size);
bool konngara_slash_asset_valid(const void *data, size_t size);
bool konngara_defeat_asset_valid(const void *data, size_t size);
struct KonngaraRenderWork {
    unsigned background_bytes = 0, head_uploads = 0, palette_writes = 0, slash_uploads = 0, sprite_bytes = 0;
};

// Exact hybrid: stable BG palette, one sparse post-page patch, cached slash
// objects. frame() prepares shadow OAM/RAM effects; present() commits in VBlank.
class KonngaraCombatView {
public:
    KonngaraCombatView() = default;
    ~KonngaraCombatView() { end(); }
    KonngaraCombatView(const KonngaraCombatView &) = delete;
    KonngaraCombatView &operator=(const KonngaraCombatView &) = delete;
    bool begin();
    bool frame(const KonngaraCombatState &state);
    bool present(); // call after canvas_buffer_prepare(), at the start of VBlank
    bool defeat(bool hit_flash = true); // blocking, once; leaves a black result screen
    void end();
    const KonngaraRenderWork &work() const { return work_; }
private:
    u8 *data_ = nullptr;
    u8 *slash_data_ = nullptr;
    u16 *background_backup_ = nullptr;
    u16 *extra_gfx_ = nullptr;
    u16 bg_palette_[256]{}, obj_palette_[144]{};
    bool active_ = false, defeated_ = false, initial_head_ = true, last_flash_ = false;
    KonngaraDirection direction_ = KonngaraDirection::Center;
    KonngaraExpression expression_ = KonngaraExpression::Neutral;
    int last_pose_ = -1, last_siddham_ = -1;
    int last_slash_ = -1, shadow_objects_ = 0;
    bool post_slash_ = false, last_post_ = false, pending_ = false;
    int pending_slash_ = -1, pending_pose_ = 0, pending_shade_ = 0;
    bool pending_post_ = false, pending_flash_ = false;
    KonngaraRenderWork work_;
};

// Internal review helper retained for focused host checks. It has no in-ROM
// keyboard shortcut; normal play uses the Stage20 encounter in main.cpp.
bool konngara_combat_review(int start_set = 0);
