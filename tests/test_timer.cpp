// Portable timer behavior with recording stubs only at the projectile seam.
#include <cassert>
#include <climits>
#include <cstdio>
#include <vector>
#include "main/stage/timer.hpp"
#include "main/bullet/pellet.hpp"
#include "main/playfld.hpp"

rank_t rank = RANK_NORMAL;
CPellets Pellets;
static unsigned speed_raises;
static unsigned random_value;
struct Rain { int x, y; pellet_group_t group; subpixel_t speed; };
static std::vector<Rain> rain;

int irand() { return random_value++ & INT_MAX; }
void pellet_speed_raise(float pixels) {
    assert(pixels == 0.05f);
    ++speed_raises;
}
void CPellets::add_group(int x, int y, pellet_group_t group, subpixel_t speed) {
    rain.push_back({x, y, group, speed});
}

static void check_initial_values_and_durations()
{
    // Independently transcribed by navigable scene from ReC98 STAGE_TIMES.
    // Include the future route bosses even though they are not exposed yet.
    static const unsigned initial[7][5] = {
        {1000, 1000, 1000, 1000, 2000},
        {1200, 1200, 1200, 1200, 3000}, // Makai6..10
        {1200, 1200, 1200, 1200, 2000}, // Jigoku6..10
        {1400, 1400, 1400, 1400, 4000},
        {1400, 1400, 1400, 1400, 4000},
        {1600, 1600, 1600,  600, 6000},
        {1600, 1800, 1800, 1600, 8000},
    };
    for (int scene = 0; scene < 7; ++scene) {
        for (int stage = 0; stage < 5; ++stage) {
            // A stage can begin at any phase of the persistent game counter.
            for (unsigned phase = 0; phase < 4; ++phase) {
                timer_init_for(scene, stage);
                assert(stage_timer == initial[scene][stage]);
                rain.clear(); speed_raises = 0;
                const unsigned frames = initial[scene][stage] / 2 * 4 - phase;
                for (unsigned elapsed = 1; elapsed <= frames; ++elapsed) {
                    const unsigned ticks = (phase + elapsed) / 4;
                    const auto event = timer_update(phase + elapsed);
                    assert(stage_timer == initial[scene][stage] - ticks * 2);
                    assert(event == (elapsed == frames ? TIMER_EVENT_HARRYUP : TIMER_EVENT_NONE));
                    assert(speed_raises == (elapsed == frames ? 1u : 0u));
                    assert(rain.empty()); // expiry itself does not spawn rain
                }
            }
        }
    }
}

static void check_extensions_and_persistent_phase()
{
    timer_init_for(0, 0);
    assert(timer_update(1) == TIMER_EVENT_NONE && stage_timer == 1000);
    assert(timer_update(2) == TIMER_EVENT_NONE && stage_timer == 1000);
    timer_extend();
    assert(stage_timer == 1200);
    // Extension does not restart the four-frame cadence.
    assert(timer_update(3) == TIMER_EVENT_NONE && stage_timer == 1200);
    assert(timer_update(4) == TIMER_EVENT_NONE && stage_timer == 1198);
    // Neither does a new stage: the global counter is not a per-stage clock.
    timer_init_for(6, 1);
    assert(stage_timer == 1800);
    assert(timer_update(7) == TIMER_EVENT_NONE && stage_timer == 1800);
    assert(timer_update(8) == TIMER_EVENT_NONE && stage_timer == 1798);
    // Original unsigned frame_rand semantics, including wrap to phase zero.
    assert(timer_update(UINT_MAX) == TIMER_EVENT_NONE && stage_timer == 1798);
    unsigned frame = UINT_MAX;
    assert(timer_update(++frame) == TIMER_EVENT_NONE && stage_timer == 1796);
    // Repeated surviving misses add exactly 200, not the stage's initial value.
    timer_extend(); timer_extend();
    assert(stage_timer == 2196);
}

static void check_hurry_up_and_recovery()
{
    for (int difficulty = RANK_EASY; difficulty <= RANK_LUNATIC; ++difficulty) {
        rank = static_cast<rank_t>(difficulty);
        timer_init_for(0, 0); stage_timer = 2;
        rain.clear(); speed_raises = 0;
        assert(timer_update(4) == TIMER_EVENT_HARRYUP && stage_timer == 0);
        assert(speed_raises == 1);
        // Each timer tick is four active frames; first rain volley after 20.
        for (unsigned f = 5; f < 24; ++f) {
            assert(timer_update(f) == TIMER_EVENT_NONE);
            assert(rain.empty());
        }
        assert(timer_update(24) == TIMER_EVENT_NONE);
        assert(rain.size() == unsigned(difficulty + 1));
        for (const auto &pellet : rain) {
            assert(pellet.x >= PLAYFIELD_LEFT && pellet.x < PLAYFIELD_RIGHT);
            assert(pellet.y == PLAYFIELD_TOP && pellet.group == PG_1_RANDOM_WIDE);
            assert(pellet.speed == 3 * 16);
        }
        // Exercise every acceleration threshold, including the 8-bit cadence wrap.
        rain.clear();
        unsigned cycle = 5, expected = 0;
        for (unsigned tick = 6; tick <= 600; ++tick) {
            cycle = (cycle + 1) & 255;
            const unsigned divisor = tick < 200 ? 5 : tick < 300 ? 4 :
                                     tick < 400 ? 3 : tick < 500 ? 2 : 1;
            if (cycle % divisor == 0) expected += difficulty + 1;
            assert(timer_update(4 + tick * 4) == TIMER_EVENT_NONE);
            assert(rain.size() == expected);
        }
        assert(stage_timer == 0 && speed_raises == 1);
        timer_extend();
        assert(stage_timer == 1000);
        rain.clear();
        for (unsigned f = 1; f < 2000; ++f) {
            assert(timer_update(f) == TIMER_EVENT_NONE);
            assert(rain.empty());
        }
        assert(timer_update(2000) == TIMER_EVENT_HARRYUP && speed_raises == 2);
        for (unsigned f = 2001; f < 2020; ++f) {
            assert(timer_update(f) == TIMER_EVENT_NONE);
            assert(rain.empty());
        }
        timer_update(2020);
        assert(rain.size() == unsigned(difficulty + 1)); // rain cadence reset
    }
}

int main()
{
    check_initial_values_and_durations();
    check_extensions_and_persistent_phase();
    check_hurry_up_and_recovery();
    std::puts("Timer: all 35 stage/route values, four frame phases, exact durations, extensions, wrap, HURRY UP/rain/recovery passed");
}
