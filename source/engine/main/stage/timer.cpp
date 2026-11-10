// Port of ReC98 th01/main/stage/timer.cpp. The banner drawing lives in
// the platform layer (the original used the PC-98 text layer; DS uses a
// blocking BG2 banner); everything else follows the original.

#include "timer.hpp"
#include "main/bullet/pellet.hpp"
#include "main/playfld.hpp"
#include "main/random.hpp"
#include "rank.h"

unsigned int stage_timer;
static unsigned int frame_since_harryup;
static unsigned char harryup_cycle;

static const int TIMER_TICK = 2;

// ReC98 timer.cpp: STAGE_TIMES. Indexing: 0-4 = Shrine; route stages are
// addressed as (route * 15) + stage_id.
static const unsigned int STAGE_TIMES[35] = {
    1000, 1000, 1000, 1000, 2000,   // Shrine
    // Makai
    1200, 1200, 1200, 1200, 3000,  // (06 - 10)
    1400, 1400, 1400, 1400, 4000,  // (11 - 15)
    1600, 1600, 1600,  600, 6000,  // (16 - 20)
    // Jigoku
    1200, 1200, 1200, 1200, 2000,  // (06 - 10)
    1400, 1400, 1400, 1400, 4000,  // (11 - 15)
    1600, 1800, 1800, 1600, 8000,  // (16 - 20)
};

void timer_init_for(int scene, int stage)
{
    // Scene 0 = Shrine; scenes 1/2 = Makai/Jigoku 06-10, 3/4 = 11-15,
    // 5/6 = 16-20 (the routes are parallel). Scene 7 is leftover content.
    int stage_id;
    int route = 0;
    if (scene == 0 || scene >= 7) {
        stage_id = stage;
    } else {
        route = (scene & 1) ? 0 : 1; // odd scenes = Makai, even = Jigoku
        stage_id = 5 + (((scene - 1) / 2) * 5) + stage;
    }

    if (stage_id < 5) {
        stage_timer = STAGE_TIMES[stage_id];
    } else {
        stage_timer = STAGE_TIMES[(route * 15) + stage_id];
    }
    frame_since_harryup = 0;
    harryup_cycle = 0;
}

timer_event_t timer_update(unsigned int gameplay_frame)
{
    return (gameplay_frame & 3u) == 0 ? timer_tick() : TIMER_EVENT_NONE;
}

timer_event_t timer_tick()
{
    if (stage_timer > 0) {
        stage_timer -= TIMER_TICK;
        if (stage_timer == 0) {
            pellet_speed_raise(0.05f);
            return TIMER_EVENT_HARRYUP;
        }
        return TIMER_EVENT_NONE;
    }

    // HARRY UP phase: accelerating rain of wide random pellets.
    frame_since_harryup++;
    harryup_cycle++;

    bool fire =
        (frame_since_harryup < 200) ? ((harryup_cycle % 5) == 0) :
        (frame_since_harryup < 300) ? ((harryup_cycle % 4) == 0) :
        (frame_since_harryup < 400) ? ((harryup_cycle % 3) == 0) :
        (frame_since_harryup < 500) ? ((harryup_cycle % 2) == 0) :
        true;

    if (fire) {
        for (int i = 0; i <= rank; i++) {
            Pellets.add_group(
                 playfield_rand_x(), PLAYFIELD_TOP, PG_1_RANDOM_WIDE, TO_SP(3)
            );
        }
    }
    return TIMER_EVENT_NONE;
}

void timer_extend()
{
    if (stage_timer != 0)
        stage_timer += 200;
    else
        stage_timer += 1000;
    frame_since_harryup = 0;
    harryup_cycle = 0;
}
