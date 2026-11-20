#pragma once
#include "boss.hpp"
#include "main/bullet/missile.hpp"
#include "main/bullet/shootout_laser.hpp"

struct ElisPoint {int x=0,y=0;};
struct ElisRift {int x=0,y=0,cel=4,color=4;};
struct ElisClusterSphere {int x=0,y=0;bool visible=false,fired=false;};
struct ElisPhase1State {
    BossEntity entity{256,96,128,96,16,-4,112,80,true};
    BossHealth health{};
    int phase=1,frame=0,pattern=1,hp_displayed=0,image=0;
    bool teleport_done=false,checkpoint=false,flash=false;
    ElisPoint stars[5]{};
    ElisRift rifts[5]{};
    bool stars_visible=false,rifts_visible=false;
    enum Circle {None,Sweep,Summon,Full} circle=None;
    int radius=0,step=1,angle=0,circle_color=4;
    bool attack_started=false,attack_overlay=false,hexagram=false;
    ElisClusterSphere spheres[10]{};
    ShootoutLaser lasers[SHOOTOUT_LASER_COUNT]{};
    BossEntity bat{296,128,48,32,-8,-8,56,40,true};
    bool bat_form=false,transforming=false,circle_override=false;
    int bat_image=0,bat_shown=-1,bat_draw_x=296,bat_draw_y=128;
    int shake=0,hit_pause=0,circle_x=0,circle_y=0;
};

// b15m.cpp phases1..5. Uses native PC-98 coordinates; caller updates before
// orb/pellets/shots, and updates lasers in the original global pass afterward.
// The checkpoint is deliberately the source's HP+teleport+grace boundary, not
// an immediate stop at10/6HP. Default mode retains the first-block oracle;
// second_block enables phase3; final_block enables bat/girl combat through0HP.
// Caller presents defeat at the final checkpoint; no natural progression.
class ElisPhase1 {
    ElisPhase1State state_{};
    MissilePool missiles_;
    unsigned gameplay_frame_=0;
    int hit_clock_=0,direction_=0,parameter_=0,circle_frame_=0;
    bool hp_filled_=false,second_block_=false,final_block_=false;
    int bat_lock_=0,bat_vx_=0,bat_vy_=0,bat_until_=0,safety_frame_=0;
    ElisPoint random_surround();
    void teleport(bool centered=false);
    void sweep();
    void rifts();
    void circle();
    void star_summon();
    void attack_render();
    void stacked_rings();
    void sphere_clusters();
    void rotating_rifts();
    void rifts_refresh(int end_frame);
    void hit(bool bomb_damage);
    void transform(bool to_girl);
    void bat_move();
    void final_pattern();
    void bat_pattern();
    void girl_pattern();
    void symmetric_stacks();
    void safety_rain();
    void wing_spreads();
    void end_girl();
public:
    void reset(bool second_block=false,bool final_block=false);
    void update(bool bomb_damage=false);
    void update_lasers();
    void clear_defeat_projectiles(); // source0HP clear; caller clears moving pellets
    const ElisPhase1State &state() const {return state_;}
    const MissilePool &missiles() const {return missiles_;}
};
