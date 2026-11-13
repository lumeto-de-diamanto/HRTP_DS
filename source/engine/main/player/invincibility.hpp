#pragma once
#include <stdint.h>

// ReC98 main/player/inv_spr.cpp: eight stationary particles, randomly seeded
// around Reimu, with the 0,1,2,3,3,2,1,0 cel cycle and staggered respawns.
// Cosmetic RNG is deliberately private: sparkle draws must not change boss,
// orb or item randomness, and rendering itself never advances this timeline.
class InvincibilitySparkles {
public:
    static constexpr int count = 8;
    struct Particle { int left = 0, top = 0, frame = 0, cel = 0; };
private:
    Particle particles_[count]{};
    uint32_t random_ = 0x9e3779b9u;
    bool active_ = false;
    int roll(int bound) {
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        return int(random_ % uint32_t(bound));
    }
public:
    void reset() {
        active_ = false;
        for (auto &p : particles_) p = {};
    }
    void update(bool invincible, int player_left, int player_top) {
        if (!invincible) { reset(); return; }
        active_ = true;
        for (auto &p : particles_) {
            if (!p.frame || p.frame == 9) {
                p.left = player_left - 8 + roll(48);
                p.top = player_top - 16 + roll(48);
                p.frame = 1 + roll(7);
            }
            p.cel = p.frame <= 4 ? p.frame - 1 : 8 - p.frame;
            ++p.frame;
        }
    }
    bool active() const { return active_; }
    const Particle *particles() const { return particles_; }
};
