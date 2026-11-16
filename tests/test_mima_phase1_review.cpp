#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "mima_combat_render.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/timer.hpp"
#include "main/stage/item.hpp"
#include "main/bullet/pellet.hpp"
#include "main/boss/singyoku.hpp"
#include "main/boss/mima_defeat.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "boss_gfx.hpp"
#include "boss_render.hpp"
#include "background.hpp"
#include "heap_probe.hpp"
#include "player_render.hpp"
#include "orb_render.hpp"
#include "main/random.hpp"
#include "main/math/trig.hpp"
#include "sound_effects.hpp"
#include "sprites/Miko.h"
#include "backgrounds/MimaDefeatPalette.h"
#include "backgrounds/MimaPaletteFx.h"

extern std::string test_practice_label;
extern int test_mima_music_starts,test_mima_pauses,test_mima_resumes,test_mima_stops,test_mima_fades;
extern bool test_mima_music_allowed,invincible;
extern std::vector<int> test_effects;
namespace {
int ticks,mode,paused;
size_t heap_attempts,obj_attempts;
unsigned pause_timer;
int pause_bomb;
OrbState pause_orb{};
bool saw_checkpoint;
int second_started;
int practice_defeat_waits,last_damage_tick,last_damage_count,practice_boss_sound_start,practice_fade_start;
bool forced_lethal_miss;
PlayerState practice_defeat_player{};
OrbState practice_defeat_orb{};
ShotsState practice_defeat_shots{};
unsigned practice_defeat_timer;
int practice_defeat_score,practice_defeat_bombs;
size_t practice_defeat_sounds;

struct DefeatProbe {
    int waits=0,lines=0,center=0,camera=0,score=0,bombs=0,speed=0,stops=0,fades=0;
    unsigned timer=0;
    PlayerState player{};OrbState orb{};ShotsState shots{};Pellet cloud{};
    size_t heap=0,objects=0,sounds=0;
    std::vector<u16> palettes;
} defeat;

void observe_defeat()
{
    ++defeat.waits;assert(defeat.waits<=defeat.lines+79);
    assert(!std::memcmp(&defeat.player,&player_get_state(),sizeof(defeat.player)));
    assert(!std::memcmp(&defeat.orb,&orb_get_state(),sizeof(defeat.orb)));
    assert(!std::memcmp(&defeat.shots,&shots_get_state(),sizeof(defeat.shots)));
    assert(!std::memcmp(&defeat.cloud,&Pellets.at(1),sizeof(defeat.cloud)));
    assert(stage_timer==defeat.timer && stage_score==defeat.score && rem_bombs==defeat.bombs && pellet_speed==defeat.speed);
    assert(camera.left==defeat.camera && test_heap_attempts==defeat.heap && test_obj_attempts==defeat.objects);
    assert(test_effects.size()==defeat.sounds && test_mima_stops==defeat.stops && test_mima_fades==defeat.fades+1);
    assert(!video_bomb_effect_active());
    if(defeat.waits==1) return; // callback precedes the current VBlank commit
    const int previous=defeat.waits-2;
    const bool fading=previous>=defeat.lines;
    const int step=fading ? (previous-defeat.lines+1)/5 : std::min(15,previous/7);
    const auto *palette=defeat.palettes.data()+((fading?16:0)+step)*MIMA_BG_COLORS;
    for(int i=0;i<MIMA_BG_COLORS;++i) if(Mima_defeat_signatures[i]!=0x10000) assert(BG_PALETTE[i]==palette[i]);
    assert(test_objs[40].hidden && test_objs[41].hidden && test_objs[17].hidden);
    assert(REG_WININ==0x1d1d && test_bgs[0].priority==1);
    assert(test_bgs[3].x==camera.left && !test_bgs[3].y && !test_bgs[2].y);
    if(fading) {
        for(int i=0;i<256*192;++i) assert(!BG_BMP_RAM(CANVAS_BMP_BLOCK)[i]);
        for(int i=1;i<48;++i) assert(test_objs[i].hidden);
        assert(test_objs[0].size==SpriteSize_16x16);
        assert(!std::memcmp(test_objs[0].gfx,Miko_tiles,128));
    } else {
        const int origin=Camera::y(defeat.center);
        for(int row=0;row<192;++row) {
            const int distance=std::abs(row-origin);
            const bool stripe=distance<previous && distance%2==0;
            const u16 expected=stripe ? u16(0x8000|palette[7]) : 0;
            // Check BOTH painted rows and transparent intervening gaps. The
            // old positive-only assertions let an entirely filled wipe pass.
            for(int x=0;x<256;++x) assert(BG_BMP_RAM(CANVAS_BMP_BLOCK)[row*256+x]==expected);
        }
    }
    // Input during the blocking source effect cannot advance or skip it.
    test_keys_down=KEY_SELECT|KEY_START|KEY_A;test_keys_held=KEY_RIGHT|KEY_B;
}

void check_defeat_renderer()
{
    defeat.palettes.resize(32*MIMA_BG_COLORS);
    std::ifstream input("mima-defeat-palettes.bin",std::ios::binary);
    input.read(reinterpret_cast<char *>(defeat.palettes.data()),defeat.palettes.size()*2);
    assert(input.gcount()==std::streamsize(defeat.palettes.size()*2));
    const auto timer=stage_timer;const int score=stage_score,bombs=rem_bombs,speed=pellet_speed;
    MimaPhase1State s;s.phase=3;s.checkpoint=true;s.health.hp=0;
    MimaCombatView view;assert(!view.defeat(s));
    for(int cam:{0,32,64}) for(int center:{16,167,168,399}) {
        assert(view.begin());MimaPhase1State wrong=s;wrong.health.hp=1;assert(!view.defeat(wrong));
        wrong=s;wrong.phase=1;assert(!view.defeat(wrong));
        player_initialize();player_update(KEY_A,KEY_A);assert(player_get_state().spritesheet==PLAYER_48);
        orb_initialize();orb_set_position(300,center-16);camera.left=cam;
        shots_initialize();shots_add(240,200);Pellets.init();Pellets.add_single(100,140,0,32);
        Pellets.spawn_with_cloud=true;Pellets.add_single(100,140,0,32);Pellets.spawn_with_cloud=false;
        Pellets.reset_nonclouds();assert(!Pellets.at(0).moving && Pellets.at(1).cloud_frame);
        MissilePool empty;canvas_begin_frame();assert(view.compose(s,empty));
        player_render_frame();orb_render_frame();canvas_buffer_prepare();REG_VCOUNT=192;assert(view.present());
        defeat.waits=0;defeat.center=center;defeat.camera=cam;defeat.lines=(std::max(center,399-center)/2)+1;
        defeat.player=player_get_state();defeat.orb=orb_get_state();defeat.shots=shots_get_state();defeat.cloud=Pellets.at(1);
        defeat.timer=stage_timer;defeat.score=stage_score;defeat.bombs=rem_bombs;defeat.speed=pellet_speed;
        defeat.heap=test_heap_attempts;defeat.objects=test_obj_attempts;defeat.sounds=test_effects.size();defeat.stops=test_mima_stops;
        defeat.fades=test_mima_fades;
        initialize_irand(999);const int random=irand();initialize_irand(999);
        const u16 window=REG_WININ,sub=REG_MASTER_BRIGHT_SUB;
        test_heap_fail_after=0;test_vblank_hook=observe_defeat;
        assert(view.defeat(s));test_vblank_hook=nullptr;test_heap_fail_after=-1;
        test_keys_down=test_keys_held=0;
        assert(defeat.waits==defeat.lines+79 && irand()==random);
        assert(REG_WININ==window && REG_MASTER_BRIGHT_SUB==sub && test_bgs[0].priority==0);
        assert(!view.defeat(s) && !view.compose(s,empty) && !view.present());
        for(int i=0;i<MIMA_BG_COLORS;++i) assert(BG_PALETTE[i]==Mima_palette_fx[0][0][i]);
        view.end();assert(test_objs[0].gfx && test_objs[40].hidden && test_objs[17].hidden);
    }
    stage_timer=timer;stage_score=score;rem_bombs=bombs;pellet_speed=speed;
    player_initialize();orb_initialize();shots_initialize();Pellets.init();camera.snap_center();
    std::puts("Mima defeat render: twelve orb-height/camera/parity cases, exact RGB4/signature palettes and alternating full-width white/transparent rows, unchanged expansion/fade timing, white arena/default pose, frozen actors/shots/clouds/timer/RNG/resources, guarded replay, no allocation/I/O/new SFX, window/bottom-screen restoration and cache cleanup passed");
}

void check_second_effects()
{
    unsigned char masks[128];std::ifstream input("mima-pillar.bin",std::ios::binary);
    input.read(reinterpret_cast<char *>(masks),sizeof(masks));assert(input.gcount()==sizeof(masks));
    static u16 expected[256*192];
    const auto put=[&](int x,int y,u16 color) {
        if(x>=0 && x<256 && y>=24 && y<192) expected[y*256+x]=0x8000|color;
    };
    MimaCombatView view;assert(view.begin());
    const auto heap=test_heap_attempts,objects=test_obj_attempts;
    MissilePool missiles;
    for(int cam:{0,32,64}) for(int dim:{0,4}) for(int visual=0;visual<4;++visual) {
        camera.left=cam;MimaPhase1State s;s.darkness=dim;
        std::fill(std::begin(expected),std::end(expected),0);
        const auto *colors=Mima_palette_fx[dim/4][0];
        if(visual<2) {
            for(int i=0;i<8;++i) {
                auto &p=s.pillars[i];p.center_x=19+i*85;p.height=visual==0?32:160;
                // Original32px body/cap masks, byte-aligned sprite X but not
                // the logical center. Growing pillars retain their first cap.
                for(int h:{32,p.height}) for(int y=0;y<h/2;++y) for(int x=0;x<16;++x) {
                    const int cel=y<4?1:0;
                    if(masks[cel*64+(y%4)*16+x])
                        put(((p.center_x-16)&~7)/2-cam+x,192-h/2+y,colors[10]);
                }
            }
        } else if(visual==2) {
            auto &p=s.pillars[0];p.center_x=19;p.radius=120;p.ray=true;
            // Source sampled-circle oracle, retaining only flushed byte/y groups.
            std::vector<MimaPoint> group;int old=-1;
            for(int a=0;a<256;a+=4) {
                const int x=19+((120*Cos8(a))>>8),y=400+((120*Sin8(a))>>8),offset=y*80+(x>>3);
                if(offset!=old) {
                    if(x>=0 && x<640 && old>=0 && old<32000)
                        for(const auto &q:group) put(half_floor(q.x)-cam,Camera::y(q.y),colors[7]);
                    group.clear();old=offset;
                }
                group.push_back({x,y});
            }
            int x=160-cam,y=92,tx=9-cam,ty=192;
            const int dx=std::abs(tx-x),dy=-std::abs(ty-y);int error=dx+dy;
            while(true) {
                put(x,y,colors[1]);if(x==tx && y==ty) break;
                const int twice=error*2;if(twice>=dy) { error+=dy;--x; }if(twice<=dx) { error+=dx;++y; }
            }
        } else {
            assert(shootout_laser_spawn(s.lasers[0],302,150,304,400,54,20,4));
            for(int i=0;i<35;++i) shootout_laser_update(s.lasers[0]);
            struct Context { u16 *pixels;int cam;u16 color; } context{expected,cam,colors[7]};
            shootout_laser_visit(s.lasers[0],[](int left,int y,uint16_t dots,void *data) {
                auto &c=*static_cast<Context *>(data);const int row=Camera::y(y);
                for(int bit=0;bit<16;++bit) if(dots&(0x8000>>bit)) {
                    const int x=half_floor((left&~7)+bit)-c.cam;
                    if(x>=0 && x<256 && row>=24 && row<192) c.pixels[row*256+x]=0x8000|c.color;
                }
            },&context);
        }
        canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,missiles));canvas_playfield_clip(false);
        assert(std::equal(std::begin(expected),std::end(expected),g_canvas_target));
        canvas_buffer_prepare();REG_VCOUNT=192;assert(view.present());
        assert(std::equal(std::begin(expected),std::end(expected),BG_BMP_RAM(CANVAS_BMP_BLOCK)));
        assert(test_heap_attempts==heap && test_obj_attempts==objects);
    }
    canvas_begin_frame();MimaPhase1State empty;assert(view.compose(empty,missiles));
    for(int i=0;i<256*192;++i) assert(!g_canvas_target[i]);
    view.end();camera.snap_center();player_is_hit=false;
    std::puts("Mima second-block render: original pillar masks/byte alignment/cap trails, sampled circles/rays, grouped four-pixel laser ink, local darkening, three camera edges, exact RAM/VBlank pixels, complete dirty clearing and no new allocations passed");
}

struct DeathProbe {
    const MimaPhase1 *boss=nullptr;
    unsigned char state[sizeof(MimaPhase1State)]{},missiles[sizeof(MissilePool)]{};
    PlayerState player{};OrbState orb{};Pellet pellet{};
    unsigned timer=0;
    int score=0,bombs=0,speed=0,pose=22,waits=0,step=-1,until=0;
    int right=0,left=0,right_delta=0,left_delta=0,camera=32,pairs=0;
    bool fatal=false;
    size_t heap=0,objects=0,sounds=0;
} death;

void observe_death()
{
    ++death.waits;assert(death.waits<=(death.fatal?31:99));
    assert(!std::memcmp(death.state,&death.boss->state(),sizeof(death.state)));
    assert(!std::memcmp(death.missiles,&death.boss->missiles(),sizeof(death.missiles)));
    assert(!std::memcmp(&death.player,&player_get_state(),sizeof(death.player)));
    assert(!std::memcmp(&death.orb,&orb_get_state(),sizeof(death.orb)));
    assert(stage_timer==death.timer && stage_score==death.score && rem_bombs==death.bombs && pellet_speed==death.speed);
    assert(test_heap_attempts==death.heap && test_obj_attempts==death.objects && test_effects.size()==death.sounds);
    assert(REG_VCOUNT>=192 && REG_VCOUNT<200 && !video_bomb_effect_active());
    const bool first=death.waits>death.until;
    if(first) {
        ++death.step;
        const int frame=death.step-17;
        const int hold=death.fatal ? (death.step<16 ? 1+death.step/6 : 1) :
            death.step<16 ? 3 : death.step==16 ? 1 : frame<6 ? 1 : (30-frame)/10+1;
        death.until+=hold;
        if(!death.fatal && death.step>=17) {
            death.right+=frame<10 ? 16 : death.right_delta;
            death.left+=frame<10 ? -16 : death.left_delta;
            death.right=std::min(608,death.right);death.left=std::max(0,death.left);
            death.camera=std::max(0,std::min(64,((death.right+death.left+32)/2)/2-128));
        }
    }
    const bool returning=!death.fatal && death.step>=17;
    if(returning) ++death.pairs;
    const int shake=death.step<16 ? (death.step%2)*4 : 0;
    assert(camera.left==death.camera);
    assert(!std::memcmp(test_objs[0].gfx,reinterpret_cast<const u8 *>(Miko_tiles)+death.pose*128,128));
    assert(test_objs[0].hidden==returning);
    const auto &pellet=Pellets.at(0);
    assert(pellet.cur_left.v==death.pellet.cur_left.v && pellet.cur_top.v==death.pellet.cur_top.v);
    assert(pellet.velocity.x.v==(returning ? death.pellet.velocity.x.v*2/3 : death.pellet.velocity.x.v));
    assert(pellet.velocity.y.v==(returning ? death.pellet.velocity.y.v*2/3 : death.pellet.velocity.y.v));
    assert(pellet.decay_frame==(returning?1:0));
    assert(Pellets.at(1).cloud_frame==4 && !Pellets.at(1).moving);
    assert(shots_get_state().alive[0]==!returning);
    // On hold frames the prior VBlank has committed the complete shaken scene.
    // First-wait callbacks deliberately run BEFORE present() publishes it.
    if(!first) {
        assert(test_bgs[1].y==-shake && test_bgs[2].y==-shake && test_bgs[3].y==-shake);
        assert(test_objs[40].y==Camera::y(death.boss->state().entity.top)+shake);
        assert(test_objs[41].y==Camera::y(death.boss->state().entity.top)+64+shake);
        if(!test_objs[10].hidden) assert(test_objs[10].y==Camera::y(death.orb.y_position)+shake);
        const auto &entity=death.boss->state().entity;
        const auto marker=orb_indicator(camera,entity.left,entity.top,128,160);
        if(marker.direction) assert(test_objs[17].y==marker.y-7); // no shake on markers
        const auto orb_marker=orb_indicator(camera,death.orb.x_position,death.orb.y_position,32,32);
        if(orb_marker.direction) assert(test_objs[11].y==orb_marker.y-7);
    }
    if(returning) for(int i=0;i<2;++i) {
        assert(test_objs[12+i].priority==0);
        assert(test_objs[12+i].x==camera.x(i==0?death.right:death.left));
    }
}

void check_death_renderer()
{
    const int score=stage_score,bombs=rem_bombs,speed=pellet_speed;
    const auto timer=stage_timer;
    for(bool second:{false,true}) for(bool fatal:{false,true}) for(int left:{0,304,608}) for(bool alternate:{false,true}) {
        MimaCombatView view;assert(view.begin());
        player_initialize();orb_set_position(10000,10000);invincible=true;
        MimaPhase1 boss;boss.reset();const_cast<MimaPhase1State&>(boss.state()).pattern=1;
        for(int i=0;i<250;++i) boss.update();
        assert(boss.missiles().alive_count()>0);
        auto &state=const_cast<MimaPhase1State&>(boss.state());
        if(second) {
            state.phase=3;state.pattern=alternate?3:0;state.frame=0;
            for(int i=0;i<200;++i) { boss.update();boss.update_lasers(); }
            if(alternate) assert(state.lasers[0].alive);
            else assert(state.pillars[0].height==160);
            // Keep a resident missile alongside the phase3 hazards as well.
            assert(const_cast<MissilePool&>(boss.missiles()).add(300,100,0,0));
        }
        state.entity.left=left==0?512:left==608?0:256;
        state.darkness=alternate?4:0;state.flash=alternate;state.flash_darkness=alternate?4:0;
        player_initialize();const_cast<PlayerState&>(player_get_state()).x_position=left;
        player_update(KEY_A,KEY_A);assert(player_get_state().spritesheet==PLAYER_48);
        orb_initialize();orb_set_position(600,100);camera.follow(left+16);
        Pellets.init();Pellets.add_single(100,140,32,48);
        Pellets.spawn_with_cloud=true;Pellets.add_single(120,140,32,48);Pellets.spawn_with_cloud=false;
        Pellets.at(1).cloud_frame=4;
        shots_initialize();shots_add(200,300);
        stage_timer=100;stage_score=12345;rem_bombs=2;
        canvas_begin_frame();assert(view.compose(state,boss.missiles()));
        canvas_buffer_prepare();REG_VCOUNT=192;assert(view.present());
        int next_random=0;
        for(unsigned seed=0;;++seed) {
            initialize_irand(seed);
            if(((irand()%8)==0)==alternate) { next_random=irand();initialize_irand(seed);break; }
            assert(seed<1000);
        }
        death={};death.boss=&boss;death.player=player_get_state();death.orb=orb_get_state();
        death.pellet=Pellets.at(0);death.timer=stage_timer;death.score=stage_score;
        death.bombs=rem_bombs;death.speed=pellet_speed;death.pose=alternate?23:22;death.fatal=fatal;
        death.left=death.right=left;death.camera=camera.left;
        death.right_delta=(304-std::min(608,left+160))/20;
        death.left_delta=(304-std::max(0,left-160))/20;
        death.heap=test_heap_attempts;death.objects=test_obj_attempts;death.sounds=test_effects.size();
        std::memcpy(death.state,&boss.state(),sizeof(death.state));
        std::memcpy(death.missiles,&boss.missiles(),sizeof(death.missiles));
        test_vblank_hook=observe_death;
        assert(mima_death_animate(view,boss,fatal));test_vblank_hook=nullptr;
        assert(death.waits==(fatal?31:99) && death.step==(fatal?16:46) && death.pairs==(fatal?0:50));
        assert(irand()==next_random);
        assert(test_bgs[1].y==0 && test_bgs[2].y==0 && test_bgs[3].y==0);
        if(!fatal) player_miss();
        player_render_frame();
        for(int i=0;i<5;++i) assert(test_objs[12+i].hidden);
        view.end();
    }
    stage_score=score;rem_bombs=bombs;pellet_speed=speed;stage_timer=timer;
    player_initialize();Pellets.init();shots_initialize();orb_initialize();camera.snap_center();
    std::puts("Mima miss render: all24 block/edge/pose/fatal cases,99-frame shake/split recovery and31-frame five-puff burst, frozen boss/missiles/lasers/pillars/particles/orb/timer/RNG, one-time shot reset/pellet decay, camera/marker anchoring and zero new allocations passed");
}

int practice_deaths=0,practice_death_waits=0;
OrbState practice_death_orb{};
unsigned practice_death_timer=0;
int practice_death_bombs=0,practice_death_score=0;
size_t practice_death_sounds=0;
unsigned char practice_death_hud[sizeof(TestObj)*27]{};

void observe_practice_misses()
{
    ++ticks;assert(ticks<=1432);
    assert(test_heap_attempts==heap_attempts && test_obj_attempts==obj_attempts);
    test_keys_down=test_keys_held=0;invincible=true;
    if(test_practice_label=="PRACTICE OVER") {
        assert(ticks==1432 && practice_deaths==4 && practice_death_waits==31);
        assert(stage_timer==practice_death_timer && rem_bombs==5 && stage_score==practice_death_score);
        assert(!test_objs[0].hidden);
        for(int i=0;i<5;++i) assert(!test_objs[12+i].hidden && test_objs[12+i].priority==0);
        test_keys_down=KEY_A;return;
    }
    for(int trigger:{800,1000,1200,1400}) {
        const int duration=trigger==1400 ? 31 : 99;
        if(ticks>trigger && ticks<=trigger+duration) {
            if(ticks==trigger+1) {
                ++practice_deaths;practice_death_waits=0;practice_death_orb=orb_get_state();
                practice_death_timer=stage_timer;practice_death_bombs=rem_bombs;practice_death_score=stage_score;
                practice_death_sounds=test_effects.size();
                assert(rem_bombs==practice_deaths+1);
                std::memcpy(practice_death_hud,test_objs+48,sizeof(practice_death_hud));
            }
            ++practice_death_waits;
            assert(!std::memcmp(&practice_death_orb,&orb_get_state(),sizeof(practice_death_orb)));
            assert(stage_timer==practice_death_timer && stage_score==practice_death_score && rem_bombs==practice_death_bombs);
            assert(test_effects.size()==practice_death_sounds);
            assert(!std::memcmp(practice_death_hud,test_objs+48,sizeof(practice_death_hud)));
            return;
        }
        if(trigger<1400 && ticks==trigger+100) {
            assert(practice_death_waits==99 && rem_bombs==practice_death_bombs+1);
            // Entrance consumes101 VBlanks; prior misses consume99 each,
            // without advancing gameplay. Recovery resumes the same cadence:
            // +200, then a two-unit tick only on every fourth gameplay frame.
            const unsigned recovery_frame=trigger-101-(practice_deaths-1)*99+1;
            assert(practice_death_timer>0);
            assert(stage_timer==practice_death_timer+200-(recovery_frame%4==0 ? 2 : 0));
            assert(player_get_state().x_position==PLAYER_LEFT_START && player_is_invincible());
            assert(!std::memcmp(&practice_death_orb,&orb_get_state(),sizeof(practice_death_orb)));
            for(int i=0;i<5;++i) assert(test_objs[12+i].hidden);
        }
        if(ticks==trigger) player_is_hit=true;
    }
    orb_set_position(10000,10000);
}

void observe()
{
    ++ticks;assert(ticks<2600);
    assert(test_heap_attempts==heap_attempts && test_obj_attempts==obj_attempts);
    test_keys_held=test_keys_down=0;
    invincible=true; // observe complete patterns without injecting practice misses
    if(test_practice_label=="MIMA DEFEAT") {
        assert(test_mima_fades==practice_fade_start+1);
        if(!practice_defeat_waits) {
            practice_defeat_player=player_get_state();practice_defeat_orb=orb_get_state();practice_defeat_shots=shots_get_state();
            practice_defeat_timer=stage_timer;practice_defeat_score=stage_score;practice_defeat_bombs=rem_bombs;
            practice_defeat_sounds=test_effects.size();
            assert(std::count(test_effects.begin(),test_effects.end(),SFX_BOSS_DAMAGE)==practice_boss_sound_start+12);
            // Orb/pellet contact can add ordinary points during the real loop;
            // require all12 boss hits but no invented50,000 defeat bonus.
            assert(practice_defeat_score>=60000 && practice_defeat_score<110000 && hiscore>=stage_score && forced_lethal_miss);
        }
        ++practice_defeat_waits;
        assert(!std::memcmp(&practice_defeat_player,&player_get_state(),sizeof(practice_defeat_player)));
        assert(!std::memcmp(&practice_defeat_orb,&orb_get_state(),sizeof(practice_defeat_orb)));
        assert(!std::memcmp(&practice_defeat_shots,&shots_get_state(),sizeof(practice_defeat_shots)));
        assert(stage_timer==practice_defeat_timer && stage_score==practice_defeat_score && rem_bombs==practice_defeat_bombs);
        assert(test_effects.size()==practice_defeat_sounds && !player_is_hit);
        for(int i=0;i<PELLET_COUNT;++i) assert(!Pellets.at(i).moving);
        if(practice_defeat_waits>1) assert(test_objs[40].hidden && test_objs[41].hidden && test_objs[17].hidden);
        return;
    }
    if(test_practice_label=="MIMA SECOND BLOCK" && !second_started) second_started=ticks;
    if(test_practice_label=="DEFEAT REVIEW COMPLETE") {
        const int center=practice_defeat_orb.y_position+16;
        assert(practice_defeat_waits==std::max(center,399-center)/2+1+79);
        assert(stage_score==practice_defeat_score && stage_timer==practice_defeat_timer);
        saw_checkpoint=true;assert(mode==1);test_keys_down=KEY_A;return;
    }
    if(test_practice_label=="PRACTICE OVER") assert(false && "test invincibility must retain the complete practice");
    if(video_fade()==10 && (REG_BLDCNT&BLEND_FADE_BLACK)==BLEND_FADE_BLACK) {
        if(!paused) { pause_timer=stage_timer;pause_bomb=player_bomb().frame();pause_orb=orb_get_state(); }
        else {
            assert(stage_timer==pause_timer && player_bomb().frame()==pause_bomb);
            // Restore the artificially fixed test orb before checking the pause;
            // no real orb update may occur while a modal menu is active.
            assert(orb_get_state().x_position==pause_orb.x_position && orb_get_state().y_position==pause_orb.y_position);
        }
        ++paused;if(paused==2) test_keys_down=KEY_START;return;
    }
    if(mode==0) orb_set_position(10000,10000);
    else if(ticks>101) {
        // Keep all four second-block patterns alive for a full cycle before
        // testing the actual lethal-hit checkpoint/terminal cleanup.
        orb_set_position(second_started && ticks<second_started+1364 ? 10000 : 288,
            second_started && ticks<second_started+1364 ? 10000 : 152);
        const int hits=std::count(test_effects.begin(),test_effects.end(),SFX_BOSS_DAMAGE)-practice_boss_sound_start;
        if(hits!=last_damage_count) { last_damage_count=hits;last_damage_tick=ticks; }
        if(hits==11 && ticks==last_damage_tick+41) {
            // Pending player damage on the exact next lethal boss-hit frame
            // must not enter miss playback or grant recovery resources.
            player_is_hit=true;forced_lethal_miss=true;
        }
    }
    if(mode==0 && ticks==180) test_keys_down=KEY_X;
    if(mode==0 && ticks==260) test_keys_down=KEY_START;
    if(mode==0 && ticks==1500) test_keys_down=KEY_SELECT;
}
}

void test_mima_phase1_review()
{
    const auto baseline=test_heap_bytes,blocks=test_heap_count,objects=test_obj_attempts;
    MimaCombatView view;
    for(int fail=0;fail<3;++fail) {
        test_heap_fail_after=fail;assert(!view.begin());test_heap_fail_after=-1;
        assert(test_heap_count==blocks && test_heap_bytes==baseline && !g_canvas_target);
        assert(test_obj_count==79 && test_obj_attempts==objects);
    }
    const std::vector<u16> overlay(SPRITE_PALETTE+80,SPRITE_PALETTE+112);
    MimaPhase1State state;state.health.reset(12);state.hp_displayed=12;MissilePool missiles;
    assert(missiles.add(300,300,1,0));missiles.update(0);
    for(int repeat=0;repeat<10;++repeat) {
        assert(view.begin());const auto attempts=test_heap_attempts;
        assert(view.begin() && test_heap_attempts==attempts);
        for(int dim=0;dim<=16;dim+=4) for(int flash=0;flash<6;++flash) {
            state.image=41;state.darkness=dim;state.flash=flash!=0;state.flash_darkness=flash ? (flash-1)*4 : 0;
            state.squares_visible=true;state.square_count=1;
            state.corners[0][0]={100,100};state.corners[0][1]={200,100};
            state.corners[0][2]={200,200};state.corners[0][3]={100,200};
            canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(state,missiles));
            canvas_playfield_clip(false);canvas_buffer_prepare();
            REG_VCOUNT=100;const auto oam=test_oam_changes;assert(!view.present() && test_oam_changes==oam);
            REG_VCOUNT=192;assert(view.present());
            for(int i=0;i<MIMA_BG_COLORS;++i) assert(BG_PALETTE[i]==Mima_palette_fx[dim/4][flash][i]);
            for(int i=0;i<16;++i) assert(SPRITE_PALETTE[48+i]==Mima_palette_fx[dim/4][flash][(i-1)&15]);
            assert(std::equal(overlay.begin(),overlay.end(),SPRITE_PALETTE+80));
            assert(test_heap_attempts==attempts && test_obj_attempts==objects);
        }
        view.end();view.end();
        assert(test_heap_count==blocks && test_heap_bytes==baseline && !g_canvas_target);
        assert(test_objs[40].hidden && test_objs[41].hidden && test_objs[17].hidden);
    }
    test_mima_music_allowed=false;
    assert(!mima_phase1_review());test_mima_music_allowed=true;
    assert(test_heap_count==blocks && test_heap_bytes==baseline && !g_canvas_target);
    for(mode=0;mode<2;++mode) {
        const int score=stage_score,high=hiscore,bombs=rem_bombs,speed=pellet_speed;
        const auto timer=stage_timer;
        ticks=paused=second_started=practice_defeat_waits=last_damage_tick=last_damage_count=0;
        practice_boss_sound_start=std::count(test_effects.begin(),test_effects.end(),SFX_BOSS_DAMAGE);
        practice_fade_start=test_mima_fades;
        saw_checkpoint=forced_lethal_miss=false;
        const int songs=test_mima_music_starts,pauses=test_mima_pauses,resumes=test_mima_resumes;
        heap_attempts=test_heap_attempts+3;obj_attempts=test_obj_attempts;
        test_vblank_hook=observe;test_keys_down=test_keys_held=0;
        assert(mima_phase1_review());
        test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;invincible=false;
        assert(test_mima_music_starts==songs+1);
        if(mode==0) assert(ticks==1500 && paused>=22 && test_mima_pauses==pauses+1 && test_mima_resumes==resumes+1);
        else {
            // Damage remains live while phase1 finishes its current pattern,
            // so phase3 may inherit fewer than6HP. Require the full1364-frame
            // cycle, then at most six source42-frame orb-hit/grace intervals.
            assert(saw_checkpoint && second_started>450 && second_started<600);
            assert(ticks-practice_defeat_waits>second_started+1364 && ticks-practice_defeat_waits<=second_started+1364+6*42);
            assert(forced_lethal_miss);
        }
        assert(test_heap_count==blocks && test_heap_bytes==baseline && !g_canvas_target);
        assert(stage_score==score && hiscore==high && rem_bombs==bombs && pellet_speed==speed && stage_timer==timer);
        assert(test_obj_count==79 && test_obj_used==27840 && test_obj_attempts==objects);
        assert(!video_bomb_effect_active() && test_objs[40].hidden && test_objs[41].hidden && test_objs[17].hidden);
    }
    check_second_effects();check_death_renderer();check_defeat_renderer();
    {
        const int score=stage_score,high=hiscore,bombs=rem_bombs,speed=pellet_speed;
        const auto timer=stage_timer;
        const int sounds=std::count(test_effects.begin(),test_effects.end(),SFX_MISS);
        ticks=practice_deaths=practice_death_waits=0;
        heap_attempts=test_heap_attempts+3;obj_attempts=test_obj_attempts;
        test_vblank_hook=observe_practice_misses;test_keys_down=test_keys_held=0;
        assert(mima_phase1_review());
        test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;invincible=false;
        assert(practice_deaths==4 && ticks==1432);
        assert(std::count(test_effects.begin(),test_effects.end(),SFX_MISS)==sounds+3);
        assert(stage_score==score && hiscore==high && rem_bombs==bombs && pellet_speed==speed && stage_timer==timer);
        assert(test_heap_count==blocks && test_heap_bytes==baseline && test_obj_attempts==objects);
        assert(test_objs[0].hidden && test_objs[40].hidden && test_objs[41].hidden);
        std::puts("Mima miss flow: three real99-frame misses/grants then31-frame final-life burst, held HUD/resources, no fatal respawn/timer grant, preserved orb, exact SFX and practice cleanup passed");
    }
    load_boss_background();boss_render_hide();singyoku_init();boss_render_frame();
    std::puts("\nMima practice: ten buffered renderer lifecycles/all30 exact palette states/early-VBlank commits, denied allocations/audio rollback, full first-pattern cycle, live bomb and frozen pause, actual six-HP continuation/full second-block cycle/lethal-hit defeat before pending player miss, local score/resources and flat79-owner OBJ budget passed");
}
