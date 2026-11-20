#pragma once
#include <cstdint>
#include "main/math/trig.hpp"

constexpr int ELIS_LEFT=256,ELIS_TOP=96;
constexpr int ELIS_ROTATION_WAITS=659,ELIS_ENTRANCE_WAITS=799;
struct ElisSphere {int left=0,top=0,cel=0;};
struct ElisEntranceFrame {
    enum Girl {Hidden,Wave,Still,Ripple} girl=Hidden;
    ElisSphere spheres[2][3]{}; // large head, medium trail, small trail
    int rotation_tick=1,wave_frame=0,wave_put_frame=0,source_frame=0;
    unsigned revision=0;
    bool complete=false;
};

// b15m.cpp1817..2023:119 accelerating holds, then140 one-frame waits.
// KAMI2 is90 BPM,48 native ticks/beat:6 music ticks per5 source60Hz frames.
// This retains the source's approximate17/19/20-beat cues without free-running
// VBlank drift. Missing/stale/regressed playback snapshots never advance it.
class ElisEntrance {
    struct Point {int x=0,y=0;} history_[2][3]{};
    ElisEntranceFrame frame_{};
    int hold_=10;
    bool offscreen_=false;
    void put(int i,Point head,int age) {
        frame_.spheres[i][0]={head.x,head.y,3};
        frame_.spheres[i][1]=age>1 ? ElisSphere{history_[i][0].x,history_[i][0].y,2} : ElisSphere{};
        frame_.spheres[i][2]=age>2 ? ElisSphere{history_[i][1].x,history_[i][1].y,1} : ElisSphere{};
    }
    void forward(int i,Point head) {history_[i][2]=history_[i][1];history_[i][1]=history_[i][0];history_[i][0]=head;}
    void rotate() {
        const int angle=(frame_.rotation_tick-1)*24;
        for(int i=0;i<2;++i) {
            const unsigned char a=angle+i*128;
            const Point head{304+((96*Cos8(a))>>8),114+((96*Sin8(a))>>8)};
            put(i,head,frame_.rotation_tick);forward(i,head);
        }
        ++frame_.revision;
    }
    void move() {
        for(int i=0;i<2;++i) {
            const Point head{history_[i][0].x+(i ? -64 : 64),history_[i][0].y};
            put(i,head,3);
            // Preserve the original per-sphere check/copy ordering, but not
            // uninitialized reads or destructive background-unput holes.
            if(history_[0][2].x>640 && history_[1][2].x< -32) offscreen_=true;
            forward(i,head);
        }
        ++frame_.revision;
    }
    void step() {
        ++frame_.source_frame;
        if(frame_.rotation_tick<120) {
            if(--hold_) return;
            ++frame_.rotation_tick;rotate();
            if(frame_.rotation_tick==120) frame_.wave_frame=1; // last rotation has NO wait
            else hold_=(120-frame_.rotation_tick)/12+1;
            return;
        }
        const int f=++frame_.wave_frame;
        if(!offscreen_ && f%4==0) move();
        if(f>20 && f<100 && f%4==0) {frame_.girl=ElisEntranceFrame::Wave;frame_.wave_put_frame=f;++frame_.revision;}
        else if(f==100 || f==130) {frame_.girl=ElisEntranceFrame::Still;++frame_.revision;}
        else if(f==128) {frame_.girl=ElisEntranceFrame::Ripple;++frame_.revision;}
        else if(f>140) {frame_.complete=true;++frame_.revision;}
    }
public:
    ElisEntrance() {rotate();}
    const ElisEntranceFrame &state() const {return frame_;}
    const ElisEntranceFrame &at(unsigned music_tick) {
        const uint64_t target=uint64_t(music_tick)*5/6;
        while(!frame_.complete && uint64_t(frame_.source_frame)<target) step();
        return frame_;
    }
};
