// Run real shared boss, SinGyoku, orb, shot and pellet code without DS hardware.
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <vector>
#include "main/boss/boss.hpp"
#include "main/boss/singyoku.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/bullet/pellet.hpp"
#include "main/math/vector.hpp"
#include "main/progression.hpp"
#include "main/random.hpp"
#include "sound_effects.hpp"

rank_t rank = RANK_NORMAL;
int stage_score;
bool player_is_hit;
static PlayerState player{PLAYER_LEFT_START, player_top, PLAYER, 0};
static bool invincible;
static std::vector<int> effects;
const PlayerState &player_get_state() { return player; }
bool player_is_invincible() { return invincible; }
bool player_is_deflecting() { return false; }
bool player_is_sliding() { return false; }
void stage_cardcombo_reset() {}
void mdrv2_se_play(int id) { effects.push_back(id); }

static void outside_orb() { orb_set_position(10000, 10000); }

int main()
{
    initialize_irand(1234); orb_initialize(); shots_initialize(); Pellets.init();
    BossEntity entity{100, 100, 96, 96, 8, 8, 88, 88, true};
    assert(entity.hits_orb(100, 100) && entity.hits_orb(164, 164));
    assert(!entity.hits_orb(99, 100) && !entity.hits_orb(165, 164));
    entity.orb_active = false; assert(!entity.hits_orb(100, 100)); entity.orb_active = true;
    BossHealth health{}; health.reset(8);
    orb_set_position(120, 120); orb_set_velocity_x(OVX_8_LEFT);
    assert(boss_hit_update(health, entity, 3000, 100, 100, 96, 96));
    assert(health.hp == 7 && health.invincible && stage_score == 3000);
    assert(orb_get_velocity_x() == OVX_4_RIGHT && effects.back() == SFX_BOSS_DAMAGE);
    for (int i = 1; i <= 41; ++i) {
        assert(!boss_hit_update(health, entity, 3000, 100, 100, 96, 96));
        assert(health.invincible == (i <= 40));
    }
    assert(boss_hit_update(health, entity, 3000, 100, 100, 96, 96) && health.hp == 6);
    outside_orb();
    assert(boss_hit_update(health, entity, 3000, 100, 100, 96, 96, true) && health.hp == 5);
    for (int i = 0; i < 200; ++i) {
        assert(boss_bomb_damage_allowed(true, 0, i) == (i % 50 == 0));
        assert(!boss_bomb_damage_allowed(true, 1, i));
        assert(!boss_bomb_damage_allowed(false, 0, i));
    }
    health.reset(8); effects.clear(); shots_initialize();
    shots_add(110, 110);
    assert(!boss_hit_update(health, entity, 3000, 100, 100, 96, 96));
    assert(health.hp == 8 && effects == (std::vector<int>{SFX_SHOT, SFX_SHOT_HIT}));
    assert(shots_get_state().alive[0] && shots_get_state().sprite_number[0] == 1);
    assert(!shots_hit_boss(100, 100, 96, 96) && !shots_hit_pellet(110, 110));
    for (int i = 0; i < 7; ++i) shots_update();
    assert(!shots_get_state().alive[0]);

    Pellets.init(); outside_orb(); invincible = true; pellet_speed = 0;
    Pellets.add_single(100, 100, 0, 16, PM_CHASE, 64);
    auto &pel = Pellets.at(0);
    const int initial_x = pel.velocity.x.v, initial_y = pel.velocity.y.v;
    int vx, vy; vector2_between(100, 100, player_center_x() - PELLET_W, player_center_y(), vx, vy, 64);
    Pellets.update();
    assert(pel.speed.v == 64 && pel.velocity.x.v == initial_x + (vx > initial_x) - (vx < initial_x));
    assert(pel.velocity.y.v == initial_y + (vy > initial_y) - (vy < initial_y));
    pel.cur_top.v = (PLAYFIELD_BOTTOM - 80) * 16;
    vx = pel.velocity.x.v; vy = pel.velocity.y.v; Pellets.update();
    assert(pel.velocity.x.v == vx && pel.velocity.y.v == vy);
    Pellets.init(); Pellets.add_single(100, 100, 0, 0, PM_SLING_AIMED, 48);
    Pellets.at(0).sling_direction = 1;
    for (int i = 0; i < 32; ++i) Pellets.update();
    assert(Pellets.at(0).angle == 256 && Pellets.at(0).from_group == PG_NONE);
    Pellets.update(); assert(Pellets.at(0).from_group == PG_1_AIMED);
    vx = Pellets.at(0).velocity.x.v; vy = Pellets.at(0).velocity.y.v;
    Pellets.update(); assert(Pellets.at(0).velocity.x.v == vx && Pellets.at(0).velocity.y.v == vy);
    for (bool grace : {false, true}) {
        invincible = grace; Pellets.init();
        Pellets.add_single(player.x_position + 8, player_top + 1, 64, 16);
        assert(Pellets.update() == !grace);
    }

    invincible = true; outside_orb(); shots_initialize(); Pellets.init(); effects.clear();
    singyoku_init();
    for (int i = 0; i < 199; ++i) singyoku_update();
    assert(singyoku_state().phase == SG_ENTRANCE && singyoku_state().health.hp == 8);
    assert(Pellets.alive_count() == 0 && effects.empty());
    singyoku_update(); assert(singyoku_state().phase == SG_SPHERE && singyoku_state().frame == 0);
    for (int i = 0; i < 160; ++i) singyoku_update();
    assert(singyoku_state().pattern == 1 && singyoku_state().frame == 0);
    assert(Pellets.alive_count() == 15 && effects == (std::vector<int>{SFX_BOMB_START}));

    // Damage may not transition out of the slam halfway down the screen.
    for (int i = 0; i < 101; ++i) singyoku_update();
    while (singyoku_state().health.hp > 6) {
        const auto &e = singyoku_state().entity;
        orb_set_position(e.left + 24, e.top + 24); singyoku_update();
    }
    assert(singyoku_state().phase == SG_SPHERE && singyoku_state().pattern == 1);
    outside_orb();
    for (int i = 0; i < 500 && singyoku_state().phase != SG_TRANSFORM; ++i) singyoku_update();
    assert(singyoku_state().phase == SG_TRANSFORM && singyoku_state().entity.top < 96);
    unsigned seen = 0;
    for (int i = 0; i < 30000 && seen != 31; ++i) {
        seen |= 1u << singyoku_state().pattern;
        singyoku_update(); Pellets.update();
        assert(singyoku_state().entity.left >= 0 && singyoku_state().entity.left <= PLAYFIELD_RIGHT - 96);
        assert(singyoku_state().cel >= 0 && singyoku_state().cel < (singyoku_state().sheet == SG_SHEET_SPHERE ? 8 : singyoku_state().sheet == SG_SHEET_FLASH ? 3 : 5));
    }
    assert(seen == 31);
    while (singyoku_state().phase != SG_DEFEATED) {
        const auto &e = singyoku_state().entity;
        orb_set_position(e.left + 24, e.top + 24); singyoku_update();
    }
    assert(effects.back() == SFX_MISS && singyoku_state().health.hp == 0);
    const auto requests = effects.size();
    for (int i = 0; i < 60; ++i) singyoku_update();
    assert(effects.size() == requests);
    singyoku_init(); assert(singyoku_state().health.hp == 8 && singyoku_state().frame == 0);
    assert(scene_stage_count(0) == 5 && scene_stage_count(1) == 4);
    assert(stage_has_singyoku(0, 4) && !stage_has_singyoku(1, 4));
    assert(stage_number(0, 4) == 5 && stage_number(1, 0) == 6 && stage_number(2, 0) == 6);
    assert(route_first_scene(ROUTE_MAKAI) == 1 && route_first_scene(ROUTE_JIGOKU) == 2);
    std::puts("Boss tests: hitboxes, HP/i-frames, shot-only contact, bomb policy, chase/sling, grace, SinGyoku phases/patterns/defeat and route indexing passed");
}
