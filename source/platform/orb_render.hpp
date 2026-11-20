#pragma once
#include <nds.h>

bool orb_render_init();
void orb_render_frame();
void orb_render_hide();
void orb_indicator_render(bool hidden=false); // blocking/interactive boss intros suppress the marker

// Shared left/right edge marker for fully offscreen world-space bounds.
// Caller supplies resident 8x16 graphics using the overlay palette.
void offscreen_indicator_render(int slot,int left,int top,int width,int height,u16 *gfx,bool hidden=false);
