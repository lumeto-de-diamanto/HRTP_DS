// Real card hit/flip/scoring logic; presentation, drops and orb state are seams.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
#include "main/stage/card.hpp"
#include "main/stage/item.hpp"
#include "rank.h"
#include "sound_effects.hpp"

rank_t rank=RANK_NORMAL;
StageCard stage_cards[STAGE_MAX_CELLS]{};
int stage_card_total,stage_cards_removed_count,stage_score,stage_cardcombo_cur,stage_cardcombo_max;
CItems Items;
static OrbState orb;
static std::vector<int> popups,effects;
const OrbState &orb_get_state() { return orb; }
int irand() { return 1; } // successful bomb flip, independent of score calculation
void CItems::add_point(int,int) {}
void CItems::add_bomb(int,int) {}
void popup_add(int,int,int value) { popups.push_back(value); }
void mdrv2_se_play(int id) { effects.push_back(id); }

int main()
{
    const int bases[]={100,200,300,400},factors[]={20,20,20,35};
    for(int stage=0;stage<20;++stage) for(int difficulty=0;difficulty<4;++difficulty)
        for(bool bomb:{false,true}) for(int combo=0;combo<=50;++combo) {
            rank=rank_t(difficulty);orb={bomb ? 10000 : 100,100,0};
            stage_card_total=1;stage_cards_removed_count=0;
            stage_cards[0]={100,100,CARD_ALIVE,0,1};
            stage_score=1234;stage_cardcombo_cur=stage_cardcombo_max=combo;
            popups.clear();effects.clear();
            const int points=std::min(25600,bases[stage/5]+combo*combo*factors[difficulty]);
            cards_hittest(stage,bomb,0);
            assert(stage_score==1234+points && stage_cardcombo_cur==combo+1 && stage_cardcombo_max==combo+1);
            assert(popups==std::vector<int>{points} && effects==std::vector<int>{SFX_CARD});
            assert(stage_cards[0].flag==CARD_FLIPPING && !stage_cards[0].flip_frame);
            cards_hittest(stage,bomb,0);
            assert(stage_score==1234+points && popups.size()==1 && effects.size()==1);
            for(int tick=0;tick<30;++tick) cards_update();
            assert(stage_cards[0].flag==CARD_ALIVE && stage_cards[0].hp==0 && !stage_cards_removed_count);
            // A surviving multi-HP card earns the NEXT combo value on its next flip.
            const int next=std::min(25600,bases[stage/5]+(combo+1)*(combo+1)*factors[difficulty]);
            cards_hittest(stage,bomb,0);
            assert(stage_score==1234+points+next && popups.back()==next && effects.size()==2);
            for(int tick=0;tick<30;++tick) cards_update();
            assert(stage_cards[0].flag==CARD_REMOVED && stage_cards_removed_count==1);
            cards_hittest(stage,bomb,0);
            assert(stage_score==1234+points+next && effects.size()==2);
        }
    // Hit order matters when two cards flip together; retain a larger max combo.
    rank=RANK_LUNATIC;orb={100,100,0};stage_card_total=2;
    stage_cards[0]=stage_cards[1]={100,100,CARD_ALIVE,0,0};
    stage_score=0;stage_cardcombo_cur=2;stage_cardcombo_max=9;
    popups.clear();effects.clear();cards_hittest(15);
    assert(stage_score==540+715 && stage_cardcombo_cur==4 && stage_cardcombo_max==9);
    assert(popups==(std::vector<int>{540,715}) && effects==(std::vector<int>{SFX_CARD,SFX_CARD}));
    std::puts("Card scoring: all20 stages/four ranks, orb/bomb flips, exact combo factors/cap and popup/SFX awards, multi-HP single transactions and simultaneous-card ordering passed");
}
