#include "elis_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/math/vector.hpp"
#include "main/player/player.hpp"
#include "main/random.hpp"
#include "sound_effects.hpp"
#include <algorithm>
#include <cstdlib>

namespace {
int for_rank(int easy,int normal,int hard,int lunatic)
{
    const int values[]={easy,normal,hard,lunatic};return values[int(rank)>=0 && int(rank)<4 ? int(rank) : 1];
}
void source_group(int x,int y,pellet_group_t group,int speed)
{
    // Native CPellets::add_group spawn bounds. Keep this final-block adapter
    // local: do not retune the already approved earlier boss patterns.
    if(x>=640 || x< -8 || y<56 || y>400) return;
    Pellets.add_group(x,y,group,speed);
}
}
void ElisPhase1::transform(bool to_girl)
{
    auto &s=state_;const int f=s.frame;const auto &body=to_girl ? s.bat : s.entity;
    if(f==20 && !to_girl) {s.image=5;s.attack_overlay=false;}
    if(f>=20 && f<=100 && f%4==0) {
        for(int i=0;i<5;++i) {
            auto &r=s.rifts[i];
            if(f%16==(i*4)%16 || f==20) {
                r.x=body.left+irand()%192-(192-body.width)/2;
                r.y=body.top+irand()%128+(to_girl ? -48 : -32);
            }
            if(f<100) {r.cel=4+irand()%2;r.color=irand()%16;}
        }
        s.rifts_visible=f<100;mdrv2_se_play(SFX_ATTACK_7);
    }
    // Native scroll waits advance z_vsync_Count1, satisfying the outer
    // frame_delay(1). They are not an additional frozen gameplay frame.
    if(f>=20 && f%2==0) s.shake=f%4; // native4px scroll ->DS2px, retained on odd frames
    if(f>100) {
        if(to_girl) {
            s.entity.left=std::clamp(s.bat.left-40,0,512);s.entity.top=std::max(64,s.bat.top-32);
            s.image=6;s.attack_overlay=false;s.bat_shown=-1;
        } else {
            s.bat.left=s.entity.left+40;s.bat.top=s.entity.top+32;s.bat_image=0;s.bat_shown=-1;
            s.image=-1;s.attack_overlay=false; // source has no bat put on this update
        }
        s.bat_form=!to_girl;s.frame=0;s.pattern=0;s.transforming=false;s.shake=0;
    }
}
void ElisPhase1::bat_move()
{
    auto &s=state_;
    if(!bat_lock_) {
        s.bat.left=std::clamp(s.bat.left+bat_vx_,0,736);s.bat.top=std::clamp(s.bat.top+bat_vy_,64,304);
        s.bat_draw_x=s.bat.left;s.bat_draw_y=s.bat.top;s.bat_shown=s.bat_image;bat_lock_=1;
    } else if(bat_lock_>=2) bat_lock_=0;
    else ++bat_lock_;
    // Set the NEXT cel after blitting. This lock persists across transformations.
    if(s.frame%9==0) s.bat_image=0;
    else if(s.frame%9==3) s.bat_image=1;
    else if(s.frame%9==6) s.bat_image=2;
}
void ElisPhase1::bat_pattern()
{
    auto &s=state_;const int x=s.bat.left+20,y=s.bat.top+12;
    auto group=[&](pellet_group_t type,int speed) {source_group(x,y,type,speed);};
    if(s.pattern==1 && s.frame%16==0)
        group(static_cast<pellet_group_t>(for_rank(PG_3_SPREAD_WIDE_AIMED,PG_5_SPREAD_WIDE_AIMED,
              PG_5_SPREAD_NARROW_AIMED,PG_5_SPREAD_NARROW)),56);
    else if(s.pattern==2 && s.frame%4==0) group(irand()&1 ? PG_2_SPREAD_NARROW_AIMED : PG_2_SPREAD_WIDE_AIMED,88);
    else if(s.pattern==3 && s.frame%4==0) Pellets.add_single(x,y,irand(),2,PM_GRAVITY,1);
    else if(s.pattern==4 && s.frame%4==0) {
        if(s.frame%50<10) group(PG_2_SPREAD_NARROW_AIMED,85);
        else if(s.frame%50>=25 && s.frame%50<35) group(PG_3_SPREAD_NARROW_AIMED,85);
    }
}
void ElisPhase1::final_pattern()
{
    auto &s=state_;
    if(s.transforming) {transform(s.bat_form);return;}
    s.shake=0;
    if(!s.bat_form) {girl_pattern();return;}
    if(s.frame<3) bat_vx_=bat_vy_=0;
    else {
        if(s.frame==3) {
            int x,y;
            do {x=irand()%512;y=96+irand()%136;}
            while(std::abs(s.bat.left-x)<128 && std::abs(s.bat.top-y)<64);
            vector2_between(s.bat.left,s.bat.top,x,y,bat_vx_,bat_vy_,2);
            bat_until_=std::abs(x-s.bat.left)/2; // source ignores vertical distance
        }
        // Always zero with truncation toward0, but still consumes a roll.
        bat_vy_+=((irand()%5)-2)/10;
        if(s.frame/3>=bat_until_) {bat_vx_=bat_vy_=0;s.frame=0;s.transforming=true;}
    }
    if(!s.pattern) s.pattern=irand()%4+1;
    bat_pattern(); // arrival resets frame0 FIRST, so the final shot still fires
}
void ElisPhase1::girl_pattern()
{
    auto &s=state_;
    if(!s.pattern) {s.pattern=irand()%3+1;return;}
    if(!s.attack_started) {star_summon();return;}
    attack_render();
    if(s.pattern==1) symmetric_stacks();
    else if(s.pattern==2) safety_rain();
    else wing_spreads();
}
void ElisPhase1::end_girl()
{
    auto &s=state_;s.frame=0;s.pattern=0;s.attack_started=false;s.transforming=true;
    s.circle=ElisPhase1State::None;s.circle_override=false;s.angle=0;circle_frame_=0;
}
void ElisPhase1::symmetric_stacks()
{
    auto &s=state_;const int f=s.frame;
    auto pair=[&](int angle,int speed) {
        Pellets.add_single(s.entity.left+56,s.entity.top+40,128-angle,speed*16);
        Pellets.add_single(s.entity.left+64,s.entity.top+40,angle,speed*16);
    };
    if(f==50 || f==60 || f==70) {
        if(f==50) parameter_=for_rank(2,3,3,4);
        for(int i=0;i<4;++i) pair(f==50 ? 64 : f==60 ? 48 : 24,parameter_+i);
        if(f!=50) mdrv2_se_play(SFX_ATTACK_7);
    } else if(f==80) {for(int i=0;i<10;++i) pair(i*6,parameter_+2);mdrv2_se_play(SFX_ATTACK_7);}
    else if(f>=120) end_girl();
}
void ElisPhase1::safety_rain()
{
    auto &s=state_;const int f=s.frame;
    if(f<10) safety_frame_=0;
    if(f<60) return;
    if(f==60) {
        parameter_=for_rank(4,2,2,2);s.angle=0;s.circle_x=player_get_state().x_position+16;s.circle_y=player_center_y();
        s.circle_override=true;mdrv2_se_play(SFX_BOMB_START);
    }
    if(f%2==0 && !safety_frame_) {
        s.circle=ElisPhase1State::Summon;s.radius=128;s.step=1;s.angle+=2;
        if(s.angle>=64) {s.circle=ElisPhase1State::Full;s.circle_color=7;safety_frame_=1;s.image=0;s.attack_overlay=false;}
    } else if(safety_frame_ && safety_frame_<200) {
        ++safety_frame_;const int target=s.circle_x-16,left=player_get_state().x_position;
        if(!player_is_invincible() && (left>=target+104 || left<=target-128)) {player_is_hit=true;s.hit_pause=6;}
        if(player_is_hit) safety_frame_=200;
        if(f%parameter_==0) source_group(irand()%632,64,rank==RANK_LUNATIC ? PG_1_RANDOM_WIDE : PG_1,72);
        if(safety_frame_>180 && safety_frame_%4==0) source_group(s.entity.left+60,s.entity.top+44,PG_1_AIMED,72);
    } else if(safety_frame_) {
        if(safety_frame_==200) {s.circle=ElisPhase1State::None;s.circle_override=false;}
        ++safety_frame_;
        if(safety_frame_%8==0) source_group(s.bat.left+60,s.bat.top+44,PG_1_AIMED,72); // retained BAT anchor
        if(safety_frame_>260) {safety_frame_=0;end_girl();}
    }
}
void ElisPhase1::wing_spreads()
{
    auto &s=state_;const int f=s.frame;
    if(f<50) return;
    if(f==50) {s.image=1;s.attack_overlay=false;s.angle=0;parameter_=for_rank(40,30,20,15);}
    if(f>=60 && f%2==0 && !circle_frame_) {
        if(f==60) mdrv2_se_play(SFX_BOMB_START);
        s.circle=ElisPhase1State::Summon;s.radius=128;s.step=1;s.angle+=2;
        if(s.angle>=64) {s.circle=ElisPhase1State::Full;s.circle_color=7;circle_frame_=1;s.image=0;s.attack_overlay=false;}
    } else if(circle_frame_ && circle_frame_<40) {
        ++circle_frame_;
        if(circle_frame_%8==0) s.circle_color=4;
        else if(circle_frame_%8==4) s.circle_color=7;
    } else if(circle_frame_) {
        for(int i=0;i<32;++i) source_group(polar_x_fast(s.entity.left+64,128,i*8),
            polar_y_fast(s.entity.top+28,128,i*8),PG_1,64);
        end_girl();return;
    }
    if(f%parameter_==0) {
        source_group(s.entity.left+22,s.entity.top+40,PG_5_SPREAD_WIDE_AIMED,64);
        source_group(s.entity.left+100,s.entity.top+40,PG_5_SPREAD_WIDE_AIMED,64);
    }
    if(f%(parameter_*2)==0) {
        shootout_laser_spawn(s.lasers[(f/10)%10],s.entity.left+60,s.entity.top+28,
            player_get_state().x_position,400,50,20,8);mdrv2_se_play(SFX_ATTACK_6);
    }
}
