#include "nds/ndstypes.h"
#include "random.hpp"

// XORWOW implementation from Wikipedia

static uint32 x[5];
static uint32 counter;

void initialize_irand(uint32 seed) {
    x[0] = seed;
    // Can't have all of them be zero!
    x[1] = 1;
    x[2] = 4;
    x[3] = 9;
    x[4] = 16;
    counter = 25;
}

int irand() {
    uint32_t t = x[4];

    uint32_t s = x[0]; // Perform a contrived 32-bit rotate.
    x[4] = x[3];
    x[3] = x[2];
    x[2] = x[1];
    x[1] = s;

    t ^= t >> 2;
    t ^= t << 1;
    t ^= s ^ (s << 4);

    x[0] = t;
    counter += 362437;
    // Bug fix (docs/ds-port-plan.md section 5): the raw uint32 sum converts
    // to int implementation-definedly, yielding negative rolls ~half the
    // time, which then wrap `irand() % n` into negative ranges (e.g. the
    // portal exit force (irand() % 19) - 9 would fire at up to -27
    // px/frame). master.lib's irand() is non-negative; keep it that way.
    return (int)((t + counter) & 0x7FFFFFFF);
}