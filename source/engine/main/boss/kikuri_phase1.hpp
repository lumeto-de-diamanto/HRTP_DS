#pragma once
#include "boss.hpp"
#include "main/bullet/shootout_laser.hpp"

struct KikuriSoul {
    int left=0,top=0,image=0,shown_image=0,lock=0;
    bool visible=false;
};
constexpr int KIKURI_TEAR_COUNT=10;
struct KikuriTear {
    int left=0,top=0,frame=0,lock=0,generation=0;
    bool visible=false;
};
struct KikuriPhase1State {
    BossEntity entity{230,90,180,180,0,0,180,180,true}; // disc/radar, NOT collision
    BossHealth health{};
    int phase=2,frame=0,patterns_done=0,hp_displayed=0,lightball_step=0;
    bool flash=false,checkpoint=false;
    int soul_phase=0; // activation / spreads / done; phase4 timer carries over
    int pattern=0; // phase6: disc spiral / left-eye lasers / rain / top lasers
    KikuriSoul souls[2]{};
    KikuriTear tears[KIKURI_TEAR_COUNT]{};
    ShootoutLaser lasers[SHOOTOUT_LASER_COUNT]{};
};

// b15j.cpp phases2..6. Caller updates before moving the orb. Preserve the
// original above-disc orb hitbox bug by explicit user choice; shots do not hit
// this static bitmap boss. Default mode retains the approved phase4 checkpoint;
// optional phase4/5 combat stops at the next phase; phase6 stops at zero HP.
// Defeat and progression remain outside this controller.
class KikuriPhase1 {
    KikuriPhase1State state_{};
    unsigned char angle_=0,drift_=0;
    int interval_=1,distance_=90,hit_clock_=0,laser_range_=0;
    bool phase4_=false,phase5_=false,phase6_=false;
    void spiral();
    void rings();
    void soul_move(int index,int dx,int dy);
    void souls();
    void tear_add(int left,int top);
    void tears();
    void phase5();
    void phase6();
    void hit(bool bomb_damage);
public:
    void reset(bool phase4=false,bool phase5=false,bool phase6=false);
    void update(bool bomb_damage=false);
    void update_lasers(); // global pass, after boss/before orb; frozen at checkpoint
    void clear_defeat_projectiles(); // guarded zero-HP laser clear; caller clears moving pellets
    const KikuriPhase1State &state() const { return state_; }
};

// Literal source predicate, including BOTH '<' Y comparisons and strict edges.
constexpr bool kikuri_hittest_orb(int left,int top) {
    return left>256 && left<352 && top<132 && top<180;
}
// Source ripple collision deliberately uses INTERNAL X, not byte-aligned ink.
// No Y/slide check; only the ripple's age and ordinary invincibility matter.
constexpr bool kikuri_ripple_hits(int left,int frame,int player_left) {
    return frame>=2 && frame<60 &&
        player_left>=left-(frame<10 ? 16 : frame<20 ? 32 : 48) &&
        player_left<=left+(frame<10 ? 0 : frame<20 ? 16 : 32);
}
