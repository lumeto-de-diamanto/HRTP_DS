#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "main/boss/yuugenmagan_phase1.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/stage/stageobj.hpp"
#include "main/random.hpp"
#include "yuugenmagan_fixture.hpp"

extern PlayerState player;
extern bool invincible;
namespace {
void enter_final(YuugenMaganPhase1 &boss)
{
    yuugenmagan_enter(boss,13);
    auto &s=const_cast<YuugenMaganPhase1State&>(boss.state());
    assert(s.phase==13 && !s.checkpoint && !s.frame && s.pentagram.attack_phase==0);
    s.health.hp=s.hp_displayed=16;Pellets.init();
}
void points(YuugenMaganPoint (&out)[5],int x,int y,int radius,int angle)
{
    for(int i=0;i<5;++i) {const unsigned char a=angle+i*51;out[i]={x+((radius*Cos8(a))>>8),y+((radius*Sin8(a))>>8)};}
}
void same(const YuugenMaganPoint *a,const YuugenMaganPoint *b)
{
    for(int i=0;i<5;++i) assert(a[i].x==b[i].x && a[i].y==b[i].y);
}
// Independent source-frame oracle. Each put is recorded separately so a stage
// handoff with TWO collision passes is not reduced to the final visible star.
struct Oracle {
    int frame=0,stage=0,angle=0,x=316,y=189,radius=64,vx=0,vy=0,slams=0,put_count=0;
    int emission_x=316,emission_y=189;
    YuugenMaganPoint corners[5]{},ink[5]{},puts[2][5]{},emissions[5]{};
    bool visible=true,emitted=false;
    Oracle() {points(corners,x,y,radius,192);path();}
    void path() {const int order[]={0,2,4,1,3};for(int i=0;i<5;++i) ink[i]=corners[order[i]];}
    void aim(int target) {const int a=iatan2(400-y,target-x);vx=(4*Cos8(a))>>8;vy=(4*Sin8(a))>>8;}
    void put(int direction) {
        angle+=direction*(2+frame/64);points(corners,x,y,radius,angle);path();visible=true;
        assert(put_count<2);for(int i=0;i<5;++i) puts[put_count][i]=ink[i];++put_count;
    }
    void slam(int target,int limit) {
        if(frame%4) return;
        if(y<limit) aim(target);x+=vx;y+=vy;put(1);
    }
    void step(int target,int interval) {
        ++frame;put_count=0;emitted=frame%interval==0 && stage!=6 && stage!=9;
        if(emitted) {emission_x=x;emission_y=y;for(int i=0;i<5;++i) emissions[i]=corners[i];}
        if((stage==0 || stage==7) && frame==100) {++stage;frame=0;}
        if(stage==1 || stage==3 || stage==5) {
            if(!(frame&3)) put(1);
            if(frame==200) {++stage;frame=0;}
        }
        if(stage==2 || stage==4) {
            if(!(frame&3)) put(-1);
            if(frame==200) {++stage;frame=0;aim(target);}
        } else if(stage==6) {
            slam(target,300);if(y>=420) {stage=7;frame=0;visible=false;++slams;}
        } else if(stage==8) {
            if(!(frame&3)) {++radius;put(1);}
            if(frame==200) {stage=9;frame=0;aim(target);}
        }
        if(stage==9) {slam(target,260);if(y>=420) {stage=0;frame=0;visible=false;++slams;}}
    }
    bool hits(int left) const {
        for(int n=0;n<put_count;++n) for(const auto &p:puts[n]) if(p.x>left && p.x<left+32 && p.y>384) return true;
        return false;
    }
    void rebuilt() {frame=angle=0;x=316;y=189;radius=64;visible=true;points(corners,x,y,radius,192);path();}
};
int signed_byte(int value) {value%=256;if(value<0) value+=256;return value>=128 ? value-256 : value;}
void cycles()
{
    for(int difficulty=0;difficulty<4;++difficulty) for(int pattern=0;pattern<4;++pattern) {
        rank=static_cast<rank_t>(difficulty);pellet_speed=pattern==3 ? 20 : -4;
        YuugenMaganPhase1 boss;enter_final(boss);Oracle ref;int ticks=0,red=126;unsigned stages=1;
        auto &pool=const_cast<MissilePool&>(boss.missiles());pool.add(300,200,0,0);
        initialize_irand(881);const int random=irand();initialize_irand(881);
        const int intervals[]={24,14,10,8};const int interval=intervals[difficulty];
        while(ref.slams<4) {
            assert(++ticks<12000);
            const int target=pattern==3 ? (ticks*17)%609 : pattern*304;
            const auto before=boss;const int old_stage=ref.stage;
            ref.step(target,interval);stages|=1u<<ref.stage;
            // All threatening endpoints occur after homing has latched, so
            // changing player X here cannot alter the geometry being tested.
            bool threatening=false;for(int n=0;n<ref.put_count;++n) for(const auto &p:ref.puts[n]) threatening|=p.y>384;
            if(difficulty==1 && pattern==1 && threatening && ref.slams<2) {
                for(int left:{0,160,288,304,320,448,608}) {
                    auto probe=before;player.x_position=left;invincible=false;player_is_hit=false;Pellets.init();probe.update();
                    assert(player_is_hit==ref.hits(left));
                }
                auto probe=before;player.x_position=target;invincible=true;player_is_hit=false;Pellets.init();probe.update();assert(!player_is_hit);
            }
            player.x_position=target;invincible=false;player_is_hit=false;Pellets.init();boss.update();
            const auto &s=boss.state();const auto &p=s.pentagram;
            assert(player_is_hit==ref.hits(target));
            assert(p.attack_phase==ref.stage && s.frame==ref.frame && p.angle==ref.angle);
            assert(p.center_x==ref.x && p.center_y==ref.y && p.radius==ref.radius && p.velocity_x==ref.vx && p.velocity_y==ref.vy);
            assert(p.slams==ref.slams && p.visible==ref.visible && !s.checkpoint && s.health.hp==16);
            same(p.corners,ref.corners);same(p.ink,ref.ink);
            assert(Pellets.alive_count()==(ref.emitted ? 5 : 0));
            if(ref.emitted) for(int i=0;i<5;++i) {
                const auto &pel=Pellets.p[i];const auto at=ref.emissions[i];const int a=iatan2(at.y-ref.emission_y,at.x-ref.emission_x);
                // Keep the PRE-move center even on an emitting transition
                // update that immediately performs the first slam step.
                int speed=48+difficulty*6;speed+=pellet_speed*speed/40;speed=std::max(16,speed);
                assert(pel.moving && !pel.cloud_frame && pel.from_group==PG_NONE && pel.motion_type==PM_REGULAR && !pel.speed.v);
                assert(pel.cur_left.v==at.x*16 && pel.cur_top.v==at.y*16 && pel.angle==a);
                assert(pel.velocity.x.v==((speed*Cos8(a))>>8) && pel.velocity.y.v==((speed*Sin8(a))>>8));
            }
            if(old_stage==0 && ref.stage==1) assert(ref.angle==2 && !ref.frame);
            if(old_stage==1 && ref.stage==2) assert(ref.angle==157 && !ref.frame && ref.put_count==2);
            if(old_stage==7 && ref.stage==8) assert(ref.radius==65 && ref.angle==2 && !ref.frame);
            if(old_stage==8 && ref.stage==9) assert(ref.radius==115 && ref.angle==161 && !ref.frame && ref.put_count==2);
            if(s.phase==10) {
                assert(!p.visible && p.repeat && pool.alive_count()==1);
                const int next_red=signed_byte(red+63);
                for(int t=1;t<=70;++t) {Pellets.init();boss.update();assert(boss.state().visual.color15[0]==red);}
                for(int t=1;t<=221;++t) {Pellets.init();boss.update();}
                assert(boss.state().phase==12);
                for(int t=1;t<=70;++t) {
                    Pellets.init();boss.update();assert(boss.state().visual.color15[0]==signed_byte(red+t-t/10));
                }
                red=next_red;assert(boss.state().phase==13 && !boss.state().checkpoint && boss.state().visual.color15[0]==red);
                assert(boss.state().visual.color15[1]==0 && boss.state().visual.color15[2]==5);
                ref.rebuilt();same(boss.state().pentagram.corners,ref.corners);
            }
        }
        assert((stages&1023)==1023 && red==122 && irand()==random);
    }
}
void eyes()
{
    rank=RANK_NORMAL;YuugenMaganPhase1 boss;enter_final(boss);auto &s=const_cast<YuugenMaganPhase1State&>(boss.state());
    player.x_position=304;int locks[]={0,0,2,2,3},image[5],shown[5];
    for(int i=0;i<5;++i) {image[i]=s.visual.image[i];shown[i]=s.visual.shown[i];}
    auto blit=[&](unsigned mask) {for(int i=0;i<5;++i) if(mask&(1u<<i)) {
        if(!locks[i]) {shown[i]=image[i];locks[i]=1;} else locks[i]=(locks[i]+1)%4;
    }};
    auto tracking=[&](unsigned mask) {for(int i=0;i<5;++i) if(mask&(1u<<i)) {
        const int d=YUUGENMAGAN_LEFT[i]+32-304;image[i]=d>32 ? YMG_LEFT : d< -32 ? YMG_RIGHT : YMG_DOWN;
    }};
    unsigned selected=1;
    for(int hit=0;hit<5;++hit) {
        blit(selected);tracking(selected);
        const int index=hit;orb_set_position(YUUGENMAGAN_LEFT[index]+16,YUUGENMAGAN_TOP[index]);
        Pellets.init();invincible=true;boss.update();orb_set_position(10000,10000);
        assert(s.health.hp==15-hit && s.health.invincible && !s.pentagram.after_hit_frame);
        for(int t=1;t<=70;++t) {
            blit(selected);const unsigned old=1u<<index,new_eye=old==16 ? 1 : old<<1;
            if(t==1) selected=new_eye;
            else {
                blit(old|new_eye);
                for(int i=0;i<5;++i) {
                    if(t==20) {if(old&(1u<<i)) image[i]=YMG_HALFOPEN;if(new_eye&(1u<<i)) image[i]=YMG_CLOSED;}
                    if(t==40) {if(old&(1u<<i)) image[i]=YMG_CLOSED;if(new_eye&(1u<<i)) image[i]=YMG_HALFOPEN;}
                    if(t==60) {if(old&(1u<<i)) image[i]=YMG_HIDDEN;if(new_eye&(1u<<i)) image[i]=YMG_AHEAD;}
                }
            }
            Pellets.init();boss.update();
            assert(s.pentagram.eyes_open==selected && s.pentagram.after_hit_frame==(t==70 ? 0 : t));
            assert(s.health.invincible==(t<=40));
            const unsigned active=t<20 ? old : t<40 ? old|new_eye : new_eye;
            for(int i=0;i<5;++i) {
                assert(s.eyes[i].orb_active==bool(active&(1u<<i)));
                assert(s.visual.image[i]==image[i] && s.visual.shown[i]==shown[i]);
            }
            assert(s.visual.color15[0]==126 && !s.visual.color15[1] && s.visual.color15[2]==5);
        }
        assert(!s.pentagram.unused_frame);
    }
    assert(selected==1 && s.health.hp==11);
    enter_final(boss);s.health.hp=1;
    auto &pool=const_cast<MissilePool&>(boss.missiles());pool.add(305,369,0,0);player.x_position=304;invincible=false;player_is_hit=false;
    orb_set_position(80,128);const int score=stage_score;boss.update();
    assert(s.checkpoint && s.health.hp==0 && s.phase==13 && player_is_hit && stage_score==score+5000 && pool.alive_count()==1);
    const auto frozen=boss;boss.update(true);assert(!std::memcmp(&frozen,&boss,sizeof(boss)));
}
}

void test_yuugenmagan_phase13()
{
    const auto difficulty=rank;const bool inv=invincible;const int score=stage_score,speed=pellet_speed;
    cycles();eyes();rank=difficulty;stage_score=score;pellet_speed=speed;player_initialize();orb_initialize();shots_initialize();Pellets.init();invincible=inv;player_is_hit=false;
    std::puts("YuugenMagan final attack: all-rank source spin/grow/slam/pellet oracle, immediate handoffs/radius115, endpoint collision samples, signed palette wraps/reassembly, five eye switches/double locks and HP0 priority boundary passed");
}
