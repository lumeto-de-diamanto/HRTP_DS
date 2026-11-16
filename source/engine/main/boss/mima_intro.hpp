#pragma once

// b10j.cpp phase_spreadin(): frame11..50 expands two source rows per step;
// frame51 snaps to the complete still image. The source waits twice for each
// of frames1..50; the final image gets one review-only presentation frame.
struct MimaEntranceFrame { int source_frame, asset_frame; bool complete; };
class MimaEntrance {
    int tick_ = 0;
public:
    bool next(MimaEntranceFrame &out) {
        if(tick_ > 100) return false;
        const int source = tick_ / 2 + 1;
        out = {source, source <= 10 ? 0 : source <= 50 ? source - 10 : 41, tick_ == 100};
        ++tick_;
        return true;
    }
};
