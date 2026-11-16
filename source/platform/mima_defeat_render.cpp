#include "mima_combat_render.hpp"
#include "main/boss/mima_defeat.hpp"
#include "main/player/orb.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "boss_gfx.hpp"
#include "music.hpp"
#include "player_render.hpp"
#include "orb_render.hpp"
#include "shot_render.hpp"
#include "pellet_render.hpp"
#include "backgrounds/MimaPaletteFx.h"
#include "backgrounds/MimaDefeatPalette.h"

namespace {
static_assert(sizeof(Mima_defeat_signatures)/sizeof(Mima_defeat_signatures[0])==MIMA_BG_COLORS);
int component(int normal,int initial,const MimaDefeatFrame &f)
{
    if(f.phase==MD_FADE) return normal>15-f.fade_steps ? normal : 15-f.fade_steps;
    return initial+f.white_steps<15 ? initial+f.white_steps : 15;
}

u16 actor_color(u16 color,const MimaDefeatFrame &f)
{
    // Keep the exact approved5-bit target at the final step. Intermediate
    // levels use source RGB4, not a linear DS master-brightness approximation.
    if((f.phase==MD_FADE && f.fade_steps==15) || (f.phase!=MD_FADE && !f.white_steps)) return color;
    u16 out=0;
    for(int shift=0;shift<=10;shift+=5) {
        const int normal=(((color>>shift)&31)*15+15)/31;
        out|=((component(normal,normal,f)*17)>>3)<<shift;
    }
    return out;
}

void arena_palette(u16 *out,const MimaPhase1State &s,const MimaDefeatFrame &f)
{
    int rgb[16][3];
    for(int i=0;i<16;++i) for(int c=0;c<3;++c) {
        const int normal=Mima_defeat_rgb[i][c];
        int initial=normal-s.darkness;if(initial<0) initial=0;
        if(s.flash && (i==3 || i==9)) { initial=15-s.flash_darkness;if(initial<0) initial=0; }
        rgb[i][c]=component(normal,initial,f)*17;
    }
    for(int i=0;i<MIMA_BG_COLORS;++i) {
        const unsigned signature=Mima_defeat_signatures[i];
        if(signature==0x10000) { out[i]=actor_color(Mima_palette_fx[0][0][i],f);continue; }
        int channels[3]={};
        for(int c=0;c<3;++c) {
            if(i<16) channels[c]=rgb[i][c]>>3;
            else for(int p=0;p<4;++p) channels[c]+=rgb[(signature>>(p*4))&15][c]/4;
            if(i>=16) channels[c]>>=3;
        }
        out[i]=RGB15(channels[0],channels[1],channels[2]);
    }
}
}

bool MimaCombatView::defeat(const MimaPhase1State &s)
{
    if(!active_ || defeated_ || s.phase!=3 || !s.checkpoint || s.health.hp!=0) return false;
    // ARM7 owns the native-tick fade, independently of this blocking effect.
    // No ACK/VBlank wait, note restart or per-frame audio IPC on the render path.
    music_fade_out_nonblock();video_bomb_reset();
    u16 bg[256],obj[256],colors[MIMA_BG_COLORS];
    for(int i=0;i<256;++i) { bg[i]=BG_PALETTE[i];obj[i]=SPRITE_PALETTE[i]; }
    for(int i=0;i<16;++i) obj[i]=player_palette_[i];
    int center=orb_get_state().y_position+ORB_H/2;
    if(center<0) center=0;
    if(center>399) center=399;
    MimaDefeat timeline(center);MimaDefeatFrame f;
    MimaPhase1State visual=s;for(auto &laser:visual.lasers) laser={};
    MissilePool empty;
    struct Foreground {
        bool entered=false;
        ~Foreground() { if(entered) video_canvas_foreground_end(); }
    } foreground;
    while(timeline.next(f)) {
        arena_palette(colors,s,f);
        canvas_begin_frame();canvas_playfield_clip(false);
        if(f.phase==MD_FADE) {
            // Source page1 -> page0 removes every transient and the orb, then
            // player_put_default() draws the fixed left-facing idle cel.
            player_render_default();
            for(int slot=1;slot<48;++slot) oamSetHidden(&oamMain,slot,true);
        } else {
            if(!compose(visual,empty,colors)) return false;
            player_render_frame();shots_render_frame();orb_render_frame();orb_indicator_render();
            pellet_render_colored(colors[7]);
            // Halving the source's2px row spacing made adjacent DS rows solid.
            // Keep every second mapped line so the original alternating-row
            // appearance survives downscaling; the source timeline is unchanged.
            for(int step=0;step<f.line_steps;step+=2) {
                const int top=center-step*2,bottom=center+step*2;
                if(top>=0) canvas_hline(0,Camera::y(top),VIEW_W,colors[7]);
                if(bottom<=399) canvas_hline(0,Camera::y(bottom),VIEW_W,colors[7]);
            }
        }
        canvas_buffer_prepare();
        do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
        if(!foreground.entered) { video_canvas_foreground_begin();foreground.entered=true; }
        for(int i=0;i<256;++i) {
            BG_PALETTE[i]=i<MIMA_BG_COLORS ? colors[i] : actor_color(bg[i],f);
            SPRITE_PALETTE[i]=actor_color(obj[i],f);
        }
        oamSetHidden(&oamMain,BOSS_OAM_SLOT,true);oamSetHidden(&oamMain,BOSS_OAM_SLOT+1,true);
        oamSetHidden(&oamMain,OAM_SLOT_BOSS_ARROW,true);
        video_apply_death_shake(0,false);
        oamUpdate(&oamMain);video_stage_reveal(true);
        if(!canvas_buffer_present(true)) return false;
    }
    pending_=false;defeated_=true;
    return true;
}
