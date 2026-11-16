#pragma once

enum MimaDefeatPhase { MD_SETTLE, MD_LINES, MD_FADE };
struct MimaDefeatFrame {
    MimaDefeatPhase phase=MD_SETTLE;
    int line_steps=0,white_steps=0,fade_steps=0;
};

// defeat.cpp:29..94,192..197: initial scroll-reset VBlank, two-pixel
// expanding lines with RGB4 increments every7 frames, then white -> arena
// decrements every5 frames. The final line iteration has NO delay; neither
// does the final all-components-already-done check at fade frame80.
class MimaDefeat {
    int tick_=0,lines_;
public:
    explicit MimaDefeat(int orb_center_y) {
        // Real orb centers are on-screen. Bound corrupt/debug-only positions
        // without turning an offscreen test orb into thousands of hold frames.
        const int y=orb_center_y<0 ? 0 : orb_center_y>399 ? 399 : orb_center_y;
        lines_=(y>399-y ? y : 399-y)/2+1;
    }
    bool next(MimaDefeatFrame &out) {
        out={};
        if(tick_==0) out.phase=MD_SETTLE;
        else if(tick_<lines_) {
            out.phase=MD_LINES;out.line_steps=tick_;out.white_steps=tick_/7;
        } else if(tick_<lines_+79) {
            out.phase=MD_FADE;out.fade_steps=(tick_-lines_+1)/5;
        } else return false;
        ++tick_;return true;
    }
};
