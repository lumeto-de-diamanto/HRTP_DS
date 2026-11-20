#pragma once

#include "nds/ndstypes.h"

void initialize_irand(uint32 seed);

int irand();

// Isolated practice may consume its own rolls without advancing the saved run.
struct IrandState {uint32 x[5],counter;};
IrandState irand_state();
void irand_restore(const IrandState &state);
