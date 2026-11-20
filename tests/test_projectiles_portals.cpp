// Real projectile, portal and orb code; only player/audio/RNG services are fake.
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>
#include "main/bullet/pellet.hpp"
#include "main/player/orb.hpp"
#include "main/player/player.hpp"
#include "main/player/shot.hpp"
#include "main/playfld.hpp"
#include "main/stage/stageobj.hpp"
#include "sound_effects.hpp"

rank_t rank = RANK_NORMAL;
bool player_is_hit;
static PlayerState player{PLAYER_LEFT_START, player_top, PLAYER, 0};
const PlayerState &player_get_state() { return player; }
static bool invincible = true;
bool player_is_invincible() { return invincible; }
bool player_is_deflecting() { return false; }
bool player_is_sliding() { return false; }
static std::vector<int> effects;
void mdrv2_se_play(int id) { effects.push_back(id); }

static int portal_frame, exit_roll, exit_force = 9;
static bool fallback;
int irand() {
    if (portal_frame == 40) return exit_roll++ == 0 ? OVX_4_RIGHT : exit_force + 9;
    return fallback ? 1 : 0; // select a peer, or force the existing self fallback
}
extern fixed orb_force;

static void card_presence_and_hp()
{
    // ReC98 creates cards ONLY from the card bitfield, then looks up obstacle
    // HP markers at those existing card coordinates. An orphan marker creates
    // neither a card nor an obstacle (Makai18 has one at column1,row0).
    uint8_t obstacles[STAGE_ROWS][STAGE_COLS] = {};
    uint8_t cards[STAGE_ROWS][STAGE_COLS] = {};
    for(int y=0;y<STAGE_ROWS;++y) for(int x=0;x<STAGE_COLS;++x) {
        for(int type:{int(OT_NONE),int(OT_CARD_2FLIP),int(OT_CARD_3FLIP),int(OT_CARD_4FLIP)}) {
            obstacles[y][x]=type;
            for(int present:{0,1}) {
                cards[y][x]=present;
                stage_init(obstacles,cards);stage_reset();
                assert(stage_card_count()==present && stage_obstacle_count()==0);
                assert(stage_cards_removed()==0 && !stage_is_cleared());
                if(present) {
                    const auto &c=stage_card(0);
                    assert(c.left==x*32 && c.top==64+y*32);
                    assert(c.hp==(type ? type-13 : 0));
                    assert(c.flag==CARD_ALIVE && c.flip_frame==0);
                    stage_cards_removed_count=1;assert(stage_is_cleared());
                }
            }
            cards[y][x]=0;
        }
        obstacles[y][x]=0;
    }
    stage_init(obstacles,cards);stage_reset();
    std::puts("Stage cards: all200 cells, presence-bit authority, plain/2/3/4-flip HP, ignored orphan markers and correct clear counts passed");
}

static void orb_ceiling_bounces()
{
    // Whole-pixel Q16.16 conversion, including negative swing forces and both
    // representable endpoints. Compare exact fixed values, not rounded pixels.
    for(int value:{-32768,-32767,-100,-13,-1,0,1,13,100,32767}) {
        const fixed converted(value),exact=fixed::from_raw(value*65536);
        assert(!(converted<exact) && !(converted>exact));
        assert(converted.trunc_to_int()==value);
    }
    for(int value:{-100,-13,-1,0,1,13,100}) {
        const auto fraction=fixed::from_raw(32768);
        const auto sum=fraction+value,difference=fraction-value;
        const auto expected_sum=fixed::from_raw(32768+value*65536);
        const auto expected_difference=fixed::from_raw(32768-value*65536);
        assert(!(sum<expected_sum) && !(sum>expected_sum));
        assert(!(difference<expected_difference) && !(difference>expected_difference));
    }
    // Exercise the real repel path that implicitly constructs fixed(-13).
    // Nonzero horizontal separation avoids the unrelated centered-orb RNG.
    for(int friction=1;friction<=OR_MAX;++friction) for(int side:{-1,1}) {
        orb_initialize();orb_set_position(player.x_position+side*8,player.y_position);
        assert(!orb_hits_player(player.x_position,player.y_position,friction,false,false));
        const auto force=fixed::from_raw((friction/2-13)*65536);
        assert(!(orb_force<force) && !(orb_force>force));
        assert(orb_get_velocity_x()==(side<0 ? OVX_4_LEFT : OVX_4_RIGHT));
        assert(orb_get_state().x_position==player.x_position+side*8 && orb_get_state().y_position==player.y_position);
    }
    // The longer YuugenMagan practice reaches a negative numerator in fixed
    // division. Check exact raw results, not rounded pixel conversions.
    constexpr auto quotient=fixed::from_raw(-293600)/fixed(2.5);
    constexpr auto expected=fixed::from_raw(-117440);
    static_assert(!(quotient<expected) && !(quotient>expected));
    for(int32 numerator:{-2147483647,-293600,-1,0,1,293600,2147483647})
        for(int32 denominator:{-163840,-65536,65536,163840}) {
            const auto result=fixed::from_raw(numerator)/fixed::from_raw(denominator);
            const auto exact=fixed::from_raw(int32(int64(numerator)*65536/denominator));
            assert(!(result<exact) && !(result>exact));
        }
    // ReC98 orb_velocity_y_update() returns pixel_t (int), so truncate each
    // displacement BEFORE adding it to the integer position. The fixed class's
    // ordinary int conversion intentionally still rounds to nearest.
    for (double force : {-20.0, -1.75, -1.5, -0.75, -0.5, 0.5, 0.75, 1.5, 1.75, 20.0}) {
        orb_initialize(); orb_set_position(300, 200); orb_set_velocity_x(OVX_0);
        orb_force_new(force, OF_IMMEDIATE); orb_update();
        assert(orb_get_state().y_position == 200 + int(force));
    }
    assert(int(fixed(0.75)) == 1 && int(fixed(-0.75)) == -1);

    // Literal double/integer source oracle; no player, shots or stage obstacles
    // inject new force. -17.5 used to settle into a perpetual 73-frame full-height
    // orbit instead of losing energy after the first ceiling contact.
    for (double initial : {-18.0, -17.5, -16.0, -13.0, -10.0, -8.0}) {
        orb_initialize(); orb_set_position(300, 368); orb_set_velocity_x(OVX_0);
        orb_force_new(initial, OF_IMMEDIATE);
        int y = 368, age = 0, ceiling_contacts = 0, first_floor = 0;
        double force = initial;
        for (int frame = 1; frame <= 2000; ++frame) {
            ++age;
            const double gravity = age / 5 + force;
            const double velocity = gravity > 16 ? 16 : gravity < -16 ? -16 : gravity;
            y += int(gravity); // source's unclamped pixel_t return
            if (y > PLAYFIELD_BOTTOM - ORB_H) {
                force = -velocity * 0.78; age = 0; y = PLAYFIELD_BOTTOM - ORB_H;
                if (!first_floor) first_floor = frame;
            }
            if (y < PLAYFIELD_TOP) {
                force = -velocity - age / 4; age = 0; y = PLAYFIELD_TOP;
                ++ceiling_contacts;
            }
            orb_update();
            assert(orb_get_state().y_position == y);
        }
        if (initial == -17.5) assert(ceiling_contacts == 1 && first_floor == 49);
    }
    std::puts("Orb: exact signed fixed conversion/offsets and real repel forces, source truncation/unclamped movement, unchanged fixed rounding, ceiling/floor trajectories and no self-sustaining -17.5 loop passed");
}

static void pellet_orb_contacts()
{
    orb_initialize(); orb_set_position(300, 180);
    shots_initialize(); pellet_speed = 0;
    for (int difficulty = RANK_EASY; difficulty <= RANK_LUNATIC; ++difficulty) {
        rank = static_cast<rank_t>(difficulty);
        for (int dx = -18; dx <= 34; ++dx) {
            for (int dy = -10; dy <= 34; ++dy) {
                for (int fraction : {0, 1, 15}) {
                    for (int direction : {-1, 1}) {
                        Pellets.init(); stage_score = 0;
                        Pellets.add_group(300, 180, PG_1, 4 * 16);
                        auto &p = Pellets.at(0);
                        const int vx = direction * 31, vy = -direction * 17;
                        const int end_x = (300 + dx) * 16 + fraction;
                        const int end_y = (180 + dy) * 16 + fraction;
                        p.cur_left.v = end_x - vx; p.cur_top.v = end_y - vy;
                        p.velocity.x.v = vx; p.velocity.y.v = vy;
                        // Independent expansion of ReC98's strict overlap:
                        // pellet16x8 versus orb32x32, integer-pixel positions.
                        const bool hit = dx > -16 && dx < 32 && dy > -8 && dy < 32;
                        assert(!Pellets.update());
                        assert(p.cur_left.v == end_x && p.cur_top.v == end_y);
                        assert(p.velocity.x.v == (hit ? vx >> 1 : vx));
                        assert(p.velocity.y.v == (hit ? vy >> 1 : vy));
                        assert(p.decay_frame == int(hit));
                        assert(stage_score == (hit ? 10 : 0));
                    }
                }
            }
        }
    }
    // Decay cannot award the same contact again or repeatedly halve velocity.
    Pellets.init(); stage_score = 0;
    Pellets.add_group(320, 200, PG_1, 4 * 16);
    auto &p = Pellets.at(0);
    p.velocity.x.v = -33; p.velocity.y.v = 65;
    assert(!Pellets.update() && p.decay_frame == 1 && stage_score == 10);
    assert(p.velocity.x.v == -17 && p.velocity.y.v == 32);
    orb_set_position(10000, 10000);
    for (int frame = 1; frame <= PELLET_DECAY_FRAMES + 1; ++frame) {
        const int x = p.cur_left.v, y = p.cur_top.v;
        assert(!Pellets.update());
        assert(p.cur_left.v == x - 17 && p.cur_top.v == y + 32);
        assert(p.velocity.x.v == -17 && p.velocity.y.v == 32 && stage_score == 10);
        assert(p.moving == (frame <= PELLET_DECAY_FRAMES));
    }
    std::puts("Projectiles: source overlap boundaries/subpixels/all ranks, signed half-speed, single scoring and decay passed");
}

static void periodic_pellet_speed()
{
    const int old_speed=pellet_speed;const auto old_rank=rank;
    for(int lives=0;lives<=6;++lives) for(int bombs=0;bombs<=5;++bombs)
        for(int initial:{-15,-4,0,19,20}) {
            const unsigned cycle=1800-lives*200-bombs*50;
            pellet_speed=initial;int expected=initial;
            for(unsigned tick=1;tick<=cycle*3;++tick) {
                if(tick%cycle==0) expected=std::min(expected+1,20);
                pellet_speed_update(tick,lives,bombs);
                assert(pellet_speed==expected);
            }
        }
    // Re-evaluate the interval against the global phase, not a countdown reset
    // by resource changes. At1100, a newly changed1200-frame cycle must not fire.
    pellet_speed=0;pellet_speed_update(1100,3,0);assert(pellet_speed==0);
    pellet_speed_update(1200,3,0);assert(pellet_speed==1);
    pellet_speed_update(1400,2,0);assert(pellet_speed==2);
    pellet_speed_update(0,3,2);assert(pellet_speed==3); // unsigned frame-counter wrap
    pellet_speed_update(100,9,0);assert(pellet_speed==3); // invalid debug zero cycle
    // Only new volleys use the changed multiplier; live pellets never accelerate.
    rank=RANK_EASY;pellet_speed=0;Pellets.init();Pellets.add_group(100,100,PG_1,64);
    const auto live=Pellets.at(0);
    pellet_speed_update(1100,3,2);assert(pellet_speed==1);
    assert(!std::memcmp(&live,&Pellets.at(0),sizeof(live)));
    Pellets.add_group(200,100,PG_1,64);
    assert(Pellets.at(0).velocity.y.v==64 && Pellets.at(1).velocity.y.v==65);
    Pellets.init();rank=old_rank;pellet_speed=old_speed;
    std::puts("Pellet-speed creep: every normal life/bomb interval, three cycles/all speed limits, live resource-phase changes, unsigned wrap and new-volley-only tuning passed");
}

static void ofuda_contacts()
{
    for (int difficulty = RANK_EASY; difficulty <= RANK_LUNATIC; ++difficulty) {
        rank = static_cast<rank_t>(difficulty);
        // Plain contact, simultaneous orb contact, and a player-threatening
        // pellet must all resolve to immediate interception by the ofuda.
        for (int mode = 0; mode < 3; ++mode) {
            Pellets.init(); shots_initialize(); effects.clear(); stage_score = 0;
            const int x = mode == 2 ? player.x_position + 8 : 300;
            const int y = mode == 2 ? player_top : 160;
            invincible = mode != 2;
            orb_set_position(mode == 1 ? x : 10000, mode == 1 ? y : 10000);
            shots_add(x, y);
            Pellets.add_group(x, y, PG_1, 3 * 16);
            auto &p = Pellets.at(0);
            p.velocity.x.v = p.velocity.y.v = 0;
            assert(!Pellets.update());
            assert(!p.moving && p.decay_frame == 0 && Pellets.alive_count() == 0);
            assert(stage_score == 0); // only orb destruction awards10
            assert(effects == (std::vector<int>{SFX_SHOT, SFX_SHOT_HIT}));
            assert(shots_get_state().alive[0] && shots_get_state().sprite_number[0] == 1);
            assert(!shots_hit_pellet(x, y) && !shots_hit_boss(x, y, 96, 96));
            // A second pellet is not erased by the non-moving impact sprite.
            invincible = true; orb_set_position(10000, 10000);
            Pellets.add_group(x, y, PG_1, 3 * 16);
            p.velocity.x.v = p.velocity.y.v = 0;
            assert(!Pellets.update() && p.moving && p.decay_frame == 0);
            assert(effects.size() == 2);
            for (int frame = 1; frame <= 7; ++frame) {
                shots_update();
                assert(shots_get_state().x_position[0] == x && shots_get_state().y_position[0] == y);
                assert(shots_get_state().alive[0] == (frame < 7));
                if (frame < 7) assert(shots_get_state().sprite_number[0] == (frame + 1) / 4 + 1);
            }
            shots_add(x + 40, y);
            assert(shots_get_state().alive[0] && shots_get_state().sprite_number[0] == 0);
            assert(shots_get_state().x_position[0] == x + 40); // slot reusable after animation
        }
    }
    // Pellets already shattering from the orb remain untouched by ofuda.
    Pellets.init(); shots_initialize(); orb_set_position(10000, 10000);
    Pellets.add_group(300, 160, PG_1, 3 * 16);
    auto &p = Pellets.at(0);
    p.velocity.x.v = p.velocity.y.v = 0; p.decay_frame = 1;
    shots_add(300, 160); effects.clear();
    assert(!Pellets.update() && p.moving && p.decay_frame == 2);
    assert(shots_get_state().alive[0] && shots_get_state().sprite_number[0] == 0 && effects.empty());
    std::puts("Ofuda: immediate pellet erase, stationary impact cels/lifetime, single SFX/contact, hit priority and slot reuse passed");
}

static void spinning_pellets()
{
    orb_set_position(10000,10000);shots_initialize();invincible=true;
    for(int difficulty=0;difficulty<4;++difficulty) for(int knob:{-15,0,20})
    for(int aim:{0,32,64,96,128,192,255}) {
        rank=rank_t(difficulty);pellet_speed=knob;Pellets.init();
        for(int i=0;i<8;++i) {
            const int x=316+((16*Cos8(i*32))>>8),y=224+((16*Sin8(i*32))>>8);
            Pellets.add_single(x,y,aim,48,PM_SPIN,32,312,224);
        }
        assert(Pellets.alive_count()==8);
        int tuned=48+(difficulty==0 ? 0 : difficulty*6);tuned+=(knob*tuned)/40;
        for(int i=0;i<8;++i) {
            const auto &p=Pellets.at(i);
            assert(p.velocity.x.v==((tuned*Cos8(aim))>>8) && p.velocity.y.v==((tuned*Sin8(aim))>>8));
            assert(p.spin_velocity.x.v==((32*Cos8(aim))>>8) && p.spin_velocity.y.v==((32*Sin8(aim))>>8));
        }
        for(int f=0;f<64;++f) {
            assert(!Pellets.update());
            for(int i=0;i<8;++i) {
                const auto &p=Pellets.at(i);
                const int start=iatan2((16*Sin8(i*32))>>8,4+((16*Cos8(i*32))>>8));
                const int a=(start+f*4)&255;
                assert(p.moving && p.cur_left.v==312*16+f*((32*Cos8(aim))>>8)+Cos8(a));
                assert(p.cur_top.v==224*16+f*((32*Sin8(aim))>>8)+Sin8(a));
                assert(p.spin_center.x.v==312*16+(f+1)*((32*Cos8(aim))>>8));
                assert(p.spin_center.y.v==224*16+(f+1)*((32*Sin8(aim))>>8));
                assert(!p.velocity.x.v && !p.velocity.y.v && p.angle==start+(f+1)*4);
            }
        }
    }
    // Decay keeps PM_SPIN automation; bomb/orb velocity changes do not alter
    // the independent center drift, and the shatter still expires normally.
    rank=RANK_EASY;pellet_speed=0;Pellets.init();
    Pellets.add_single(332,224,64,48,PM_SPIN,32,312,224);Pellets.decay();
    for(int f=0;f<21;++f) {
        Pellets.update();const auto &p=Pellets.at(0);
        assert(p.spin_center.y.v==224*16+(f+1)*32 && p.cur_top.v==224*16+f*32+Sin8(f*4));
        assert(p.moving==(f<20));
    }
    // A reused slot must initialize a fresh center/angle, not retain old spin.
    Pellets.add_single(200,200,128,48,PM_SPIN,32,184,200);
    assert(Pellets.at(0).angle==0 && Pellets.at(0).spin_center.x.v==184*16);
    Pellets.update();assert(Pellets.at(0).cur_left.v==200*16 && Pellets.at(0).spin_center.x.v==182*16);
    Pellets.init();
    for(int i=0;i<101;++i) Pellets.add_single(332,224,64,48,PM_SPIN,32,312,224);
    assert(Pellets.alive_count()==100);Pellets.init();
    const auto old_player=player;player.x_position=320;player.y_position=224;invincible=false;
    // Spin position is resolved BEFORE ordinary shot/orb/player hit tests.
    orb_set_position(328,224);shots_initialize();shots_add(328,224);stage_score=0;
    Pellets.add_single(332,224,64,48,PM_SPIN,32,312,224);
    assert(!Pellets.update() && !Pellets.at(0).moving && stage_score==0);
    shots_initialize();Pellets.init();Pellets.add_single(332,224,64,48,PM_SPIN,32,312,224);
    assert(!Pellets.update() && Pellets.at(0).decay_frame==1 && stage_score==10);
    Pellets.update();assert(Pellets.at(0).decay_frame==2 && stage_score==10 && Pellets.at(0).spin_center.y.v==228*16);
    orb_set_position(10000,10000);Pellets.init();Pellets.add_single(332,224,64,48,PM_SPIN,32,312,224);
    assert(Pellets.update()); // a spinning ring is damaging, not decoration
    player=old_player;invincible=true;Pellets.init();
    std::puts("Spin pellets: all ranks/speed offsets/aim octants, exact Q4 radius/offset-angle/old-center-before-drift order through64 frames, rank-independent2px center speed, decay automation/lifetime, shot/orb/player hit ordering, slot reuse and100-slot capacity passed");
}

static int expected_portal_cel(int frame, bool entered, bool destination)
{
    if (entered && frame < 10) return 0;
    if (entered && frame < 20) return 1;
    if (destination && frame >= 20 && frame < 30) return 1;
    if (destination && frame >= 30 && frame < 40) return 0;
    return -1;
}

static void barrier_resets()
{
    for(int type:{int(OT_BAR_LEFT),int(OT_BAR_RIGHT)}) for(bool fast:{false,true}) {
        const auto incoming=type==OT_BAR_LEFT ? (fast ? OVX_8_RIGHT : OVX_4_RIGHT) :
            (fast ? OVX_8_LEFT : OVX_4_LEFT);
        const auto reflected=type==OT_BAR_LEFT ? (fast ? OVX_8_LEFT : OVX_4_LEFT) :
            (fast ? OVX_8_RIGHT : OVX_4_RIGHT);
        uint8_t obstacles[STAGE_ROWS][STAGE_COLS]={};
        const uint8_t cards[STAGE_ROWS][STAGE_COLS]={};
        obstacles[2][6]=obstacles[2][10]=type;
        const auto aim_at=[&](int slot) {
            const auto &bar=stage_obstacle(slot);
            orb_set_position(bar.left+(type==OT_BAR_LEFT ? -8 : 8),bar.top);
            orb_set_velocity_x(incoming);
        };
        for(bool reload:{false,true}) for(int elapsed=0;elapsed<6;++elapsed) {
            stage_init(obstacles,cards);stage_reset();orb_initialize();aim_at(0);
            stage_obstacles_update();
            assert(orb_get_velocity_x()==reflected && stage_obstacle(0).since_collision==2);
            for(int tick=0;tick<elapsed;++tick) {
                aim_at(1);stage_obstacles_update();
                assert(orb_get_velocity_x()==incoming && !stage_obstacle(1).since_collision);
            }
            assert(stage_obstacle(0).since_collision==2+elapsed);
            // Misses reset in place; retries/stage changes rebuild first. Both
            // must clear the shared lock along with the per-bar cooldowns.
            if(reload) stage_init(obstacles,cards);
            stage_reset();
            assert(!stage_obstacle(0).since_collision && !stage_obstacle(1).since_collision);
            aim_at(1);stage_obstacles_update();
            assert(orb_get_velocity_x()==reflected && stage_obstacle(1).since_collision==2);
        }
        // Ordinary updates must still retain the cross-bar lock until the
        // original eight-count cooldown releases it, not clear it every frame.
        stage_init(obstacles,cards);stage_reset();orb_initialize();aim_at(0);stage_obstacles_update();
        for(int tick=1;tick<=6;++tick) {
            aim_at(1);stage_obstacles_update();
            assert(orb_get_velocity_x()==(tick<6 ? incoming : reflected));
            assert(stage_obstacle(1).since_collision==(tick<6 ? 0 : 2));
        }
        assert(!stage_obstacle(0).since_collision);
    }
    std::puts("Vertical barriers: both sides/speeds, all active cooldown reset phases, miss/reload lock recovery and unchanged cross-bar cooldown expiry passed");
}

static void portals()
{
    for (int count : {1, 2}) for (int entry = 0; entry < count; ++entry) {
        for (bool self : {false, true}) for (int force = -9; force <= 9; ++force) {
            uint8_t obstacles[STAGE_ROWS][STAGE_COLS] = {};
            const uint8_t cards[STAGE_ROWS][STAGE_COLS] = {};
            obstacles[0][0] = OT_BUMPER; // entered portal need not be slot0
            for (int i = 0; i < count; ++i) obstacles[2][6 + i * 4] = OT_PORTAL;
            stage_init(obstacles, cards); stage_reset(); orb_initialize();
            const int source = entry + 1;
            const int destination = count == 1 || self ? source : 2 - entry;
            fallback = self; exit_force = force; exit_roll = 0;
            orb_set_position(stage_obstacle(source).left, stage_obstacle(source).top);
            for (int frame = 1; frame <= 60; ++frame) {
                portal_frame = frame;
                if (frame > 40) {
                    // Hold the orb on the other portal while blocked; move it
                    // clear at60 so legitimate re-entry does not start a new trip.
                    const int other = count == 2 ? 2 - entry : source;
                    orb_set_position(frame == 60 ? 10000 : stage_obstacle(other).left,
                                     frame == 60 ? 10000 : stage_obstacle(other).top);
                }
                stage_obstacles_update();
                assert(orb_get_in_portal() == (frame < 40));
                for (int slot = 0; slot < stage_obstacle_count(); ++slot)
                    assert(stage_portal_anim(slot) == expected_portal_cel(
                        frame, slot == source, slot == destination));
                if (frame == 40) {
                    assert(orb_get_state().x_position == stage_obstacle(destination).left);
                    assert(orb_get_state().y_position == stage_obstacle(destination).top);
                    assert(orb_get_velocity_x() == OVX_4_RIGHT && int(orb_force) == force);
                    assert(exit_roll == 2);
                }
            }
            assert(stage_obstacle(source).since_collision == 0);
            orb_set_position(stage_obstacle(destination).left, stage_obstacle(destination).top);
            portal_frame = 61; stage_obstacles_update();
            assert(orb_get_in_portal() && stage_portal_anim(destination) == 0);
            stage_reset();
            assert(!orb_get_in_portal());
            for (int slot = 0; slot < stage_obstacle_count(); ++slot)
                assert(stage_portal_anim(slot) == -1 && stage_obstacle(slot).since_collision == 0);
        }
    }
    std::puts("Portals: single/paired/self fallback, both slot orders, all 60 frames/19 exit forces, cooldown/reset/re-entry passed");
}

static void delay_clouds()
{
    orb_set_position(10000,10000);invincible=true;rank=RANK_EASY;pellet_speed=0;
    shots_initialize();Pellets.init();Pellets.spawn_with_cloud=true;
    Pellets.add_single(100,100,0,32);Pellets.spawn_with_cloud=false;
    const int x=Pellets.at(0).cur_left.v;
    for(int age=2;age<=8;++age) {
        assert(!Pellets.update());const auto &p=Pellets.at(0);
        assert(p.cloud_frame==age && !p.moving && p.cur_left.v==x && Pellets.alive_count()==1);
        Pellets.decay();assert(!p.decay_frame); // bombs do not invent moving clouds
    }
    Pellets.update();assert(Pellets.at(0).moving && !Pellets.at(0).cloud_frame && Pellets.at(0).cloud_released);
    assert(Pellets.at(0).cur_left.v==x);
    Pellets.update();assert(!Pellets.at(0).cloud_released && Pellets.at(0).cur_left.v==x+32);
    Pellets.init();Pellets.spawn_with_cloud=true;
    for(int i=0;i<101;++i) Pellets.add_single(100+i,100,0,32);
    assert(Pellets.alive_count()==100);
    Pellets.init();assert(!Pellets.spawn_with_cloud && !Pellets.alive_count());
    Pellets.add_single(100,100,0,32);Pellets.decay();
    Pellets.spawn_with_cloud=true;Pellets.add_single(200,100,0,32);Pellets.spawn_with_cloud=false;
    const auto cloud=Pellets.at(1);
    Pellets.reset_nonclouds();
    assert(!Pellets.at(0).moving && !Pellets.at(0).decay_frame && Pellets.alive_count()==1);
    assert(!std::memcmp(&cloud,&Pellets.at(1),sizeof(cloud)));
    Pellets.reset_nonclouds();assert(!std::memcmp(&cloud,&Pellets.at(1),sizeof(cloud)));
    Pellets.init();
    std::puts("Delay clouds: source2..8 cel timing, release after movement/collision, reserved100-slot capacity, unchanged bomb behavior and ordinary-pellet default passed");
}

#include "test_score_extends.cpp"

int main(int argc, char **argv)
{
    if (argc == 1) score_extends_rules();
    if (argc == 1) card_presence_and_hp();
    if (argc == 1 || std::strcmp(argv[1], "orb") == 0) orb_ceiling_bounces();
    if (argc == 1) periodic_pellet_speed();
    if (argc == 1 || std::strcmp(argv[1], "pellets") == 0) pellet_orb_contacts();
    if (argc == 1 || std::strcmp(argv[1], "pellets") == 0) delay_clouds();
    if (argc == 1 || std::strcmp(argv[1], "pellets") == 0) spinning_pellets();
    if (argc == 1 || std::strcmp(argv[1], "ofuda") == 0) ofuda_contacts();
    if (argc == 1) barrier_resets();
    if (argc == 1 || std::strcmp(argv[1], "portals") == 0) portals();
}
