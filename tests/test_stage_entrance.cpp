// Actual stage entrance/renderers, observed at every VBlank. The oracle uses
// ReC98's per-card counters, NOT the renderer's frame/index shortcut.
#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
#include "heap_probe.hpp"
#include "stage_entrance.hpp"
#include "stage_render.hpp"
#include "tiles.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "video.hpp"
#include "upper_hud.hpp"
#include "player_render.hpp"
#include "orb_render.hpp"
#include "boss_render.hpp"
#include "background.hpp"
#include "palette_fx.hpp"
#include "main/boss/singyoku.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/timer.hpp"
#include "main/random.hpp"
#include "sound_effects.hpp"
#include "stagedat/stagedat0.h"
#include "stagedat/stagedat1.h"
#include "stagedat/stagedat2.h"
#include "stagedat/stagedat3.h"
#include "stagedat/stagedat4.h"
#include "stagedat/stagedat5.h"
#include "stagedat/stagedat6.h"

extern std::vector<int> test_effects;
namespace {
const int faces[] = {STGOBJ_CARD_0HP, STGOBJ_CARD_1HP, STGOBJ_CARD_2HP, STGOBJ_CARD_3HP};
const int halves[] = {STGOBJ_CARD_0HP_HALF, STGOBJ_CARD_1HP_HALF, STGOBJ_CARD_2HP_HALF, STGOBJ_CARD_3HP_HALF};
// Source indexes CARD_ANIM[hp+1]: edge belongs to the preceding face,
// back-half/final to the new one. The highest row repeats the 3HP art.
const int edges[] = {STGOBJ_CARD_1HP_EDGE, STGOBJ_CARD_2HP_EDGE, STGOBJ_CARD_3HP_EDGE, STGOBJ_CARD_3HP_EDGE};
std::vector<std::vector<int>> trace;
std::vector<StageCard> cards;
std::vector<StageObstacle> obstacles;
PlayerState player;
OrbState orb;
int ticks;
bool title;
bool boss;
unsigned timer;

void check_cell(int left, int top, int obj)
{
    const int mx = left / 16, my = (top - 64) / 16 + 3;
    for (int q = 0; q < 4; ++q) {
        const int x = mx + q % 2, y = my + q / 2;
        const u16 tile = BG_MAP_RAM(8)[(x / 32) * 1024 + y * 32 + x % 32];
        const int expected = (obj < 0 ? 0 : 1 + obj * 4 + q) | (15 << 12);
        if (tile != expected) std::fprintf(stderr,
            "Entrance tick%d title%d cell(%d,%d) quadrant%d: tile%04x expected%04x\n",
            ticks, title, left, top, q, tile, expected);
        assert(tile == expected);
    }
}

void make_trace()
{
    trace.clear();
    std::vector<int> frame_for(cards.size(), 0), image(cards.size(), -1);
    int total_frame = 0;
    for (;;) {
        int animated = 0;
        for (size_t i = 0; i < cards.size(); ++i) {
            if (frame_for[i] == -1) ++animated;
            if (frame_for[i] < 12) continue;
            if (frame_for[i] % 6 == 0) {
                const int hp = cards[i].hp;
                switch (frame_for[i] / 6) {
                case 2: image[i] = edges[hp]; break;
                case 3: image[i] = halves[hp]; break;
                case 4: image[i] = faces[hp]; break;
                default: assert(false);
                }
            }
            if (++frame_for[i] > 24) frame_for[i] = -1;
        }
        if (animated >= int(cards.size())) break;
        if (total_frame < int(cards.size())) frame_for[total_frame] = 12;
        ++total_frame;
        assert(total_frame <= 213); // full 200-card layout must terminate
        trace.push_back(image);
    }
    assert(trace.size() == (cards.empty() ? 0 : cards.size() + 13));
}

void observe()
{
    const bool ripple = !title && ticks < int(trace.size());
    assert(++ticks <= int(trace.size()) + 118);
    assert(camera.left == 32 && test_bgs[1].x == 32 && test_bgs[3].x == 32);
    assert(stage_timer == timer && stage_score == 12345 && stage_cardcombo_cur == 3);
    assert(stage_cardcombo_max == 7 && stage_cards_removed() == 0);
    assert(player_get_state().x_position == player.x_position && player_get_state().y_position == player.y_position);
    assert(orb_get_state().x_position == orb.x_position && orb_get_state().y_position == orb.y_position);
    assert(orb_get_state().sprite_number == orb.sprite_number && !orb_get_in_portal());
    if (boss) assert(singyoku_state().phase == SG_ENTRANCE && singyoku_state().frame == 0);
    assert(test_objs[0].hidden == title);
    for (int slot = 1; slot <= 40; ++slot) assert(test_objs[slot].hidden);
    assert(!test_objs[OAM_SLOT_HUD_LIVES].hidden && !test_objs[OAM_SLOT_HUD_BOMBS].hidden);
    assert(REG_WININ == (ripple ? 0x1919 : 0x1D1D));
    assert(test_objs[OAM_SLOT_HUD_LIVES].priority == (ripple ? 0 : 1));
    for (size_t i = 0; i < cards.size(); ++i) {
        const auto &c = stage_card(i), &before = cards[i];
        assert(c.left == before.left && c.top == before.top && c.hp == before.hp);
        assert(c.flag == CARD_ALIVE && c.flip_frame == 0);
        check_cell(c.left, c.top, ripple ? trace[ticks - 1][i] : faces[c.hp]);
    }
    for (size_t i = 0; i < obstacles.size(); ++i) {
        const auto &o = stage_obstacle(i), &before = obstacles[i];
        assert(o.left == before.left && o.top == before.top && o.type == before.type);
        assert(o.since_collision == 0 && o.fire_cycle == 0 && o.turret_flag == TF_READY);
        int obj = STGOBJ_TURRET;
        switch (o.type) {
        case OT_BUMPER: obj = STGOBJ_BUMPER; break;
        case OT_PORTAL: obj = STGOBJ_PORTAL; break;
        case OT_BAR_TOP: obj = STGOBJ_BAR_TOP; break;
        case OT_BAR_BOTTOM: obj = STGOBJ_BAR_BOTTOM; break;
        case OT_BAR_LEFT: obj = STGOBJ_BAR_LEFT; break;
        case OT_BAR_RIGHT: obj = STGOBJ_BAR_RIGHT; break;
        }
        check_cell(o.left, o.top, obj);
    }
}

void run(const ds_stage_layout_t &layout, bool show_title, bool prepare_boss = false)
{
    // Simulate the stale visuals that a real stage change must discard.
    for (int slot = 0; slot <= 40; ++slot) oamSetHidden(&oamMain, slot, false);
    canvas_playfield_clip(false); canvas_fill_rect(0, 0, VIEW_W, VIEW_H, 0x7fff);
    tiles_put(19, 9, STGOBJ_BUMPER);
    stage_entrance_prepare();
    for (int slot = 0; slot <= 40; ++slot) assert(test_objs[slot].hidden);
    assert(std::all_of(BG_MAP_RAM(8), BG_MAP_RAM(8) + 2048, [](u16 v) { return v == 0; }));
    assert(std::all_of(canvas_at(0, 0), canvas_at(0, VIEW_H), [](u16 v) { return v == 0; }));
    stage_init(layout.obstacle, layout.card); stage_reset();
    player_initialize(); orb_initialize(); shots_initialize();
    stage_score = 12345; stage_cardcombo_cur = 3; stage_cardcombo_max = 7;
    timer_init_for(0, 1); timer = stage_timer;
    upper_hud_render(3, 2, timer, UpperHudMode::Regular);
    camera.snap_center(); video_apply_camera();
    cards.assign(stage_cards, stage_cards + stage_card_count());
    obstacles.assign(stage_obstacles, stage_obstacles + stage_obstacle_count());
    player = player_get_state(); orb = orb_get_state();
    make_trace();
    ticks = 0; title = show_title; boss = prepare_boss;
    if (boss) {
        load_boss_background(); palette_apply_round();
        boss_render_hide(); singyoku_init(); boss_render_frame();
    }
    const std::vector<u16> palette(std::begin(SPRITE_PALETTE), std::end(SPRITE_PALETTE));
    test_effects.clear();
    initialize_irand(321); const int next_random = irand(); initialize_irand(321);
    const auto obj_attempts = test_obj_attempts, obj_bytes = test_obj_used;
    const auto heap_attempts = test_heap_attempts, heap_bytes = test_heap_bytes;
    test_vblank_hook = observe;
    stage_entrance_animate(12, show_title);
    test_vblank_hook = nullptr;
    assert(ticks == (show_title ? 118 : int(trace.size()) + 70));
    assert(test_effects == std::vector<int>(show_title ? 6 : 0, SFX_TITLE_GLYPH));
    assert(irand() == next_random);
    assert(test_obj_attempts == obj_attempts && test_obj_used == obj_bytes);
    assert(test_heap_attempts == heap_attempts && test_heap_bytes == heap_bytes);
    assert(std::equal(palette.begin(), palette.end(), SPRITE_PALETTE));
    assert(REG_WININ == 0x1919 && test_bgs[0].priority == 0 && test_objs[OAM_SLOT_HUD_LIVES].priority == 0);
    assert(test_bgs[2].sx == 256 && test_bgs[2].sy == 256 && test_bgs[2].x == 0 && test_bgs[2].y == 0);
    // The caller's ordinary render hands off to the serve without advancing
    // physics. No card flips/HP losses or render-cache discontinuity remain.
    const std::vector<u16> field(BG_MAP_RAM(8), BG_MAP_RAM(8) + 2048);
    stage_render_frame(); player_render_frame(); orb_render_frame();
    assert(std::equal(field.begin(), field.end(), BG_MAP_RAM(8)));
    assert(!test_objs[0].hidden); // orb's starting position is outside centered view
    assert(orb_get_state().x_position == orb.x_position && orb_get_state().y_position == orb.y_position);
    orb_set_position(320, 120); orb_render_frame(); assert(!test_objs[10].hidden);
}
}

void test_stage_entrances()
{
    const ds_stage_layout_t *scenes[] = {ds_stagedat0, ds_stagedat1, ds_stagedat2,
        ds_stagedat3, ds_stagedat4, ds_stagedat5, ds_stagedat6};
    for (const auto *scene : scenes)
        for (int stage = 0; stage < 4; ++stage)
            for (bool show_title : {false, true}) run(scene[stage], show_title);
    for (int count : {0, 1, 49, 50, 51, 200}) {
        ds_stage_layout_t layout{};
        for (int i = 0; i < count; ++i) {
            layout.card[i / 20][i % 20] = 1;
            if (i % 4) layout.obstacle[i / 20][i % 20] = OT_CARD_2FLIP + i % 4 - 1;
        }
        for (bool show_title : {false, true}) run(layout, show_title);
    }
    run(ds_stage_layout_t{}, true, true); // boss palette prepared, OAM still hidden
    std::puts("Stage entrance: all 28 regular layouts, every reveal frame/cel/order, scene/boss title paths, 0/1/49/50/51/200 cards, orb/player visibility, frozen state/RNG, HUD/camera restoration and zero allocations passed");
}
