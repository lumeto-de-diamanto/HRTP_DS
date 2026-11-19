#pragma once
#include <cstdint>

constexpr int SCORE_PER_EXTEND=400000;
constexpr int SCORE_EXTEND_LIVES_MAX=6;

// ReC98 th01/main/extend.cpp: consume ONE reached threshold per update,
// including at the life cap. Presentation never advances this cursor.
class ScoreExtends {
    int64_t next_score_=SCORE_PER_EXTEND;
public:
    void reset() { next_score_=SCORE_PER_EXTEND; }
    int64_t next_score() const { return next_score_; }
    bool update(int score,int &lives) {
        if(score<next_score_) return false;
        next_score_+=SCORE_PER_EXTEND; // wide enough to exceed the signed score range
        if(lives>=SCORE_EXTEND_LIVES_MAX) return false;
        ++lives;
        return true;
    }
};
