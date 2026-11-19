#pragma once

// b10m.cpp: west, east, southwest, southeast, north. World coordinates;
// rendering alone halves them. The large kimono/kanji belong to BOSS2.GRP.
constexpr int YUUGENMAGAN_EYES = 5;
constexpr int YUUGENMAGAN_CELS = 7;
constexpr int YUUGENMAGAN_LEFT[YUUGENMAGAN_EYES] = {64,512,192,384,288};
constexpr int YUUGENMAGAN_TOP[YUUGENMAGAN_EYES] = {128,128,176,176,64};
enum YuugenMaganCel { YMG_HIDDEN, YMG_CLOSED, YMG_HALFOPEN, YMG_DOWN, YMG_LEFT, YMG_RIGHT, YMG_AHEAD };

struct YuugenMaganOpeningFrame {
    int frame = 0;
    int image[YUUGENMAGAN_EYES]{};
    int shown[YUUGENMAGAN_EYES]{};
    unsigned lasers = 0;
    int recolor = 0;
    // Combat keeps the source's unclamped signed-byte palette components;
    // the art review retains its original relative opening recolor interface.
    bool color15_override = false;
    int color15[3]{};
    bool complete = false;
};

// Pure presentation timeline, not a blocking entrance or a combat controller.
// Phase0 is interactive in the source. The controller retains its laser hitbox
// asymmetry and rank-specific pellets independently of this view.
class YuugenMaganOpening {
    YuugenMaganOpeningFrame state_{};
public:
    const YuugenMaganOpeningFrame &state() const { return state_; }
    bool next() {
        if(state_.complete) return false;
        const int frame = ++state_.frame;
        // CBossEntity::locked_unput_and_put_8() blits BEFORE cel changes,
        // once every four updates. Preserve cel latency, not unblit holes.
        if((frame-1)%4==0) for(int i=0;i<YUUGENMAGAN_EYES;++i) state_.shown[i]=state_.image[i];
        state_.recolor=frame/40;
        state_.lasers=(frame>160 && frame<300 ? 3u : 0u) |
            (frame>180 && frame<320 ? 12u : 0u) | (frame>200 && frame<=330 ? 16u : 0u);
        if(frame>=100 && frame<=200 && frame%20==0) {
            for(int i=0;i<YUUGENMAGAN_EYES;++i) {
                const int first=i<2 ? 100 : i<4 ? 120 : 140;
                if(frame>=first && frame<=first+60)
                    state_.image[i]=(frame-first)/20+YMG_CLOSED;
                // Opening's third cel is AHEAD, not enum value DOWN.
                if(frame==first+40) state_.image[i]=YMG_AHEAD;
                if(frame==first+60) state_.image[i]=YMG_DOWN;
            }
        } else if(frame==240 || frame==260 || frame==280) {
            for(auto &cel:state_.image) cel=frame==240 ? YMG_HALFOPEN : frame==260 ? YMG_CLOSED : YMG_HIDDEN;
        } else if(frame==300 || frame==320) {
            state_.image[0]=state_.image[1]=frame==300 ? YMG_CLOSED : YMG_HALFOPEN;
        } else if(frame==331) {
            state_.image[0]=YMG_LEFT;state_.image[1]=YMG_DOWN;
            state_.complete=true;
        }
        return true;
    }
};
