// ReC98 th01/main/boss/b05.cpp, retargeted to the DS crop and native rendering.
#include "singyoku.hpp"
#include "main/bullet/pellet.hpp"
#include "main/player/player.hpp"
#include "main/math/vector.hpp"
#include "main/playfld.hpp"
#include "main/random.hpp"
#include "sound_effects.hpp"

namespace {
SinGyokuState state;
// Original base (272,96), with no crop-dependent coordinate translation.
constexpr int base_top = PLAYFIELD_TOP + ((PLAYFIELD_H / 21) * 5) - 48;
int rotation_interval, direction, spray_angle, spray_count;
int slam_x, slam_y, person_cel;
enum SlamPhase { SLAM_DOWN, SLAM_UP, SLAM_DONE };
SlamPhase slam_phase;

int for_rank(int easy, int normal, int hard, int lunatic)
{
    const int values[] = {easy, normal, hard, lunatic};
    return values[(int)rank >= 0 && (int)rank < 4 ? (int)rank : 1];
}

void show(SinGyokuSheet sheet, int cel)
{
    state.sheet = sheet;
    state.cel = cel;
}

void rotate(int delta)
{
    state.sphere_cel = (state.sphere_cel + delta + 8) % 8;
    show(SG_SHEET_SPHERE, state.sphere_cel);
}

void accelerate(int delta)
{
    if (state.frame < 50) {
        state.sphere_cel = 0;
        if (state.frame % 4 == 0)
            show(SG_SHEET_SPHERE, 0);
        return;
    }
    if (state.frame == 50)
        mdrv2_se_play(SFX_BOMB_START); // original shared ID8 transform/charge cue
    if (state.frame < 100 && state.frame % 4 == 0) {
        show(SG_SHEET_SPHERE, state.sphere_cel);
        if (state.frame == 60 || state.frame == 68 || state.frame > 82)
            rotate(delta);
    }
}

void spray()
{
    if (state.frame == 10)
        direction = (irand() % 2) ? 1 : -1;
    if (state.frame < 100) {
        accelerate(direction);
        return;
    }
    if (state.frame == 100) {
        spray_count = for_rank(10, 15, 20, 30);
        spray_angle = direction == -1 ? 0x00 : 0x80;
    }
    if (state.frame < 160) {
        // In the original the direction argument is a +/-1 interval and the
        // cel delta is +1, so the visual rotation remains forward either way.
        rotate(1);
        if (state.frame % (60 / spray_count) == 0) {
            Pellets.add_single(state.entity.left + 44, state.entity.top + 44,
                               spray_angle, 50, PM_REGULAR);
            spray_angle = (spray_angle - direction * 128 / spray_count) & 255;
        }
    } else
        state.frame = 0;
}

void slam()
{
    if (state.frame < 100) {
        accelerate(1);
        return;
    }
    if (state.frame == 100) {
        vector2_between(state.entity.left + 32, state.entity.top + 32,
            player_get_state().x_position, player_top, slam_x, slam_y,
            for_rank(4, 4, 5, 6));
        slam_phase = SLAM_DOWN;
    }
    if (slam_phase == SLAM_DOWN) {
        state.entity.left += slam_x;
        state.entity.top += slam_y;
        if (state.entity.top > PLAYFIELD_BOTTOM - 96) {
            slam_phase = SLAM_UP;
            slam_y = -4;
        }
    } else if (slam_phase == SLAM_UP) {
        state.entity.top -= 4;
        if (state.entity.top < base_top)
            slam_phase = SLAM_DONE; // retain the original return overshoot
    } else
        state.frame = 0;
    if (state.entity.left < PLAYFIELD_LEFT)
        state.entity.left = PLAYFIELD_LEFT;
    if (state.entity.left > PLAYFIELD_RIGHT - 96)
        state.entity.left = PLAYFIELD_RIGHT - 96;
    if (state.frame % 2 == 0)
        rotate(1);
    const int player_left = player_get_state().x_position;
    if (!player_is_invincible() && state.entity.left <= player_left &&
        state.entity.left + 64 >= player_left && state.entity.top >= player_top - 96)
        player_is_hit = true;
}

void person_attack(bool sling)
{
    for (int i = 0; i < 10; ++i) {
        if (sling)
            Pellets.add_single(state.entity.left + irand() % 96,
                state.entity.top + irand() % 96, 0, 0, PM_SLING_AIMED,
                for_rank(48, 64, 80, 96));
        else
            Pellets.add_single(state.entity.left + 44, state.entity.top + 44,
                irand() % 128, for_rank(48, 54, 60, 66), PM_REGULAR);
    }
}

void transform()
{
    const bool man = state.pattern == 1 || state.pattern == 3;
    const bool sling = state.pattern == 3;
    const int form = man ? 1 : 0;
    if (state.frame < 100) {
        accelerate(1);
        return;
    }
    switch (state.frame) {
    case 100: show(SG_SHEET_FLASH, 0); break;
    case 105: case 110: show(SG_SHEET_FLASH, 1 + form); break;
    case 115: person_cel = 3 * form; show(SG_SHEET_PERSON, person_cel); break;
    case 135:
        person_cel = 1 + 3 * form;
        show(SG_SHEET_PERSON, person_cel);
        if (man) person_attack(sling);
        break;
    case 160:
        // Woman has a second attack cel; man reuses his first attack cel.
        person_cel = man ? 4 : 2;
        show(SG_SHEET_PERSON, person_cel);
        if (man) person_attack(sling);
        break;
    case 185:
        person_cel = 3 * form;
        show(SG_SHEET_PERSON, person_cel);
        if (man) person_attack(sling);
        break;
    case 240: case 245: show(SG_SHEET_FLASH, 1 + form); break;
    case 250: show(SG_SHEET_FLASH, 0); break;
    case 260: show(SG_SHEET_SPHERE, 0); state.frame = 0; break;
    }
    // Source ordering deliberately redraws the person over the flash on
    // divisible-by-four frames between 135 and 240.
    if (state.frame > 135 && state.frame < 240 && state.frame % 4 == 0)
        show(SG_SHEET_PERSON, person_cel);
    if (state.frame <= 140 || state.frame % 8)
        return;
    if (state.pattern == 0 && state.frame <= 220) {
        const int speed = for_rank(55, 58, 62, 65);
        Pellets.add_single(state.entity.left + 44, state.entity.top + 44,
                           0xf0, 16, PM_CHASE, speed);
        Pellets.add_single(state.entity.left + 44, state.entity.top + 44,
                           0x90, 16, PM_CHASE, speed);
    } else if (state.pattern == 2) {
        const int speed = for_rank(60, 70, 75, 80);
        Pellets.add_single(state.entity.left + 12, state.entity.top + 44, 0x30, speed, PM_REGULAR);
        Pellets.add_single(state.entity.left + 84, state.entity.top + 44, 0x50, speed, PM_REGULAR);
    }
}
} // namespace

void singyoku_init()
{
    state = {};
    state.entity = {PLAYFIELD_CENTER_X - 48, base_top, 96, 96, 8, 8, 88, 88, false};
    state.health.reset(8);
    state.phase = SG_ENTRANCE;
    state.sheet = SG_SHEET_SPHERE;
    rotation_interval = 18;
    direction = 1;
    person_cel = 0;
    slam_phase = SLAM_DONE;
}

void singyoku_update(bool bomb_damage)
{
    if (state.phase == SG_DEFEATED)
        return;
    ++state.frame;
    if (state.phase == SG_ENTRANCE) {
        if (state.frame % rotation_interval == 0) {
            rotate(1);
            rotation_interval -= 2;
            if (rotation_interval < 1) rotation_interval = 1;
        }
        if (state.frame == 200) {
            state.phase = SG_SPHERE;
            state.frame = 0;
            state.entity.orb_active = true;
        }
        return;
    }
    if (state.hp_displayed < state.health.hp)
        ++state.hp_displayed;
    if (state.phase == SG_SPHERE) {
        if (state.pattern == 0) spray(); else slam();
        if (state.frame == 0) state.pattern = 1 - state.pattern;
    } else {
        if (state.pattern == 4) slam(); else transform();
        if (state.frame == 0) state.pattern = state.pattern == 4 ? irand() % 4 : 4;
    }
    boss_hit_update(state.health, state.entity, 3000,
        state.entity.left + 16, state.entity.top + 32, 112, 64, bomb_damage);
    if (state.hp_displayed > state.health.hp)
        state.hp_displayed = state.health.hp;
    if (state.phase == SG_SPHERE && state.health.hp <= 6 &&
        !state.health.invincible && state.pattern != 1) {
        state.phase = SG_TRANSFORM;
        state.pattern = 0;
        state.frame = 0;
    }
    if (state.health.hp <= 0) {
        state.phase = SG_DEFEATED;
        state.frame = 0;
        state.health.flashing = false;
        show(SG_SHEET_SPHERE, state.sphere_cel);
        mdrv2_se_play(SFX_MISS); // original SinGyoku defeat cue, not a player miss
    }
}

const SinGyokuState &singyoku_state() { return state; }
