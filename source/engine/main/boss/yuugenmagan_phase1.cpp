#include "yuugenmagan_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/math/vector.hpp"
#include "main/random.hpp"

void YuugenMaganPhase1::reset(bool phase3,bool phase5,bool phase9,bool phase12,bool phase13)
{
    *this=YuugenMaganPhase1{};state_.health.reset(16);
    phase3_=phase3 || phase5 || phase9 || phase12 || phase13;phase5_=phase5 || phase9 || phase12 || phase13;
    phase9_=phase9 || phase12 || phase13;phase12_=phase12 || phase13;phase13_=phase13;
    for(int i=0;i<YUUGENMAGAN_EYES;++i)
        state_.eyes[i]={YUUGENMAGAN_LEFT[i],YUUGENMAGAN_TOP[i],64,48,-4,-4,68,48,false};
}

void YuugenMaganPhase1::eye_blit(int index)
{
    if(!locks_[index]) {state_.visual.shown[index]=state_.visual.image[index];locks_[index]=1;}
    else if(locks_[index]>=3) locks_[index]=0;
    else ++locks_[index];
}

void YuugenMaganPhase1::track(int index)
{
    eye_blit(index);
    track_image(index);
}

void YuugenMaganPhase1::track_image(int index)
{
    const int delta=state_.eyes[index].left+32-player_get_state().x_position;
    state_.visual.image[index]=delta>32 ? YMG_LEFT : delta< -32 ? YMG_RIGHT : YMG_DOWN;
}

void YuugenMaganPhase1::toggle_eyes(unsigned closing,unsigned opening,int &frame,int dec,int inc)
{
    auto &s=state_;
    // Source blits BEFORE advancing this phase's clock or changing its cels.
    for(int i=0;i<YUUGENMAGAN_EYES;++i) if((closing|opening)&(1u<<i)) eye_blit(i);
    ++frame;
    if(frame%10) {
        if(dec<3 && s.visual.color15[dec]>0) --s.visual.color15[dec];
        if(inc<3) {
            const int value=s.visual.color15[inc]+1;
            s.visual.color15[inc]=value>127 ? value-256 : value; // source signed-byte overflow, NOT hardware clamping
        }
    }
    for(int i=0;i<YUUGENMAGAN_EYES;++i) {
        const bool close=closing&(1u<<i),open=opening&(1u<<i);
        if(frame==20) {
            if(close) s.visual.image[i]=YMG_HALFOPEN;
            if(open) {s.visual.image[i]=YMG_CLOSED;s.eyes[i].orb_active=true;}
        } else if(frame==40) {
            if(close) {s.visual.image[i]=YMG_CLOSED;s.eyes[i].orb_active=false;}
            if(open) s.visual.image[i]=YMG_HALFOPEN;
        } else if(frame==60) {
            if(close) s.visual.image[i]=YMG_HIDDEN;
            if(open) s.visual.image[i]=YMG_AHEAD;
        }
    }
}

void YuugenMaganPhase1::transition()
{
    auto &s=state_;
    const unsigned closing=s.phase==10 ? 0u : s.phase==12 ? 30u : (s.phase==4 || s.phase==8) ? 12u : 3u;
    const unsigned opening=s.phase==10 ? 31u : s.phase==12 ? 1u : s.phase==8 ? 16u : s.phase==4 ? 3u : 12u;
    const bool unchanged=s.phase==10 && s.pentagram.repeat;
    const int dec=unchanged ? 3 : (s.phase==4 || s.phase>=10) ? 1 : s.phase==8 ? 2 : 0;
    const int inc=unchanged || s.phase==6 ? 3 : s.phase==8 ? 1 : (s.phase==4 || s.phase>=10) ? 0 : 2;
    toggle_eyes(closing,opening,s.frame,dec,inc);
    if(s.phase==12 && hit_clock_%4==0) {
        s.pentagram.center_y-=3;pentagram_regular(192);
        const int path[]={0,2,4,1,3};
        for(int i=0;i<5;++i) s.pentagram.ink[i]=s.pentagram.corners[path[i]];
        pentagram_put();
    }
    if(s.frame>=70) {
        ++s.phase;s.frame=0;hit_clock_=s.health.invincibility_frame=0;
        // Retain the rank-selected but UNUSED source field. Firing is every8
        // updates on ALL ranks; do not substitute this interval into the pattern.
        if(s.phase==3) {
            const int intervals[]={8,12,16,20};
            s.interval=intervals[int(rank)>=0 && int(rank)<4 ? int(rank) : 1];
        } else if(s.phase==5) {
            const int spray_intervals[]={12,8,4,2};
            s.interval=spray_intervals[int(rank)>=0 && int(rank)<4 ? int(rank) : 1];
            s.spray_subphase=s.spray_frame=s.iterations=0;s.pellet_angle=0;
        } else if(s.phase==7) {
            const int shifted_intervals[]={10,16,20,24}; // still unused by the source pattern
            s.interval=shifted_intervals[int(rank)>=0 && int(rank)<4 ? int(rank) : 1];
            s.missile_subphase=s.pairs_fired=s.iterations=0;
        } else if(s.phase==9) {
            s.laser_subphase=s.laser_edge=0;s.laser_count=0;
        } else if(s.phase==11) {
            s.pentagram.subphase=s.pentagram.distance=s.pentagram.angle=0;
        } else if(s.phase==13) {
            s.pentagram.angle=0;s.pentagram.eyes_open=1;s.pentagram.after_hit_frame=s.pentagram.unused_frame=0;
            s.checkpoint=!phase13_;s.timed_out=!phase13_;
        }
    }
}

void YuugenMaganPhase1::fire_missiles()
{
    for(int i=0;i<2;++i) {
        int vx,vy;vector2(vx,vy,8,state_.missile_angles[i]);
        missiles_.add(state_.eyes[2+i].left+28,state_.eyes[2+i].top+28,vx,vy);
    }
}

void YuugenMaganPhase1::missile_pattern()
{
    auto &s=state_;track(2);track(3);
    if(s.frame>=90 && s.missile_subphase==0) s.visual.image[2]=s.visual.image[3]=YMG_CLOSED;
    if(s.frame==100 || s.missile_subphase%4==1) {
        if(s.missile_subphase==0) {
            s.missile_subphase=(irand()%(s.phase==7 ? 2 : 3))*4+1;
            s.target_left=player_get_state().x_position-8;
            for(int i=0;i<2;++i) {
                const auto &eye=s.eyes[2+i];
                // Original PLUS offsets, not aim-from-spawn subtraction.
                s.missile_angles[i]=iatan2(player_top+PLAYER_H-eye.top+28,s.target_left-eye.left+28)+(i ? 16 : -16);
            }
        }
        if(s.frame%8==0) fire_missiles();
        if(s.frame==200) {++s.missile_subphase;s.pairs_fired=0;}
    }
    if(s.missile_subphase%4==2) {
        if(s.frame%8==1) {
            ++s.pairs_fired;
            if(s.phase==7) {
                const int delta=s.missile_subphase<4 ? -2 : 2;
                s.missile_angles[0]+=delta;s.missile_angles[1]+=delta;
            } else {s.missile_angles[0]-=2;s.missile_angles[1]+=2;}
            fire_missiles();
            if(s.pairs_fired>10) {++s.missile_subphase;s.pairs_fired=0;}
        }
    } else if(s.missile_subphase%4==3) {
        if(s.frame%8==1) {
            ++s.pairs_fired;
            const int variation=s.missile_subphase/4;
            if(s.phase==7) {
                const int delta=variation==0 ? 3 : -3;
                s.missile_angles[0]+=delta;s.missile_angles[1]+=delta;
            } else if(variation==0) s.missile_angles[0]+=4;
            else if(variation==1) s.missile_angles[1]-=4;
            else {s.missile_angles[0]+=2;s.missile_angles[1]-=2;}
            fire_missiles();
            if(s.pairs_fired>10) {s.missile_subphase=0;s.pairs_fired=0;s.frame=0;++s.iterations;}
        }
    }
}

void YuugenMaganPhase1::spray_pattern()
{
    auto &s=state_;
    ++s.spray_frame;--s.pellet_angle;track(0);track(1);
    if(s.frame==100 && s.spray_subphase==0) {s.spray_frame=0;s.spray_subphase=1;}
    auto fire=[&](bool falling) {
        if(s.spray_frame%s.interval) return;
        const auto motion=falling ? PM_FALL_STRAIGHT_FROM_TOP_THEN_REGULAR : PM_REGULAR;
        // Source add_single accepts a BYTE, including the promoted subtraction.
        Pellets.add_single(s.eyes[0].left+28,s.eyes[0].top+32,
            static_cast<unsigned char>(128-s.pellet_angle),64,motion,falling ? 64 : 0);
        Pellets.add_single(s.eyes[1].left+28,s.eyes[1].top+32,s.pellet_angle,64,motion,falling ? 64 : 0);
    };
    if(s.spray_subphase==1) {
        fire(false); // includes counter0 AND counter180 when divisible
        if(s.spray_frame/5>=36) {s.spray_subphase=2;s.frame=0;}
    }
    if(s.spray_subphase==2 && s.frame>50) {
        s.spray_frame=-15;s.spray_subphase=3;s.pellet_angle=128;
    }
    if(s.spray_subphase==3) {
        fire(true); // signed modulo executes immediately at -15, not next tick
        if(s.spray_frame/5>=20) {
            s.spray_subphase=0;s.frame=0;++s.iterations;s.pellet_angle=0;
            // Independent counter stays100 through the next preparation.
        }
    }
}

void YuugenMaganPhase1::laser_pattern()
{
    auto &s=state_;eye_blit(4);
    if(s.frame>=30 && !(s.laser_subphase&1)) s.visual.image[4]=YMG_CLOSED;
    const int iteration=s.laser_subphase/2;
    if(!(s.laser_subphase&1)) {
        if(s.frame!=40) return;
        ++s.laser_subphase;
        if(iteration) s.laser_edge=0;
    }
    if(s.frame%4!=1) return; // ink and collision BOTH update only every fourth frame
    const bool dual=iteration==2,leftward=iteration==1;
    s.visual.image[4]=dual ? YMG_AHEAD : s.laser_edge<=256 ? (leftward ? YMG_RIGHT : YMG_LEFT) :
        s.laser_edge>=384 ? (leftward ? YMG_LEFT : YMG_RIGHT) : YMG_DOWN;
    s.laser_count=dual ? 2 : 1;
    s.laser_targets[0]=leftward ? 640-s.laser_edge : s.laser_edge;
    if(dual) s.laser_targets[1]=640-s.laser_edge;
    for(unsigned i=0;i<s.laser_count;++i) {
        // Source linebullet collision tests ONLY the bottom endpoint, not the
        // sloping ink or the full3-pixel width. Keep strict left/right bounds.
        const int end=s.laser_targets[i],left=player_get_state().x_position;
        if(end>left && end<left+PLAYER_W && 400>player_top+PLAYER_H/2 && !player_is_invincible()) player_is_hit=true;
    }
    if(s.laser_edge>=(dual ? 288 : 592)) {
        s.laser_count=0; // final invisible update still performed its hit test above
        s.laser_subphase=dual ? 6 : (iteration+1)*2;s.frame=s.laser_edge=0;
        if(rank>RANK_NORMAL) {
            const bool five=dual && rank==RANK_LUNATIC;
            Pellets.add_group(s.eyes[4].left+28,s.eyes[4].top+28,
                five ? PG_5_SPREAD_NARROW_AIMED : PG_3_SPREAD_NARROW_AIMED,five ? 4 : 3);
        }
    }
    s.laser_edge+=16; // yes: completion leaves16, despite resetting to0 above
}

void YuugenMaganPhase1::pentagram_shrink(int distance)
{
    auto &p=state_.pentagram;
    // b10m's integer-derived divisors, including its lateral-vs-southern Y
    // baseline mismatch.21 steps of8 overshoot the nominal160px shrink.
    p.ink[0]={state_.eyes[4].left+28,state_.eyes[4].top+28+distance/2};
    p.ink[1]={state_.eyes[2].left+28+distance/3,state_.eyes[2].top+28+distance/2};
    p.ink[2]={state_.eyes[1].left+28-distance,state_.eyes[1].top+28+distance/3};
    p.ink[3]={state_.eyes[0].left+28+distance,state_.eyes[0].top+28+distance/3};
    p.ink[4]={state_.eyes[3].left+28-distance/3,state_.eyes[3].top+28+distance/2};
}

void YuugenMaganPhase1::pentagram_regular(int angle)
{
    auto &p=state_.pentagram;
    for(int i=0;i<5;++i) {
        p.corners[i]={polar_x_fast(p.center_x,p.radius,static_cast<unsigned char>(angle)),
            polar_y_fast(p.center_y,p.radius,static_cast<unsigned char>(angle))};
        angle+=51; // source256/5 truncation, not a symmetric float pentagon
    }
}

void YuugenMaganPhase1::pentagram_put()
{
    auto &p=state_.pentagram;p.visible=true;
    // Source linebullet tests only each segment's endpoint. The present
    // assembly/rise remains above Reimu, but retain this rule for later slams.
    const int left=player_get_state().x_position;
    if(!player_is_invincible()) for(const auto &point:p.ink)
        if(point.x>left && point.x<left+PLAYER_W && point.y>player_top+PLAYER_H/2) player_is_hit=true;
}

void YuugenMaganPhase1::pentagram_pattern(bool bomb_damage)
{
    auto &s=state_;auto &p=s.pentagram;
    for(int i=0;i<YUUGENMAGAN_EYES;++i) track(i);
    if(s.frame==100 || p.subphase==1) {
        if(!p.subphase) {p.subphase=1;s.target_left=player_get_state().x_position;} // unused source latch
        pentagram_shrink(0);pentagram_put();
        if(s.frame==140) {p.subphase=2;p.distance=0;}
    }
    if(p.subphase==2 && s.frame%4==1) {
        p.distance+=8;pentagram_shrink(p.distance);pentagram_put();
        if(p.distance>160) {
            p.corners[0]=p.ink[0];p.subphase=3;
        }
    }
    hit(bomb_damage);
    if(s.health.hp<=0) {
        s.checkpoint=true;s.timed_out=false;s.defeat_phase=11;
        return;
    }
    if(p.subphase==3 && !s.health.invincible) {
        s.phase=12;s.frame=0;hit_clock_=s.health.invincibility_frame=0;
        p.eyes_open=1;
        p.center_x=p.corners[0].x;p.center_y=p.corners[0].y+64;p.radius=64;
        // Source overwrites the geometry for its WRONG first unput in12.
        // Retain the separate correct shrink ink until the next actual put.
        pentagram_regular(0);p.corners[1].y=p.corners[4].y;
        const int intervals[]={24,14,10,8};s.interval=intervals[int(rank)>=0 && int(rank)<4 ? int(rank) : 1];
    }
}

void YuugenMaganPhase1::pentagram_render_regular()
{
    auto &p=state_.pentagram;pentagram_regular(p.angle);
    const int path[]={0,2,4,1,3};
    for(int i=0;i<5;++i) p.ink[i]=p.corners[path[i]];
    pentagram_put();
}

void YuugenMaganPhase1::pentagram_aim()
{
    auto &p=state_.pentagram;
    vector2_between(p.center_x,p.center_y,player_get_state().x_position,player_top+PLAYER_H,p.velocity_x,p.velocity_y,4);
}

void YuugenMaganPhase1::pentagram_back(int attack_phase)
{
    auto &s=state_;auto &p=s.pentagram;
    p.attack_phase=attack_phase;p.repeat=true;p.visible=false;p.unused_frame=0;++p.slams;
    s.phase=10;s.frame=0;hit_clock_=s.health.invincibility_frame=0;
}

void YuugenMaganPhase1::pentagram_attack(bool bomb_damage)
{
    auto &s=state_;auto &p=s.pentagram;++p.unused_frame;
    for(int i=0;i<5;++i) if(p.eyes_open&(1u<<i)) eye_blit(i);
    if(s.health.invincible && !p.after_hit_frame) {
        p.eyes_open<<=1;if(p.eyes_open==32) p.eyes_open=1;
        p.after_hit_frame=1;
    } else if(p.after_hit_frame>0) {
        // The selected new eye gets TWO source lock calls: once above, once
        // inside the transition. Do not merge these into a single blit pass.
        toggle_eyes(p.eyes_open==1 ? 16 : p.eyes_open>>1,p.eyes_open,p.after_hit_frame,3,3);
        if(p.after_hit_frame>=70) {p.after_hit_frame=0;p.unused_frame=0;}
    } else {
        for(int i=0;i<5;++i) if(p.eyes_open&(1u<<i)) track_image(i);
    }
    if(s.frame%s.interval==0 && p.attack_phase!=6 && p.attack_phase!=9) {
        // Source emits BEFORE changing the star. Use stored polar corners,
        // including the preparation frame's old angle, not newly rendered ink.
        for(const auto &point:p.corners) Pellets.add_single(point.x,point.y,
            iatan2(point.y-p.center_y,point.x-p.center_x),48);
    }
    if(s.frame==100 && (p.attack_phase==0 || p.attack_phase==7)) {++p.attack_phase;s.frame=0;}
    auto spin=[&](int direction) {
        p.angle+=direction*(s.frame/64+2);pentagram_render_regular();
    };
    auto slam=[&](int threshold,bool move_first) {
        if(s.frame%4) return;
        if(p.center_y<400-threshold) pentagram_aim();
        if(move_first) {p.center_x+=p.velocity_x;p.center_y+=p.velocity_y;}
        p.angle+=s.frame/64+2;
        if(!move_first) {p.center_x+=p.velocity_x;p.center_y+=p.velocity_y;}
        pentagram_render_regular();
    };
    if(p.attack_phase==1 || p.attack_phase==3 || p.attack_phase==5) {
        if(s.frame%4==0) spin(1);
        if(s.frame>=200) {++p.attack_phase;s.frame=0;}
    }
    if(p.attack_phase==2 || p.attack_phase==4) {
        if(s.frame%4==0) spin(-1);
        if(s.frame>=200) {++p.attack_phase;s.frame=0;pentagram_aim();}
    } else if(p.attack_phase==6) {
        slam(100,false);
        if(p.center_y>=420) pentagram_back(7);
    } else if(p.attack_phase==8) {
        if(s.frame%4==0) {++p.radius;spin(1);}
        if(s.frame>=200) {p.attack_phase=9;s.frame=0;pentagram_aim();}
    }
    if(p.attack_phase==9) {
        // The nominal grown radius is114 even though the immediate frame0
        // update makes the real radius115. Preserve that homing threshold.
        slam(140,true);
        if(p.center_y>=420) pentagram_back(0);
    }
    hit(bomb_damage);
    if(s.health.hp<=0) {s.checkpoint=true;s.timed_out=false;s.defeat_phase=13;}
}

void YuugenMaganPhase1::clear_defeat_projectiles()
{
    if(state_.checkpoint && state_.health.hp==0 && state_.defeat_phase==13) missiles_.reset();
}

void YuugenMaganPhase1::opening_lasers(int frame)
{
    // Source north laser still HITTESTS on331, then gets unput on the phase1
    // handoff. Thus its collision lasts one update longer than visible ink.
    const unsigned mask=(frame>160 && frame<300 ? 3u : 0u) |
        (frame>180 && frame<320 ? 12u : 0u) | (frame>200 ? 16u : 0u);
    const int left=player_get_state().x_position;
    for(int i=0;i<YUUGENMAGAN_EYES;++i) if(mask&(1u<<i)) {
        const int center=state_.eyes[i].left+28;
        const int width=i==1 ? PLAYER_W/4 : PLAYER_W;
        // Literal overlap_low_center_lt_gt(...,48), including east asymmetry.
        if(left>center-24 && left<center+24-width) player_is_hit=true;
    }
    if(player_is_invincible()) player_is_hit=false; // source clears an earlier hit too
}

void YuugenMaganPhase1::hit(bool bomb_damage)
{
    auto &s=state_;
    const auto &orb=orb_get_state();
    BossEntity contact{};
    for(int i=0;i<YUUGENMAGAN_EYES;++i)
        if(s.visual.image[i]!=YMG_HIDDEN && s.eyes[i].hits_orb(orb.x_position,orb.y_position)) {
            contact=s.eyes[i];break;
        }
    // Source supplies NO shot hitbox: ordinary shots pass through the eyes.
    if(boss_hit_update(s.health,contact,5000,0,0,0,0,bomb_damage)) {hit_clock_=0;s.flash=true;}
    // Keep the source's flash/restore order local to this boss. Hit frame0
    // immediately restores; frame40 retains38's flash until expiry on41.
    if(s.health.invincible && hit_clock_<40) {
        if(hit_clock_%4==2) s.flash=true;
        else if(hit_clock_%4==0) s.flash=false;
    } else if(hit_clock_>40) {s.flash=false;hit_clock_=0;}
    s.health.invincibility_frame=hit_clock_;
    if(s.hp_displayed>s.health.hp) s.hp_displayed=s.health.hp;
}

void YuugenMaganPhase1::update(bool bomb_damage)
{
    auto &s=state_;
    if(s.checkpoint) return;
    // Source global missile pass precedes eye movement, targeting, and spawns.
    if(missiles_.update(++gameplay_frame_)) player_is_hit=true;
    if(s.phase==0) {
        if(s.hp_displayed<16) ++s.hp_displayed;
        opening_.next();s.visual=opening_.state();s.frame=s.visual.frame;
        for(auto &lock:locks_) lock=s.frame%4;
        opening_lasers(s.frame);
        if(s.frame==100) s.eyes[0].orb_active=s.eyes[1].orb_active=true;
        if(s.frame==120) s.eyes[2].orb_active=s.eyes[3].orb_active=true;
        if(s.frame==140) s.eyes[4].orb_active=true;
        if(s.frame==240) for(auto &eye:s.eyes) eye.orb_active=false;
        if(s.frame==320) s.eyes[0].orb_active=s.eyes[1].orb_active=true;
        if((rank==RANK_HARD && s.frame==240) || (rank==RANK_LUNATIC && s.frame==260)) {
            // HARD only at240; the source's nested Lunatic branch is unreachable.
            const int order[]={4,3,2,0,1};
            for(int n=0;n<(rank==RANK_HARD ? 3 : 5);++n) {
                const auto &eye=s.eyes[order[n]];
                Pellets.add_group(eye.left+28,eye.top+28,PG_5_SPREAD_WIDE_AIMED,54);
            }
        }
        if(s.visual.complete) {
            s.phase=1;s.frame=0;
            const int intervals[]={350,300,200,130};
            s.interval=intervals[int(rank)>=0 && int(rank)<4 ? int(rank) : 1];
        }
        return; // opening never calls boss hit handling, even for a bomb
    }
    if(s.phase==2 || s.phase==4 || s.phase==6 || s.phase==8) {transition();return;} // no hit/bomb handling
    if(s.phase==10) {transition();return;}
    if(s.phase==12) {
        s.health.invincibility_frame=++hit_clock_; // source's independent four-update translation clock
        transition();return; // no damage/grace handling while changing eyes/palette
    }
    ++s.frame;++hit_clock_;
    if(s.phase==11) {pentagram_pattern(bomb_damage);return;}
    if(s.phase==13) {pentagram_attack(bomb_damage);return;}
    if(s.phase==3 || s.phase==7) {
        missile_pattern();hit(bomb_damage);
        const bool later=s.phase==7;
        if(s.health.hp<=(later ? 8 : 12) || s.iterations>=5) {
            s.checkpoint=!later && !phase5_;s.timed_out=s.checkpoint && s.health.hp>12;
            s.phase=later ? 8 : 4;s.frame=0;
            hit_clock_=s.health.invincibility_frame=0;
        }
        return;
    }
    if(s.phase==5) {
        spray_pattern();hit(bomb_damage);
        if(s.health.hp<=10 || s.iterations>4) {
            s.timed_out=!phase9_ && s.health.hp>10;s.phase=6;s.frame=0;s.checkpoint=!phase9_;
            hit_clock_=s.health.invincibility_frame=0;
        }
        return;
    }
    if(s.phase==9) {
        laser_pattern();hit(bomb_damage);
        if(s.health.hp<=2 || s.laser_subphase==6) {
            s.timed_out=!phase12_ && s.health.hp>2;s.phase=10;s.frame=0;s.checkpoint=!phase12_;
            hit_clock_=s.health.invincibility_frame=0;
            // Source HP exit leaves current beam ink on screen. Freeze it at
            // this checkpoint; it is not a boss defeat or projectile clear.
        }
        return;
    }
    track(0);track(1);
    const int clock=s.frame%s.interval;
    const int times[]={10,25,40,60,80,110},speeds[]={24,28,32,44,50,68};
    for(int n=0;n<6;++n) if(clock==times[n])
        for(int i=0;i<2;++i) Pellets.add_group(s.eyes[i].left+28,s.eyes[i].top+32,
            n==5 ? PG_1_AIMED : PG_2_SPREAD_WIDE_AIMED,speeds[n]);
    hit(bomb_damage);
    if(s.health.hp<=15 || s.frame>1100) {
        s.timed_out=s.health.hp>15;s.phase=2;s.frame=0;s.checkpoint=!phase3_;
        if(phase3_) {
            s.timed_out=false;s.visual.color15_override=true;
            s.visual.color15[0]=s.visual.color15[1]=13;s.visual.color15[2]=5;
        }
        hit_clock_=s.health.invincibility_frame=0;
    }
}
