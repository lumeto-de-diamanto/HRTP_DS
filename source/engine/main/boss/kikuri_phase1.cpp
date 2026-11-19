#include "kikuri_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/math/trig.hpp"
#include "main/player/player.hpp"
#include "main/playfld.hpp"
#include "main/random.hpp"
#include "sound_effects.hpp"
#include <climits>

namespace {
// Shared BossEntity subtracts8/24 from raw inset bounds. This adapter yields
// x257..351 / yINT_MIN..131, exactly the intentionally preserved source bug.
constexpr BossEntity hitbox{0,0,0,0,265,INT_MIN+8,375,155,true};
int for_rank(int easy,int normal,int hard,int lunatic) {
    const int values[]={easy,normal,hard,lunatic};return values[int(rank)>=0 && int(rank)<4 ? int(rank) : 1];
}
}

void KikuriPhase1::reset(bool phase4,bool phase5,bool phase6)
{
    *this=KikuriPhase1{};state_.health.reset(14);
    phase4_=phase4 || phase5 || phase6;phase5_=phase5 || phase6;phase6_=phase6;
}
void KikuriPhase1::spiral()
{
    auto &s=state_;
    if(s.frame<100) return;
    if(s.frame==100) {
        angle_=drift_=0;interval_=for_rank(4,3,2,1);distance_=for_rank(90,90,90,50);
    }
    if(s.frame%interval_) return;
    for(int side=0;side<2;++side) {
        const unsigned char angle=angle_+side*128;
        Pellets.add_single(polar_x_fast(320,distance_,angle),polar_y_fast(180,distance_,angle),
                           (angle+drift_)&255,50); // to_sp(3.125), before shared rank/speed offset
    }
    if(s.frame>=900) s.frame=0;
    angle_-=8;++drift_;
}
void KikuriPhase1::hit(bool bomb_damage)
{
    auto &s=state_;
    const bool damaged=boss_hit_update(s.health,hitbox,7000,0,0,0,0,bomb_damage);
    if(damaged) {hit_clock_=0;s.flash=true;}
    // Source flashes then immediately restores at hit frame0, and alternates
    // at2/4 afterward. Keep its free-running palette clock local to Kikuri.
    if(s.health.invincible && hit_clock_<40) {
        if(hit_clock_%4==2) s.flash=true;
        else if(hit_clock_%4==0) s.flash=false;
    } else if(hit_clock_>40) {s.flash=false;hit_clock_=0;}
    // Source phase6's hidden timeout changes HP without touching the bar.
    if(s.hp_displayed>s.health.hp && (s.phase!=6 || damaged)) s.hp_displayed=s.health.hp;
}
void KikuriPhase1::rings()
{
    if(state_.frame%for_rank(80,60,55,50)) return;
    const int aim=iatan2(player_center_y()+4-224,player_center_x()+4-320);
    for(int i=0;i<8;++i)
        Pellets.add_single(polar_x_fast(316,16,i*32),polar_y_fast(224,16,i*32),
                           aim,48,PM_SPIN,32,312,224);
}
void KikuriPhase1::soul_move(int index,int dx,int dy)
{
    auto &soul=state_.souls[index];
    // CBossEntity's lock_frames=1 moves/blits on alternating CALLS, not every
    // call. Activation calls only on even frames, so it moves every4 frames.
    if(!soul.lock) {
        soul.left+=dx;soul.top+=dy;
        if(soul.left<32) soul.left=32;else if(soul.left>576) soul.left=576;
        if(soul.top<64) soul.top=64;else if(soul.top>400) soul.top=400;
        soul.shown_image=soul.image;soul.visible=true;soul.lock=1;
    } else soul.lock=0;
    // The cel advances AFTER the blit, including on a locked/no-blit call.
    if(state_.frame%12==0) soul.image=(soul.image+1)%3;
}
void KikuriPhase1::souls()
{
    auto &s=state_;
    if(s.soul_phase==0) {
        if(s.frame==200) {
            for(auto &soul:s.souls) {soul.left=304;soul.top=208;}
        } else if(s.frame>200 && s.frame%2==0) {
            soul_move(0,-4,-3);soul_move(1,4,-3);
            if(s.souls[0].top<96) s.soul_phase=1;
        }
    } else {
        soul_move(0,0,0);soul_move(1,0,0);
        if(s.frame%80==0) {
            const pellet_group_t groups[]={PG_2_SPREAD_WIDE_AIMED,PG_3_SPREAD_WIDE_AIMED,
                                           PG_5_SPREAD_WIDE_AIMED,PG_5_SPREAD_WIDE};
            for(const auto &soul:s.souls)
                Pellets.add_group(soul.left+12,soul.top+8,groups[for_rank(0,1,2,3)],55);
        }
        if(s.frame>600) s.soul_phase=2;
    }
}
void KikuriPhase1::tear_add(int left,int top)
{
    for(auto &tear:state_.tears) if(!tear.frame) {
        tear.left=left;tear.top=top;tear.frame=1;tear.visible=false;++tear.generation;
        return; // the source retains this slot's movement lock across reuse
    }
}
void KikuriPhase1::tears()
{
    for(auto &tear:state_.tears) if(tear.frame) {
        if(tear.top<=384) {
            if(!tear.lock) {
                if(tear.left<32) tear.left=32;else if(tear.left>576) tear.left=576;
                tear.top+=8;if(tear.top>400) tear.top=400;
                tear.visible=true;tear.lock=1;
            } else tear.lock=0;
        } else {
            ++tear.frame;
            if(tear.frame<5) mdrv2_se_play(SFX_ATTACK_7); // source repeats at2/3/4
            if(!player_is_invincible() && kikuri_ripple_hits(tear.left,tear.frame,player_get_state().x_position))
                player_is_hit=true;
            if(tear.frame>=75) {tear.frame=0;tear.visible=false;}
        }
    }
}
void KikuriPhase1::phase5()
{
    auto &s=state_;
    if(s.frame<10) interval_=for_rank(200,160,140,120);
    if(s.frame%interval_==0) for(const auto &soul:s.souls)
        tear_add(soul.left+12,soul.top+8); // source TEAR_W/4, deliberately not half
    tears();
    const int dx=polar_x_fast(0,6,s.frame),dy=polar_x_fast(0,2,s.frame);
    soul_move(0,dx,dy);soul_move(1,-dx,dy); // BOTH axes use cosine
    if(s.frame>200 && s.frame%200==0) {
        const int left=player_get_state().x_position;
        shootout_laser_spawn(s.lasers[0],307,147,left+60,400,50,20,8);
        shootout_laser_spawn(s.lasers[1],336,147,left-60,400,50,20,8);
        mdrv2_se_play(SFX_ATTACK_6);
    }
}
void KikuriPhase1::update_lasers()
{
    if(!state_.checkpoint) for(auto &laser:state_.lasers) shootout_laser_update(laser);
}
void KikuriPhase1::clear_defeat_projectiles()
{
    if(state_.phase==6 && state_.checkpoint && state_.health.hp==0)
        for(auto &laser:state_.lasers) laser={};
}
void KikuriPhase1::phase6()
{
    auto &s=state_;
    // Shared helper runs BEFORE soul movement and before a pattern can reset
    // the frame. Existing tears/ripples keep updating, but no new tears spawn.
    if(s.frame%200==0) for(const auto &soul:s.souls)
        Pellets.add_group(soul.left+12,soul.top+12,PG_1_AIMED,40);
    tears();
    const int dx=polar_x_fast(0,4,s.frame),dy=polar_x_fast(0,2,s.frame);
    soul_move(0,dx,dy);soul_move(1,-dx,dy);
    if(s.pattern==0) {
        if(s.frame<100) return;
        if(s.frame==100) {angle_=32;interval_=for_rank(14,8,6,4);}
        if(s.frame%interval_==0) {
            // Literal i=0,2 and128*i: both origins wrap to the SAME point.
            for(int i=0;i<4;i+=2) {
                const int x=polar_x_fast(320,90,s.frame*4+128*i),y=polar_y_fast(180,90,s.frame*4+128*i);
                for(int a=0;a<256;a+=64) Pellets.add_single(x,y,(angle_+a)&255,36);
            }
            if(s.frame<=600) angle_-=5;
            else {s.frame=0;s.pattern=1;}
        }
    } else if(s.pattern==1) {
        if(s.frame>=250 && s.frame<=400 && (s.frame-250)%30==0) {
            if(s.frame==250) interval_=for_rank(52,56,60,64); // union now stores laser speed*8
            mdrv2_se_play(SFX_ATTACK_6);
            const int target=player_get_state().x_position+(irand()%120)-60;
            shootout_laser_spawn(s.lasers[(s.frame-250)/30],307,147,target,400,interval_,20,4);
        }
        if(s.frame>500) {s.frame=0;s.pattern=2;}
    } else if(s.pattern==2) {
        // Source's frame==100 initialization is nested inside frame>100 and
        // unreachable. Reuse the preceding laser speed:52/56/60/64, NOT35/25/23/22.
        if(s.frame>100 && s.frame%interval_==0) for(const auto &soul:s.souls) {
            const int speeds[]={48,32,16,16,32,48};
            for(int i=0;i<6;++i) Pellets.add_single(soul.left+12,soul.top+12,i<3 ? 0 : 128,speeds[i],PM_GRAVITY,1);
            mdrv2_se_play(SFX_ATTACK_7);
        }
        if(s.frame>250) {s.frame=0;s.pattern=3;}
    } else {
        if(s.frame<100) return;
        if(s.frame==100) {interval_=for_rank(60,64,68,72);laser_range_=for_rank(0,10,16,20);}
        if(s.frame%10==0) {
            const int i=(s.frame-100)/10;
            const int x=i*64+(irand()%(laser_range_*2+1))-laser_range_; // Easy still consumes RNG (%1)
            shootout_laser_spawn(s.lasers[i],x,64,x,400,interval_,20,8);
            mdrv2_se_play(SFX_ATTACK_6);
        }
        if(s.frame>=190) {s.frame=0;s.pattern=0;}
    }
}
void KikuriPhase1::update(bool bomb_damage)
{
    auto &s=state_;
    if(s.checkpoint) return;
    if(s.phase==2) {
        if(s.hp_displayed<s.health.hp && s.hp_displayed<14) ++s.hp_displayed;
        ++s.frame;++hit_clock_;spiral();hit(bomb_damage);
        if(s.frame==0) ++s.patterns_done;
        if(!s.health.invincible && (s.health.hp<=10 || s.patterns_done>=6)) {
            s.phase=3;s.frame=0;s.patterns_done=0;hit_clock_=0;
            s.health.invincibility_frame=0;s.flash=false;s.entity.orb_active=false;
        }
    } else if(s.phase==3) {
        ++s.frame;
        if(s.frame%20==19) ++s.lightball_step; // source RGB4 color5: R++ / B--
        if(s.frame>=100) {s.phase=4;s.frame=0;s.checkpoint=!phase4_;s.entity.orb_active=phase4_;}
    } else if(s.phase==4) {
        ++s.frame;++hit_clock_;rings();souls();hit(bomb_damage);
        if(!s.health.invincible && s.soul_phase==2) {
            s.phase=5;s.frame=0;s.checkpoint=!phase5_;s.entity.orb_active=phase5_;
        }
    } else if(s.phase==5) {
        ++s.frame;++hit_clock_;phase5();hit(bomb_damage);
        if(!s.health.invincible && (s.health.hp<=6 || s.frame>1600)) {
            s.phase=6;s.frame=0;s.patterns_done=0;hit_clock_=0;
            s.health.invincibility_frame=0;s.checkpoint=!phase6_;s.entity.orb_active=phase6_;
        }
    } else if(s.phase==6) {
        ++s.frame;++hit_clock_;phase6();
        if(s.frame==0) ++s.patterns_done;
        if(s.patterns_done>29) s.health.hp=1; // deliberately no HP-bar update
        hit(bomb_damage);
        if(s.health.hp<=0) {s.checkpoint=true;s.entity.orb_active=false;}
    }
}
