#pragma once

// b20j.cpp enum values are BOS cel indices, not left-to-right ordering.
enum class KonngaraDirection { Right = 0, Left = 1, Center = 2 };
enum class KonngaraExpression { Neutral = 0, Closed = 1, Glare = 2, Aim = 3 };

// slash_animate() emits a background overwrite only at these keyframes.
// The caller owns the phase clock, SFX8 at frame 50 and reset after frame 200.
constexpr int konngara_slash_cel_on_frame(int frame)
{
    switch (frame) {
    case 50: return 0;
    case 60: return 1;
    case 100: return 2;
    case 120: return 3;
    case 140: return 4;
    case 160: return 5;
    case 170: return 6;
    default: return -1;
    }
}
