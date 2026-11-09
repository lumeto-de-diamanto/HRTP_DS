#pragma once

// b20j.cpp's blocking entrance, expressed as presentation frames. No gameplay,
// hardware, input or audio state is advanced here. One step = one VBlank at the
// port's approved 60 Hz (the original runs at ~56.4 Hz).
enum KonngaraIntroPhase {
    KI_SCROLL, KI_HOLD_BELOW, KI_SHAKE_BELOW, KI_PAN,
    KI_SHAKE_ABOVE, KI_HOLD_ABOVE, KI_SIDDHAM, KI_DONE
};

struct KonngaraIntroFrame {
    KonngaraIntroPhase phase;
    int line_on_top;       // source pixels, wrapped at 400 by the renderer
    int arena_from;        // page-1 strips already copied to page 0; 400 = none
    int ramp;              // RGB4 black-in steps, 0..15
    int syllable_step;     // -1 = absent; 0..15 = post-decrement palette step
    bool shake_sound;     // source SFX9, only every eighth below-shake frame
};

class KonngaraIntro {
public:
    KonngaraIntro();
    bool next(KonngaraIntroFrame &out);
private:
    KonngaraIntroPhase phase_ = KI_SCROLL;
    int frame_ = 0, line_ = 0, quarters_ = 32, quarter_ = 0, ramp_ = 0;
};
