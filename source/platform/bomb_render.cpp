#include "bomb_render.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "main/player/player.hpp"
#include "main/math/trig.hpp"
#include "sprites/BombKuji.h"

void bomb_render_frame()
{
    const auto &bomb=player_bomb();
    if(!bomb.active()) return;
    const int frame=bomb.frame();
    if(frame<50) {
        if(frame<2) return;
        const int movement_frame=frame&~1;
        const int fade=frame>40 ? 50-frame : 10;
        const u16 colors[]={0,RGB15(12*fade/10,16*fade/10,22*fade/10)};
        for(int ring=0;ring<2;++ring) {
            const int radius=500-ring*90-(movement_frame+60)*4;
            for(int glyph=0;glyph<9;++glyph) {
                const unsigned char angle=(ring ? -movement_frame : movement_frame)+glyph*(256/9);
                const int left=polar_x_fast(304,radius,angle)&~7;
                const int top=polar_y_fast(184,radius,angle);
                canvas_put_indexed(camera.x(left),Camera::y(top),16,16,BombKuji_cels[glyph],colors);
            }
        }
    }
}
