#pragma once

// Hide stale gameplay OAM/effects/tiles before loading a new stage background.
// Leaves the fixed HUD and the boot-lifetime sprite allocations intact.
void stage_entrance_prepare();

// After prepare and state initialization: present the field, optional card
// ripple, then the approved title/banner. Does not update simulation, timer,
// camera, input or music. Caller renders the player/orb for the serve afterward.
void stage_entrance_animate(int stage_num, bool show_title);
