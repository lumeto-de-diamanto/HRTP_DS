#pragma once

#include "main/stage/stageobj.hpp"
#include "main/player/orb.hpp"

// Card-flip logic (ReC98 th01/main/stage/card.cpp): the orb flips cards it
// passes near; flipping them all clears the stage.

// stage_id is the original 0-based stage number (0..19), not a scene index.
void cards_hittest(int stage_id, bool bomb_damaging = false, unsigned bomb_frame = 0);
void cards_update();
