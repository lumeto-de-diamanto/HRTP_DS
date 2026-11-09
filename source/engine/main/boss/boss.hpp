#pragma once

// Portable shared boss collision/health state. Coordinates are DS world units.
struct BossEntity {
    int left, top, width, height;
    int hit_left, hit_top, hit_right, hit_bottom; // raw relative orb bounds
    bool orb_active;
    bool hits_orb(int previous_left, int previous_top) const;
};

struct BossHealth {
    int hp, total;
    int invincibility_frame;
    bool invincible;
    bool flashing;
    void reset(int points);
};

// Caller invokes once per active gameplay frame, before moving the orb. Shots
// are contacts only; actual HP damage is from the orb or a qualifying bomb.
bool boss_hit_update(BossHealth &health, const BossEntity &entity,
                     int hit_score, int shot_left, int shot_top,
                     int shot_width, int shot_height, bool bomb_damage = false);
bool boss_bomb_damage_allowed(bool active, int remaining_lives, int frame);
