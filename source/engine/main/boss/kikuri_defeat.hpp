#pragma once

enum KikuriDefeatPhase { KD_SETTLE, KD_LINES, KD_BLACK };
struct KikuriDefeatFrame {
    KikuriDefeatPhase phase=KD_SETTLE;
    int line_steps=0,white_steps=0,fade_steps=0;
    int brightness=0; // native white coefficient; final black is separate
};

// Shared source defeat.cpp: orb-centered2px expansion, whiteout every7 frames,
// then a5-frame fade cadence. main_01.cpp gives Kikuri an ALL-BLACK target.
// Initial scroll-reset wait is included. Final line and fade80 have NO wait.
class KikuriDefeat {
    int tick_=0,lines_=0;
    static int white(int level) {return level>=15 ? 16 : (level*16+7)/15;}
public:
    explicit KikuriDefeat(int center) {
        const int y=center<0 ? 0 : center>399 ? 399 : center;
        lines_=(y>399-y ? y : 399-y)/2+1;
    }
    bool next(KikuriDefeatFrame &out) {
        out={};
        if(tick_==0) out.phase=KD_SETTLE;
        else if(tick_<lines_) {
            out.phase=KD_LINES;out.line_steps=tick_;out.white_steps=tick_/7;
            out.brightness=white(out.white_steps);
        } else if(tick_<lines_+79) {
            out.phase=KD_BLACK;out.fade_steps=(tick_-lines_+1)/5;
            out.brightness=white(15-out.fade_steps);
        } else return false;
        ++tick_;return true;
    }
};
