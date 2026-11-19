#pragma once
#include <cassert>
#include <initializer_list>
#include "main/boss/yuugenmagan_phase1.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/bullet/pellet.hpp"

extern bool invincible;

// Reach real production phases through their HP transitions, never by forging
// private controller clocks. Only the HP thresholds are test inputs.
inline void yuugenmagan_enter(YuugenMaganPhase1 &boss,int target)
{
    boss.reset(true,true,true,true,true);
    player_initialize();invincible=true;player_is_hit=false;shots_initialize();
    orb_set_position(10000,10000);
    for(int i=0;i<331;++i) {Pellets.init();boss.update();}
    auto &s=const_cast<YuugenMaganPhase1State&>(boss.state());
    for(int hp:{15,12,10,8,2}) {
        if(s.phase>=target) break;
        s.health.hp=hp;boss.update();
        for(int i=0;i<70 && s.phase<target;++i) boss.update();
    }
    for(int i=0;s.phase<target;++i) {assert(i<400);Pellets.init();boss.update();}
    assert(s.phase==target && !s.checkpoint);Pellets.init();
}
