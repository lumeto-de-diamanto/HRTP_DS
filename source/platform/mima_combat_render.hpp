#pragma once
#include "mima_render.hpp"
#include "main/boss/mima_phase1.hpp"

// Shares Mima's approved art/cache and adds a temporary128KiB effects buffer.
// Software composition is separate from early-VBlank OBJ/palette presentation.
class MimaCombatView {
    MimaView sprite_;
    u16 *effects_=nullptr;
    u16 player_palette_[16]{};
    int image_=0,left_=256,top_=120,darkness_=0,last_darkness_=-1,flash_mode_=0,last_flash_mode_=-1;
    bool flash_=false,pending_=false,active_=false,defeated_=false;
public:
    ~MimaCombatView() { end(); }
    MimaCombatView()=default;
    MimaCombatView(const MimaCombatView &)=delete;
    MimaCombatView &operator=(const MimaCombatView &)=delete;
    bool begin(bool background_loaded=false); // normal Stage10 loads scenery before its title
    void end();
    bool compose(const MimaPhase1State &state,const MissilePool &missiles,const u16 *colors=nullptr);
    bool present(int death_shake=-1); // -1 uses bomb shake; miss frames supply0/4
    bool defeat(const MimaPhase1State &state);
};

// Approved101-frame spread-in. Presentation only; reset combat/RNG afterward.
bool mima_entrance_play(MimaCombatView &view);

// Blocking presentation only: reuses the approved miss/game-over animation,
// freezing boss/projectiles/orb/timer. Caller applies life/recovery resources.
bool mima_death_animate(MimaCombatView &view,const MimaPhase1 &boss,bool fatal);

// Both combat blocks. SELECT exits; START opens the normal pause menu.
// Score/resources are practice-local and restored before reloading the caller.
bool mima_phase1_review();
