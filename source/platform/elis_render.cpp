#include "elis_render.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "sprites/Missile.h"
#include "sprites/BossArrow.h"
#include "boss_gfx.hpp"
#include "orb_render.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
unsigned word(const u8 *p) {return p[0]|(p[1]<<8);}
unsigned dword(const u8 *p) {return word(p)|(word(p+2)<<16);}
constexpr unsigned indices=80,signatures=61520;
constexpr unsigned girl_bytes=8*3072,mask_bytes=5*256,star_bytes=16,bat_bytes=3*384,payload=girl_bytes+mask_bytes+star_bytes+bat_bytes;
constexpr unsigned mask_plane_bytes=40*192,composite_offset=mask_plane_bytes*2;
// The canvas's unused64-row tail already belongs to this view. Merge native
// dots at DS resolution, then dirty/blit each colored run once. Separate
// circle/ray planes preserve their colors and source draw order without
// restoring thousands of duplicate native-dot rectangle calls per sweep.
struct EffectMask {
    u8 *pixels;
    int first=192,last=-1;
    bool initialized=false;
    EffectMask(u16 *canvas,unsigned plane):pixels(reinterpret_cast<u8*>(canvas+256*192)+plane*40*192) {}
    void put(int left,int y,uint16_t dots) {
        y=Camera::y(y);left=(left&~7)/2;
        if(!dots || y<UPPER_HUD_H || y>=192 || left<0 || left>=320) return;
        if(!initialized) {std::memset(pixels,0,40*192);initialized=true;}
        // OR each native pixel pair, then compact the even bits into one byte.
        unsigned bits=(dots|(dots>>1))&0x5555;
        bits=(bits|(bits>>1))&0x3333;bits=(bits|(bits>>2))&0x0f0f;bits=(bits|(bits>>4))&0xff;
        auto *row=pixels+y*40;const unsigned shift=left&7;
        row[left/8]|=bits>>shift;
        if(shift && left/8+1<40) row[left/8+1]|=bits<<(8-shift);
        if(y<first) first=y;
        if(y>last) last=y;
    }
    void blit(u16 ink) const {
        const int right=camera.left+256;
        for(int y=first;y<=last;++y) {
            const auto *row=pixels+y*40;
            for(int x=camera.left;x<right;) {
                if(!row[x/8]) {x=(x|7)+1;continue;}
                if(!(row[x/8]&(0x80>>(x&7)))) {++x;continue;}
                const int start=x++;
                while(x<right && (row[x/8]&(0x80>>(x&7)))) ++x;
                canvas_hline(start-camera.left,y,x-start,ink);
            }
        }
    }
};
static_assert(composite_offset+3072<=(256*64)*sizeof(u16));
void laser_pixels(int left,int y,uint16_t dots,void *context)
{
    static_cast<EffectMask*>(context)->put(left,y,dots);
}
void arc(EffectMask &mask,int cx,int cy,int radius,int step,int first,int last)
{
    // shape_ellipse_arc_put's native byte cache, including no final flush.
    int previous=-1;unsigned dots=0;
    for(int angle=first;angle<=last;angle+=step) {
        const int x=polar_x_fast(cx,radius,angle),y=polar_y_fast(cy,radius,angle);
        const int offset=y*80+(x>>3);
        if(offset!=previous) {
            if(x>=0 && x<640 && previous>=0 && previous<32000)
                mask.put((previous%80)*8,previous/80,dots<<8);
            previous=offset;dots=0x80>>(x&7);
        } else dots|=0x80>>(x&7);
    }
}
void star_line(EffectMask &mask,int left,int top,int right,int bottom)
{
    // graph_r_line's native integer-before-float clipping and byte/y cache.
    // Keep the omitted final flush, but never destructive unput writes.
    if(left>right) {const int x=left,y=top;left=right;top=bottom;right=x;bottom=y;}
    if(top==bottom) {
        if(top<0 || top>399) return;
        for(int x=left<0 ? 0 : left;x<=right && x<640;++x) mask.put(x,top,0x8000>>(x&7));
        return;
    }
    if(left==right) {
        if(left<0 || left>639) return;
        if(top>bottom) {const int y=top;top=bottom;bottom=y;}
        for(int y=top<0 ? 0 : top;y<=bottom && y<400;++y) mask.put(left,y,0x8000>>(left&7));
        return;
    }
    if(left<0) {if(right<0) return;top+=((bottom-top)/(right-left))*(-left);left=0;}
    if(right>639) {if(left>639) return;bottom-=((bottom-top)/(right-left))*(right-639);right=639;}
    if(top<0) {if(bottom<0) return;left+=((right-left)/(bottom-top))*(-top);top=0;}
    if(bottom>399) {if(top>399) return;right-=((right-left)/(bottom-top))*(bottom-399);bottom=399;}
    if(bottom<0) {right+=((right-left)/(bottom-top))*(-bottom);bottom=0;}
    if(top>399) {left-=((right-left)/(bottom-top))*(top-399);top=399;}
    const int w=right-left,dir=top<bottom ? 1 : -1,h=(bottom-top)*dir;
    const int steps=w>h ? w : h,minor=w>h ? h : w;
    int x=left,y=top,bx=x&~7,row=y,error=steps/2;unsigned dots=0;
    for(int i=0;i<=steps;++i) {
        if((x&~7)!=bx || y!=row) {mask.put(bx,row,dots);dots=0;bx=x&~7;row=y;}
        dots|=0x8000>>(x&7);error-=minor;
        if(w>h) ++x;else y+=dir;
        if(error<0) {error+=steps;if(w>h) y+=dir;else ++x;}
    }
}
}
bool elis_art_asset_valid(const void *data,size_t size)
{
    if(!data || size<signatures+2+payload || size>signatures+384+payload) return false;
    const auto *p=static_cast<const u8*>(data);const unsigned count=word(p+14),body=signatures+count*2;
    if(std::memcmp(p,"ELS4",4) || word(p+4)!=128 || word(p+6)!=96 || word(p+8)!=8 ||
       word(p+10)!=5 || word(p+12)!=256 || !count || count>192 ||
       dword(p+16)!=indices || dword(p+20)!=signatures || dword(p+24)!=body ||
       dword(p+28)!=body+girl_bytes || size!=body+payload) return false;
    for(unsigned i=32;i<80;++i) if(p[i]>15) return false;
    for(unsigned i=indices;i<signatures;++i) if(p[i]<16 || p[i]>=16+count) return false;
    for(unsigned i=body;i<body+girl_bytes;++i) if(p[i]>15) return false;
    for(unsigned i=body+girl_bytes;i<body+girl_bytes+mask_bytes+star_bytes;++i) if(p[i]>1) return false;
    for(unsigned i=body+girl_bytes+mask_bytes+star_bytes;i<size;++i) if(p[i]>15) return false;
    return true;
}
bool ElisView::prepare_entrance()
{
    if(data_) return !defeating_ && !defeated_;
    if(g_canvas_target || camera.left<0 || camera.left>VIEW_MAX_LEFT) return false;
    FILE *file=std::fopen("nitro:/elis_art.bin","rb");if(!file) return false;
    if(std::fseek(file,0,SEEK_END)) {std::fclose(file);return false;}
    const long size=std::ftell(file);
    if(size<long(signatures+2+payload) || size>long(signatures+384+payload) || std::fseek(file,0,SEEK_SET)) {std::fclose(file);return false;}
    data_=static_cast<u8*>(std::malloc(size));
    const bool read=data_ && std::fread(data_,1,size,file)==size_t(size) && std::fgetc(file)==EOF && !std::ferror(file);
    const bool closed=std::fclose(file)==0;
    if(!read || !closed || !elis_art_asset_valid(data_,size)) {end();return false;}
    if(!boss_gfx_init()) {end();return false;}
    body_=dword(data_+24);spheres_=dword(data_+28);
    palette(false);
    for(int i=0;i<16;++i) BG_PALETTE[i]=colors_[i];
    for(unsigned i=0;i<word(data_+14);++i) {
        const unsigned signature=word(data_+signatures+i*2);unsigned sum[3]{};
        for(int p=0;p<4;++p) for(int c=0;c<3;++c) sum[c]+=data_[32+((signature>>(p*4))&15)*3+c]*17/4;
        BG_PALETTE[16+i]=RGB15(sum[0]/8,sum[1]/8,sum[2]/8);
    }
    dmaFillHalfWords(0,BG_BMP_RAM(4),128*1024);
    for(int y=0;y<192;++y) for(int x=0;x<320;x+=2) BG_BMP_RAM(4)[y*256+x/2]=word(data_+indices+y*320+x);
    pending_=combat_=defeating_=defeated_=false;revision_=~0u;camera_=composite_image_=-1;published_flash_=0;
    static_assert(3072+sizeof(BossArrow_tiles)<=4096);
    for(unsigned i=0;i<sizeof(BossArrow_tiles)/2;++i) boss_gfx()[3072/2+i]=reinterpret_cast<const u16*>(BossArrow_tiles)[i];
    oamSetHidden(&oamMain,OAM_SLOT_BOSS_ARROW,true);shake_=0;
    return true;
}
bool ElisView::begin()
{
    if(active_) return !defeating_ && !defeated_;
    if(g_canvas_target || camera.left<0 || camera.left>VIEW_MAX_LEFT || !prepare_entrance()) return false;
    effects_=static_cast<u16*>(std::malloc(256*256*2));
    if(!effects_ || !canvas_buffer_begin(effects_)) {end();return false;}
    normal_master_=REG_MASTER_BRIGHT;active_=true;return true;
}
bool ElisView::compose(const ElisEntranceFrame &f)
{
    if(!active_ || defeating_ || defeated_ || combat_ || unsigned(f.girl)>ElisEntranceFrame::Ripple || camera.left<0 || camera.left>VIEW_MAX_LEFT ||
       (f.girl==ElisEntranceFrame::Wave && (f.wave_put_frame<24 || f.wave_put_frame>96 || f.wave_put_frame%4))) return false;
    for(const auto &trail:f.spheres) for(const auto &s:trail)
        if(s.cel<0 || s.cel>3 || s.left< -1024 || s.left>1024 || s.top< -1024 || s.top>1024) return false;
    if(revision_==f.revision && camera_==camera.left) return true;
    canvas_begin_frame();canvas_playfield_clip(false);
    if(f.girl!=ElisEntranceFrame::Hidden) for(int y=0;y<48;++y) {
        const int len=f.girl==ElisEntranceFrame::Ripple ? 3 : f.wave_put_frame+2;
        const int amp=f.girl==ElisEntranceFrame::Ripple ? 8 : 100-f.wave_put_frame;
        const int phase=f.girl==ElisEntranceFrame::Ripple ? 64 : f.wave_put_frame*6;
        const int offset=f.girl==ElisEntranceFrame::Still ? 0 : (amp*Sin8((phase+y*2*(256/len))&255))/256;
        canvas_put_indexed(camera.x(ELIS_LEFT+offset),Camera::y(ELIS_TOP)+y,64,1,data_+body_+y*64,sprite_colors_);
    }
    const u16 sphere_colors[]={0,colors_[8]};
    for(const auto &trail:f.spheres) for(const auto &s:trail) if(s.cel) {
        // grc_put_8 truncates X to its native8-pixel byte boundary.
        const int aligned=s.left-(s.left%8+8)%8;
        canvas_put_indexed(camera.x(aligned),Camera::y(s.top),16,16,data_+spheres_+(s.cel-1)*256,sphere_colors);
    }
    canvas_buffer_prepare();revision_=f.revision;camera_=camera.left;pending_=true;return true;
}
void ElisView::palette(bool flash)
{
    flash_=flash;
    for(int i=0;i<16;++i) {
        colors_[i]=flash && (i==3 || i==6 || i==8 || i==2) ? RGB15(31,31,31) :
            RGB15(data_[32+i*3]*17/8,data_[33+i*3]*17/8,data_[34+i*3]*17/8);
        sprite_colors_[(i+1)&15]=colors_[i];
    }
}
bool ElisView::compose(const ElisPhase1State &s,const MissilePool &missiles)
{
    if(!active_ || defeating_ || defeated_ || camera.left<0 || camera.left>VIEW_MAX_LEFT || s.image< -1 || s.image>=8 ||
       (s.attack_overlay && (s.image<0 || s.image>6)) ||
       s.entity.left<0 || s.entity.left>512 || s.entity.top<64 || s.entity.top>272 ||
       s.bat.left<0 || s.bat.left>736 || s.bat.top<64 || s.bat.top>304 || s.bat_shown< -1 || s.bat_shown>2 ||
       s.bat_draw_x<0 || s.bat_draw_x>736 || s.bat_draw_y<64 || s.bat_draw_y>304 || s.shake<0 || s.shake>2 ||
       unsigned(s.circle)>ElisPhase1State::Full || s.angle<0 || s.angle>64 || s.radius<0 || s.radius>272 ||
       s.step<1 || s.step>32 || s.circle_color<0 || s.circle_color>15) return false;
    if(s.circle_override && (s.circle_x<16 || s.circle_x>624 || s.circle_y<64 || s.circle_y>400)) return false;
    for(const auto &r:s.rifts) if(r.cel<4 || r.cel>5 || r.x< -96 || r.x>864 || r.y< -16 || r.y>416 || r.color<0 || r.color>15) return false;
    for(const auto &p:s.stars) if(p.x< -32 || p.x>672 || p.y<0 || p.y>328) return false;
    for(const auto &p:s.spheres) if(p.x<0 || p.x>672 || p.y<0 || p.y>448) return false;
    for(int i=0;i<MISSILE_COUNT;++i) if(missiles.at(i).visible && unsigned(missiles.at(i).cel)>=18) return false;
    palette(s.flash);combat_=true;camera_=camera.left;shake_=s.shake;
    for(int i=0;i<MISSILE_COUNT;++i) {
        const auto &m=missiles.at(i);
        if(m.visible) canvas_put_indexed(camera.x(m.draw_x),Camera::y(m.draw_y),8,8,Missile_pixels[m.cel],sprite_colors_);
    }
    const u8 *body=s.image>=0 ? data_+body_+s.image*3072 : nullptr;
    if(s.attack_overlay) {
        auto *combined=reinterpret_cast<u8*>(effects_+256*192)+composite_offset;
        if(composite_image_!=s.image) {
            std::memcpy(combined,body,3072);const auto *overlay=data_+body_+7*3072;
            for(int i=0;i<3072;++i) if(overlay[i]) combined[i]=overlay[i];
            composite_image_=s.image;
        }
        body=combined;
    } else composite_image_=-1;
    if(!s.bat_form && body) canvas_put_indexed(camera.x(s.entity.left),Camera::y(s.entity.top),64,48,body,sprite_colors_);
    if(s.bat_form && s.bat_shown>=0)
        canvas_put_indexed(camera.x(s.bat_draw_x&~7),Camera::y(s.bat_draw_y),24,16,
            data_+spheres_+mask_bytes+star_bytes+s.bat_shown*384,sprite_colors_);
    const auto &active=s.bat_form ? s.bat : s.entity;
    offscreen_indicator_render(OAM_SLOT_BOSS_ARROW,active.left,active.top,active.width,active.height,
        boss_gfx()+3072/2,s.health.hp<=0 || (s.bat_form ? s.bat_shown<0 : s.image<0));
    EffectMask circle(effects_,0),rays(effects_,1);
    const int cx=s.circle_override ? s.circle_x : s.entity.left+64;
    const int cy=s.circle_override ? s.circle_y : s.entity.top+(s.circle==ElisPhase1State::Sweep ? 32 : 48);
    if(s.circle==ElisPhase1State::Summon) {
        // Intentional visual fix: phase1's original0.05 endpoint truncated to
        // its start and left a quadrant empty. Use the same2-angle arc in all4.
        for(int a=0;a<s.angle;a+=2) for(int q=0;q<4;++q) arc(circle,cx,cy,128,1,a+q*64,a+q*64+2);
    } else if(s.circle!=ElisPhase1State::None) arc(circle,cx,cy,s.radius,s.step,0,255);
    if(s.hexagram) {
        const int ends[][2]={{-64,22},{-64,106},{22,106},{64,-106},{64,-22},{-106,-22}};
        for(const auto &edge:ends) star_line(circle,polar_x_fast(cx,128,edge[0]),polar_y_fast(cy,128,edge[0]),
                                            polar_x_fast(cx,128,edge[1]),polar_y_fast(cy,128,edge[1]));
    }
    circle.blit(colors_[s.circle==ElisPhase1State::Summon ? 7 : s.circle==ElisPhase1State::Sweep ? 4 : s.circle_color]);
    // b15m.cpp: rifts/COL_FX4 = gray-blue; teleport stars2 = yellow;
    // lasers/V_WHITE7 = white. BOS/GRC loaders skip their embedded palettes.
    const u16 rift_colors[]={0,colors_[4]},star_colors[]={0,colors_[2]};
    if(s.rifts_visible) for(const auto &r:s.rifts) {
        const u16 ink[]={0,colors_[r.color]};
        canvas_put_indexed(camera.x(r.x&~7),Camera::y(r.y),16,16,data_+spheres_+(r.cel-1)*256,ink);
    }
    if(s.stars_visible) for(const auto &p:s.stars)
        canvas_put_indexed(camera.x(p.x),Camera::y(p.y),4,4,data_+spheres_+mask_bytes,star_colors);
    for(const auto &p:s.spheres) if(p.visible) {
        canvas_put_indexed(camera.x(p.x&~7),Camera::y(p.y),16,16,data_+spheres_+2*256,rift_colors);
        // Source overlays C_RIFT over the sphere rather than replacing it.
        if(p.fired) canvas_put_indexed(camera.x(p.x&~7),Camera::y(p.y),16,16,data_+spheres_+3*256,rift_colors);
    }
    for(const auto &laser:s.lasers) shootout_laser_visit(laser,laser_pixels,&rays);
    rays.blit(colors_[7]);
    pending_=true;return true;
}
void ElisView::publish_palette()
{
    if(published_flash_!=int(flash_)) {
        for(int i=0;i<16;++i) BG_PALETTE[i]=colors_[i];
        for(unsigned i=0;i<word(data_+14);++i) {
            const unsigned signature=word(data_+signatures+i*2);unsigned sum[3]{};
            for(int p=0;p<4;++p) for(int c=0;c<3;++c) {
                const int index=(signature>>(p*4))&15;
                sum[c]+=(flash_ && (index==3 || index==6 || index==8 || index==2) ? 15 : data_[32+index*3+c])*17/4;
            }
            BG_PALETTE[16+i]=RGB15(sum[0]/8,sum[1]/8,sum[2]/8);
        }
        published_flash_=flash_;video_bomb_palette_changed();
    }
}
bool ElisView::present(int death_shake)
{
    if(!active_ || defeating_ || defeated_ || camera_!=camera.left || REG_VCOUNT<192 || REG_VCOUNT>=200) return false;
    if(combat_ && (!pending_ || !canvas_buffer_ready())) return false;
    if(combat_) {
        publish_palette();
        video_bomb_commit();
    }
    video_apply_death_shake(death_shake<0 ? (combat_ ? shake_+video_bomb_shake() : 0) : death_shake,true);
    bgShow(g_bg_id_background);oamUpdate(&oamMain);video_stage_reveal(true);
    if(pending_) {pending_=false;return canvas_buffer_present(true);}
    return true;
}
void ElisView::end()
{
    if(active_) {oamSetHidden(&oamMain,OAM_SLOT_BOSS_ARROW,true);if(combat_) video_bomb_reset();canvas_buffer_end();canvas_clear();}
    std::free(effects_);std::free(data_);effects_=nullptr;data_=nullptr;active_=pending_=defeating_=defeated_=false;
    revision_=~0u;camera_=-1;
}
