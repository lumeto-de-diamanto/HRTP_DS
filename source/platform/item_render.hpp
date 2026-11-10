#pragma once

// Renders the bomb/point items as OAM sprites (16x16 at screen scale)
// and their splash rings on the effects canvas.

bool item_render_init();
void item_render_frame();
void item_rings_frame();
