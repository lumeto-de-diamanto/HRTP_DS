#include "boss.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/playfld.hpp"
#include "main/stage/stageobj.hpp"
#include "sound_effects.hpp"

bool BossEntity::hits_orb(int previous_left, int previous_top) const
{
    // ReC98 CBossEntity::hitbox_orb_set + hittest_orb: this intentionally
    // uses the prior top-left point and the original inset arithmetic.
    return orb_active &&
        previous_left >= left + hit_left + ORB_HITBOX_W - ORB_HITBOX_RIGHT &&
        previous_left <= left + hit_right - ORB_HITBOX_W - ORB_HITBOX_LEFT &&
        previous_top >= top + hit_top + ORB_HITBOX_H - ORB_HITBOX_BOTTOM &&
        previous_top <= top + hit_bottom - ORB_HITBOX_H - ORB_HITBOX_TOP;
}

void BossHealth::reset(int points)
{
    hp = total = points;
    invincibility_frame = 0;
    invincible = flashing = false;
}

bool boss_bomb_damage_allowed(bool active, int remaining_lives, int frame)
{
    return active && remaining_lives == 0 && frame % 50 == 0;
}

bool boss_hit_update(BossHealth &health, const BossEntity &entity,
                     int hit_score, int shot_left, int shot_top,
                     int shot_width, int shot_height, bool bomb_damage)
{
    if (health.invincible)
        ++health.invincibility_frame;
    if (health.hp > 0) shots_hit_boss(shot_left, shot_top, shot_width, shot_height);
    const auto &orb = orb_get_state();
    // Konngara can reach zero before phase7. Keep expiring hit grace so the
    // preceding transitions cannot deadlock, but never subtract/score again.
    const bool hit = health.hp > 0 && ((!health.invincible && entity.hits_orb(orb.x_position, orb.y_position)) || bomb_damage);
    if (hit) {
        --health.hp;
        health.invincible = true;
        health.invincibility_frame = 0;
        const auto vx = orb_get_velocity_x();
        if (vx == OVX_4_LEFT || vx == OVX_8_LEFT)
            orb_set_velocity_x(OVX_4_RIGHT);
        else if (vx == OVX_4_RIGHT || vx == OVX_8_RIGHT)
            orb_set_velocity_x(OVX_4_LEFT);
        else
            orb_set_velocity_x(orb.x_position < PLAYFIELD_CENTER_X - ORB_W / 2 ? OVX_4_RIGHT : OVX_4_LEFT);
        stage_score += hit_score;
        mdrv2_se_play(SFX_BOSS_DAMAGE);
    }
    // Preserve the source's >40 expiry/order, but never reset a global palette
    // just because an unhit boss's free-running counter reached that value.
    if (health.invincible && health.invincibility_frame > 40) {
        health.invincible = false;
        health.invincibility_frame = 0;
    }
    health.flashing = health.invincible && (health.invincibility_frame % 4 >= 2);
    return hit;
}
