#include "yuugenmagan_render.hpp"
#include "boss_gfx.hpp"
#include "boss_render.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "video.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace {
constexpr unsigned indices=80,signatures=indices+61440,eye_bytes=7*512;
constexpr unsigned minimum=signatures+2+eye_bytes,maximum=signatures+192*2+eye_bytes;
constexpr int palette_bank=3;
unsigned word(const u8 *p) { return p[0]|(p[1]<<8); }
unsigned dword(const u8 *p) { return word(p)|(word(p+2)<<16); }
static_assert(eye_bytes<=4096);
unsigned component(const u8 *data,int index,int c,const YuugenMaganOpeningFrame &state,bool flash)
{
    int value=data[32+index*3+c];
    if(index==15) value=state.color15_override ? state.color15[c] : value+(c<2 ? state.recolor : 0);
    if(flash && (index==1 || index==11)) value=15;
    return unsigned(value<0 ? 0 : value>15 ? 15 : value)*17;
}
}

bool yuugenmagan_visual_valid(const YuugenMaganOpeningFrame &state)
{
    if(state.recolor<0 || state.recolor>8 || state.lasers>31) return false;
    for(int cel:state.shown) if(cel<0 || cel>=YUUGENMAGAN_CELS) return false;
    if(state.color15_override) for(int c:state.color15) if(c< -128 || c>127) return false;
    return true;
}

bool yuugenmagan_art_asset_valid(const void *data,size_t size)
{
    if(!data || size<minimum || size>maximum) return false;
    const auto *p=static_cast<const u8*>(data);
    const unsigned count=word(p+14),sprites=signatures+count*2;
    if(std::memcmp(p,"YMG1",4) || word(p+4)!=64 || word(p+6)!=48 || word(p+8)!=7 ||
        word(p+10)!=512 || word(p+12)!=5 || !count || count>192 ||
        dword(p+16)!=indices || dword(p+20)!=signatures || dword(p+24)!=sprites ||
        dword(p+28)!=size || size!=sprites+eye_bytes) return false;
    for(unsigned i=32;i<80;++i) if(p[i]>15) return false;
    for(unsigned i=indices;i<signatures;++i) if(p[i]<16 || p[i]>=16+count) return false;
    for(unsigned i=0;i<eye_bytes;++i)
        if((i<512 || i%512>=384) && p[sprites+i]) return false; // hidden cel and padded bottom8 rows
    return true;
}

bool YuugenMaganView::begin()
{
    if(data_) return true;
    if(g_canvas_target) return false; // never steal another boss's canvas lease
    FILE *f=std::fopen("nitro:/yuugenmagan_art.bin","rb");if(!f) return false;
    u8 header[32];
    if(std::fread(header,1,sizeof(header),f)!=sizeof(header) || std::memcmp(header,"YMG1",4) ||
        dword(header+28)<minimum || dword(header+28)>maximum) {std::fclose(f);return false;}
    const unsigned size=dword(header+28);
    auto *data=static_cast<u8*>(std::malloc(size));
    if(!data) {std::fclose(f);return false;}
    std::memcpy(data,header,sizeof(header));
    const bool read=std::fread(data+32,1,size-32,f)==size-32 && std::fgetc(f)==EOF && !std::ferror(f);
    const bool closed=std::fclose(f)==0;
    if(!read || !closed || !yuugenmagan_art_asset_valid(data,size) || !boss_gfx_init()) {
        std::free(data);return false;
    }
    for(int i=0;i<16;++i) saved_obj_palette_[i]=SPRITE_PALETTE[palette_bank*16+i];
    data_=data;signatures_=word(data+14);sprites_=dword(data+24);
    installed_=false;recolor_=-1;boss_render_hide();return true;
}

void YuugenMaganView::colors(const YuugenMaganOpeningFrame &state,bool flash)
{
    if(recolor_==state.recolor && flash_==flash && color15_override_==state.color15_override &&
        (!state.color15_override || std::equal(state.color15,state.color15+3,color15_))) return;
    unsigned rgb[16][3];
    for(int i=0;i<16;++i) for(int c=0;c<3;++c) rgb[i][c]=component(data_,i,c,state,flash);
    for(int i=0;i<16;++i) {
        const u16 color=RGB15(rgb[i][0]>>3,rgb[i][1]>>3,rgb[i][2]>>3);
        BG_PALETTE[i]=color;
        SPRITE_PALETTE[palette_bank*16+((i+1)&15)]=i==15 ? 0 : color;
    }
    for(unsigned i=0;i<signatures_;++i) {
        const unsigned signature=word(data_+signatures+i*2);
        unsigned sum[3]{};
        for(int p=0;p<4;++p) for(int c=0;c<3;++c) sum[c]+=rgb[(signature>>(p*4))&15][c]/4;
        BG_PALETTE[16+i]=RGB15(sum[0]>>3,sum[1]>>3,sum[2]>>3);
    }
    recolor_=state.recolor;flash_=flash;color15_override_=state.color15_override;
    std::copy(state.color15,state.color15+3,color15_);
}

bool YuugenMaganView::sprite_palette(u16 (&out)[16],const YuugenMaganOpeningFrame &state,bool flash) const
{
    if(!data_ || !yuugenmagan_visual_valid(state)) return false;
    out[0]=0;
    for(int i=1;i<16;++i) out[i]=RGB15(component(data_,i-1,0,state,flash)>>3,
        component(data_,i-1,1,state,flash)>>3,component(data_,i-1,2,state,flash)>>3);
    return true;
}

void YuugenMaganView::cover_eyes(const YuugenMaganOpeningFrame &state) const
{
    if(!data_ || !yuugenmagan_visual_valid(state)) return;
    for(int i=0;i<YUUGENMAGAN_EYES;++i) if(state.shown[i]!=YMG_HIDDEN) {
        const int left=camera.x(YUUGENMAGAN_LEFT[i]),top=Camera::y(YUUGENMAGAN_TOP[i]);
        const u8 *tiles=data_+sprites_+state.shown[i]*512;
        for(int y=0;y<24;++y) for(int x=0;x<32;++x) {
            const int px=left+x,py=top+y;
            if(px<0 || px>=256 || py<UPPER_HUD_H || py>=192 || !*canvas_at(px,py)) continue;
            const int offset=((y/8)*4+x/8)*32+(y%8)*4+(x%8)/2;
            if((tiles[offset]>>((x%2)*4))&15) canvas_clear_rect(px,py,1,1);
        }
    }
}

void yuugenmagan_lasers_render(unsigned mask)
{
    for(int i=0;i<YUUGENMAGAN_EYES;++i) if(mask&(1u<<i)) {
        const int top=Camera::y(YUUGENMAGAN_TOP[i]+28);
        canvas_vline(camera.x(YUUGENMAGAN_LEFT[i]+28),top,Camera::y(398)-top+1,RGB15(31,31,31));
    }
}

bool YuugenMaganView::frame(const YuugenMaganOpeningFrame &state,bool flash)
{
    if(!data_ || camera.left<0 || camera.left>VIEW_MAX_LEFT || !yuugenmagan_visual_valid(state)) return false;
    if(!installed_) {
        // No DMA from stack/DTCM; only halfword writes to bitmap/OBJ VRAM.
        auto *dst=reinterpret_cast<u16*>(bgGetGfxPtr(g_bg_id_background));
        dmaFillHalfWords(0,dst,512*256);
        for(int y=0;y<192;++y) for(int x=0;x<320;x+=2)
            dst[y*256+x/2]=word(data_+indices+y*320+x);
        for(unsigned i=0;i<eye_bytes/2;++i) boss_gfx()[i]=word(data_+sprites_+i*2);
        installed_=true;
    }
    colors(state,flash);
    for(int i=0;i<YUUGENMAGAN_EYES;++i) {
        const int x=camera.x(YUUGENMAGAN_LEFT[i]),y=Camera::y(YUUGENMAGAN_TOP[i]);
        oamSet(&oamMain,BOSS_OAM_SLOT+i,x,y,OAM_PRIORITY_GAMEPLAY,palette_bank,SpriteSize_32x32,
            SpriteColorFormat_16Color,boss_gfx()+state.shown[i]*256,-1,false,
            state.shown[i]==YMG_HIDDEN || !view_visible(x,y,32,24),false,false,false);
    }
    yuugenmagan_lasers_render(state.lasers);
    // A distributed five-eye boss has no single offscreen-arrow target yet.
    // Do not accidentally reuse the previous boss's shared-cache marker.
    oamSetHidden(&oamMain,OAM_SLOT_BOSS_ARROW,true);
    video_apply_death_shake(0,true);bgShow(g_bg_id_background);bgUpdate();return true;
}

void YuugenMaganView::end()
{
    if(!data_) return;
    for(int i=0;i<YUUGENMAGAN_EYES;++i) oamSetHidden(&oamMain,BOSS_OAM_SLOT+i,true);
    boss_render_hide();
    if(installed_) for(int i=0;i<16;++i) SPRITE_PALETTE[palette_bank*16+i]=saved_obj_palette_[i];
    std::free(data_);data_=nullptr;installed_=false;recolor_=-1;
}
