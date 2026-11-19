#pragma once
#include "yuugenmagan_render.hpp"
#include "main/boss/yuugenmagan_phase1.hpp"

// Render-only128KiB canvas lease. All simulation precedes compose(); all
// palette/OAM/scroll/DMA publication is grouped in an early-VBlank present().
class YuugenMaganCombatView {
    YuugenMaganView sprite_;
    u16 *effects_ = nullptr;
    YuugenMaganOpeningFrame visual_{};
    bool active_=false,pending_=false,flash_=false,last_flash_=false;
    bool defeating_=false,defeated_=false;
    int last_recolor_=-1;
    bool last_color15_override_=false;
    int last_color15_[3]{};
public:
    ~YuugenMaganCombatView() { end(); }
    YuugenMaganCombatView() = default;
    YuugenMaganCombatView(const YuugenMaganCombatView &) = delete;
    YuugenMaganCombatView &operator=(const YuugenMaganCombatView &) = delete;
    // Install the arena under native black, without borrowing BG2's title
    // mask storage. begin() reuses this asset and leases the combat canvas later.
    bool prepare_entrance();
    bool begin(); // caller holds native black, or has prepared the title arena
    bool compose(const YuugenMaganPhase1State &state,const MissilePool *missiles=nullptr);
    bool present(int death_shake=-1);
    bool defeat(const YuugenMaganPhase1State &state,const MissilePool &missiles);
    void end(); // caller holds black, or is restoring a failed pre-publish begin
};

bool yuugenmagan_phase1_review(bool phase3=true,bool phase5=true,bool phase9=true,bool phase12=true,bool phase13=true,bool defeat=true);
bool yuugenmagan_death_animate(YuugenMaganCombatView &view,const YuugenMaganPhase1 &boss,bool fatal);
// Exactly one native enemy glyph per active orb hitbox, independent of cel,
// renderer lock, boss hit invincibility, camera visibility, or attack phase.
void yuugenmagan_radar_render(const YuugenMaganPhase1State &state);
