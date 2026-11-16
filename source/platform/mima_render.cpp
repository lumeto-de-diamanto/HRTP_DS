#include "mima_render.hpp"
#include "boss_gfx.hpp"
#include "boss_render.hpp"
#include "orb_render.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "sprites/BossArrow.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace {
constexpr size_t asset_size = 48 + MIMA_FRAME_BYTES * MIMA_FRAME_COUNT;
constexpr int palette_bank = 3, lower_slot = BOSS_OAM_SLOT + 1;
unsigned le16(const u8 *p) { return p[0] | (p[1] << 8); }
unsigned le32(const u8 *p) { return le16(p) | (le16(p+2) << 16); }
static_assert(MIMA_FRAME_BYTES + sizeof(BossArrow_tiles) <= 4096);
}

bool mima_art_asset_valid(const void *data,size_t size)
{
    if(!data || reinterpret_cast<uintptr_t>(data)%4 || size!=asset_size) return false;
    const auto *p=static_cast<const u8 *>(data);
    if(std::memcmp(p,"MIM1",4) || le16(p+4)!=64 || le16(p+6)!=96 ||
       le16(p+8)!=MIMA_FRAME_COUNT || le16(p+10)!=MIMA_FRAME_BYTES || le32(p+12)!=size) return false;
    for(int i=0;i<16;++i) if(le16(p+16+i*2)>0x7fff) return false;
    return le16(p+16)==0;
}

bool MimaView::begin()
{
    if(data_) return true;
    FILE *input=std::fopen("nitro:/mima_art.bin","rb");
    if(!input) return false;
    u8 header[16];
    if(std::fread(header,1,sizeof(header),input)!=sizeof(header) ||
       std::memcmp(header,"MIM1",4) || le32(header+12)!=asset_size) {
        std::fclose(input);return false;
    }
    auto *data=static_cast<u8 *>(std::malloc(asset_size));
    if(!data) { std::fclose(input);return false; }
    std::memcpy(data,header,sizeof(header));
    const bool read=std::fread(data+16,1,asset_size-16,input)==asset_size-16 &&
        std::fgetc(input)==EOF && !std::ferror(input);
    const bool closed=std::fclose(input)==0;
    if(!read || !closed || !mima_art_asset_valid(data,asset_size) || !boss_gfx_init()) {
        std::free(data);return false;
    }
    boss_render_hide();
    data_=data;last_frame_=-1;
    return true;
}

void MimaView::end()
{
    if(!data_) return;
    boss_render_hide();oamSetHidden(&oamMain,lower_slot,true);
    std::free(data_);data_=nullptr;last_frame_=-1;
}

bool MimaView::frame(int asset_frame,int left,int top,bool hidden)
{
    if(!data_ || asset_frame<0 || asset_frame>=MIMA_FRAME_COUNT) return false;
    if(last_frame_<0) {
        for(int i=0;i<16;++i) SPRITE_PALETTE[palette_bank*16+i]=le16(data_+16+i*2);
        std::memcpy(boss_gfx()+MIMA_FRAME_BYTES/2,BossArrow_tiles,sizeof(BossArrow_tiles));
    }
    if(last_frame_!=asset_frame) {
        // Halfword-safe CPU VRAM upload; no byte stores or cache-unsafe DMA.
        const auto *src=reinterpret_cast<const u16 *>(data_+48+asset_frame*MIMA_FRAME_BYTES);
        for(int i=0;i<MIMA_FRAME_BYTES/2;++i) boss_gfx()[i]=src[i];
        last_frame_=asset_frame;
    }
    const int x=camera.x(left),y=camera.y(top);
    const bool blank=asset_frame==0 || hidden;
    oamSet(&oamMain,BOSS_OAM_SLOT,x,y,OAM_PRIORITY_GAMEPLAY,palette_bank,SpriteSize_64x64,
        SpriteColorFormat_16Color,boss_gfx(),-1,false,blank || !view_visible(x,y,64,64),false,false,false);
    oamSet(&oamMain,lower_slot,x,y+64,OAM_PRIORITY_GAMEPLAY,palette_bank,SpriteSize_64x32,
        SpriteColorFormat_16Color,boss_gfx()+2048/2,-1,false,blank || !view_visible(x,y+64,64,32),false,false,false);
    // Real128x160 bounds, not the lower OBJ's16px padding.
    offscreen_indicator_render(OAM_SLOT_BOSS_ARROW,left,top,128,160,
        boss_gfx()+MIMA_FRAME_BYTES/2,blank);
    return true;
}
