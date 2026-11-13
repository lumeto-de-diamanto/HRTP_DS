#pragma once
#include "player.hpp"

// Blocking presentation only: no gameplay position, force or AI is advanced.
// ReC98 player.cpp:922..1018 and player/gameover.cpp, with safe effect indexing
// and genuine 45-degree diagonals instead of integer isqrt(2) == 1.
struct PlayerDeathFrame {
    int left[5]{}, top[5]{};
    bool visible[5]{};
    int count = 0, cel = 20, pose = 22, pose_left = 0, pose_top = player_top;
    int shake_y = 0, hold = 1; // DS pixels; source timings in VBlanks
    int burst_step = -1;
    bool show_pose = true, returning = false;
};

class PlayerDeathAnimation {
    bool fatal_;
    int left_, top_, pose_, step_ = 0, right_, left_effect_, right_delta_, left_delta_;
    bool clipped_[5]{};
public:
    PlayerDeathAnimation(bool fatal, int left, int top, bool alternate)
        : fatal_(fatal), left_(left), top_(top), pose_(alternate ? 23 : 22),
          right_(left), left_effect_(left)
    {
        const int distance = PLAYFIELD_W / 4;
        const int r = left + distance > PLAYER_LEFT_MAX ? PLAYER_LEFT_MAX : left + distance;
        const int l = left - distance < PLAYER_LEFT_MIN ? PLAYER_LEFT_MIN : left - distance;
        right_delta_ = (PLAYER_LEFT_START - r) / 20;
        left_delta_ = (PLAYER_LEFT_START - l) / 20;
    }

    bool next(PlayerDeathFrame &out)
    {
        out = {}; out.pose = pose_; out.pose_left = left_; out.pose_top = top_;
        if (!fatal_) {
            if (step_ >= 47) return false;
            if (step_ < 16) { out.shake_y = (step_++ % 2) * 4; out.hold = 3; return true; }
            if (step_++ == 16) return true; // source's scroll-reset VBlank
            const int frame = step_ - 18;
            right_ += frame < 10 ? PLAYFIELD_W / 4 / 10 : right_delta_;
            left_effect_ += frame < 10 ? -PLAYFIELD_W / 4 / 10 : left_delta_;
            if (right_ > PLAYER_LEFT_MAX) right_ = PLAYER_LEFT_MAX;
            if (left_effect_ < PLAYER_LEFT_MIN) left_effect_ = PLAYER_LEFT_MIN;
            out.returning = true; out.show_pose = false; out.count = 2;
            out.left[0] = right_; out.left[1] = left_effect_;
            out.top[0] = out.top[1] = top_;
            out.visible[0] = out.visible[1] = true;
            out.cel = 20 + frame % 2;
            out.hold = frame < 6 ? 1 : (30 - frame) / 10 + 1;
        } else {
            if (step_ >= 17) return false;
            const int frame = step_ < 16 ? step_ : 15;
            out.burst_step = frame;
            const int distance = (frame + 1) * 8;
            const int diagonal = ((frame + 1) * 370728) / 65536; // 8 / sqrt(2)
            out.count = 5; out.cel = 20 + frame % 2;
            const int dx[5] = {-distance, -diagonal, 0, diagonal, distance};
            const int dy[5] = {0, -diagonal, -distance, -diagonal, 0};
            for (int i = 0; i < 5; ++i) {
                out.left[i] = left_ + dx[i]; out.top[i] = top_ + dy[i];
                clipped_[i] = clipped_[i] || out.left[i] < PLAYFIELD_LEFT || out.left[i] >= PLAYFIELD_RIGHT - PLAYER_W;
                out.visible[i] = !clipped_[i];
            }
            if (step_ < 16) { out.shake_y = (frame % 2) * 4; out.hold = 1 + frame / 6; }
            ++step_;
        }
        return true;
    }
};
