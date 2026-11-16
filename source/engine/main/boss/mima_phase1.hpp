#pragma once
#include "boss.hpp"
#include "main/bullet/missile.hpp"
#include "main/bullet/shootout_laser.hpp"

struct MimaPoint { int x=0,y=0; };
struct MimaParticle { int x=0,y=0,vx=0,vy=0;bool alive=false; };
struct MimaPillar {
    int time=128,center_x=0,radius=0,height=0;
    bool ray=false;
};
struct MimaPhase1State {
    BossEntity entity{256,120,128,160,16,16,112,144,true};
    BossHealth health{};
    int phase=1,frame=0,pattern=0,hp_displayed=0;
    int image=41,darkness=0,flash_darkness=0; // local RGB4 subtracts of4, capped16
    bool flash=false,squares_visible=false,checkpoint=false;
    int square_count=0;
    MimaPoint corners[2][4]{};
    MimaParticle particles[40]{};
    MimaPillar pillars[8]{};
    ShootoutLaser lasers[SHOOTOUT_LASER_COUNT]{};
};

// Source b10j.cpp phases1/2/3. Optional first-block-only mode retains the
// isolated six-HP oracle. Zero HP yields to the blocking defeat renderer;
// progression remains owned by the caller, not this controller.
// Caller updates before the orb; misses retain controller/HP/projectiles.
class MimaPhase1 {
    MimaPhase1State state_{};
    MissilePool missiles_;
    int angle_=0,radius_=0,speed_=0,target_left_=0;
    int hop_=-1,direction_=0,hit_clock_=0,particle_cycle_=1;
    bool meteor_=true;
    bool second_block_=false;
    int firing_angle_=0;
    unsigned gameplay_frame_=0;
    void particles();
    void square_set();
    void squares();
    void hop();
    void hit(bool bomb_damage);
    void pillars();
    void second_squares();
public:
    void reset(bool second_block=false);
    void update(bool bomb_damage=false);
    void update_lasers(); // original global laser pass, after boss/before orb
    void clear_defeat_projectiles();
    const MimaPhase1State &state() const { return state_; }
    const MissilePool &missiles() const { return missiles_; }
};
