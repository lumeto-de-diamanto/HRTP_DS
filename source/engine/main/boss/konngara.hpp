#pragma once
#include "konngara_phase1.hpp"

// Source combat phases1..7. Phase1 patterns are reused by the final mix;
// complete marks combat's end; the platform handles defeat and future ending handoff.
class KonngaraCombat {
public:
    void reset(int start_set = 0); // 0:first, 1:phase2, 2:phase4, 3:phase6 (practice only)
    void update(bool bomb_damage = false);
    void update_lasers(); // after boss dispatch, before orb/pellet movement
    const KonngaraCombatState &state() const { return state_; }
private:
    KonngaraPhase1 first_;
    KonngaraCombatState state_{};
    int previous_ = 99, interval_ = 0;
    int diamond_x_ = 0, diamond_y_ = 0, diamond_vx_ = 0, diamond_vy_ = 0, direction_ = 99, speed_ = 0;
    int spray_offset_ = 0, spray_delta_ = 0, spray_count_ = 0;
    int end_x_ = 0, end_y_ = 0;
    int slash_x_ = 410, slash_y_ = 70;
    bool direction_locked_ = false, lasers_right_to_left_ = false;
    bool clear_bonus_awarded_ = false;
    struct Snake { int x[5]{}, y[5]{}, target = 0; bool active = false, locked = false; } snakes_[4];
    void next(int phase);
    void award_clear_bonus();
    void visual(int x, int y, int color);
    void rows();
    void spray();
    void snakes();
    void snake_spawn(int index, int x, int y);
    void snake_update(int index);
    void edge_rain();
    enum class Slash { Rain, Triangular, Aimed };
    void slash(Slash kind);
    void lasers_and_spread();
    void sleeve_rain();
};
