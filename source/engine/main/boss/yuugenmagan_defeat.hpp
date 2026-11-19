#pragma once

enum YuugenMaganDefeatPhase { YMD_SETTLE,YMD_LINES,YMD_ARENA };
struct YuugenMaganDefeatFrame {
    YuugenMaganDefeatPhase phase=YMD_SETTLE;
    int line_steps=0,white_steps=0,fade_steps=0,brightness=0;
};

// defeat.cpp29..94,192..197: scroll-reset wait, orb-centered2px lines,
// whitening every7 updates, then original arena restored from white every5.
// Final line and fade80 do not wait. Native brightness owns the top screen;
// main_01.cpp148..154 gives YuugenMagan its ORIGINAL palette, not black.
class YuugenMaganDefeat {
    int tick_=0,lines_;
    static int white(int level) {return level>=15 ? 16 : (level*16+7)/15;}
public:
    explicit YuugenMaganDefeat(int center) {
        const int y=center<0 ? 0 : center>399 ? 399 : center;
        lines_=(y>399-y ? y : 399-y)/2+1;
    }
    bool next(YuugenMaganDefeatFrame &out) {
        out={};
        if(tick_==0) out.phase=YMD_SETTLE;
        else if(tick_<lines_) {
            out.phase=YMD_LINES;out.line_steps=tick_;out.white_steps=tick_/7;
            out.brightness=white(out.white_steps);
        } else if(tick_<lines_+79) {
            out.phase=YMD_ARENA;out.fade_steps=(tick_-lines_+1)/5;
            out.brightness=white(15-out.fade_steps);
        } else return false;
        ++tick_;return true;
    }
};
