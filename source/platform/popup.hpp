#pragma once

// Score popups on the playfield (card flips, item collects): small rising
// numbers drawn on the effects canvas.

void popup_add(int world_x, int world_y, int value);
void popup_render_frame();
void popup_clear();
