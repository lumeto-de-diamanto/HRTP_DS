// ReC98 b20j.cpp patterns560..825 and phase1 dispatch1599..1628.
#include "konngara_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/math/vector.hpp"
#include "main/random.hpp"
#include "sound_effects.hpp"

namespace {
int for_rank(int easy, int normal, int hard, int lunatic)
{
    const int values[] = {easy, normal, hard, lunatic};
    return values[int(rank) >= 0 && int(rank) < 4 ? int(rank) : 1];
}
// Adapt the shared entity's inset arithmetic to the source inclusive orb
// top-left rectangle x288..352/y120..160. Shot contact has a separate box.
const BossEntity hitbox = {288, 120, 112, 72, 8, 8, 88, 64, true};
}

void KonngaraPhase1::reset()
{
    *this = KonngaraPhase1{};
    state_.health.reset(18);
    state_.pattern = previous_ = 99;
    state_.direction = KonngaraDirection::Center;
    state_.expression = KonngaraExpression::Neutral;
}

void KonngaraPhase1::visual(int left, int top, int color)
{
    // Source shape8x8_put rejects these origins, independent of the DS camera.
    if (left < 0 || left >= 640 || top < 0 || top >= 400) return;
    if (state_.visual_count < 12)
        state_.visuals[state_.visual_count++] = {left, top, color};
}

void KonngaraPhase1::diamond_cross()
{
    const int frame = state_.frame;
    if (frame == 10) state_.expression = KonngaraExpression::Neutral;
    if (frame < 100) return;
    if (frame == 100) {
        group_ = for_rank(PG_2_SPREAD_NARROW_AIMED, PG_3_SPREAD_NARROW_AIMED,
                         PG_5_SPREAD_WIDE_AIMED, PG_5_SPREAD_NARROW_AIMED);
        vector2_between(316, 236, 0, player_center_y(), bottom_vx_, bottom_vy_, 7);
        vector2_between(316, 236, 0, 64, top_vx_, top_vy_, 7);
        for (int i = 0; i < 4; ++i) { diamond_x_[i] = 316; diamond_y_[i] = 236; }
        Pellets.add_group(312, 188, pellet_group_t(group_), 48);
        interval_ = for_rank(18, 16, 14, 12);
        mdrv2_se_play(12);
    } else if (diamond_x_[0] > 0) {
        diamond_x_[0] += bottom_vx_; diamond_y_[0] += bottom_vy_;
        diamond_x_[1] -= bottom_vx_; diamond_y_[1] += bottom_vy_;
        diamond_x_[2] += top_vx_; diamond_y_[2] += top_vy_;
        diamond_x_[3] -= top_vx_; diamond_y_[3] += top_vy_;
        if (diamond_x_[0] <= 0) {
            diamond_x_[0] = diamond_x_[2] = 0;
            diamond_x_[1] = diamond_x_[3] = 632;
        } else for (int i = 0; i < 4; ++i) visual(diamond_x_[i], diamond_y_[i], 9);
        return;
    } else if (diamond_y_[0] > 64) {
        diamond_y_[0] -= 3; diamond_y_[1] -= 3;
        diamond_x_[2] += 6; diamond_x_[3] -= 6;
        // The source only clamps diamond0's Y, not its symmetrical peer.
        if (diamond_y_[0] <= 64) diamond_y_[0] = 64;
        else for (int i = 0; i < 4; ++i) visual(diamond_x_[i], diamond_y_[i], 9);
        return;
    } else if (edge_frame_ < 200) {
        if (++edge_frame_ % interval_ == 0) {
            int y = 64 + irand() % 200, target = 240 + irand() % 400;
            Pellets.add_single(0, y, iatan2(400 - y, target), 40);
            y = 64 + irand() % 200; target = irand() % 400;
            Pellets.add_single(632, y, iatan2(400 - y, target - 632), 40);
            for (int i = 0; i < 2; ++i) {
                const int x = irand() % 640, to = irand() % 640;
                Pellets.add_single(x, 64, iatan2(336, to - x), 40);
            }
            const int x = irand() % 640;
            Pellets.add_group(x, 64, PG_1_AIMED, 40);
        }
        return;
    } else state_.frame = 0;
    edge_frame_ = 0;
}

void KonngaraPhase1::cup_spread()
{
    const int frame = state_.frame;
    if (frame == 10) state_.expression = KonngaraExpression::Closed;
    if (frame < 100) return;
    if (frame == 100) { angle_ = 0x40; speed_ = for_rank(80, 80, 96, 112); }
    auto fire = [&](int angle) {
        Pellets.add_single(328, 188, angle & 255, speed_);
        Pellets.add_single(296, 188, angle & 255, speed_);
    };
    // Preserve the source's fall-through: on divisible-by-eight frames below
    // 140 the first branch returns; intermediate frames reach later branches.
    if (frame < 140 && frame % 8 == 0) { fire(0x40); return; }
    if (frame < 220 && frame % 8 == 0) {
        angle_ = (angle_ + 5) & 255; fire(angle_); fire(0x80 - angle_); return;
    }
    if (frame < 300 && frame % 8 == 0) {
        angle_ = (angle_ - 12) & 255; fire(angle_); fire(0x80 - angle_);
    }
    if (frame >= 300) state_.frame = 0;
}

void KonngaraPhase1::snake_spawn(int index)
{
    auto &snake = snakes_[index];
    snake.active = true;
    for (int i = 0; i < 5; ++i) { snake.x[i] = 312; snake.y[i] = 188; }
    // Spawn computes an unused 6px velocity, but still consumes a random roll.
    const int target = player_get_state().x_position + 12;
    const unsigned char angle = iatan2(player_center_y() - 188, target - 312) + (irand() % 2 == 0 ? 40 : -40);
    int vx, vy; vector2(vx, vy, 6, angle);
}

void KonngaraPhase1::snake_update(int index)
{
    auto &snake = snakes_[index];
    if (!snake.active) return;
    for (int i = 3; i >= 1; --i) visual(snake.x[i], snake.y[i], 9);
    visual(snake.x[0], snake.y[0], 7);
    if (!snake.locked) snake.target = player_get_state().x_position;
    const unsigned char angle = iatan2(player_center_y() - snake.y[0],
        snake.target + 12 - snake.x[0]) + (irand() % 2 == 0 ? 40 : -40);
    int vx, vy; vector2(vx, vy, 7, angle);
    if (snake.y[0] > 300) snake.locked = true;
    for (int i = 4; i >= 1; --i) { snake.x[i] = snake.x[i - 1]; snake.y[i] = snake.y[i - 1]; }
    snake.x[0] += vx; snake.y[0] += vy;
    const auto &player = player_get_state();
    if (konngara_snake_hits_player(snake.x[0], snake.y[0], player.x_position, player_top,
        player_is_sliding(), player_is_invincible()))
        player_is_hit = true;
}

void KonngaraPhase1::snakes_and_spreads()
{
    const int frame = state_.frame;
    if (frame == 10) state_.expression = KonngaraExpression::Glare;
    if (frame < 100) return;
    if (frame == 100) {
        snakes_[0].locked = snakes_[1].locked = false;
        snakes_[1].active = false;
        snake_spawn(0); mdrv2_se_play(12); return;
    }
    if (frame < 500) {
        snake_update(0); snake_update(1);
        if (frame == 150) { snake_spawn(1); mdrv2_se_play(12); }
        if (frame > 240 - int(rank) * 40 && frame % 40 == 0) {
            int angle = irand() % 12;
            const int x = frame % 120 == 0 ? 410 : frame % 120 == 40 ? 316 : 312;
            const int y = frame % 120 == 0 ? 70 : frame % 120 == 40 ? 140 : 188;
            for (int i = 0; i < 10; ++i) {
                const int speed = 40 + (i % 4 == 1 ? 16 : i % 4 == 3 ? 32 : 0);
                Pellets.add_single(x, y, (0x80 - angle) & 255, speed);
                Pellets.add_single(x, y, angle, speed);
                angle = (angle + 12) & 255;
            }
            mdrv2_se_play(7);
        }
    } else state_.frame = 0;
}

void KonngaraPhase1::update(bool bomb_damage)
{
    if (state_.complete) return;
    state_.visual_count = 0;
    if (!started_) { started_ = true; return; } // source phase0 setup call
    if (state_.hp_displayed < state_.health.hp) ++state_.hp_displayed;
    ++state_.frame;
    const int player_left = player_get_state().x_position;
    state_.direction = player_left < 198 ? KonngaraDirection::Left :
                       player_left > 396 ? KonngaraDirection::Right : KonngaraDirection::Center;
    if (state_.pattern == 0) diamond_cross();
    else if (state_.pattern == 1) cup_spread();
    else if (state_.pattern == 2) snakes_and_spreads();
    else if (state_.frame > 120) {
        state_.frame = 1;
        state_.pattern = irand() % 3;
        if (state_.pattern == previous_) state_.pattern = irand() % 3;
        previous_ = state_.pattern;
        ++state_.patterns_done;
    }
    if (state_.frame == 0) {
        state_.expression = KonngaraExpression::Neutral;
        state_.pattern = 99;
    }
    boss_hit_update(state_.health, hitbox, 10000, 288, 120, 112, 56, bomb_damage);
    if (state_.hp_displayed > state_.health.hp) state_.hp_displayed = state_.health.hp;
    if (!state_.health.invincible && (state_.patterns_done >= 7 || state_.health.hp < 16) && state_.pattern == 99)
        state_.complete = true;
}

void KonngaraPhase1::update_pattern(KonngaraCombatState &state)
{
    state_ = state;
    if (state_.pattern == 0) diamond_cross();
    else if (state_.pattern == 1) cup_spread();
    else if (state_.pattern == 2) snakes_and_spreads();
    state = state_;
}
