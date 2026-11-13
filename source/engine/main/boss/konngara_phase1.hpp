#pragma once
#include "konngara_state.hpp"

constexpr bool konngara_snake_hits_player(int x, int y, int left, int top, bool sliding, bool invincible)
{
    return !invincible && x > left && x < left + 24 &&
        y > top + (sliding ? 10 : 0) && y < top + 24;
}
using KonngaraPhase1State = KonngaraCombatState;

// Source phase1 controller; its three patterns are also reused in phase7.
// Later phase dispatch and final defeat stay separate.
// Update once before the orb moves; misses do not reset this boss state.
class KonngaraPhase1 {
public:
    void reset();
    void update(bool bomb_damage = false);
    // Reuse the three source patterns in phase7, without advancing the clock,
    // selecting a pattern, updating health, or resetting the caller's fight.
    void update_pattern(KonngaraCombatState &state);
    const KonngaraPhase1State &state() const { return state_; }
    int last_pattern() const { return previous_; }
private:
    KonngaraPhase1State state_{};
    bool started_ = false;
    int previous_ = 99, interval_ = 0, group_ = 0, speed_ = 0, angle_ = 0;
    int diamond_x_[4]{}, diamond_y_[4]{}, bottom_vx_ = 0, bottom_vy_ = 0;
    int top_vx_ = 0, top_vy_ = 0, edge_frame_ = 0;
    struct Snake { int x[5], y[5], target; bool active, locked; } snakes_[2]{};
    void visual(int left, int top, int color);
    void diamond_cross();
    void cup_spread();
    void snakes_and_spreads();
    void snake_spawn(int index);
    void snake_update(int index);
};
