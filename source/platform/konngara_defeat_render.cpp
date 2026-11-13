#include "konngara_combat_render.hpp"
#include "main/boss/konngara_defeat.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "text_layer.hpp"
#include "music.hpp"
#include "sound_effects.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr size_t header=232,page_bytes=320*200,max_size=header+10*(510+page_bytes);
unsigned word(const u8 *p) { return p[0]|(p[1]<<8); }
unsigned dword(const u8 *p) { return word(p)|(word(p+2)<<16); }
const u8 *entry(const u8 *p,int page) { return p+112+page*12; }

void palette(u16 (&out)[256],const u8 *data,const KonngaraDefeatFrame &frame,bool hit_flash)
{
    const u8 *d=entry(data,frame.page),*rgb=data+16+(frame.page==9?48:0);
    int components[16][3];
    for(int p=0;p<16;++p) for(int c=0;c<3;++c) {
        int value=rgb[p*3+c];
        if(frame.page<9 && hit_flash && (p==3 || p==4 || p==5)) value=15;
        if(frame.page<9 && p==0) value=frame.page%2 ? 15 : 0;
        if(value>frame.ramp) value=frame.ramp;
        value-=frame.fade;if(value<0) value=0;
        components[p][c]=value*17/4;
    }
    std::memset(out,0,sizeof(out));
    for(unsigned i=0;i<word(d);++i) {
        unsigned signature=word(data+dword(d+4)+i*2);int sums[3]={};
        for(int pixel=0;pixel<4;++pixel) for(int c=0;c<3;++c)
            sums[c]+=components[(signature>>(pixel*4))&15][c];
        out[i+1]=RGB15(sums[0]>>3,sums[1]>>3,sums[2]>>3);
    }
}
}

bool konngara_defeat_asset_valid(const void *data,size_t size)
{
    if(!data || size<header || size>max_size) return false;
    const auto *p=static_cast<const u8*>(data);
    if(std::memcmp(p,"KGD1",4) || word(p+4)!=320 || word(p+6)!=200 || word(p+8)!=10 || word(p+10) || dword(p+12)!=size) return false;
    for(int i=16;i<112;++i) if(p[i]>15) return false;
    size_t offset=header;
    for(int page=0;page<10;++page) {
        const u8 *d=entry(p,page);unsigned count=word(d);
        if(!count || count>255 || (page==9 && count>207) || word(d+2) || dword(d+4)!=offset ||
           dword(d+8)!=offset+count*2 || offset+count*2+page_bytes>size) return false;
        for(unsigned i=0;i<count;++i) {
            unsigned s=word(p+offset+i*2);
            if((s&15)>((s>>4)&15) || ((s>>4)&15)>((s>>8)&15) || ((s>>8)&15)>(s>>12)) return false;
            if(i && word(p+offset+(i-1)*2)>=s) return false;
        }
        offset+=count*2;
        for(size_t i=0;i<page_bytes;++i) if(!p[offset+i] || p[offset+i]>count) return false;
        offset+=page_bytes;
    }
    return offset==size;
}

bool KonngaraCombatView::defeat(bool hit_flash)
{
    if(!active_ || defeated_) return false;
    // Stop ALICE at the lethal hit, before potentially slow NitroFS reads.
    // A load failure still leaves live graphics/palettes untouched for cleanup.
    music_stop();
    bgSetScroll(g_bg_id_background,0,0);
    FILE *file=std::fopen("nitro:/konngara_defeat.bin","rb");if(!file) return false;
    u8 h[16];
    if(std::fread(h,1,16,file)!=16) { std::fclose(file);return false; }
    size_t size=dword(h+12);
    if(size<header || size>max_size) { std::fclose(file);return false; }
    auto *data=static_cast<u8*>(std::malloc(size));
    if(!data) { std::fclose(file);return false; }
    std::memcpy(data,h,16);
    bool loaded=std::fread(data+16,1,size-16,file)==size-16 && std::fgetc(file)==EOF && !std::ferror(file);
    std::fclose(file);
    if(!loaded || !konngara_defeat_asset_valid(data,size)) { std::free(data);return false; }
    text_layer_clear();
    defeated_=true;pending_=false;
    // Reuse the existing128KiB staging area; no second framebuffer/OBJ lease.
    auto *pixels=reinterpret_cast<u8*>(background_backup_+65536);
    KonngaraDefeat timeline(data[16+48+7*3]);
    KonngaraDefeatFrame frame{},previous{};bool first=true;
    u16 colors[256];
    while(timeline.next(frame)) {
        bool image=first || frame.page!=previous.page || frame.line_on_top!=previous.line_on_top;
        bool recolor=first || frame.page!=previous.page || frame.ramp!=previous.ramp || frame.fade!=previous.fade;
        if(image) {
            const u8 *page=data+dword(entry(data,frame.page)+8);
            for(int y=0;y<VIEW_H;++y) {
                int row=((16+frame.line_on_top)/2+y)%200;
                std::memcpy(pixels+y*256,page+row*320+VIEW_MAX_LEFT/2,256);
            }
            DC_FlushRange(pixels,256*192);
        }
        if(recolor) palette(colors,data,frame,hit_flash);
        do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
        if(first) {
            for(int i=0;i<128;++i) oamSetHidden(&oamMain,i,true);
            oamUpdate(&oamMain);
            bgHide(0);bgHide(g_bg_id_tiles);bgHide(g_bg_id_canvas);
            bgSetScroll(g_bg_id_background,0,0);bgUpdate();
        }
        if(image) for(int y=0;y<VIEW_H;++y)
            dmaCopyHalfWords(3,pixels+y*256,BG_BMP_RAM(4)+y*256,256);
        if(recolor) for(int i=0;i<256;++i) BG_PALETTE[i]=colors[i];
        if(first) bgShow(g_bg_id_background);
        if(frame.sound) mdrv2_se_play(7);
        if(first) {
            // OAM is now committed hidden. No dangling visible lease pointers,
            // and no combat cache/frame I/O is needed by the remaining sequence.
            if(extra_gfx_) { oamFreeGfx(&oamMain,extra_gfx_);extra_gfx_=nullptr; }
            std::free(data_);data_=nullptr;std::free(slash_data_);slash_data_=nullptr;
        }
        previous=frame;first=false;
    }
    std::free(data);
    // Last scroll/reset is black. Restore unused text inks only so the practice
    // result can be shown, retaining the black scene until its normal cleanup.
    for(int i=208;i<256;++i) BG_PALETTE[i]=bg_palette_[i];
    bgShow(0);
    return true;
}
