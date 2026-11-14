// Port of ReC98 th01/main/stage/card.cpp: the orb flips cards it passes
// near; flipping all of them clears the stage. Scoring follows ReC98's
// formula minus the difficulty multiplier (M3 adds rank handling).

#include <stdlib.h>

#include "main/stage/card.hpp"
#include "main/stage/item.hpp"
#include "popup.hpp"
#include "sound_effects.hpp"
#include "main/random.hpp"

static int card_flip_cycle;

void cards_hittest(bool bomb_damaging, unsigned bomb_frame)
{
    const OrbState &orb = orb_get_state();

    for (int i = 0; i < stage_card_total; i++) {
        StageCard &c = stage_cards[i];
        int dx = abs(c.left - orb.x_position);
        int dy = abs(c.top - orb.y_position);

        if ((dx < STAGEOBJ_ORB_DISTANCE_X && dy < STAGEOBJ_ORB_DISTANCE_Y && c.flag == CARD_ALIVE) ||
            (bomb_damaging && bomb_frame % unsigned(stage_card_total) == unsigned(i) &&
             irand()%4 != 0 && c.flag == CARD_ALIVE)) {
            c.flag = CARD_FLIPPING;
            c.flip_frame = 0;

            // ReC98 card.cpp: score = base + combo^2 * factor, capped.
            int score_value = 100 + (stage_cardcombo_cur * stage_cardcombo_cur) * 20;
            if (score_value > CARD_SCORE_CAP)
                score_value = CARD_SCORE_CAP;
            stage_score += score_value;
            stage_cardcombo_cur++;
            if (stage_cardcombo_max < stage_cardcombo_cur)
                stage_cardcombo_max = stage_cardcombo_cur;
            popup_add(c.left + 16, c.top, score_value);

            // ReC98 card.cpp: every 10th flip drops a point item, every
            // CARD_FLIP_CYCLE_MAX-th drops a bomb item instead.
            if (((card_flip_cycle++) % 10) == 0) {
                if (card_flip_cycle >= CARD_FLIP_CYCLE_MAX) {
                    card_flip_cycle = 1;
                    if(bomb_damaging) Items.add_point(c.left,c.top);
                    else Items.add_bomb(c.left, c.top);
                } else {
                    Items.add_point(c.left, c.top);
                }
            }
            mdrv2_se_play(SFX_CARD);
        }
    }
}

void cards_update()
{
    int removed = stage_cards_removed_count;

    for (int i = 0; i < stage_card_total; i++) {
        StageCard &c = stage_cards[i];
        if (c.flag != CARD_FLIPPING)
            continue;

        c.flip_frame++;
        // 5 cels x CARD_FRAMES_PER_CEL frames per flip (ReC98
        // cards_update_and_render). At the end the card either loses one
        // HP (staying alive) or flips to the REMOVED state.
        if (c.flip_frame >= card_first_frame_of(CARD_CELS)) {
            if (c.hp == 0) {
                c.flag = CARD_REMOVED;
                removed++;
            } else {
                c.flag = CARD_ALIVE;
                c.hp--;
            }
            c.flip_frame = 0;
        }
    }

    stage_cards_removed_count = removed;
}
