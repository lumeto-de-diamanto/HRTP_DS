// Compact source controller + GPU oracle; shared projectiles retain their own
// existing tests. No exhaustive practice/checkpoint cross-product.
#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "elis_render.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "heap_probe.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/bullet/pellet.hpp"
#include "main/stage/stageobj.hpp"
#include "main/random.hpp"
#include "sprites/BossArrow.h"

namespace {
ElisPhase1State &state(ElisPhase1 &boss) {return const_cast<ElisPhase1State&>(boss.state());}
void patterns()
{
    player_initialize();orb_initialize();orb_set_position(0,300);shots_initialize();
    for(int r=0;r<4;++r) {
        rank=static_cast<rank_t>(r);initialize_irand(91);const int direction=irand()%2;initialize_irand(91);
        ElisPhase1 boss;boss.reset();
        for(int f=1;f<=170;++f) {
            boss.update();const auto &s=boss.state();
            assert(s.health.hp==14 && s.hp_displayed==std::min(14,f));
            assert(s.image==(f<50 || f>=150 ? 0 : 1));
            if(f>=70 && f%10==0) {
                const auto &laser=s.lasers[f/10%10];assert(laser.alive && laser.age==0 && laser.width==4);
                assert(laser.speed==(r<2 ? 6 : r==2 ? 7 : 8) && laser.moveout_age==25);
                const int target=direction==0 ? (f-70)/10*64 : 640-(f-70)/10*64;
                assert(laser.step_x==Cos8(iatan2(400-124,target-316)) && laser.step_y==Sin8(iatan2(400-124,target-316)));
            }
            boss.update_lasers();
        }
        assert(boss.state().pattern==0 && boss.state().frame==0); //11, not10 beams

        boss.reset();state(boss).pattern=2;int shots=0;
        for(int f=1;f<=171;++f) {
            const auto previous=boss.state();boss.update();const auto &s=boss.state();
            if(f>=60 && f<160) assert(s.rifts_visible);
            if(f==160) assert(!s.rifts_visible);
            if(f>=60 && f<=160 && f%4==0) for(int i=0;i<5;++i) {
                const auto &p=s.rifts[i];
                assert(p.x>=224 && p.x<416 && p.y>=64 && p.y<192 && p.cel>=4 && p.cel<=5);
                if(f!=60 && f%16!=(i*4)%16) assert(p.x==previous.rifts[i].x && p.y==previous.rifts[i].y);
            }
            if(f>60 && f%3==0) {
                ++shots;bool found=false;
                for(int i=0;i<MISSILE_COUNT;++i) {
                    const auto &m=boss.missiles().at(i);
                    if(m.flag==1 && m.previous_x==-1 && !m.visible) {
                        for(const auto &p:previous.rifts) found|=m.x==p.x*16 && m.y==p.y*16;
                        assert(m.vy>0 && m.vy<=7*16);
                    }
                }
                assert(found); // pre-update rift, including the final frame171
            }
        }
        assert(shots==37 && boss.state().frame==0 && boss.state().pattern==0 && boss.missiles().alive_count()>0);

        boss.reset();state(boss).pattern=3;Pellets.init();
        for(int f=1;f<=161;++f) {boss.update();assert(!Pellets.alive_count());}
        assert(boss.state().circle==ElisPhase1State::Full && boss.state().circle_color==4);
        boss.update();assert(boss.state().frame==0 && boss.state().pattern==0 && boss.state().circle==ElisPhase1State::None);
        const int counts[]={23,23,69,100},stride=r==2 ? 3 : r==3 ? 5 : 1;
        assert(Pellets.alive_count()==counts[r]);
        int slot=0;
        for(int angle=0;angle<256 && slot<counts[r];angle+=8) {
            const int x=polar_x_fast(316,128,angle),y=polar_y_fast(144,128,angle);
            if(x>=640 || x< -8 || y<56 || y>400) continue;
            for(int member=0;member<stride && slot<counts[r];++member,++slot) {
                const auto &p=Pellets.at(slot);
                assert(p.cur_left.to_pixel()==x && p.cur_top.to_pixel()==y); //eye X, not center320
            }
        }
        assert(slot==counts[r]);
    }
    rank=RANK_NORMAL;Pellets.init();
}
void circle_allocation()
{
    const int saved_speed=pellet_speed;
    player_initialize();orb_set_position(10000,10000);shots_initialize();pellet_speed=0;
    const pellet_group_t groups[]={PG_1_AIMED,PG_1_RANDOM_NARROW_AIMED,PG_3_SPREAD_WIDE_AIMED,PG_5_SPREAD_NARROW_AIMED};
    for(int difficulty=0;difficulty<4;++difficulty) for(auto position:{ElisPoint{19,197},ElisPoint{0,199},ElisPoint{511,64}})
        for(int occupied:{0,97,100}) {
            rank=rank_t(difficulty);ElisPhase1 boss;boss.reset();auto &s=state(boss);
            s.pattern=3;s.entity.left=position.x;s.entity.top=position.y;
            Pellets.init();for(int tick=1;tick<=161;++tick) boss.update();
            for(int i=0;i<occupied;++i) Pellets.add_group(320,300,PG_1,16);
            CPellets expected=Pellets;
            initialize_irand(713);const auto before=irand_state();
            for(int angle=0;angle<256;angle+=8) {
                const int x=polar_x_fast(position.x+60,128,angle),y=polar_y_fast(position.y+48,128,angle);
                if(x< -8 || x>=640 || y<56 || y>400) continue;
                expected.add_group(x,y,groups[difficulty],4);
            }
            const auto rng=irand_state();irand_restore(before);boss.update();const auto actual_rng=irand_state();
            assert(!std::memcmp(&rng,&actual_rng,sizeof(rng)) && Pellets.alive_count()==expected.alive_count());
            for(int i=0;i<expected.alive_count();++i) {
                const auto &a=Pellets.at(i),&b=expected.at(i);
                assert(a.moving && a.cur_left.v==b.cur_left.v && a.cur_top.v==b.cur_top.v);
                assert(a.velocity.x.v==b.velocity.x.v && a.velocity.y.v==b.velocity.y.v && a.angle==b.angle && a.from_group==b.from_group);
            }
        }
    // No forged position or pattern: first sweep -> seeded teleport -> circle.
    rank=RANK_LUNATIC;initialize_irand(516);ElisPhase1 boss;boss.reset();Pellets.init();
    for(int tick=0;tick<252;++tick) {boss.update();boss.update_lasers();}
    assert(boss.state().pattern==3 && boss.state().entity.left==19 && boss.state().entity.top==197);
    for(int tick=0;tick<161;++tick) {boss.update();boss.update_lasers();}
    assert(Pellets.alive_count()==100);Pellets.update(true);assert(Pellets.alive_count()==100);
    rank=RANK_NORMAL;pellet_speed=saved_speed;Pellets.init();
    std::puts("Elis circle allocation: source-filtered angular ordering/velocities/RNG on all ranks and edges, partial/full100-slot pools and natural seed516 volley survive first movement passed");
}
void teleport_and_hit()
{
    ElisPhase1 boss;boss.reset();auto &s=state(boss);s.pattern=0;
    initialize_irand(713);auto rng=irand_state();
    struct Frame {int x,y;ElisPoint stars[5];};Frame expected[81]{};int x=256,y=96;ElisPoint stars[5]{};
    for(int f=1;f<=81;++f) {
        const int tx=irand()%512,ty=64+irand()%136;if(f==52) {x=tx;y=ty;}
        if(f<=80 && f%4==0) for(auto &p:stars) {
            const int sx=x+irand()%192-32,sy=y+irand()%128-32;
            if(f<40 || f>52) p={sx,sy};else {p.x+=(sx-p.x)/3;p.y+=(sy-p.y)/3;}
        }
        expected[f-1].x=x;expected[f-1].y=y;std::copy(stars,stars+5,expected[f-1].stars);
    }
    const int next=irand();irand_restore(rng);
    for(int f=1;f<=81;++f) {
        boss.update();const auto &e=expected[f-1];assert(s.entity.left==e.x && s.entity.top==e.y);
        assert(!std::memcmp(s.stars,e.stars,sizeof(s.stars)));
        assert(s.entity.orb_active==(f<20 || f>=76));
        const int image=f<20 || f>=76 ? 0 : f<28 || f>=68 ? 2 : f<36 || f>=60 ? 3 : f<44 || f>=52 ? 4 : -1;
        assert(s.image==image);
    }
    assert(irand()==next && s.teleport_done && s.frame==0);
    // Explicitly exercise CHOOSE_NEW's0 roll and its extra selector frame.
    for(unsigned seed=0;;++seed) {initialize_irand(seed);if(irand()%4==0) {initialize_irand(seed);break;}}
    boss.update();assert(!s.teleport_done && s.pattern==0 && s.frame==1);
    boss.update();assert(s.frame==2);

    boss.reset();orb_set_position(264,84);const int score=stage_score;
    boss.update();assert(s.health.hp==13 && stage_score==score+7000 && !s.flash);
    boss.update();assert(s.health.hp==13 && s.health.invincibility_frame==1);
    boss.update();assert(s.flash);
    orb_set_position(0,300);for(int i=0;i<39;++i) boss.update();assert(!s.health.invincible && !s.flash);
    s.entity.orb_active=false;shots_add(288,128);boss.update();assert(s.health.hp==13); //ofuda contact only
    s.health.hp=10;s.pattern=0;s.frame=80;boss.update();assert(s.checkpoint);
    const auto frozen=s;boss.update(true);boss.update_lasers();assert(!std::memcmp(&frozen,&s,sizeof(s)));
    boss.reset();s.pattern=0;s.frame=75;s.health.hp=11;boss.update(true);
    for(int i=0;i<5;++i) boss.update();
    assert(s.teleport_done && !s.checkpoint && s.health.hp==10 && s.health.invincible);
    assert(boss_bomb_damage_allowed(true,0,50) && !boss_bomb_damage_allowed(true,1,50) && !boss_bomb_damage_allowed(true,0,51));
}
void combat_pixels()
{
    std::ifstream file("elis-combat-expected.bin",std::ios::binary);assert(file.good());
    const std::vector<u8> source{std::istreambuf_iterator<char>(file),{}};
    const auto blocks=test_heap_count,bytes=test_heap_bytes,objects=test_obj_attempts;
    const std::vector<u16> actors(SPRITE_PALETTE,SPRITE_PALETTE+256);
    ElisView view;video_stage_fade_out();camera.left=0;assert(view.begin());const auto heap=test_heap_attempts;
    MissilePool empty;ElisPhase1State s{};
    for(int image=0;image<8;++image) {
        s.image=image;s.entity.left=image%2 ? 472 : 0;s.entity.top=96;s.flash=image%2;
        camera.left=image%2 ? 64 : 0;canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,empty));
        for(int y=0;y<48;++y) for(int x=0;x<64;++x) {
            const int index=source[48+image*3072+y*64+x],px=camera.x(s.entity.left)+x;
            if(px<0 || px>=256) continue;
            const int c=index-1;
            const u16 color=s.flash && (c==2 || c==3 || c==6 || c==8) ? RGB15(31,31,31) :
                index ? RGB15(source[c*3]*17/8,source[c*3+1]*17/8,source[c*3+2]*17/8) : 0;
            assert(g_canvas_target[(40+y)*256+px]==(index ? 0x8000|color : 0));
        }
        canvas_playfield_clip(false);canvas_buffer_prepare();REG_VCOUNT=80;assert(!view.present());
        REG_VCOUNT=192;assert(view.present());assert(std::equal(g_canvas_target,g_canvas_target+49152,BG_BMP_RAM(16)));
    }
    // C_ATTACK_2 overlays prepare/attack1; cache transitions must not replace
    // the underlying transparent regions or alias the circle/ray scratch.
    camera.left=32;s.entity.left=256;s.flash=false;
    for(int pose:{5,6,-6,6,0,1}) {
        s.image=pose<0 ? -pose : pose;s.attack_overlay=pose>=0;
        canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,empty));
        for(int y=0;y<48;++y) for(int x=0;x<64;++x) {
            const int at=y*64+x,overlay=source[48+7*3072+at];
            const int index=s.attack_overlay && overlay ? overlay : source[48+s.image*3072+at],c=index-1;
            const u16 ink=index ? 0x8000|RGB15(source[c*3]*17/8,source[c*3+1]*17/8,source[c*3+2]*17/8) : 0;
            assert(g_canvas_target[(40+y)*256+96+x]==ink);
        }
        canvas_playfield_clip(false);canvas_buffer_prepare();assert(view.present());
    }
    s.attack_overlay=false;
    auto render=[&]() {
        canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,empty));
        canvas_playfield_clip(false);canvas_buffer_prepare();REG_VCOUNT=192;assert(view.present());
    };
    s.health.reset(14);s.image=0;s.entity.left=0;camera.left=64;render();
    assert(!test_objs[OAM_SLOT_BOSS_ARROW].hidden && test_objs[OAM_SLOT_BOSS_ARROW].hflip);
    assert(!std::memcmp(test_objs[OAM_SLOT_BOSS_ARROW].gfx,BossArrow_tiles,64));
    s.entity.left=512;camera.left=0;render();assert(!test_objs[OAM_SLOT_BOSS_ARROW].hidden && !test_objs[OAM_SLOT_BOSS_ARROW].hflip);
    s.image=-1;render();assert(test_objs[OAM_SLOT_BOSS_ARROW].hidden);
    s.image=2;render();assert(!test_objs[OAM_SLOT_BOSS_ARROW].hidden); //visible warp, not a vanished body
    s.entity.left=256;s.image=0;render();assert(test_objs[OAM_SLOT_BOSS_ARROW].hidden);
    s.bat_form=true;s.image=-1;s.bat.left=552;s.bat.top=128;s.bat_draw_x=555;s.bat_draw_y=128;camera.left=64;
    for(int cel=0;cel<3;++cel) {
        s.bat_shown=cel;render();
        for(int y=0;y<16;++y) for(int x=0;x<24;++x) {
            const int index=source[48+24576+1296+cel*384+y*24+x],c=index-1;
            const u16 ink=index ? 0x8000|RGB15(source[c*3]*17/8,source[c*3+1]*17/8,source[c*3+2]*17/8) : 0;
            assert(g_canvas_target[(56+y)*256+212+x]==ink);
        }
    }
    camera.left=0;render();assert(!test_objs[OAM_SLOT_BOSS_ARROW].hidden && !test_objs[OAM_SLOT_BOSS_ARROW].hflip);
    s.bat_shown=-1;render();assert(test_objs[OAM_SLOT_BOSS_ARROW].hidden);
    s.bat_shown=0;s.bat.left=s.bat_draw_x=48;s.bat.top=s.bat_draw_y=160;camera.left=64;render();
    assert(!test_objs[OAM_SLOT_BOSS_ARROW].hidden && test_objs[OAM_SLOT_BOSS_ARROW].hflip && test_objs[OAM_SLOT_BOSS_ARROW].y==73);
    {ElisView other;assert(!other.begin());}assert(!test_objs[OAM_SLOT_BOSS_ARROW].hidden); //failed lease cannot hide owner marker
    s.bat_form=false;s.bat_shown=-1;s.entity.left=256;s.entity.top=96;
    s.image=-1;s.rifts_visible=s.stars_visible=true;camera.left=0;
    static u16 expected[49152];
    auto color=[&](int index) -> u16 {
        return s.flash && (index==2 || index==3 || index==6 || index==8) ? 0xffff :
            0x8000|RGB15(source[index*3]*17/8,source[index*3+1]*17/8,source[index*3+2]*17/8);
    };
    auto mask=[&](int left,int top,int size,int offset,int index) {
        const u16 ink=color(index);
        for(int y=0;y<size;++y) for(int x=0;x<size;++x) if(source[offset+y*size+x])
            expected[(Camera::y(top)+y)*256+camera.x(left)+x]=ink;
    };
    for(int i=0;i<5;++i) {
        s.rifts[i]={35+i*80,128,4+i%2};s.stars[i]={20+i*80,96};
    }
    s.spheres[0]={80,176,true,false};s.spheres[1]={160,176,true,true};
    for(bool flash:{false,true}) {
        s.flash=flash;std::fill(std::begin(expected),std::end(expected),0);
        for(int i=0;i<5;++i) {
            mask(s.rifts[i].x&~7,128,16,48+24576+(s.rifts[i].cel-1)*256,4);
            mask(s.stars[i].x,96,4,48+24576+1280,2);
        }
        for(const auto &p:s.spheres) if(p.visible) {
            mask(p.x&~7,p.y,16,48+24576+2*256,4);
            if(p.fired) mask(p.x&~7,p.y,16,48+24576+3*256,4);
        }
        canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,empty));
        assert(std::equal(std::begin(expected),std::end(expected),g_canvas_target));
        canvas_playfield_clip(false);canvas_buffer_prepare();assert(view.present());
    }
    s.flash=false;
    for(auto &p:s.spheres) p.visible=false;
    s.rifts_visible=s.stars_visible=false;s.circle=ElisPhase1State::Summon;s.radius=128;
    s.entity.left=256;s.entity.top=164;camera.left=32; //whole circle visible, including all four quadrants
    for(int angle:{2,16,32,62}) {
        s.angle=angle;std::fill(std::begin(expected),std::end(expected),0);
        for(int q=0;q<4;++q) for(int a=0;a<angle;a+=2) {
            // Each short native arc omits its final byte/y cache group. Derive
            // the retained dots from that endpoint, not the renderer's cache.
            const int last=a+q*64+2,lx=polar_x_fast(320,128,last),ly=polar_y_fast(212,128,last);
            for(int t=last-2;t<=last;++t) {
                const int x=polar_x_fast(320,128,t),y=polar_y_fast(212,128,t);
                if(x/8!=lx/8 || y!=ly) expected[Camera::y(y)*256+camera.x(x)]=color(7);
            }
        }
        canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,empty));
        assert(std::equal(std::begin(expected),std::end(expected),g_canvas_target));
        assert(g_canvas_target[98*256+192]==color(7)); //previously missing right-hand quadrant
        if(angle>=16) {
            int quadrants[4]{};
            for(int y=0;y<192;++y) for(int x=0;x<256;++x) if(g_canvas_target[y*256+x])
                ++quadrants[(y<98 ? 2 : 0)+(x<128 ? 1 : 0)];
            for(int count:quadrants) assert(count>0);
        }
        canvas_playfield_clip(false);canvas_buffer_prepare();assert(view.present());
    }
    camera.left=0;s.entity.top=96;s.angle=2;s.circle=ElisPhase1State::Sweep;s.radius=32;s.step=2;s.circle_color=7;
    for(int i=0;i<3;++i) {
        const int ink=i==1 ? 7 : 4;
        if(i) {s.circle=ElisPhase1State::Full;s.radius=128;s.step=1;s.circle_color=ink;}
        canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,empty));
        int count=0;for(int p=0;p<49152;++p) if(g_canvas_target[p]) {++count;assert(g_canvas_target[p]==color(ink));}
        assert(count>0);canvas_playfield_clip(false);canvas_buffer_prepare();assert(view.present());
    }
    // Dense sweep: optimized scratch-mask output must match every visited
    // native dot, including a colored circle underneath, odd camera pans,
    // varied widths, and clipping. The two mask planes must not alias/recolor.
    static u16 colored_circle[49152];std::copy(g_canvas_target,g_canvas_target+49152,colored_circle);
    s.circle=ElisPhase1State::None;
    for(int i=0;i<10;++i) {
        assert(shootout_laser_spawn(s.lasers[i],316,124,i*71,400,60,25,1+i%8));
        s.lasers[i].length=240;
    }
    for(int pan:{0,1,32,64}) {
        camera.left=pan;std::fill(std::begin(expected),std::end(expected),0);
        s.circle=pan==0 ? ElisPhase1State::Full : ElisPhase1State::None;
        if(pan==0) std::copy(std::begin(colored_circle),std::end(colored_circle),expected);
        auto reference=[](int left,int y,uint16_t dots,void *p) {
            auto *pixels=static_cast<u16*>(p);const int py=Camera::y(y);
            for(int bit=0;bit<16;++bit) if(dots&(0x8000>>bit)) {
                const int x=camera.x((left&~7)+bit);
                if(x>=0 && x<256 && py>=24 && py<192) pixels[py*256+x]=0xffff;
            }
        };
        for(const auto &laser:s.lasers) shootout_laser_visit(laser,reference,expected);
        canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,empty));
        assert(std::equal(std::begin(expected),std::end(expected),g_canvas_target));
        canvas_playfield_clip(false);canvas_buffer_prepare();assert(view.present());
    }
    assert(std::equal(actors.begin(),actors.end(),SPRITE_PALETTE));
    assert(test_heap_attempts==heap && test_obj_attempts==objects);
    s.image=0;s.entity.left=0;camera.left=64;render();assert(!test_objs[OAM_SLOT_BOSS_ARROW].hidden);
    video_stage_fade_out();view.end();
    assert(!g_canvas_target && test_heap_count==blocks && test_heap_bytes==bytes && test_objs[OAM_SLOT_BOSS_ARROW].hidden);
    canvas_playfield_clip(false);video_stage_reveal();camera.snap_center();
}
}
void test_elis_phase1()
{
    patterns();circle_allocation();teleport_and_hit();combat_pixels();player_initialize();orb_initialize();shots_initialize();Pellets.init();
    std::puts("Elis phase1: all3 rank-aware source attacks,11 sweep lasers/37 missiles/100-slot ring cap, teleport RNG/cels/skip,14HP fill/orb-shot/grace gate, native combat cels/flash/presentation and lease passed");
}
