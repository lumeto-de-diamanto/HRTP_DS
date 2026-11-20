#pragma once

enum ElisDefeatPhase { ED_SETTLE,ED_LINES,ED_BLACK };
struct ElisDefeatFrame {
    ElisDefeatPhase phase=ED_SETTLE;
    int line_steps=0,white_steps=0,fade_steps=0,brightness=0;
};

// defeat.cpp29..94,192..197: scroll-reset wait, orb-centered2px lines,
// whiteout every7 updates, then fade every5. main_01.cpp148..151 selects
// an all-black target for Elis. Final line/fade80 have no additional wait.
class ElisDefeat {
    int tick_=0,lines_;
    static int white(int level) {return level>=15 ? 16 : (level*16+7)/15;}
public:
    explicit ElisDefeat(int center) {
        const int y=center<0 ? 0 : center>399 ? 399 : center;
        lines_=(y>399-y ? y : 399-y)/2+1;
    }
    bool next(ElisDefeatFrame &out) {
        out={};
        if(tick_==0) out.phase=ED_SETTLE;
        else if(tick_<lines_) {
            out.phase=ED_LINES;out.line_steps=tick_;out.white_steps=tick_/7;
            out.brightness=white(out.white_steps);
        } else if(tick_<lines_+79) {
            out.phase=ED_BLACK;out.fade_steps=(tick_-lines_+1)/5;
            out.brightness=white(15-out.fade_steps);
        } else return false;
        ++tick_;return true;
    }
};
