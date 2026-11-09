#pragma once

#include "main/stage/stageobj.hpp"
#include "main/player/orb.hpp"

// Card-flip logic (ReC98 th01/main/stage/card.cpp): the orb flips cards it
// passes near; flipping them all clears the stage.

void cards_hittest();
void cards_update();
