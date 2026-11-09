#pragma once
#include <cstdint>

// Original ZIGOKU.MDE IDs. Each entry is one complete mono PCM sample.
enum SfxId {
    SFX_SHOT = 1, SFX_CARD = 2, SFX_BOSS_DAMAGE = 3, SFX_ROUTE_CONFIRM = 4,
    SFX_MISS = 5, SFX_ATTACK_6 = 6, SFX_ATTACK_7 = 7, SFX_BOMB_START = 8,
    SFX_BOMB_ACTIVE = 9, SFX_ATTACK_10 = 10, SFX_SLIDE = 11,
    SFX_ATTACK_12 = 12, SFX_UNUSED_13 = 13, SFX_TITLE_GLYPH = 14,
    SFX_PICKUP = 15, SFX_SHOT_HIT = 16, SFX_HURRY_UP = 17,
};

bool sfx_init();
void mdrv2_se_play(int id); // nonblocking; no filesystem access or VBlank wait
void sfx_stop();           // explicit whole-SFX lifecycle, separate from BGM
uint32_t sfx_dropped_requests();
