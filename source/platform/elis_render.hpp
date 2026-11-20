#pragma once
#include <nds.h>
#include <cstddef>
#include "main/boss/elis_intro.hpp"
#include "main/boss/elis_phase1.hpp"

bool elis_art_asset_valid(const void *data,size_t size);
// Immutable arena/body/masks plus the existing128KiB BG2 canvas lease. No OBJ
// allocation, actor palette writes or per-frame I/O. Caller holds black while
// loading/ending, and reloads scenery afterward. Entrance may cover HUD rows.
class ElisView {
    u8 *data_=nullptr;
    u16 *effects_=nullptr;
    u16 colors_[16]{},sprite_colors_[16]{};
    unsigned revision_=~0u,body_=0,spheres_=0;
    int camera_=-1,composite_image_=-1,shake_=0;
    bool active_=false,pending_=false,combat_=false,flash_=false;
    bool defeating_=false,defeated_=false;
    int published_flash_=-1;
    u16 normal_master_=0;
    void palette(bool flash);
    void publish_palette();
public:
    ~ElisView() {end();}
    ElisView()=default;
    ElisView(const ElisView &)=delete;
    ElisView &operator=(const ElisView &)=delete;
    bool begin();
    bool prepare_entrance(); // arena only; title retains the hardware BG2 canvas
    bool compose(const ElisEntranceFrame &frame);
    // Caller begins/clips/prepares the combat canvas including actors/bullets.
    bool compose(const ElisPhase1State &state,const MissilePool &missiles);
    u16 pellet_color() const {return colors_[7];}
    bool present(int death_shake=-1); // early VBlank only; held intro uploads no rows
    bool defeat(const ElisPhase1State &state,const MissilePool &missiles);
    u16 normal_brightness() const {return normal_master_;}
    void end();
};

bool elis_entrance_play(ElisView &view);
