#include <string.h>

#include "pellet_render.hpp"
#include "canvas.hpp"
#include "camera.hpp"

#include "main/bullet/pellet.hpp"

#include "sprites/Pellet.h"

// Keep intact/shatter cels pure white
// independently of the scene's indexed background and stage-object palette.
static const u16 PELLET_PAL[16] = {
    0x0000, 0x7FFF,
};

#define PELLET_SPRITE 4 // halved from 8x8

void pellet_render_frame()
{
    // The shared canvas is cleared once per frame, before any effects draw.
    // No stale screen-space erasures may punch holes after the camera moves.
    for (int i = 0; i < PELLET_COUNT; i++) {
        const Pellet &pel = Pellets.at(i);
        if (!pel.moving)
            continue;
        // Destruction animation: cel 0 intact, cels 1..2 shatter stages
        // (ReC98 renders only while decay_frame < PELLET_DECAY_FRAMES).
        int cel = 0;
        if (pel.decay_frame) {
            if (pel.decay_frame >= PELLET_DECAY_FRAMES)
                continue;
            cel = (pel.decay_frame + PELLET_DECAY_FRAMES_PER_CEL - 1) /
                  PELLET_DECAY_FRAMES_PER_CEL;
            if (cel > PELLET_DECAY_CELS)
                cel = PELLET_DECAY_CELS;
        }
        int x = camera.x(TO_PIXEL(pel.cur_left.v));
        int y = camera.y(TO_PIXEL(pel.cur_top.v));
        if (!view_visible(x, y, PELLET_SPRITE, PELLET_SPRITE))
            continue;
        canvas_put_indexed(x, y, PELLET_SPRITE, PELLET_SPRITE,
                           Pellet_cels[cel], PELLET_PAL);
    }
}
