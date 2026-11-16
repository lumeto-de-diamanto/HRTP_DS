// ReC98 b10j.cpp:480..1312 and particle.cpp PO_TOP_RIGHT.
#include "mima_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/math/vector.hpp"
#include "main/math/trig.hpp"
#include "main/random.hpp"
#include "sound_effects.hpp"

namespace {
int for_rank(int easy,int normal,int hard,int lunatic) {
    const int values[]={easy,normal,hard,lunatic};return values[int(rank)>=0 && int(rank)<4 ? int(rank) : 1];
}
}

void MimaPhase1::reset(bool second_block)
{
    *this=MimaPhase1{};state_.health.reset(12);
    second_block_=second_block;
    // Source phase0's particle update increments cycle once without spawning,
    // then initializes the alternating hop direction after entrance playback.
    direction_=irand()%2;
}

void MimaPhase1::particles()
{
    if(++particle_cycle_>=80) particle_cycle_=0;
    if(particle_cycle_%2==0) {
        const int i=particle_cycle_/2;auto &p=state_.particles[i];
        if(!p.alive) {
            p.alive=true;const int velocity=irand()%10+1;
            if(i%2==0) { p.x=(irand()%640)*16;p.y=0; }
            else { p.x=639*16;p.y=(irand()%400)*16; }
            p.vx=-velocity*13;p.vy=velocity*8;
        }
    }
    for(auto &p:state_.particles) if(p.alive) {
        p.x+=p.vx;p.y+=p.vy;
        if(p.x<0 || p.x>639*16 || p.y<0 || p.y>399*16) p.alive=false;
    }
}

void MimaPhase1::square_set()
{
    auto &s=state_;
    for(int square=0;square<s.square_count;++square) for(int i=0;i<4;++i) {
        const unsigned char angle=(square ? -angle_ : angle_)+i*64;
        s.corners[square][i]={polar_x_fast(s.entity.left+64,radius_,angle),polar_y_fast(s.entity.top+80,radius_,angle)};
    }
}

void MimaPhase1::squares()
{
    auto &s=state_;const int f=s.frame,p=s.pattern;
    if(p==2 && f==50) { meteor_=false;s.image=42; }
    if(f<100) return;
    if(f==100) {
        radius_=32;angle_=0;s.square_count=p==2 ? 2 : 1;
        speed_=p==1 ? for_rank(96,104,112,118) : for_rank(64,72,80,88);
        mdrv2_se_play(8);
    }
    if(f%8==0) {
        square_set(); // old corners, BEFORE this frame's rotation/growth
        angle_=(angle_+(p==0 ? (f<260 ? 12 : -12) : p==1 ? -12 : -6))&255;
        if(radius_<(p==1 ? 80 : 112)) radius_+=8;
        else if(p==1) {
            if(f==224) target_left_=player_get_state().x_position;
            else if(f>240) {
                int vx,vy;vector2_between(s.entity.left+64,s.entity.top+80,target_left_+8,player_center_y(),vx,vy,speed_);
                for(int i=0;i<4;++i) missiles_.add(s.corners[0][i].x,s.corners[0][i].y,vx>>4,vy>>4);
                mdrv2_se_play(6);
            }
        } else {
            Pellets.spawn_with_cloud=p==2;
            for(int i=0;i<4;++i) {
                for(int square=0;square<s.square_count;++square) {
                    const auto &corner=s.corners[square][i];
                    const int angle=p==0 && f<=280 ? iatan2(player_center_y()-corner.y,player_center_x()-4-corner.x) :
                        iatan2(corner.y-s.entity.top-80,corner.x-s.entity.left-64);
                    Pellets.add_single(corner.x,corner.y,angle,p==0 && f<=280 ? speed_/2 : speed_);
                }
                mdrv2_se_play(7);
            }
            Pellets.spawn_with_cloud=false;
        }
        square_set();s.squares_visible=true;
    }
    if(f>(p==0 ? 360 : 320)) {
        s.squares_visible=false;s.square_count=0;s.frame=0;
        if(p==2) { meteor_=true;s.image=43; }
    }
}

void MimaPhase1::hop()
{
    auto &s=state_;
    if(hop_<0) { s.image=0;meteor_=false;hop_=0;s.frame=1; }
    if(hop_==0) {
        s.entity.orb_active=false;
        if(s.frame>=10 && s.frame%4==0 && (s.frame-10)*2>=80) s.frame=0;
    } else {
        if(s.frame==4) { s.entity.left=direction_==0 ? (hop_-1)*128 : 640-hop_*128;s.entity.top=96; }
        s.entity.orb_active=s.frame>=12;
        if(s.frame>10 && s.frame%4==0) {
            const int extent=(s.frame-10)*4;
            if(extent>80) { s.frame=0;s.image=41; }
            else s.image=extent/2;
        }
    }
    if(s.frame!=0) return;
    if(hop_!=0) {
        if(hop_!=4) s.image=0;
        const int count=for_rank(5,8,10,12),speed=for_rank(30,35,40,45);
        for(int i=0,angle=0;i<count;++i,angle+=256/count)
            Pellets.add_single(s.entity.left+64,s.entity.top+80,angle,speed,PM_CHASE,speed);
    }
    if(hop_>=4) {
        s.image=41;s.darkness=0;s.flash=false;s.flash_darkness=0;
        hop_=-1;direction_=1-direction_;meteor_=true;s.frame=0;
    } else {
        s.darkness=s.darkness<12 ? s.darkness+4 : 16;
        if(s.flash) s.flash_darkness=s.flash_darkness<12 ? s.flash_darkness+4 : 16;
        ++hop_;s.frame=1;
    }
}

void MimaPhase1::hit(bool bomb_damage)
{
    auto &s=state_;
    if(boss_hit_update(s.health,s.entity,5000,s.entity.left+16,s.entity.top+48,112,64,bomb_damage)) {
        hit_clock_=0;s.flash=true;s.flash_darkness=0;
    }
    // Mima's hops depend on the source's otherwise unwanted periodic palette
    // restore. Keep it LOCAL to Mima; do not reintroduce it to approved bosses.
    if(s.health.invincible && hit_clock_<40) {
        if(hit_clock_%4==2) { s.flash=true;s.flash_darkness=0; }
        // Source immediately restores at hit frame0 as well as4/8/etc.
        else if(hit_clock_%4==0) { s.flash=false;s.darkness=0;s.flash_darkness=0; }
    } else if(hit_clock_>40) { s.flash=false;s.darkness=0;s.flash_darkness=0;hit_clock_=0; }
    if(s.hp_displayed>s.health.hp) s.hp_displayed=s.health.hp;
}

void MimaPhase1::pillars()
{
    auto &s=state_;const int f=s.frame;
    if(f==50) { meteor_=false;s.image=42; }
    if(f<100) return;
    if(f==100) {
        for(auto &p:s.pillars) p=MimaPillar{};
        // Keep the original eight calls and ranges, including the peculiar
        // rightmost expression and its unaligned logical hitbox center.
        s.pillars[0].center_x=playfield_rand_x(0.025f,0.100f);
        s.pillars[1].center_x=624-playfield_rand_x(0.0f,0.075f);
        s.pillars[2].center_x=playfield_rand_x(0.100f,0.450f);
        s.pillars[3].center_x=playfield_rand_x(0.525f,0.875f);
        s.pillars[4].center_x=playfield_rand_x(0.200f,0.450f);
        s.pillars[5].center_x=playfield_rand_x(0.525f,0.775f);
        s.pillars[6].center_x=playfield_rand_x(0.100f,0.450f);
        s.pillars[7].center_x=playfield_rand_x(0.525f,0.875f);
        mdrv2_se_play(8);
    }
    if(f<=240 && f%20==0) mdrv2_se_play(12);
    if(f==240) { meteor_=true;s.image=43; }
    bool done=true;
    for(int i=0;i<8;++i) {
        auto &p=s.pillars[i];
        if(f%8==i && f>100+i*20) {
            if(p.time>32) p.time-=8;
            if(p.time>32) { p.radius=p.time;p.ray=true; }
            else if(p.time>16) {
                if(p.time==32) mdrv2_se_play(7);
                p.radius=0;p.ray=false;p.height=16*(34-p.time);
                // Source tests only on each pillar's presentation cadence,
                // strictly inside the unaligned center's48px horizontal band.
                const int x=player_get_state().x_position;
                if(!player_is_invincible() && x>p.center_x-40 && x<p.center_x+8) player_is_hit=true;
                p.time-=8;
            } else if(p.time!=-9999) {
                Pellets.add_group(s.entity.left+64,s.entity.top+80,PG_3_SPREAD_WIDE_AIMED,36);
                p.height=0;p.time=-9999;
            }
        }
        if(p.time!=-9999) done=false;
    }
    if(done) s.frame=0;
}

void MimaPhase1::second_squares()
{
    auto &s=state_;const int f=s.frame,p=s.pattern;
    if(p==3 && f==50) { meteor_=false;s.image=42; }
    if(f<100) return;
    if(f==100) {
        radius_=32;angle_=0;s.square_count=1;
        firing_angle_=p==2 ? 128 : 0;
        speed_=p==1 ? for_rank(4,4,5,5) : p==2 ? for_rank(32,40,48,56) : for_rank(50,54,58,62);
        mdrv2_se_play(8);
    }
    if(f%8==0) {
        square_set(); // missiles/spray retain the old-corner quirk
        angle_=(angle_+(p==3 ? 3 : p==2 && f>270 ? 12 : -12))&255;
        if(radius_<80) radius_+=8;
        else if(p==1 && f>180 && f%16==8) {
            int vx,vy;vector2(vx,vy,speed_,firing_angle_);
            for(int i=0;i<4;++i) missiles_.add(s.corners[0][i].x,s.corners[0][i].y,vx,vy);
            mdrv2_se_play(6);firing_angle_=(firing_angle_+13)&255;
        } else if(p==2 && f>180) {
            firing_angle_=(firing_angle_+(f>270 ? 12 : -12))&255;
            for(int i=0;i<4;++i) Pellets.add_single(s.corners[0][i].x,s.corners[0][i].y,firing_angle_,speed_);
            mdrv2_se_play(6);
        }
        square_set();s.squares_visible=true;
    }
    // Unlike the missile/spray patterns, these use the newly rotated corners.
    // Only slots0..3 are attempted; live slots refuse replacement.
    if(p==3 && f>180 && f<300) {
        const int i=f%4;
        shootout_laser_spawn(s.lasers[i],s.corners[0][i].x,s.corners[0][i].y,
            player_center_x()-2,player_get_state().y_position+PLAYER_H,speed_,20,4);
    }
    if(f>(p==1 ? 340 : p==2 ? 370 : 300)) {
        s.squares_visible=false;s.square_count=0;s.frame=0;
    }
}

void MimaPhase1::update_lasers()
{
    if(!state_.checkpoint) for(auto &laser:state_.lasers) shootout_laser_update(laser);
}

void MimaPhase1::clear_defeat_projectiles()
{
    if(state_.phase!=3 || !state_.checkpoint) return;
    missiles_.reset();for(auto &laser:state_.lasers) laser={};
}

void MimaPhase1::update(bool bomb_damage)
{
    auto &s=state_;if(s.checkpoint) return;
    if(missiles_.update(++gameplay_frame_)) player_is_hit=true;
    particles();++s.frame;++hit_clock_;
    if(s.phase==2) {
        if(s.frame>10 && s.frame%4==0) {
            const int extent=(s.frame-10)*2;
            if(extent>80) {
                s.image=41;s.frame=0;s.phase=3;s.entity.orb_active=true;
                hit_clock_=0;s.checkpoint=!second_block_;
            }
            else s.image=extent/2;
        }
        return;
    }
    if(s.phase==3) {
        if(meteor_ && s.frame%8==0) s.image=43+(s.frame/8)%4;
        if(s.pattern==0) pillars();else second_squares();
        if(s.frame==0) s.pattern=(s.pattern+1)%4;
        hit(bomb_damage);
        // The caller owns defeat presentation, results and progression.
        if(s.health.hp<=0) s.checkpoint=true;
        return;
    }
    if(s.hp_displayed<s.health.hp) ++s.hp_displayed;
    if(meteor_ && s.frame%8==0) s.image=43+(s.frame/8)%4;
    if(s.pattern<3) squares();else if(s.pattern==3) hop();
    if(s.frame==0) s.pattern=s.health.hp<=6 ? 99 : (s.pattern+1)%4;
    hit(bomb_damage);
    if(s.pattern==99 && !s.health.invincible) {
        s.phase=2;s.frame=0;hit_clock_=0;s.pattern=0;s.image=0;
        s.entity.left=256;s.entity.top=120;s.entity.orb_active=false;
    }
}
