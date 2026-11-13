#pragma once

enum KonngaraDefeatPhase { KD_KUJI, KD_SHAKE, KD_SCROLL, KD_RESET };
struct KonngaraDefeatFrame {
    KonngaraDefeatPhase phase;
    int page, line_on_top, ramp, fade;
    bool sound;
};

// b20j.cpp1737..1832. Both scrollup() and frame_delay() observe the same
// source VSYNC count: together they are ONE frame, not two. Disk/DOS text
// clear time is machine-dependent; the intervening black screen needs no hold.
class KonngaraDefeat {
public:
    explicit KonngaraDefeat(int white_red = 15)
        : last_scroll_(152 + (white_red > 0 ? white_red - 1 : 0) * 8) {}
    bool next(KonngaraDefeatFrame &out) {
        if (tick_ < 90) out={KD_KUJI,tick_/10,0,15,0,false};
        else if (tick_ < 155) {
            int frame=tick_-89;
            out={KD_SHAKE,8,(408+(frame%2)*16)%400,15,frame/4,frame%4==0};
        } else if (tick_ <= 155+last_scroll_) {
            int frame=tick_-155,step=frame/8+1;
            out={KD_SCROLL,9,(frame*32)%400,step<15?step:15,
                 frame<152?0:(frame-152)/8+1,
                 frame%8==0 && (frame<150 || (frame<220 && frame<last_scroll_))};
        } else if (tick_ == 156+last_scroll_) out={KD_RESET,9,0,15,(last_scroll_-152)/8+1,false};
        else return false;
        ++tick_;return true;
    }
private:
    int tick_=0,last_scroll_;
};
