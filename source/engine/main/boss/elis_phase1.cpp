#include "elis_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/math/vector.hpp"
#include "main/player/player.hpp"
#include "main/random.hpp"
#include "sound_effects.hpp"

namespace {
int for_rank(int easy,int normal,int hard,int lunatic)
{
    const int values[]={easy,normal,hard,lunatic};return values[int(rank)>=0 && int(rank)<4 ? int(rank) : 1];
}
}
void ElisPhase1::reset(bool second_block,bool final_block)
{
    *this=ElisPhase1{};state_.health.reset(14);second_block_=second_block || final_block;final_block_=final_block;
}
ElisPoint ElisPhase1::random_surround()
{
    //192x128, bottom-aligned with the96px girl, not vertically centered.
    return {state_.entity.left+irand()%192-32,state_.entity.top+irand()%128-32};
}
void ElisPhase1::teleport(bool centered)
{
    auto &s=state_;const int f=s.frame;
    // Source evaluates these arguments on EVERY call, not just frame52.
    const int x=centered ? 256 : irand()%512,y=centered ? 96 : 64+irand()%136;
    if(f==20 || f==28 || f==36 || f==52 || f==60 || f==68) {
        s.image=f==20 || f==68 ? 2 : f==28 || f==60 ? 3 : 4;
        s.attack_overlay=false;
        s.entity.orb_active=false;
        if(f==52) {s.entity.left=x;s.entity.top=y;}
    } else if(f==44) {s.image=-1;s.attack_overlay=false;s.entity.orb_active=false;}
    else if(f==76) {s.image=0;s.entity.orb_active=true;}
    else if(f>80) {s.frame=0;s.teleport_done=true;return;}
    if(f%4) return;
    for(auto &star:s.stars) {
        const auto target=random_surround();
        if(f<40 || f>52) star=target;
        else {star.x+=(target.x-star.x)/3;star.y+=(target.y-star.y)/3;}
    }
    s.stars_visible=f<68;
}
void ElisPhase1::sweep()
{
    auto &s=state_;const int f=s.frame;
    if(f==50) {direction_=irand()%2;s.image=1;parameter_=for_rank(50,55,60,65);}
    else if(f==60) {s.circle=ElisPhase1State::Sweep;s.radius=32;s.step=2;}
    else if(f>60 && f<120 && f%2==0) {s.radius+=8;s.step=f<100 ? 2 : f<110 ? 8 : 32;}
    else if(f==120) s.circle=ElisPhase1State::None;
    else if(f==150) s.image=0;
    if(f>=70 && f%10==0) {
        const int target=direction_==0 ? (f-70)/10*64 : 640-(f-70)/10*64;
        shootout_laser_spawn(s.lasers[(f/10)%SHOOTOUT_LASER_COUNT],s.entity.left+60,s.entity.top+28,
                            target,400,parameter_,25,4);
        mdrv2_se_play(SFX_ATTACK_6);
        if((direction_==0 && target>=640) || (direction_==1 && target<=0)) {s.frame=0;s.pattern=0;}
    }
}
void ElisPhase1::rifts()
{
    auto &s=state_;const int f=s.frame;
    if(f==50) {s.image=1;parameter_=for_rank(15,21,25,29);}
    // Fire BEFORE replacing the rifts, including the lingering shots after160.
    if(f>60 && f%3==0) {
        const int i=irand()%5;
        const unsigned char angle=irand()%parameter_-(parameter_-1)/2+64;
        int vx,vy;vector2(vx,vy,7,angle);missiles_.add(s.rifts[i].x,s.rifts[i].y,vx,vy);
    }
    rifts_refresh(160);
    if(f>170) {s.frame=0;s.pattern=0;}
}
void ElisPhase1::rifts_refresh(int end_frame)
{
    auto &s=state_;const int f=s.frame;
    if(f>=60 && f<=end_frame && f%4==0) {
        for(int i=0;i<5;++i) {
            auto &rift=s.rifts[i];
            if(f%16==(i*4)%16 || f==60) {const auto p=random_surround();rift.x=p.x;rift.y=p.y;}
            if(f<end_frame) rift.cel=4+irand()%2;
        }
        s.rifts_visible=f<end_frame;mdrv2_se_play(SFX_ATTACK_7);
    }
}
void ElisPhase1::circle()
{
    auto &s=state_;const int f=s.frame;
    if(f<10) circle_frame_=0;
    if(f==50) {
        s.image=1;s.angle=0;
        parameter_=for_rank(PG_1_AIMED,PG_1_RANDOM_NARROW_AIMED,PG_3_SPREAD_WIDE_AIMED,PG_5_SPREAD_NARROW_AIMED);
    }
    if(f>=60 && f%2==0 && !circle_frame_) {
        if(f==60) mdrv2_se_play(SFX_BOMB_START);
        s.circle=ElisPhase1State::Summon;s.angle+=2;s.radius=128;s.step=1;
        if(s.angle>=64) {s.circle=ElisPhase1State::Full;s.circle_color=7;circle_frame_=1;s.image=0;}
    } else if(circle_frame_ && circle_frame_<40) {
        ++circle_frame_;
        if(circle_frame_%8==0) s.circle_color=4;
        else if(circle_frame_%8==4) s.circle_color=7;
    } else if(circle_frame_) {
        s.circle=ElisPhase1State::None;s.angle=0;
        for(int i=0;i<32;++i) {
            Pellets.add_group(polar_x_fast(s.entity.left+60,128,i*8),polar_y_fast(s.entity.top+48,128,i*8),
                              static_cast<pellet_group_t>(parameter_),4);
        }
        s.frame=0;s.pattern=0;circle_frame_=0;
    }
}
void ElisPhase1::update(bool bomb_damage)
{
    auto &s=state_;if(s.checkpoint) return;
    s.hit_pause=0;
    if(missiles_.update(++gameplay_frame_)) player_is_hit=true;
    // The source fills before incrementing its pattern clock. Bound this to
    // current HP instead of reproducing the debug-mode out-of-range HUD write.
    if(s.phase==1 && !hp_filled_) {s.hp_displayed=s.frame+1<s.health.hp ? s.frame+1 : s.health.hp;hp_filled_=s.hp_displayed>=s.health.hp;}
    if(s.phase==2) {
        // Source has no boss hit/grace pass during the fixed-center return.
        // Missiles/global lasers and the caller's gameplay still keep ticking.
        ++s.frame;teleport(true);
        if(s.teleport_done) {s.phase=3;s.frame=0;s.pattern=1;s.teleport_done=false;s.attack_started=false;}
        return;
    }
    if(s.phase==4) {
        ++s.frame;
        if(!s.teleport_done) teleport(true);
        else transform(false);
        if(s.bat_form) {
            s.phase=5;s.frame=0;s.pattern=0;s.teleport_done=false;s.transforming=false;s.attack_started=false;
            hit_clock_=s.health.invincibility_frame=0;
        }
        return;
    }
    ++s.frame;++hit_clock_;
    if(s.phase==5) {
        if(s.bat_form) bat_move();
        final_pattern();hit(bomb_damage);
        if(s.health.hp<=0) s.checkpoint=true;
        return;
    }
    if(s.phase==3 && s.pattern) {
        if(!s.attack_started) star_summon();
        else {
            attack_render();
            if(s.pattern==1) stacked_rings();
            else if(s.pattern==2) sphere_clusters();
            else rotating_rifts();
            if(!s.pattern) s.attack_started=false;
        }
    } else if(s.pattern==1) sweep();
    else if(s.pattern==2) rifts();
    else if(s.pattern==3) circle();
    else if(!s.teleport_done) teleport();
    else {s.pattern=irand()%4;s.teleport_done=false;}
    hit(bomb_damage);
    if(s.health.hp<=(s.phase==1 ? 10 : 6) && s.teleport_done && !s.health.invincible) {
        if((s.phase==1 && second_block_) || (s.phase==3 && final_block_)) {
            ++s.phase;s.frame=0;s.pattern=0;s.teleport_done=false;
            hit_clock_=s.health.invincibility_frame=0;
        } else s.checkpoint=true;
    }
}
void ElisPhase1::hit(bool bomb_damage)
{
    auto &s=state_;const auto &body=s.bat_form ? s.bat : s.entity;
    // Source bat shot-top typo is cur_left, not cur_top. Orb bounds are the
    // separate48x32 entity's expanded[-8,-8..56,40] rectangle.
    const int x=s.bat_form ? body.left : body.left+32,y=s.bat_form ? body.left : body.top+32;
    if(boss_hit_update(s.health,body,7000,x,y,s.bat_form ? 48 : 80,s.bat_form ? 48 : 64,bomb_damage)) {hit_clock_=0;s.flash=true;}
    if(s.health.invincible && hit_clock_<40) {
        if(hit_clock_%4==2) s.flash=true;
        else if(hit_clock_%4==0) s.flash=false;
    } else if(hit_clock_>40) {s.flash=false;hit_clock_=0;}
    if(s.hp_displayed>s.health.hp) s.hp_displayed=s.health.hp;
}
void ElisPhase1::update_lasers()
{
    if(!state_.checkpoint) for(auto &laser:state_.lasers) shootout_laser_update(laser);
}
void ElisPhase1::clear_defeat_projectiles()
{
    if(state_.phase!=5 || !state_.checkpoint || state_.health.hp!=0) return;
    missiles_.reset();for(auto &laser:state_.lasers) laser={};
}
