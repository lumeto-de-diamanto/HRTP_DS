#include "yuugenmagan_combat_render.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "video.hpp"
#include <cstdlib>
#include <algorithm>
#include "sprites/Missile.h"
#include "hud_render.hpp"
#include "sprites/RadarIcons.h"
#include "main/playfld.hpp"

namespace {
void laser_group(int byte_x,int y,unsigned dots)
{
    // graph_r_line_patterned writes a little-endian16-bit pattern: the first
    // byte's bit7 is the leftmost pixel, NOT bit15 as in shootout_laser.
    for(int bit=0;bit<16;++bit) {
        const int x=byte_x*8+bit;
        const unsigned mask=(0x80u>>(bit&7))<<(bit>=8 ? 8 : 0);
        if((dots&mask) && x>=0 && x<640 && y>=0 && y<400)
            canvas_fill_rect(camera.x(x),Camera::y(y),1,1,RGB15(31,31,31));
    }
}

void source_line(int left,int top,int right,int bottom,unsigned pattern,bool clip_endpoints=true)
{
    if(left==right) {
        if(left<0 || left>639) return;
        if(top>bottom) std::swap(top,bottom);
        for(int y=std::max(0,top);y<=bottom && y<400;++y) for(int dot=0;dot<8;++dot)
            if(pattern&(0x80u>>dot)) canvas_fill_rect(camera.x(left+dot),Camera::y(y),1,1,RGB15(31,31,31));
        return;
    }
    if(top==bottom) {
        if(top<0 || top>399) return;
        if(left>right) std::swap(left,right);
        left=std::max(0,left);right=std::min(639,right);if(left>right) return;
        canvas_hline(camera.x(left),Camera::y(top),camera.x(right)-camera.x(left)+1,RGB15(31,31,31));
        return;
    }
    if(left>right) {std::swap(left,right);std::swap(top,bottom);}
    // Source clipping divides as INTEGERS before its float lerp. The border at
    // y400 and endpoint640 therefore cannot be replaced by a DS Bresenham line.
    if(clip_endpoints) {
        if(left<0) {if(right<0) return;top+=((bottom-top)/(right-left))*(-left);left=0;}
        if(right>639) {if(left>639) return;bottom-=((bottom-top)/(right-left))*(right-639);right=639;}
        if(top<0) {if(bottom<0) return;left+=((right-left)/(bottom-top))*(-top);top=0;}
        if(bottom>399) {if(top>399) return;right-=((right-left)/(bottom-top))*(bottom-399);bottom=399;}
        if(bottom<0) {right+=((right-left)/(bottom-top))*(-bottom);bottom=0;}
        if(top>399) {left-=((right-left)/(bottom-top))*(top-399);top=399;}
    }
    const int w=right-left,dir=top<bottom ? 1 : -1,h=(bottom-top)*dir;
    const int steps=w>h ? w : h,minor=w>h ? h : w;
    // Unclipped pentagram samples can be negative. Keep native byte alignment
    // with floor division, not truncation toward0 which would wrap ink inward.
    auto byte_for=[](int x) {return x/8-(x<0 && x%8!=0);};
    int x=left,y=top,byte_x=byte_for(x),row=y,error=steps/2;unsigned dots=0;
    for(int i=0;i<=steps;++i) {
        if(byte_for(x)!=byte_x || y!=row) {laser_group(byte_x,row,dots);dots=0;byte_x=byte_for(x);row=y;}
        const unsigned shift=unsigned(x)&7u;
        dots|=((pattern>>shift)|(pattern<<(16-shift)))&0xffffu;
        error-=minor;
        if(w>h) ++x;else y+=dir;
        if(error<0) {error+=steps;if(w>h) y+=dir;else ++x;}
    }
    // Source never flushes the last accumulated byte/row. Keep that drawing
    // quirk, but not the destructive32-pixel unput holes between the two beams.
}
}

void yuugenmagan_radar_render(const YuugenMaganPhase1State &state)
{
    static_assert(YUUGENMAGAN_EYES<=HUD_RADAR_MAX_BOSSES);
    HudRadarBoss eyes[YUUGENMAGAN_EYES];
    for(int i=0;i<YUUGENMAGAN_EYES;++i) eyes[i]=hud_radar_boss(state.eyes[i],state.eyes[i].orb_active);
    // Presentation-only north-eye fudge: keep the full glyph below the radar's
    // top edge (currently a3px nudge). Never move the actual eye/hitbox.
    constexpr int half_icon=RadarIcons_height[RadarIcon_Boss]/2;
    constexpr int minimum_y=PLAYFIELD_TOP+(half_icon*(PLAYFIELD_H-1)+HUD_RADAR_H-2)/(HUD_RADAR_H-1);
    eyes[4].y=std::max(eyes[4].y,minimum_y);
    hud_radar_render(eyes,YUUGENMAGAN_EYES);
}

bool YuugenMaganCombatView::prepare_entrance()
{
    if(active_ || g_canvas_target || camera.left<0 || camera.left>VIEW_MAX_LEFT || !sprite_.begin()) return false;
    if(!sprite_.frame({})) {sprite_.end();return false;}
    return true;
}

bool YuugenMaganCombatView::begin()
{
    if(active_) return !defeating_ && !defeated_;
    if(g_canvas_target || camera.left<0 || camera.left>VIEW_MAX_LEFT || !sprite_.begin()) return false;
    effects_=static_cast<u16*>(std::malloc(256*256*sizeof(u16)));
    if(!effects_) {sprite_.end();return false;}
    if(!canvas_buffer_begin(effects_)) {end();return false;}
    active_=true;pending_=false;last_recolor_=-1;
    if(!sprite_.frame({})) {end();return false;}
    return true;
}

bool YuugenMaganCombatView::compose(const YuugenMaganPhase1State &state,const MissilePool *missiles)
{
    if(!active_ || defeating_ || defeated_ || camera.left<0 || camera.left>VIEW_MAX_LEFT || !yuugenmagan_visual_valid(state.visual)) return false;
    if(state.laser_count>2) return false;
    const int laser_x=state.eyes[4].left+28,laser_y=state.eyes[4].top+28;
    if(state.laser_count && (laser_x<0 || laser_x>639 || laser_y<0 || laser_y>=399)) return false;
    for(unsigned i=0;i<state.laser_count;++i) if(state.laser_targets[i]<0 || state.laser_targets[i]>640) return false;
    if(state.pentagram.visible) for(const auto &p:state.pentagram.ink)
        if(p.x< -1024 || p.x>1024 || p.y< -1024 || p.y>1024) return false;
    if(missiles) for(int i=0;i<MISSILE_COUNT;++i) {
        const auto &m=missiles->at(i);if(m.visible && (m.cel<0 || m.cel>=18)) return false;
    }
    visual_=state.visual;flash_=state.flash;
    if(missiles) {
        u16 palette[16];if(!sprite_.sprite_palette(palette,visual_,flash_)) return false;
        for(int i=0;i<MISSILE_COUNT;++i) {
            const auto &m=missiles->at(i);
            if(m.visible) canvas_put_indexed(camera.x(m.draw_x),Camera::y(m.draw_y),8,8,Missile_pixels[m.cel],palette);
        }
    }
    sprite_.cover_eyes(visual_);
    yuugenmagan_lasers_render(visual_.lasers);
    for(unsigned i=0;i<state.laser_count;++i) source_line(laser_x,laser_y,state.laser_targets[i],400,0xe0);
    if(state.pentagram.visible) for(int i=0;i<5;++i) {
        const auto a=state.pentagram.ink[i],b=state.pentagram.ink[(i+1)%5];
        // Rasterize the original edge, then let laser_group discard offscreen
        // pixels. Moving a vertex beyond y399 must not change the edge's slope
        // or raster phase. Northern lasers retain their source endpoint clip.
        source_line(a.x,a.y,b.x,b.y,0x80,false);
    }
    visual_.lasers=0; // already drawn into RAM; present must not dirty it again
    pending_=true;return true;
}

bool YuugenMaganCombatView::present(int death_shake)
{
    if(!active_ || defeating_ || defeated_ || !pending_ || !canvas_buffer_ready() || camera.left<0 || camera.left>VIEW_MAX_LEFT ||
        REG_VCOUNT<192 || REG_VCOUNT>=200) return false;
    if(!sprite_.frame(visual_,flash_)) return false;
    if(last_recolor_!=visual_.recolor || last_flash_!=flash_ || last_color15_override_!=visual_.color15_override ||
        (visual_.color15_override && !std::equal(visual_.color15,visual_.color15+3,last_color15_))) {
        video_bomb_palette_changed();last_recolor_=visual_.recolor;last_flash_=flash_;
        last_color15_override_=visual_.color15_override;std::copy(visual_.color15,visual_.color15+3,last_color15_);
    }
    video_bomb_commit();
    video_apply_death_shake(death_shake<0 ? video_bomb_shake() : death_shake,true);
    // Publish affine/OAM/palette registers BEFORE streaming the canvas; a long
    // transfer must not restart a BG reference point partway through scanout.
    oamUpdate(&oamMain);video_stage_reveal(true);pending_=false;return canvas_buffer_present(true);
}

void YuugenMaganCombatView::end()
{
    if(active_) {video_bomb_reset();canvas_buffer_end();canvas_clear();}
    sprite_.end();std::free(effects_);effects_=nullptr;active_=pending_=false;last_recolor_=-1;
    defeating_=defeated_=false;
}
