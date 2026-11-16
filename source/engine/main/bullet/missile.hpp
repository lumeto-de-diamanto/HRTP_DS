#pragma once
#include <cstdint>

constexpr int MISSILE_COUNT=50, MISSILE_W=16, MISSILE_H=16;
struct Missile {
    int x=0,y=0,previous_x=0,previous_y=0,vx=0,vy=0; // source Q12.4
    uint8_t flag=0; // free0, moving1, explosion cels2/3
    int draw_x=0,draw_y=0,cel=0;
    bool visible=false; // retained ink of BOTH interlace fields
};

// Source missile.cpp lifecycle, independent of renderers and boss controllers.
// Movement/collision runs every frame; selected parity refreshes persistent ink
// and advances explosions. Missiles are not pellets: no rank-speed adjustment,
// orb/shot erasure, deflection, decay, or bomb clearing is added here.
class MissilePool {
    Missile missiles_[MISSILE_COUNT]{};
public:
    bool add(int left,int top,double velocity_x,double velocity_y);
    void reset();
    bool update(unsigned gameplay_frame);
    const Missile &at(int slot) const { return missiles_[slot]; }
    int alive_count() const;
};
