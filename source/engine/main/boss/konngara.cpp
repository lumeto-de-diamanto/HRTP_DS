// ReC98 b20j.cpp: combat patterns/dispatch through1727, before blocking defeat.
#include "konngara.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/math/vector.hpp"
#include "main/random.hpp"
#include "main/stage/stageobj.hpp"
#include "sound_effects.hpp"

namespace {
int for_rank(int easy, int normal, int hard, int lunatic)
{
    const int values[] = {easy, normal, hard, lunatic};
    return values[int(rank) >= 0 && int(rank) < 4 ? int(rank) : 1];
}
const BossEntity hitbox = {288, 120, 112, 72, 8, 8, 88, 64, true};
}

void KonngaraCombat::reset(int start_set)
{
    *this = KonngaraCombat{};
    first_.reset(); state_ = first_.state();
    if (start_set > 0) {
        const int set = start_set < 3 ? start_set : 3;
        state_.health.hp = state_.hp_displayed = 18 - set * 3;
        next(set * 2); // practice only; continuous play retains HP
    }
}

void KonngaraCombat::next(int phase)
{
    state_.phase = phase; state_.frame = 0;
    state_.pattern = 99; state_.patterns_done = 0;
    state_.health.invincibility_frame = 0;
    state_.complete = false; state_.bos_head = true;
    if (phase & 1) state_.expression = KonngaraExpression::Neutral;
}

void KonngaraCombat::visual(int x, int y, int color)
{
    if (x >= 0 && x < 640 && y >= 0 && y < 400 && state_.visual_count < 16)
        state_.visuals[state_.visual_count++] = {x, y, color};
}

void KonngaraCombat::rows()
{
    const int frame = state_.frame;
    if (frame == 10) state_.expression = KonngaraExpression::Neutral;
    if (frame < 100) return;
    if (frame == 100) {
        vector2_between(290, 200, 64, 64, diamond_vx_, diamond_vy_, 16);
        diamond_x_ = 290; diamond_y_ = 200; speed_ = 48; direction_ = 99;
        interval_ = for_rank(12, 10, 8, 6);
        mdrv2_se_play(12); return;
    }
    if (direction_ == 99) {
        diamond_x_ += diamond_vx_; diamond_y_ += diamond_vy_;
        if (diamond_y_ <= 64) { diamond_x_ = 64; diamond_y_ = 64; direction_ = 0; return; }
        visual(diamond_x_, diamond_y_, 9);
    } else {
        diamond_x_ += direction_ == 0 ? 8 : direction_ == 1 ? -8 : 0;
        if (direction_ >= 2) {
            diamond_y_ += 8;
            if (++direction_ >= 10) direction_ = diamond_x_ > 320 ? 1 : 0;
        }
        if (diamond_x_ > 576) { diamond_x_ = 576; direction_ = 2; }
        else if (diamond_x_ < 64) { diamond_x_ = 64; direction_ = 2; }
        if (diamond_y_ >= 256) { state_.frame = 0; return; }
        if (direction_ < 2 && frame % interval_ == 0) {
            Pellets.add_group(diamond_x_, diamond_y_, PG_1_AIMED, speed_);
            speed_ += 2; // source Subpixel +=0.125f
        }
        visual(diamond_x_, diamond_y_, 6);
    }
}

void KonngaraCombat::spray()
{
    const int frame = state_.frame;
    if (frame == 10) state_.expression = KonngaraExpression::Closed;
    if (frame < 100) return;
    if (frame == 100) {
        spray_offset_ = 0x20; spray_delta_ = -8; spray_count_ = 0;
        interval_ = for_rank(5, 4, 3, 2);
    }
    if (frame % interval_ == 0) {
        // Deliberately source-imperfect aim from(278,184) to player TOP-LEFT.
        const int angle = iatan2(player_top - 184, player_get_state().x_position - 278);
        const int fire_angle = (angle + spray_offset_) & 255;
        spray_offset_ = (spray_offset_ + spray_delta_) & 255;
        if (++spray_count_ > 8) { spray_delta_ = -spray_delta_; spray_count_ = 0; }
        Pellets.add_single(312, 188, fire_angle, 48);
    }
    if (frame >= 700) state_.frame = 0; // final frame can still emit
}

void KonngaraCombat::snake_spawn(int index, int x, int y)
{
    auto &s = snakes_[index]; s.active = true;
    for (int j = 0; j < 5; ++j) { s.x[j] = x; s.y[j] = y; }
    // Source computes an unused 6px vector, including its random roll.
    const unsigned char angle = iatan2(player_center_y() - y, player_get_state().x_position + 12 - x)
        + (irand() % 2 == 0 ? 40 : -40);
    int vx, vy; vector2(vx, vy, 6, angle);
}

void KonngaraCombat::snake_update(int index)
{
    auto &s = snakes_[index];
    if (!s.active) return;
    for (int j = 3; j >= 1; --j) visual(s.x[j], s.y[j], 9);
    visual(s.x[0], s.y[0], 7);
    if (!s.locked) s.target = player_get_state().x_position;
    const unsigned char angle = iatan2(player_center_y() - s.y[0], s.target + 12 - s.x[0])
        + (irand() % 2 == 0 ? 40 : -40);
    int vx, vy; vector2(vx, vy, 7, angle);
    if (s.y[0] > 300) s.locked = true;
    for (int j = 4; j >= 1; --j) { s.x[j] = s.x[j - 1]; s.y[j] = s.y[j - 1]; }
    s.x[0] += vx; s.y[0] += vy;
    if (konngara_snake_hits_player(s.x[0], s.y[0], player_get_state().x_position, player_top,
        player_is_sliding(), player_is_invincible())) player_is_hit = true;
}

void KonngaraCombat::snakes()
{
    const int frame = state_.frame;
    if (frame == 10) state_.expression = KonngaraExpression::Glare;
    if (frame < 100) return;
    if (frame == 100) {
        for (auto &s : snakes_) { s.locked = false; s.active = false; }
        snake_spawn(0, 312, 188); mdrv2_se_play(12); return;
    }
    if (frame >= 400) { state_.frame = 0; return; }
    for (int i = 0; i < 4; ++i) snake_update(i);
    if (frame == 150 || frame == 200 || frame == 250) {
        const int i = (frame - 100) / 50;
        snake_spawn(i, i == 1 ? 312 : 290, i == 1 ? 188 : 200);
        mdrv2_se_play(12);
    }
}

void KonngaraCombat::edge_rain()
{
    const int frame = state_.frame;
    if (frame == 10) state_.expression = KonngaraExpression::Aim;
    if (frame < 100) return;
    if (frame == 100) { end_x_ = 0; end_y_ = 264; interval_ = for_rank(5, 3, 2, 2); }
    int dx = 0, dy = 0;
    if (frame < 125) dy = -8;
    else if (frame < 205) { if (frame == 125) { end_x_ = 0; end_y_ = 64; } dx = 8; }
    else if (frame < 230) { if (frame == 205) { end_x_ = 639; end_y_ = 64; } dy = 8; }
    else if (frame < 255) { if (frame == 230) { end_x_ = 639; end_y_ = 264; } dy = -8; }
    else if (frame < 335) { if (frame == 255) { end_x_ = 639; end_y_ = 64; } dx = -8; }
    else if (frame < 360) { if (frame == 335) { end_x_ = 0; end_y_ = 64; } dy = 8; }
    else { end_y_ -= 8; state_.frame = 0; }
    if (frame < 360) {
        state_.ray_active = end_x_ >= 0 && end_x_ < 640 && end_y_ >= 0 && end_y_ < 400;
        state_.ray_left = end_x_; state_.ray_top = end_y_;
        end_x_ += dx; end_y_ += dy;
    }
    // Deliberate source fall-through: frame360 resets the clock to0, still
    // plays SFX6 and emits two final rain pellets from(0,256), without a ray.
    if (state_.frame % 10 == 0) mdrv2_se_play(6);
    if (state_.frame % interval_ == 0) for (int i = 0; i < 2; ++i)
        Pellets.add_single(end_x_, end_y_, irand() & 127, 32, PM_GRAVITY, 1);
}

void KonngaraCombat::update(bool bomb_damage)
{
    if (state_.complete) return;
    if (state_.phase == 1) {
        first_.update(bomb_damage); state_ = first_.state();
        award_clear_bonus();
        if (state_.complete) { previous_ = first_.last_pattern(); next(2); }
        return;
    }
    state_.visual_count = 0; state_.ray_active = false;
    ++state_.frame;
    const int left = player_get_state().x_position;
    if (!direction_locked_)
        state_.direction = left < 198 ? KonngaraDirection::Left : left > 396 ? KonngaraDirection::Right : KonngaraDirection::Center;
    if (state_.phase == 2 || state_.phase == 4 || state_.phase == 6) {
        if (state_.frame == 50) state_.siddham_white = 15;
        if (state_.frame > 50 && state_.frame % 4 == 0 && state_.siddham_white > 0) --state_.siddham_white;
    } else if (state_.phase == 3) {
        if (state_.pattern == 0) rows();
        else if (state_.pattern == 1) spray();
        else if (state_.pattern == 2) snakes();
        else if (state_.pattern == 3) edge_rain();
        else if (state_.frame > 120) {
            state_.frame = 1; state_.pattern = irand() % 4;
            if (state_.pattern == previous_) state_.pattern = irand() % 4;
            previous_ = state_.pattern; ++state_.patterns_done;
        }
        if (!state_.frame) { state_.expression = KonngaraExpression::Neutral; state_.pattern = 99; }
    } else if (state_.phase == 5) {
        if (state_.pattern == 0) slash(Slash::Rain);
        else if (state_.pattern == 1) lasers_and_spread();
        else if (state_.pattern == 2) slash(Slash::Triangular);
        else if (state_.frame > 120) {
            state_.frame = 1; state_.pattern = irand() % 3;
            if (state_.pattern == previous_) state_.pattern = irand() % 2;
            previous_ = state_.pattern; ++state_.patterns_done;
        }
        if (!state_.frame) { state_.expression = KonngaraExpression::Neutral; state_.pattern = 99; }
    } else if (state_.phase == 7) {
        if (state_.pattern >= 0 && state_.pattern < 3) first_.update_pattern(state_);
        else if (state_.pattern == 3) rows();
        else if (state_.pattern == 4) spray();
        else if (state_.pattern == 5) snakes();
        else if (state_.pattern == 6) edge_rain();
        else if (state_.pattern == 7) slash(Slash::Rain);
        else if (state_.pattern == 8) lasers_and_spread();
        else if (state_.pattern == 9) slash(Slash::Triangular);
        else if (state_.pattern == 10) sleeve_rain();
        else if (state_.pattern == 11) slash(Slash::Aimed);
        else if (state_.frame > 5) {
            state_.frame = 1; state_.pattern = irand() % 12;
            if (state_.pattern == previous_) state_.pattern = irand() % 2;
            previous_ = state_.pattern; ++state_.patterns_done;
        }
        if (!state_.frame) { state_.expression = KonngaraExpression::Neutral; state_.pattern = 99; }
    }
    boss_hit_update(state_.health, hitbox, 10000, 288, 120, 112, 56, bomb_damage);
    award_clear_bonus();
    if (state_.hp_displayed > state_.health.hp) state_.hp_displayed = state_.health.hp;
    if (state_.phase == 2 || state_.phase == 4 || state_.phase == 6) {
        if (!state_.health.invincible && state_.frame > 120) next(state_.phase + 1);
    } else if (state_.phase == 3 && !state_.health.invincible &&
               (state_.patterns_done >= 9 || state_.health.hp < 13) && state_.pattern == 99) {
        next(4);
    } else if (state_.phase == 5 && !state_.health.invincible &&
               (state_.patterns_done >= 6 || state_.health.hp < 10) && state_.pattern == 99) {
        next(6);
    } else if (state_.phase == 7 && state_.health.hp <= 0) {
        state_.complete = true; // stop BEFORE the authored blocking defeat sequence
    }
}

void KonngaraCombat::award_clear_bonus()
{
    // User-approved timing change: award at the lethal hit, even if HP reaches
    // zero before phase7, never after the blocking cinematic or a file read.
    if(state_.health.hp<=0 && !clear_bonus_awarded_) {
        stage_score+=50000;
        clear_bonus_awarded_=true;
    }
}

void KonngaraCombat::slash(Slash kind)
{
    const bool triangular = kind == Slash::Triangular, aimed = kind == Slash::Aimed;
    int frame = state_.frame;
    if (frame == 10) {
        state_.direction = KonngaraDirection::Center;
        state_.expression = triangular || aimed ? KonngaraExpression::Aim : KonngaraExpression::Closed;
        direction_locked_ = true; slash_x_ = 410; slash_y_ = 70;
        interval_ = for_rank(5,3,2,1);
        speed_ = aimed ? for_rank(64,80,88,96) : for_rank(32,48,64,72);
    }
    const int keyframes[] = {50,60,100,120,140,160,170};
    for (int cel = 0; cel < 7; ++cel) if (frame == keyframes[cel]) {
        state_.slash_cel = cel;
        if (!cel) { ++state_.slash_cycles; mdrv2_se_play(8); }
    }
    if (frame > 200) {
        state_.frame = 0; state_.slash_cel = -1; direction_locked_ = false;
        return;
    }
    if (frame < 100) return;
    auto fire = [&]() {
        if (aimed) Pellets.add_group(slash_x_,slash_y_,PG_1_AIMED,speed_);
        else if (triangular) {
            Pellets.add_single(slash_x_,slash_y_,0x20,speed_);
            Pellets.add_single(slash_x_,slash_y_,0x60,speed_);
        } else for (int i = 0; i < 2; ++i)
            Pellets.add_single(slash_x_,slash_y_,irand() % 127,0,PM_GRAVITY,1);
    };
    if (frame < 140) {
        const int interval = triangular || aimed ? 3 : interval_;
        if (frame % interval == 0) {
            fire(); slash_x_ += 22 / (40 / interval); slash_y_ += 162 / (40 / interval);
        }
        return;
    }
    if (frame == 140) { slash_x_ = 432; slash_y_ = 232; }
    if (frame < 150) {
        fire(); slash_x_ -= -234 / -10; slash_y_ -= -34 / -10;
        return;
    }
    if (frame == 150) { slash_x_ = 198; slash_y_ = 198; interval_ = for_rank(3,2,1,1); }
    const int interval = triangular || aimed ? 2 : interval_;
    if (frame < 170 && frame % interval == 0) {
        fire(); slash_x_ -= -212 / (20 / interval); slash_y_ -= 128 / (20 / interval);
    }
}

void KonngaraCombat::lasers_and_spread()
{
    const int frame = state_.frame;
    if (frame == 10) state_.expression = KonngaraExpression::Aim;
    if (frame < 100) return;
    if (frame == 100) { lasers_right_to_left_ = irand() % 2; interval_ = for_rank(128,96,80,64); }
    if (frame % 10) return;
    const int offset = ((frame - 100) / 10) * interval_;
    const int target = lasers_right_to_left_ ? 640 - offset : offset;
    shootout_laser_spawn(state_.lasers[(frame / 10) % SHOOTOUT_LASER_COUNT],410,70,target,400);
    mdrv2_se_play(6);
    if ((!lasers_right_to_left_ && target >= 640) || (lasers_right_to_left_ && target <= 0)) state_.frame = 0;
    Pellets.add_group(410,70,PG_3_SPREAD_WIDE_AIMED,72);
}

void KonngaraCombat::update_lasers()
{
    if (!state_.complete) for (auto &laser : state_.lasers) shootout_laser_update(laser);
}

void KonngaraCombat::sleeve_rain()
{
    const int frame = state_.frame;
    if (frame == 10) state_.expression = KonngaraExpression::Aim;
    if (frame < 100) return;
    if (frame % 10 == 0) {
        const bool fall = frame % 20 == 10;
        for (int i = 0; i < 10; ++i)
            Pellets.add_single(290,200,(-i * (0x80 / 10)) & 255,32,
                fall ? PM_FALL_STRAIGHT_FROM_TOP_THEN_REGULAR : PM_GRAVITY,fall ? 48 : 1);
        mdrv2_se_play(7);
    }
    if (frame >= 300) state_.frame = 0; // includes the last gravity volley
}
