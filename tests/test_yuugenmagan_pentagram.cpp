#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include "yuugenmagan_fixture.hpp"
#include "main/math/trig.hpp"
#include "main/random.hpp"
#include "main/stage/stageobj.hpp"

namespace {
void shrink(YuugenMaganPoint (&out)[5],int d)
{
    out[0]={316,92+d/2};out[1]={220+d/3,204+d/2};out[2]={540-d,156+d/3};
    out[3]={92+d,156+d/3};out[4]={412-d/3,204+d/2};
}
void same(const YuugenMaganPoint *a,const YuugenMaganPoint *b)
{
    for(int i=0;i<5;++i) assert(a[i].x==b[i].x && a[i].y==b[i].y);
}
void assembly()
{
    YuugenMaganPhase1 boss;yuugenmagan_enter(boss,10);
    auto &s=const_cast<YuugenMaganPhase1State&>(boss.state());const int score=stage_score;
    auto &pool=const_cast<MissilePool&>(boss.missiles());pool.add(300,240,0.25,0.125);
    for(int tick=1;tick<=70;++tick) {
        orb_set_position(80,128);boss.update(true);const int changes=tick-tick/10;
        assert(s.visual.color15[0]==changes && s.visual.color15[1]==63-changes && s.visual.color15[2]==5);
        for(int i=0;i<5;++i) assert(s.eyes[i].orb_active==(tick>=20 || i==4));
        assert(s.health.hp==2 && stage_score==score && !s.checkpoint);
    }
    assert(s.phase==11 && !s.frame);orb_set_position(10000,10000);
    YuugenMaganPoint expected[5];
    for(int tick=1;tick<=221;++tick) {
        boss.update();const int distance=tick<141 ? 0 : ((tick-141)/4+1)*8;
        assert(s.pentagram.distance==distance && s.pentagram.visible==(tick>=100));
        if(tick>=100) {shrink(expected,distance);same(s.pentagram.ink,expected);}
        assert(s.pentagram.subphase==(tick<100 ? 0 : tick<140 ? 1 : tick<221 ? 2 : 3));
        assert(tick<221 ? s.phase==11 && s.frame==tick : s.phase==12 && !s.frame);
    }
    assert(s.pentagram.distance==168 && s.pentagram.center_x==316 && s.pentagram.center_y==240);
    assert(s.pentagram.radius==64 && s.pentagram.corners[1].y==s.pentagram.corners[4].y); // wrong first-unput quirk
    for(int tick=1;tick<=70;++tick) {
        orb_set_position(80,128);boss.update(true);const int cy=240-(tick/4)*3;
        assert(s.visual.color15[0]==63+tick-tick/10 && s.visual.color15[1]==0 && s.visual.color15[2]==5);
        assert(s.pentagram.center_y==cy && s.health.hp==2 && stage_score==score);
        if(tick<4) {shrink(expected,168);same(s.pentagram.ink,expected);}
        else {
            for(int i=0;i<5;++i) {const unsigned char a=192+i*51;expected[i]={316+((64*Cos8(a))>>8),cy+((64*Sin8(a))>>8)};}
            same(s.pentagram.corners,expected);
            const int path[]={0,2,4,1,3};for(int i=0;i<5;++i) assert(s.pentagram.ink[i].x==expected[path[i]].x && s.pentagram.ink[i].y==expected[path[i]].y);
        }
        for(int i=0;i<5;++i) assert(s.eyes[i].orb_active==(i==0 || tick<40));
    }
    assert(s.phase==13 && !s.frame && !s.checkpoint && s.pentagram.center_y==189);
    assert(pool.at(0).x==4800+361*4 && pool.at(0).y==3840+361*2);
    // A late orb hit holds the assembled star until the >40 grace expiry.
    yuugenmagan_enter(boss,11);for(int i=0;i<219;++i) boss.update();
    orb_set_position(80,128);boss.update();orb_set_position(10000,10000);
    assert(s.health.hp==1 && s.health.invincible);
    for(int tick=221;tick<=260;++tick) {boss.update();assert(s.phase==11 && s.pentagram.distance==168);}
    boss.update();assert(s.phase==12 && !s.health.invincible);
    yuugenmagan_enter(boss,9);for(int i=0;i<41;++i) boss.update();s.health.hp=2;boss.update();
    assert(s.phase==10 && s.laser_count==1);
    for(int i=0;i<70;++i) boss.update();assert(s.phase==11 && s.laser_count==1); // retained early-exit laser ink
}
}

void test_yuugenmagan_pentagram()
{
    const auto difficulty=rank;const bool inv=invincible;const int score=stage_score;
    rank=RANK_NORMAL;assembly();rank=difficulty;stage_score=score;
    player_initialize();orb_initialize();shots_initialize();Pellets.init();invincible=inv;player_is_hit=false;
    std::puts("YuugenMagan assembly:70/221/70 timing,168 shrink overshoot/offcenter geometry,17-step rise,126 raw palette, late-hit grace and retained northern ink passed");
}
