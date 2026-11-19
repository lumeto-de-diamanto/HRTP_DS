#pragma once
#include <cstdint>
// User-requested DS adaptation of b15j.cpp: retain the16-dot masks and8/16/24
// row spacing at native DS resolution, rather than halving the mask pixels.
// Replace the expensive square rings with a native white pulse, full-arena
// red-plane inversion, then palette recovery. No gameplay/RNG/audio updates.
// The native audio ROM labels beats from zero,48 ticks each. Start3 ticks
// before19.00 (912), leaving about42ms at KAMI's90 BPM for the VBlank commit.
constexpr unsigned KIKURI_FLASH_TICK=19*48-3;
constexpr unsigned KIKURI_SHROUD_CLEAR_TICK=KIKURI_FLASH_TICK-24; // half a beat earlier
struct KikuriEntranceFrame {
    enum Phase { Barn, Flash, Complete } phase=Barn;
    int source_frame=0,white=15,inversion=0,brightness=0;
};
class KikuriEntrance {
    int counter_=0,y_=0,flash_=0;
    uint16_t rows_[192]{};
    bool flashing_=false,complete_=false;
    void copy(int row,uint16_t mask) { if(row>=0 && row<192) rows_[row]=mask; }
public:
    const uint16_t *rows() const { return rows_; }
    bool next(KikuriEntranceFrame &out,unsigned music_tick) {
        if(complete_) return false;
        if(!flashing_) {
            // The last effective copy is counter360: counter363 would just
            // repeat the same center pair. Finish that copy half a beat before
            // the approved flash, then hold the fully revealed image. Catch up
            // after late snapshots; repeated/stale ticks cannot advance it.
            const int until=music_tick>=KIKURI_SHROUD_CLEAR_TICK ? 360 : music_tick*360/KIKURI_SHROUD_CLEAR_TICK;
            while(counter_<until) {
                ++counter_;
                if(counter_%3==0) {
                    const int offsets[]={0,8,16,24};const uint16_t masks[]={0x2222,0xaaaa,0xeeee,0xffff};
                    for(int i=0;i<4;++i) if(counter_*2>offsets[i] && (i==3 ? y_<=120 : y_<96+offsets[i])) {
                        copy(y_-offsets[i],masks[i]);copy(191-y_+offsets[i],masks[i]);
                    }
                    ++y_;
                }
            }
            flashing_=music_tick>=KIKURI_FLASH_TICK;
            if(!flashing_) { out={KikuriEntranceFrame::Barn,counter_,15,0,0};return true; }
        }
        complete_=flash_==48;
        const int inversion=flash_<16 ? 16 : flash_<48 ? 15-(flash_-16)/2 : 0;
        const int brightness=flash_<8 ? 16-flash_*2 : 0; // same8-frame pulse as bombs
        out={complete_ ? KikuriEntranceFrame::Complete : KikuriEntranceFrame::Flash,
             flash_++,0,inversion,brightness};return true;
    }
};
