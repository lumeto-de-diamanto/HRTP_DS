#include "konngara_combat_render.hpp"
#include "boss_gfx.hpp"
#include "video.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "sprites/Shape8x8.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr size_t bg_colors=208,obj_colors=144,obj_first=112,palette_start=48,palette_states=32;
constexpr size_t directory=palette_start+bg_colors*2*palette_states,backdrop=directory+16*8;
constexpr size_t poses=backdrop+320*192,pose_bytes=obj_colors*2*palette_states+4096,asset_size=poses+16*pose_bytes;
constexpr int slash_slot=75,slash_slots=32;
unsigned le16(const u8 *p) { return p[0]|(p[1]<<8); }
unsigned le32(const u8 *p) { return le16(p)|(le16(p+2)<<16); }
struct Shape { int w,h; SpriteSize size; };
const Shape shapes[]={{8,8,SpriteSize_8x8},{16,16,SpriteSize_16x16},{32,32,SpriteSize_32x32},{64,64,SpriteSize_64x64},
    {16,8,SpriteSize_16x8},{32,8,SpriteSize_32x8},{32,16,SpriteSize_32x16},{64,32,SpriteSize_64x32},
    {8,16,SpriteSize_8x16},{8,32,SpriteSize_8x32},{16,32,SpriteSize_16x32},{32,64,SpriteSize_32x64}};
const Shape *shape(int w,int h) { for (const auto &s:shapes) if(s.w==w && s.h==h) return &s; return nullptr; }
const u8 *entry(const u8 *data,int id) { return data+32+id*24; }
void copy_words(u16 *out,const u8 *in,size_t bytes) { for(size_t i=0;i<bytes/2;++i) out[i]=le16(in+i*2); }
u8 *load(const char *name,size_t &size,size_t limit)
{
    FILE *f=std::fopen(name,"rb"); if(!f) return nullptr;
    if(std::fseek(f,0,SEEK_END)) { std::fclose(f);return nullptr; }
    long length=std::ftell(f);
    if(length<=0 || size_t(length)>limit || std::fseek(f,0,SEEK_SET)) { std::fclose(f);return nullptr; }
    size=length; auto *p=static_cast<u8*>(std::malloc(size));
    bool okay=p && std::fread(p,1,size,f)==size;std::fclose(f);
    if(!okay) { std::free(p);return nullptr; } return p;
}
void laser_pixels(int left,int y,uint16_t dots,void *)
{
    int base=left&~7;
    for(int bit=0;bit<16;) {
        if(!(dots&(0x8000>>bit))) { ++bit;continue; }
        int first=bit++;while(bit<16 && (dots&(0x8000>>bit))) ++bit;
        int x=camera.x(base+first),right=camera.x(base+bit-1);
        canvas_hline(x,Camera::y(y),right-x+1,0x7fff);
    }
}
}

bool konngara_combat_asset_valid(const void *data,size_t size)
{
    if(!data || size!=asset_size) return false;
    const auto *p=static_cast<const u8*>(data);
    unsigned count=le16(p+10);
    if(std::memcmp(p,"KGC2",4) || le16(p+4)!=320 || le16(p+6)!=192 || le16(p+8)!=16 ||
       !count || count>=bg_colors || le32(p+12)!=size) return false;
    for(size_t i=16;i<48;i+=2) if(le16(p+i)&0x8000) return false;
    for(size_t state=0;state<32;++state) for(size_t i=0;i<208;++i) {
        unsigned c=le16(p+48+(state*208+i)*2);
        if((c&0x8000) || ((!i || i>count) && c)) return false;
    }
    for(size_t i=backdrop;i<poses;++i) if(!p[i] || p[i]>count) return false;
    for(size_t pose=0;pose<16;++pose) {
        const u8 *d=p+directory+pose*8;unsigned n=le16(d);
        if(!n || n>144 || le16(d+2) || le32(d+4)!=poses+pose*pose_bytes) return false;
        const u8 *head=p+poses+pose*pose_bytes;
        for(size_t state=0;state<32;++state) for(size_t i=0;i<144;++i) {
            unsigned c=le16(head+(state*144+i)*2);
            if((c&0x8000) || (i>=n && c)) return false;
        }
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
            unsigned index=head[144*64+((y/8)*8+x/8)*64+(y%8)*8+x%8];
            if(x<40 && y<40) { if(index<112 || index>=112+n) return false; }
            else if(index) return false;
        }
    }
    return true;
}

bool konngara_slash_asset_valid(const void *data,size_t size)
{
    constexpr size_t post_offset=32+28*24,patch_offset=post_offset+asset_size;
    if(!data || size<patch_offset || size>1024*1024) return false;
    const auto *p=static_cast<const u8*>(data);
    if(std::memcmp(p,"KHS2",4) || le32(p+4)!=size || le32(p+8)!=post_offset || le32(p+12)!=asset_size ||
       le32(p+16)!=patch_offset || le32(p+24)!=32 || le32(p+28)!=28 ||
       !konngara_combat_asset_valid(p+post_offset,asset_size)) return false;
    unsigned patches=le32(p+20),previous=0,bg_count=le16(p+post_offset+10);
    if(patches>30720 || patches*4>size-patch_offset) return false;
    for(unsigned i=0;i<patches;++i) {
        const u8 *q=p+patch_offset+i*4;unsigned offset=le16(q),value=le16(q+2);
        if((i && offset<=previous) || offset/256>=192 || offset%256>=160 ||
           !(value&255) || !(value>>8) || (value&255)>bg_count || (value>>8)>bg_count) return false;
        previous=offset;
    }
    size_t offset=patch_offset+patches*4;
    for(int frame=0;frame<28;++frame) {
        const u8 *d=entry(p,frame);
        unsigned objects=le16(d),colors=le16(d+2),used[2]={le16(d+4),le16(d+6)};
        if(objects>32 || colors>144 || used[0]>4096 || used[1]>4096 || (used[0]&63) || (used[1]&63) ||
           le32(d+8)!=offset || le32(d+12)!=offset+576 || le32(d+16)!=offset+576+objects*16 ||
           le32(d+20)!=le32(d+16)+used[0] || offset+576+objects*16+used[0]+used[1]>size) return false;
        for(unsigned flash=0;flash<2;++flash) for(unsigned i=0;i<144;++i) {
            unsigned c=le16(p+offset+(flash*144+i)*2);
            if((c&0x8000) || (i>=colors && c)) return false;
        }
        uint64_t occupied[2]={0,0};
        for(unsigned i=0;i<objects;++i) {
            const u8 *o=p+le32(d+12)+i*16;
            unsigned x=le16(o),y=le16(o+2),w=le16(o+4),h=le16(o+6),bank=le16(o+8),start=le16(o+10);
            if(!shape(w,h) || x+w>320 || y+h>192 || bank>1 || (start&63) || start+w*h>used[bank] || le32(o+12)) return false;
            unsigned tiles=w*h/64,first=start/64;
            uint64_t mask=tiles==64 ? ~uint64_t(0) : ((uint64_t(1)<<tiles)-1)<<first;
            if(occupied[bank]&mask) return false;
            occupied[bank]|=mask;
        }
        for(unsigned bank=0;bank<2;++bank) for(unsigned i=0;i<used[bank];++i) {
            unsigned index=p[le32(d+16+bank*4)+i];
            if(index && (index<112 || index>=112+colors)) return false;
        }
        offset=le32(d+20)+used[1];
    }
    return offset==size;
}

bool KonngaraCombatView::begin()
{
    if(active_) return true;
    if(g_canvas_target || camera.left<0 || camera.left>VIEW_MAX_LEFT) return false;
    size_t size=0;
    data_=load("nitro:/konngara_combat.bin",size,asset_size);
    if(!data_ || !konngara_combat_asset_valid(data_,size)) { end();return false; }
    slash_data_=load("nitro:/konngara_slash.bin",size,1024*1024);
    if(!slash_data_ || !konngara_slash_asset_valid(slash_data_,size)) { end();return false; }
    const u8 *post=slash_data_+le32(slash_data_+8);
    if(le16(post+10)!=le16(data_+10) || std::memcmp(post+48,data_+48,208*64)) { end();return false; }
    unsigned patch=0,count=le32(slash_data_+20);
    for(int y=0;y<192;++y) for(int x=0;x<320;x+=2) {
        unsigned a=le16(data_+backdrop+y*320+x),b=le16(post+backdrop+y*320+x);
        if(a==b) continue;
        const u8 *q=slash_data_+le32(slash_data_+16)+patch*4;
        if(patch>=count || le16(q)!=unsigned(y*256+x/2) || le16(q+2)!=b) { end();return false; }
        ++patch;
    }
    if(patch!=count) { end();return false; }
    background_backup_=static_cast<u16*>(std::malloc(256*1024));
    if(!background_backup_ || !boss_gfx_init()) { end();return false; }
    extra_gfx_=oamAllocateGfx(&oamMain,SpriteSize_64x64,SpriteColorFormat_256Color);
    if(!extra_gfx_) { end();return false; }
    for(size_t i=0;i<65536;++i) background_backup_[i]=BG_BMP_RAM(4)[i];
    for(unsigned i=0;i<256;++i) bg_palette_[i]=BG_PALETTE[i];
    for(unsigned i=0;i<144;++i) obj_palette_[i]=SPRITE_PALETTE[112+i];
    g_bg_id_background=bgInitHidden(3,BgType_Bmp8,BgSize_B8_512x256,4,0);
    bgSetPriority(g_bg_id_background,3);
    for(int y=0;y<192;++y) copy_words(BG_BMP_RAM(4)+y*256,data_+backdrop+y*320,320);
    canvas_buffer_begin(background_backup_+65536);
    for(int i=0;i<slash_slots;++i) oamSetHidden(&oamMain,slash_slot+i,true);
    active_=true;defeated_=false;initial_head_=true;post_slash_=last_post_=pending_=false;
    last_pose_=last_slash_=last_siddham_=-1;last_flash_=false;shadow_objects_=0;
    direction_=KonngaraDirection::Center;expression_=KonngaraExpression::Neutral;
    work_={};work_.background_bytes=61440;
    KonngaraCombatState initial{};initial.direction=direction_;initial.expression=expression_;
    return frame(initial);
}

bool KonngaraCombatView::frame(const KonngaraCombatState &s)
{
    if(!active_ || defeated_ || unsigned(s.direction)>=3 || unsigned(s.expression)>=4 || camera.left<0 || camera.left>64 ||
       s.visual_count<0 || s.visual_count>16 || unsigned(s.siddham_white)>=16 || s.slash_cel < -1 ||
       s.slash_cel>6 || s.slash_cycles<0 || (s.ray_active && (unsigned(s.ray_left)>=640 || unsigned(s.ray_top)>=400))) return false;
    bool slash=s.slash_cel>=0;
    if(slash && (s.siddham_white || s.slash_cycles<1 ||
       (s.expression!=KonngaraExpression::Closed && s.expression!=KonngaraExpression::Aim))) return false;
    pending_post_=s.slash_cycles>0;pending_flash_=s.health.flashing;pending_shade_=s.siddham_white;
    pending_slash_=slash ? ((s.slash_cycles>1?2:0)+(s.expression==KonngaraExpression::Aim?1:0))*7+s.slash_cel : -1;
    unsigned objects=0;
    if(slash) {
        const u8 *d=entry(slash_data_,pending_slash_);objects=le16(d);
        oamSetHidden(&oamMain,BOSS_OAM_SLOT,true);
        for(unsigned i=0;i<objects;++i) {
            const u8 *o=slash_data_+le32(d+12)+i*16;
            int x=int(le16(o))-camera.left,y=le16(o+2),w=le16(o+4),h=le16(o+6);
            u16 *gfx=(le16(o+8)?extra_gfx_:boss_gfx())+le16(o+10)/2;
            oamSet(&oamMain,slash_slot+i,x,y,OAM_PRIORITY_GAMEPLAY,0,shape(w,h)->size,SpriteColorFormat_256Color,
                   gfx,-1,false,x>=VIEW_W || x+w<=0 || y>=VIEW_H || y+h<=0,false,false,false);
        }
    } else {
        if(s.bos_head) initial_head_=false;
        if(direction_!=s.direction) { direction_=s.direction;initial_head_=false; }
        if(expression_!=s.expression) { expression_=s.expression;if(expression_==KonngaraExpression::Neutral) initial_head_=false; }
        pending_pose_=(initial_head_?12:int(direction_)*4)+int(expression_);
        oamSet(&oamMain,BOSS_OAM_SLOT,140-camera.left,32,OAM_PRIORITY_GAMEPLAY,0,SpriteSize_64x64,
               SpriteColorFormat_256Color,boss_gfx(),-1,false,false,false,false,false);
    }
    for(unsigned i=objects;i<unsigned(shadow_objects_);++i) oamSetHidden(&oamMain,slash_slot+i,true);
    shadow_objects_=objects;pending_=true;
    bgSetScroll(g_bg_id_background,camera.left,0); // shadow only; bgUpdate at commit
    for(const auto &laser:s.lasers) shootout_laser_visit(laser,laser_pixels,nullptr);
    if(s.ray_active) {
        int x=camera.x(410),y=Camera::y(70),tx=camera.x(s.ray_left),ty=Camera::y(s.ray_top);
        int dx=tx-x,dy=ty-y,sx=dx<0?-1:1,sy=dy<0?-1:1;
        if(dx<0) dx=-dx;
        if(dy<0) dy=-dy;
        int error=dx-dy;
        while(true) {
            canvas_fill_rect(x,y,1,1,le16(data_+28));if(x==tx && y==ty) break;
            int twice=error*2;if(twice>-dy) { error-=dy;x+=sx; }if(twice<dx) { error+=dx;y+=sy; }
        }
    }
    int t=s.siddham_white;
    u16 color9=RGB15(t*17>>3,(t>9?t:9)*17>>3,(t>10?t:10)*17>>3);
    for(int i=0;i<s.visual_count;++i) {
        const auto &v=s.visuals[i];u16 palette[]={0,u16(v.color==7?0x7fff:v.color==9?color9:le16(data_+28))};
        canvas_put_indexed(camera.x(v.left),Camera::y(v.top),4,4,Shape8x8_cels[0],palette);
    }
    return true;
}

bool KonngaraCombatView::present()
{
    if(!active_ || defeated_ || !pending_ || !canvas_buffer_ready() || REG_VCOUNT<192 || REG_VCOUNT>=200) return false;
    // Gate the whole commit once, before any live writes. Texture/palette work
    // must not push a second early-VBlank gate past its window mid-commit.
    bool palette_changed=last_siddham_!=pending_shade_ || last_flash_!=pending_flash_;
    if(pending_post_ && !post_slash_) {
        const u8 *p=slash_data_+le32(slash_data_+16);unsigned n=le32(slash_data_+20);
        for(unsigned i=0;i<n;++i) BG_BMP_RAM(4)[le16(p+i*4)]=le16(p+i*4+2);
        work_.background_bytes+=n*2;post_slash_=true;
    }
    if(palette_changed) {
        const u8 *p=data_+48+(pending_shade_*2+int(pending_flash_))*208*2;
        for(unsigned i=0;i<208;++i) BG_PALETTE[i]=le16(p+i*2);
        video_bomb_palette_changed();
        work_.palette_writes+=208;
    }
    if(pending_slash_>=0) {
        const u8 *d=entry(slash_data_,pending_slash_);
        if(last_slash_!=pending_slash_ || palette_changed) {
            const u8 *p=slash_data_+le32(d+8)+(pending_flash_?288:0);
            for(unsigned i=0;i<144;++i) SPRITE_PALETTE[112+i]=le16(p+i*2);
            video_bomb_obj_palette_changed();
            work_.palette_writes+=144;
        }
        if(last_slash_!=pending_slash_) {
            for(unsigned bank=0;bank<2;++bank) {
                unsigned bytes=le16(d+4+bank*2);
                copy_words(bank?extra_gfx_:boss_gfx(),slash_data_+le32(d+16+bank*4),bytes);
                work_.sprite_bytes+=bytes;
            }
            ++work_.slash_uploads;
        }
        last_pose_=-1;
    } else {
        const u8 *world=post_slash_?slash_data_+le32(slash_data_+8):data_;
        const u8 *head=world+poses+pending_pose_*pose_bytes;
        bool changed=last_pose_!=pending_pose_ || last_post_!=post_slash_;
        if(changed || palette_changed) {
            const u8 *p=head+(pending_shade_*2+int(pending_flash_))*144*2;
            for(unsigned i=0;i<144;++i) SPRITE_PALETTE[112+i]=le16(p+i*2);
            video_bomb_obj_palette_changed();
            work_.palette_writes+=144;
        }
        if(changed) { copy_words(boss_gfx(),head+144*64,4096);++work_.head_uploads; }
        last_pose_=pending_pose_;last_post_=post_slash_;
    }
    // Register/texture changes precede the potentially full-playfield stream;
    // never reset affine references after a long canvas transfer mid-scanout.
    video_bomb_commit(true);
    bgUpdate();oamUpdate(&oamMain);bgShow(g_bg_id_background);
    canvas_buffer_present(true);
    last_slash_=pending_slash_;last_flash_=pending_flash_;last_siddham_=pending_shade_;pending_=false;
    return true;
}

void KonngaraCombatView::end()
{
    if(active_) {
        bgHide(g_bg_id_background);
        for(int i=0;i<slash_slots;++i) oamSetHidden(&oamMain,slash_slot+i,true);
        oamSetHidden(&oamMain,BOSS_OAM_SLOT,true);oamUpdate(&oamMain);
        canvas_buffer_end();canvas_clear();video_bomb_reset();
        for(size_t i=0;i<65536;++i) BG_BMP_RAM(4)[i]=background_backup_[i];
        for(unsigned i=0;i<256;++i) BG_PALETTE[i]=bg_palette_[i];
        for(unsigned i=0;i<144;++i) SPRITE_PALETTE[112+i]=obj_palette_[i];
        g_bg_id_background=bgInit(3,BgType_Bmp8,BgSize_B8_512x256,4,0);bgSetPriority(g_bg_id_background,3);
        if(defeated_) { bgShow(0);bgShow(g_bg_id_tiles);bgShow(g_bg_id_canvas); }
        active_=false;pending_=false;video_apply_camera();bgShow(g_bg_id_background);
    }
    if(extra_gfx_) { oamFreeGfx(&oamMain,extra_gfx_);extra_gfx_=nullptr; }
    std::free(background_backup_);background_backup_=nullptr;
    std::free(slash_data_);slash_data_=nullptr;
    std::free(data_);data_=nullptr;
}
