#include "elis_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/random.hpp"
#include "sound_effects.hpp"

namespace {
int for_rank(int easy,int normal,int hard,int lunatic)
{
    const int values[]={easy,normal,hard,lunatic};return values[int(rank)>=0 && int(rank)<4 ? int(rank) : 1];
}
}
void ElisPhase1::star_summon()
{
    auto &s=state_;const int f=s.frame;
    if(f<5) {circle_frame_=0;s.angle=0;}
    if(f<10) return;
    if(f==10) {s.image=5;s.attack_overlay=false;s.angle=0;circle_frame_=0;}
    if(f%2==0 && !circle_frame_) {
        if(f==10) mdrv2_se_play(SFX_BOMB_START);
        s.circle=ElisPhase1State::Summon;s.radius=128;s.step=1;s.angle+=2;
        if(s.angle>=64) {circle_frame_=1;s.circle=ElisPhase1State::Full;s.circle_color=7;}
    } else if(s.angle>=64) {
        ++circle_frame_;
        if(circle_frame_==10) s.hexagram=true;
        if(circle_frame_>20 && circle_frame_%4==0) s.circle_color=4;
        else if(circle_frame_>20 && circle_frame_%4==2) s.circle_color=7;
        if(circle_frame_>60) {
            s.hexagram=false;s.circle=ElisPhase1State::None;s.frame=s.angle=circle_frame_=0;s.attack_started=true;
        }
    }
}
void ElisPhase1::attack_render()
{
    auto &s=state_;
    if(s.frame%8==0) {s.image=6;s.attack_overlay=false;}
    // C_ATTACK_2 is PUT over the previous image without an unput. In the
    // first four updates that previous image is C_PREPARE, not C_ATTACK_1.
    else if(s.frame%8==4) s.attack_overlay=true;
}
void ElisPhase1::stacked_rings()
{
    auto &s=state_;const int f=s.frame;
    int offset=0,speed=0;
    if(f==10) {parameter_=for_rank(14,16,18,20);speed=48;}
    else if(f==16) {offset=2;speed=54;}
    else if(f==24) {offset=4;speed=64;}
    else if(f==32) {offset=6;speed=72;}
    else if(f==40) {offset=8;speed=80;}
    else if(f>60) {s.frame=0;s.pattern=0;}
    if(speed) for(int i=0;i<parameter_;++i)
        Pellets.add_single(s.entity.left+60,s.entity.top+44,(256/parameter_)*i+offset,speed);
    // Do not reproduce the source page1 unput bug that tears holes in Elis.
}
void ElisPhase1::sphere_clusters()
{
    auto &s=state_;const int f=s.frame;
    if(f<=100 && f%10==0) {
        const int i=f/10;auto &sphere=s.spheres[i-1];
        const int column=((s.entity.left<256 ? 608 : 0)-s.entity.left)/10;
        sphere.x=i*column+s.entity.left+48;
        // The random row height is evaluated separately for EVERY sphere.
        sphere.y=i*((irand()%200+200-s.entity.top)/10)+s.entity.top+32;
        sphere.visible=true;sphere.fired=false;
    } else if(f>100 && f<=230 && f%10==0) {
        if(f<=200) {
            auto &sphere=s.spheres[(f-100)/10-1];sphere.fired=true;mdrv2_se_play(SFX_ATTACK_7);
            const int count=for_rank(5,7,9,13);
            for(int i=0;i<count;++i) {
                const int angle=irand()&127,speed=(unsigned(irand())+24u)&63u;
                Pellets.add_single(sphere.x,sphere.y,angle,speed);
            }
        }
        if(f>130) s.spheres[(f-130)/10-1].visible=false;
    } else if(f>250) {s.frame=0;s.pattern=0;}
}
void ElisPhase1::rotating_rifts()
{
    auto &s=state_;const int f=s.frame;
    if(f==30) parameter_=for_rank(6,3,2,2);
    if(f>60 && f<120 && f%parameter_==0) {
        const int i=irand()%5,angle=irand()%21+54;
        Pellets.add_single(s.rifts[i].x,s.rifts[i].y,angle,96);
    } else if(f>=120 && f<200) {
        const int i=irand()%5,angle=irand()%21+54;
        // Source converts to unsigned BYTE before dividing, then MULTIPLIES
        // the base angle. Negative offsets are not signed division/addition.
        const unsigned char offset=(irand()&1) ? f-120 : -(f-120);
        const unsigned char rotated=angle*(offset/2);
        Pellets.add_single(s.rifts[i].x,s.rifts[i].y,rotated,96);
    }
    rifts_refresh(200); // source fires before relocating/recoloring the rifts
    if(f>220) {s.frame=0;s.pattern=0;}
}
