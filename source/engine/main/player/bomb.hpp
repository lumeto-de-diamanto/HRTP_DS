#pragma once

// Mechanics only. Rendering may soften the original without changing these
// keyframes or the global-frame/last-life boss-damage policy.
class PlayerBomb {
    int frame_ = -1, tap_age_ = 20, shot_taps_ = 0, strike_taps_ = 0;
    bool pending_ = false, active_ = false, requested_ = false;
    void tap(int &pressed, int &other) {
        if(tap_age_ >= 20) { tap_age_ = 0; pressed = 1; other = 0; }
        else if(pressed < 2) ++pressed;
    }
public:
    static constexpr int discharge_frame = 50, done_frame = 140;
    void reset() { *this = PlayerBomb{}; }
    bool input(bool shot, bool strike, bool direct, bool opposite_directions, bool navigator) {
        if(tap_age_ < 20) ++tap_age_;
        if(navigator || active_ || pending_) { shot_taps_ = strike_taps_ = 0; requested_ = false; return false; }
        if(shot) tap(shot_taps_,strike_taps_);
        if(strike) tap(strike_taps_,shot_taps_);
        if(direct || (shot_taps_ >= 2 && strike_taps_ >= 2) || (opposite_directions && shot)) {
            shot_taps_ = strike_taps_ = 0; requested_ = true;
        }
        return requested_;
    }
    void discard_request() { requested_ = false; }
    void arm() { pending_ = true; requested_ = false; }
    // Source BF_START arms for one tick; the next tick consumes the bomb and
    // displays frame0. Frames0..139 are active; frame140 grants recovery grace.
    void advance() {
        if(pending_) { pending_ = false; active_ = true; frame_ = 0; }
        else if(active_ && ++frame_ == done_frame) active_ = false;
    }
    bool pending() const { return pending_; }
    bool active() const { return active_; }
    bool damaging() const { return active_ && frame_ >= discharge_frame; }
    int frame() const { return frame_; }
    unsigned card_frame() const { return unsigned(frame_ + 1); } // source increments before cards
};
