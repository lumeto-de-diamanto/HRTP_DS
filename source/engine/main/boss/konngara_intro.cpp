#include "konngara_intro.hpp"

KonngaraIntro::KonngaraIntro() = default;

bool KonngaraIntro::next(KonngaraIntroFrame &out)
{
    if (phase_ == KI_DONE) return false;
    out = {phase_, 0, 400, ramp_, -1, false};
    int duration = 0;
    switch (phase_) {
    case KI_SCROLL:
        out.line_on_top = line_;
        line_ += quarters_;
        // Deliberately strict >, sequential tests, and a single 400px wrap,
        // exactly as in b20j.cpp. Rounding the scroll before this loses timing.
        for (int q = 0; q < 4; ++q) {
            if (quarter_ == q && line_ > (q + 1) * 100) {
                ++quarter_;
                --quarters_;
                if (quarter_ == 4) { quarter_ = 0; line_ -= 400; }
            }
        }
        if (quarters_ <= 0) {
            phase_ = KI_HOLD_BELOW;
            frame_ = 0;
        } else {
            if (frame_ % 8 == 0 && ramp_ < 15) ++ramp_;
            ++frame_;
        }
        out.ramp = ramp_;
        return true;
    case KI_HOLD_BELOW: duration = 40; break;
    case KI_SHAKE_BELOW:
        out.line_on_top = 416 - (frame_ % 2) * 32;
        out.shake_sound = frame_ % 8 == 0;
        duration = 32;
        break;
    case KI_PAN:
        out.line_on_top = 400 - frame_ * 32;
        out.arena_from = out.line_on_top;
        // 400,368,...,16: the original does NOT have a final strip at zero.
        // Its initial y400 copy has no visible rows. Don't emulate unsafe
        // accesses to PC-98 VRAM scratch/adjacent planes on the DS.
        duration = 13;
        break;
    case KI_SHAKE_ABOVE:
        out.line_on_top = 416 - (frame_ % 2) * 16;
        out.arena_from = 16;
        duration = 32;
        break;
    case KI_HOLD_ABOVE:
        out.arena_from = 16;
        duration = 30;
        break;
    case KI_SIDDHAM:
        out.arena_from = 16;
        out.syllable_step = frame_ / 10;
        duration = 160;
        break;
    case KI_DONE: return false;
    }
    if (++frame_ == duration) {
        phase_ = static_cast<KonngaraIntroPhase>(phase_ + 1);
        frame_ = 0;
    }
    return true;
}
