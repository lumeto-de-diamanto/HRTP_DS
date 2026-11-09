// Execute the real shot/card/item hook sites with synthetic gameplay state.
#include <cstdint>
#include <cassert>
#include <vector>
#include <cstdio>
#include "main/player/shot.hpp"
#include "main/player/orb.hpp"
#include "main/player/player.hpp"
#include "main/stage/card.hpp"
#include "main/stage/item.hpp"
#include "sound_effects.hpp"

static std::vector<int> requests;
static OrbState orb{100, 100, 0};
static PlayerState player{100, player_top, PLAYER, 0};
static bool orbHit;
StageCard stage_cards[STAGE_MAX_CELLS]{};
int stage_card_total, stage_cards_removed_count, stage_score, stage_cardcombo_cur, stage_cardcombo_max;
void mdrv2_se_play(int id) { requests.push_back(id); }
bool orb_hit_by_shot(int, int) { return orbHit; }
const OrbState &orb_get_state() { return orb; }
const PlayerState &player_get_state() { return player; }
void popup_add(int, int, int) {}

int main() {
    shots_initialize();
    shots_add(PLAYFIELD_LEFT - 1, 100); shots_add(PLAYFIELD_RIGHT, 100);
    assert(requests.empty());
    for (int i = 0; i < SHOT_COUNT; ++i) shots_add(100 + i * 16, 100);
    assert(requests == std::vector<int>(SHOT_COUNT, SFX_SHOT));
    shots_add(100, 100); assert(requests.size() == SHOT_COUNT); // full pool is silent
    assert(shots_hit_pellet(100, 100) && requests.back() == SFX_SHOT_HIT);
    assert(!shots_hit_pellet(1000, 1000) && requests.size() == SHOT_COUNT + 1);
    shots_initialize(); shots_add(100, 100);
    const auto beforeOrb = requests.size();
    orbHit = true; shots_update();
    assert(requests.size() == beforeOrb); // orb hit has no generic collision SFX

    requests.clear(); Items.init();
    stage_card_total = 1;
    stage_cards[0] = StageCard{100, 100, CARD_ALIVE, 0, 1};
    cards_hittest(); cards_hittest();
    assert(requests == std::vector<int>{SFX_CARD});
    for (int i = 0; i < CARD_CELS * CARD_FRAMES_PER_CEL; ++i) cards_update();
    assert(requests.size() == 1 && stage_cards[0].flag == CARD_ALIVE);
    cards_hittest();
    assert(requests == (std::vector<int>{SFX_CARD, SFX_CARD})); // nonfinal flips also sound
    for (int i = 0; i < CARD_CELS * CARD_FRAMES_PER_CEL; ++i) cards_update();
    cards_hittest();
    assert(requests.size() == 2 && stage_cards[0].flag == CARD_REMOVED);

    for (int mode = 0; mode < 3; ++mode) {
        requests.clear(); Items.init(); rem_bombs = mode == 2 ? 5 : 0;
        Item &item = mode ? Items.bomb[0] : Items.point[0];
        item = Item{player.x_position, player.y_position, 0, IF_FALL, 0, 0};
        Items.update(); Items.update();
        assert(requests == std::vector<int>{SFX_PICKUP}); // point, bomb, over-cap once each
        assert(item.flag == (mode == 2 ? IF_COLLECTED_OVER_CAP : IF_COLLECTED));
    }
    std::puts("Gameplay SFX hooks: allocation-only shots, single hit/flip/pickup transactions, nonfinal cards and silent orb contact passed");
}
