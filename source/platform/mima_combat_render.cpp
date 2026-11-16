#include "mima_combat_render.hpp"
#include "background.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "sprites/Missile.h"
#include "sprites/Pillar.h"
#include "main/math/trig.hpp"
#include "backgrounds/MimaPaletteFx.h"
#include <cstdlib>

namespace {
void line(MimaPoint a,MimaPoint b,u16 ink)
{
    int x=camera.x(a.x),y=Camera::y(a.y),tx=camera.x(b.x),ty=Camera::y(b.y);
    int dx=tx-x,dy=ty-y,sx=dx<0?-1:1,sy=dy<0?-1:1;
    if(dx<0) dx=-dx;
    if(dy<0) dy=-dy;
    int error=dx-dy;
    while(true) {
        canvas_fill_rect(x,y,1,1,ink);if(x==tx && y==ty) break;
        int twice=error*2;if(twice>-dy) { error-=dy;x+=sx; }if(twice<dx) { error+=dx;y+=sy; }
    }
}

void laser_pixels(int left,int y,uint16_t dots,void *context)
{
    const u16 ink=*static_cast<const u16 *>(context);
    const int base=left&~7;
    for(int bit=0;bit<16;) {
        if(!(dots&(0x8000>>bit))) { ++bit;continue; }
        const int first=bit++;while(bit<16 && (dots&(0x8000>>bit))) ++bit;
        const int x=camera.x(base+first),right=camera.x(base+bit-1);
        canvas_hline(x,Camera::y(y),right-x+1,ink);
    }
}

void circle(int x,int radius,u16 ink)
{
    // Source shape_circle_put samples every four angle units, not a connected
    // ellipse. Preserve byte/y grouping and its intentionally omitted flush.
    int previous=-1,base=0,top=0;uint16_t dots=0;
    for(int a=0;a<=255;a+=4) {
        const int px=polar_x_fast(x,radius,a),py=polar_y_fast(400,radius,a);
        const int offset=py*80+(px>>3);
        if(offset!=previous) {
            if(px>=0 && px<640 && previous>=0 && previous<32000) laser_pixels(base,top,dots,&ink);
            dots=0x8000>>(px&7);base=px&~7;top=py;previous=offset;
        } else dots|=0x8000>>(px&7);
    }
}

void pillar(int center,int height,u16 ink)
{
    const u16 palette[]={0,ink};
    const int x=camera.x((center-16)&~7),top=Camera::y(400)-height/2;
    for(int y=0;y<height/2;y+=4)
        canvas_put_indexed(x,top+y,16,4,Pillar_cels[y==0 ? 1 : 0],palette);
}
}

bool MimaCombatView::begin(bool background_loaded)
{
    if(active_) return true;
    if(g_canvas_target || !sprite_.begin()) return false;
    effects_=static_cast<u16 *>(std::malloc(256*256*sizeof(u16)));
    if(!effects_) { end();return false; }
    if(!background_loaded && !load_mima_background()) { end();return false; }
    for(int i=0;i<16;++i) player_palette_[i]=SPRITE_PALETTE[i];
    if(!canvas_buffer_begin(effects_)) { end();return false; }
    active_=true;pending_=defeated_=false;last_darkness_=-1;
    return true;
}

void MimaCombatView::end()
{
    if(active_) {
        video_bomb_reset();
        for(int i=0;i<16;++i) SPRITE_PALETTE[i]=player_palette_[i];
        for(int i=0;i<MIMA_BG_COLORS;++i) BG_PALETTE[i]=Mima_palette_fx[0][0][i];
        canvas_buffer_end();canvas_clear();
    }
    sprite_.end();std::free(effects_);effects_=nullptr;active_=pending_=defeated_=false;
}

bool MimaCombatView::compose(const MimaPhase1State &s,const MissilePool &missiles,const u16 *override_colors)
{
    if(!active_ || defeated_ || s.image<0 || s.image>=47 || s.darkness<0 || s.darkness>16 || s.darkness%4 ||
       s.flash_darkness<0 || s.flash_darkness>16 || s.flash_darkness%4 ||
       s.square_count<0 || s.square_count>2) return false;
    image_=s.image;left_=s.entity.left;top_=s.entity.top;darkness_=s.darkness;flash_=s.flash;
    flash_mode_=flash_ ? 1+s.flash_darkness/4 : 0;
    const auto *colors=override_colors ? override_colors : Mima_palette_fx[darkness_/4][flash_mode_];
    const u16 ink=colors[7];
    if(s.squares_visible) for(int square=0;square<s.square_count;++square)
        for(int i=0;i<4;++i) line(s.corners[square][i],s.corners[square][(i+1)%4],ink);
    for(const auto &p:s.particles) if(p.alive)
        canvas_fill_rect(camera.x(p.x>>4),Camera::y(p.y>>4),1,1,ink);
    u16 palette[16];for(int i=0;i<16;++i) palette[i]=colors[(i-1)&15];
    for(int i=0;i<MISSILE_COUNT;++i) {
        const auto &m=missiles.at(i);
        if(m.visible) canvas_put_indexed(camera.x(m.draw_x),Camera::y(m.draw_y),8,8,Missile_pixels[m.cel],palette);
    }
    for(const auto &p:s.pillars) {
        if(p.radius) circle(p.center_x,p.radius,ink);
        if(p.ray) line({320,200},{p.center_x,400},colors[1]);
        // The larger stamp doesn't erase the first cap in the source. Keep its
        // ink until the whole pillar vanishes, without unsafe bottom VRAM writes.
        if(p.height) { pillar(p.center_x,32,colors[10]);if(p.height>32) pillar(p.center_x,p.height,colors[10]); }
    }
    u16 laser_ink=ink;
    for(const auto &laser:s.lasers) shootout_laser_visit(laser,laser_pixels,&laser_ink);
    pending_=true;
    return true;
}

bool MimaCombatView::present(int death_shake)
{
    if(!active_ || !pending_ || !canvas_buffer_ready() || REG_VCOUNT<192 || REG_VCOUNT>=200) return false;
    if(!sprite_.frame(image_,left_,top_)) return false;
    // frame() initializes the normal body palette once; apply Mima-local state
    // afterward so first-frame flash/darkening cannot be overwritten.
    const auto *colors=Mima_palette_fx[darkness_/4][flash_mode_];
    for(int i=0;i<16;++i) SPRITE_PALETTE[48+i]=colors[(i-1)&15];
    if(last_darkness_!=darkness_ || last_flash_mode_!=flash_mode_) {
        for(int i=0;i<MIMA_BG_COLORS;++i) BG_PALETTE[i]=colors[i];
        for(int i=0;i<16;++i) {
            u16 color=0;
            for(int shift=0;shift<=10;shift+=5) {
                int component=(((player_palette_[i]>>shift)&31)*15+15)/31-darkness_;
                if(component<0) component=0;
                color|=((component*17)>>3)<<shift;
            }
            if(flash_ && (i==4 || i==10)) {
                const int level=15-(flash_mode_-1)*4;
                const int white=(level>0 ? level*17 : 0)>>3;color=RGB15(white,white,white);
            }
            SPRITE_PALETTE[i]=color;
        }
        video_bomb_palette_changed();last_darkness_=darkness_;last_flash_mode_=flash_mode_;
    }
    video_bomb_commit();
    video_apply_death_shake(death_shake<0 ? video_bomb_shake() : death_shake,false);
    // reveal() commits camera AND explicit shake together in this VBlank.
    oamUpdate(&oamMain);video_stage_reveal(true);
    pending_=false;return canvas_buffer_present(true);
}
