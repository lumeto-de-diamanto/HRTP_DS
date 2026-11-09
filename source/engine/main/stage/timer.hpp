#pragma once

// Port of ReC98 th01/main/stage/timer.{hpp,cpp}: the per-stage countdown.
// When it runs out, the "HARRY UP" banner plays and the bullet-rain phase
// begins (pattern_harryup).

extern unsigned int stage_timer;

enum timer_event_t {
    TIMER_EVENT_NONE = 0,
    TIMER_EVENT_HARRYUP, // banner should play now (once)
};

void timer_init_for(int scene, int stage);
timer_event_t timer_tick(); // call every 4th frame (ReC98 TIMER_TICK = 2)
void timer_extend();        // +200 (or +1000 from zero)
