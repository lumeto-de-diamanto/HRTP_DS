#pragma once
#include "boss.hpp"
#include "yuugenmagan_opening.hpp"
#include "main/bullet/missile.hpp"

struct YuugenMaganPoint { int x=0,y=0; };
struct YuugenMaganPentagram {
    YuugenMaganPoint corners[5]{},ink[5]{}; // regular corners vs retained path-order ink
    int subphase=0,distance=0,center_x=0,center_y=0,radius=0;
    int attack_phase=0,angle=0,velocity_x=0,velocity_y=0,slams=0;
    unsigned eyes_open=1;
    int after_hit_frame=0,unused_frame=0;
    bool repeat=false;
    bool visible=false;
};

struct YuugenMaganPhase1State {
    YuugenMaganOpeningFrame visual{};
    BossEntity eyes[YUUGENMAGAN_EYES]{};
    BossHealth health{};
    int phase=0,frame=0,hp_displayed=0,interval=300;
    bool flash=false,checkpoint=false,timed_out=false;
    int defeat_phase=0; // lethal origin, including phase13's same-update return to10
    int missile_subphase=0,pairs_fired=0,iterations=0,target_left=0;
    unsigned char missile_angles[2]{};
    int spray_subphase=0,spray_frame=0;
    unsigned char pellet_angle=0;
    int laser_subphase=0,laser_edge=0;
    int laser_targets[2]{}; // retained native beam endpoints; not per-frame hits
    unsigned laser_count=0;
    YuugenMaganPentagram pentagram{};
};

// b10m.cpp phases0..13. Called after player controls and BEFORE orb movement.
// Default reset retains the approved first-block oracle; reset(true) continues
// through the70-frame eye shift and missiles to a phase4 checkpoint. The second
// flag continues through the reverse shift/spray to phase6, never a defeat.
// The third continues through shifted missiles/northern lasers to phase10.
// The fourth assembles/shrinks/raises the pentagram to a phase13 checkpoint.
// The fifth enables the looping final attack; HP0 is still a non-victory limit.
class YuugenMaganPhase1 {
    YuugenMaganPhase1State state_{};
    YuugenMaganOpening opening_;
    int locks_[YUUGENMAGAN_EYES]{};
    int hit_clock_=0;
    bool phase3_=false,phase5_=false,phase9_=false,phase12_=false,phase13_=false;
    unsigned gameplay_frame_=0;
    MissilePool missiles_;
    void opening_lasers(int frame);
    void hit(bool bomb_damage);
    void eye_blit(int index);
    void track(int index);
    void track_image(int index);
    void toggle_eyes(unsigned closing,unsigned opening,int &frame,int dec,int inc);
    void transition();
    void missile_pattern();
    void fire_missiles();
    void spray_pattern();
    void laser_pattern();
    void pentagram_shrink(int distance);
    void pentagram_regular(int angle);
    void pentagram_put();
    void pentagram_pattern(bool bomb_damage);
    void pentagram_attack(bool bomb_damage);
    void pentagram_render_regular();
    void pentagram_aim();
    void pentagram_back(int attack_phase);
public:
    void reset(bool phase3=false,bool phase5=false,bool phase9=false,bool phase12=false,bool phase13=false);
    void update(bool bomb_damage=false);
    void clear_defeat_projectiles(); // phase11 retains missiles; phase13 clears them
    const YuugenMaganPhase1State &state() const { return state_; }
    const MissilePool &missiles() const { return missiles_; }
};
