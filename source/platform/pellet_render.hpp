#pragma once

// Renders the pellet bullets onto the effects canvas (4x4 at screen
// scale). Keeps a shadow of last frame's positions so dead pellets are
// erased properly.

void pellet_render_frame();
