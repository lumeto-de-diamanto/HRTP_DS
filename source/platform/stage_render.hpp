#pragma once

// Renders the stage-object game state onto the BG1 tile layer.

void stage_render_frame(); // syncs tiles to cards/obstacles state
void stage_render_clear();
// Later-stage reveal from ReC98 stageobjs_init_and_render(). Render one
// presentation frame without mutating cards; true means hold for one VBlank.
// Frame0 is empty, then one card starts per frame in source array order.
bool stage_render_entrance_frame(int frame);
