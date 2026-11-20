#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include "elis_render.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/bullet/pellet.hpp"
#include "main/stage/stageobj.hpp"
#include "main/random.hpp"

namespace {
ElisPhase1State &state(ElisPhase1 &boss) {return const_cast<ElisPhase1State&>(boss.state());}
void start_attack(ElisPhase1 &boss,int id)
{
    boss.reset(true);auto &s=state(boss);s.phase=3;s.pattern=id;s.health.hp=s.hp_displayed=10;
    s.attack_started=true;s.image=5;Pellets.init();
}
void return_and_summon()
{
    ElisPhase1 boss;boss.reset(true);auto &s=state(boss);s.health.hp=10;s.pattern=0;s.frame=80;
    s.entity.left=50;s.entity.top=150;boss.update();assert(s.phase==2 && !s.checkpoint && !s.frame);
    initialize_irand(307);const auto seed=irand_state();for(int i=0;i<200;++i) irand();const int next=irand();irand_restore(seed);
    const int score=stage_score;
    for(int f=1;f<=81;++f) {
        boss.update(true);assert(s.health.hp==10 && stage_score==score && !s.health.invincible);
        assert(s.entity.left==(f<52 ? 50 : 256) && s.entity.top==(f<52 ? 150 : 96));
        assert(s.phase==(f<81 ? 2 : 3));
    }
    assert(irand()==next && !s.frame && s.pattern==1 && !s.teleport_done && !s.attack_started);
    for(int f=1;f<=132;++f) {
        boss.update();assert(s.hexagram==(f>=81 && f<132) && !Pellets.alive_count());
        assert(s.image==(f<10 ? 0 : 5));
        if(f>=10 && f<72) assert(s.circle==ElisPhase1State::Summon && s.angle==((f-10)/2+1)*2);
        if(f>=72 && f<132) {
            const int counter=f-71;
            const int color=counter>=24 && counter%4<2 ? 4 : 7;
            assert(s.circle==ElisPhase1State::Full && s.circle_color==color);
        }
    }
    assert(s.attack_started && !s.frame && !s.hexagram && s.circle==ElisPhase1State::None);
}
struct Shot {int x,y,angle,speed;};
void check_shots(const Shot *shots,int count)
{
    assert(Pellets.alive_count()==std::min(count,100));
    for(int i=0;i<std::min(count,100);++i) {
        const auto &p=Pellets.at(i);const auto &q=shots[i];const int speed=std::max(16,q.speed+base_speed_for_rank());
        assert(p.cur_left.to_pixel()==q.x && p.cur_top.to_pixel()==q.y && p.angle==q.angle);
        assert(p.velocity.x.v==polar_x_fast(0,speed,q.angle) && p.velocity.y.v==polar_y_fast(0,speed,q.angle));
    }
}
void attacks()
{
    for(int r=0;r<4;++r) {
        rank=static_cast<rank_t>(r);ElisPhase1 boss;start_attack(boss,1);auto &s=state(boss);
        const int ring=14+r*2;Shot shots[140]{};int count=0;
        const int frames[]={10,16,24,32,40},speeds[]={48,54,64,72,80};
        for(int f=1;f<=61;++f) {
            boss.update();for(int k=0;k<5;++k) if(f==frames[k])
                for(int i=0;i<ring;++i) shots[count++]={316,140,(256/ring)*i+k*2,speeds[k]};
            check_shots(shots,count);
            assert(s.image==(f<8 ? 5 : 6) && s.attack_overlay==(f%8>=4));
        }
        assert(!s.pattern && !s.frame && !s.attack_started);

        start_attack(boss,2);s.entity.left=r%2 ? 400 : 100;s.entity.top=199;initialize_irand(137);
        const auto seed=irand_state();ElisClusterSphere spheres[10]{};
        const int column=((s.entity.left<256 ? 608 : 0)-s.entity.left)/10,cluster=5+(r==3 ? 8 : r*2);
        for(int i=0;i<10;++i) spheres[i]={s.entity.left+48+(i+1)*column,231+(i+1)*((irand()%200+1)/10),false,false};
        count=0;for(const auto &p:spheres) for(int i=0;i<cluster;++i) {
            const int angle=irand()&127,speed=(unsigned(irand())+24u)&63u;shots[count++]={p.x,p.y,angle,speed};
        }
        const int next=irand();irand_restore(seed);
        for(int f=1;f<=251;++f) {
            boss.update();check_shots(shots,std::min(10,std::max(0,(f-100)/10))*cluster);
            for(int i=0;i<10;++i) {
                const auto &p=s.spheres[i];assert(p.visible==(f>=10*(i+1) && f<140+i*10));
                if(f>=10*(i+1)) assert(p.x==spheres[i].x && p.y==spheres[i].y && p.fired==(f>=110+i*10));
            }
        }
        assert(irand()==next && !s.pattern && !s.frame && !s.attack_started);

        start_attack(boss,3);initialize_irand(443);const auto rift_seed=irand_state();ElisRift rifts[5]{};
        const int interval=r==0 ? 6 : r==1 ? 3 : 2;count=0;
        for(int f=1;f<=221;++f) {
            if(f>60 && f<200 && (f>=120 || f%interval==0)) {
                const int index=irand()%5;int angle=irand()%21+54;
                if(f>=120) {const int sign=(irand()&1) ? 1 : -1;const unsigned wrapped=(256+sign*(f-120))%256;angle=(angle*(wrapped/2))%256;}
                shots[count++]={rifts[index].x,rifts[index].y,angle,96};
            }
            if(f>=60 && f<=200 && f%4==0) for(int i=0;i<5;++i) {
                if(f==60 || f%16==(i*4)%16) {rifts[i].x=224+irand()%192;rifts[i].y=64+irand()%128;}
                if(f<200) rifts[i].cel=4+irand()%2;
            }
        }
        const int after_rifts=irand();irand_restore(rift_seed);
        for(int f=1;f<=221;++f) {boss.update();if(f==120) assert(Pellets.at(count-80).angle==0);}
        check_shots(shots,count);assert(irand()==after_rifts && !std::memcmp(s.rifts,rifts,sizeof(rifts)));
        assert(!s.rifts_visible && !s.pattern && !s.frame && !s.attack_started);
    }
    rank=RANK_NORMAL;Pellets.init();
}
void gate()
{
    ElisPhase1 boss;start_attack(boss,1);auto &s=state(boss);s.pattern=0;s.frame=75;s.health.hp=7;
    boss.update(true);for(int i=0;i<5;++i) boss.update();assert(s.teleport_done && s.health.hp==6 && !s.checkpoint);
    s.frame=80;s.teleport_done=false;s.health.invincible=false;boss.update();assert(s.checkpoint && s.phase==3);
    const auto frozen=s;boss.update(true);boss.update_lasers();assert(!std::memcmp(&s,&frozen,sizeof(s)));
}
void hexagram_pixels()
{
    ElisView view;camera.snap_center();video_stage_fade_out();assert(view.begin());
    ElisPhase1State s{};s.phase=3;s.image=-1;s.hexagram=true;MissilePool empty;
    static u16 expected[49152];
    const int pairs[][2]={{192,22},{192,106},{22,106},{64,150},{64,234},{150,234}};
    for(int pan:{0,32,64}) for(int color:{4,7}) {
        camera.left=pan;s.circle_color=color;std::fill(std::begin(expected),std::end(expected),0);
        const u16 ink=color==7 ? 0xffff : 0x8000|RGB15(14,19,23);
        auto put=[&](int x,int y) {x=x/2-pan;y=y/2-8;if(x>=0 && x<256 && y>=24 && y<192) expected[y*256+x]=ink;};
        for(const auto &pair:pairs) {
            int x0=polar_x_fast(320,128,pair[0]),y0=polar_y_fast(144,128,pair[0]);
            int x1=polar_x_fast(320,128,pair[1]),y1=polar_y_fast(144,128,pair[1]);
            if(x0>x1) {std::swap(x0,x1);std::swap(y0,y1);}
            if(y0==y1) {for(int x=x0;x<=x1;++x) put(x,y0);continue;}
            const int dx=x1-x0,dy=std::abs(y1-y0),major=std::max(dx,dy),minor=std::min(dx,dy),sign=y1>y0 ? 1 : -1;
            const int last_byte=x1/8;
            for(int i=0;i<=major;++i) {
                const int advance=(i*minor+(major-1)/2)/major;
                const int x=x0+(dx>dy ? i : advance),y=y0+sign*(dx>dy ? advance : i);
                if(x/8!=last_byte || y!=y1) put(x,y); // omitted final cache group
            }
        }
        canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,empty));
        assert(std::equal(std::begin(expected),std::end(expected),g_canvas_target));
        canvas_playfield_clip(false);canvas_buffer_prepare();REG_VCOUNT=192;assert(view.present());
    }
    s.hexagram=false;s.circle=ElisPhase1State::Summon;s.angle=2;s.radius=128;
    camera.left=0;canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,empty));
    assert(g_canvas_target[64*256+224]==0xffff); //phase3 DOES draw quadrant0
    canvas_playfield_clip(false);video_stage_fade_out();view.end();video_stage_reveal();camera.snap_center();
}
}
void test_elis_phase3()
{
    player_initialize();shots_initialize();orb_initialize();orb_set_position(0,300);pellet_speed=0;Pellets.init();
    return_and_summon();attacks();gate();hexagram_pixels();player_initialize();orb_initialize();shots_initialize();Pellets.init();
    std::puts("Elis second block: fixed-center81-frame/no-hit/RNG return,132-frame star gate, all-rank rings/clusters/byte-wrapped rifts,6HP grace gate, native hexagram pixels/colors and pose overlays passed");
}
