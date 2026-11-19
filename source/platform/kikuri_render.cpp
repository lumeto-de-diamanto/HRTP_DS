#include "kikuri_render.hpp"
#include "background.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "boss_render.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr unsigned source=80,indices=source+128000,signatures=indices+61440;
unsigned word(const u8 *p) { return p[0]|(p[1]<<8); }
unsigned dword(const u8 *p) { return word(p)|(word(p+2)<<16); }
constexpr int ripple_w=48,ripple_h=24,ripple_size=ripple_w*ripple_h;
static_assert(KIKURI_SOUL_OBJ_BYTES<=4096);
static_assert(KIKURI_TEAR_COUNT*ripple_size<=(256*256-256*192)*2);
void laser_pixels(int left,int y,uint16_t dots,void *context)
{
    const u16 ink=*static_cast<const u16*>(context);const int base=left&~7;
    for(int bit=0;bit<16;) {
        if(!(dots&(0x8000>>bit))) {++bit;continue;}
        const int first=bit++;while(bit<16 && (dots&(0x8000>>bit))) ++bit;
        const int x=camera.x(base+first),right=camera.x(base+bit-1);
        canvas_hline(x,Camera::y(y),right-x+1,ink);
    }
}
}
bool kikuri_art_asset_valid(const void *data,size_t size)
{
    if(!data || size<signatures+2+1216 || size>signatures+192*2+1216) return false;
    const auto *p=static_cast<const u8*>(data);const unsigned count=word(p+14);
    if(std::memcmp(p,"KIK1",4) || word(p+4)!=640 || word(p+6)!=400 || word(p+8)!=3 ||
       word(p+10)!=3 || word(p+12)!=4 || !count || count>192 ||
       dword(p+16)!=source || dword(p+20)!=indices || dword(p+24)!=signatures ||
       dword(p+28)!=signatures+count*2 || size!=signatures+count*2+1216) return false;
    for(unsigned i=32;i<80;++i) if(p[i]>15) return false;
    for(unsigned i=indices;i<signatures;++i) if(p[i]<16 || p[i]>=16+count) return false;
    for(unsigned i=dword(p+28);i<size;++i) if(p[i]>15) return false;
    return true;
}
void KikuriView::colors(int white,int inversion,int lightball)
{
    if(white_==white && inversion_==inversion && lightball_==lightball) return;
    white_=white;inversion_=inversion;lightball_=lightball;
    auto component=[&](int index,int c) {
        int value=data_[32+index*3+c];
        if(index==5) {if(c==0) value+=lightball;else if(c==2) value-=lightball;}
        if(value<0) value=0;else if(value>15) value=15;
        if((index==2 || index==6 || index==8 || index==11) && value<white) value=white;
        return unsigned(value);
    };
    unsigned palette[16][3];
    for(int i=0;i<16;++i) for(int c=0;c<3;++c) {
        const unsigned v=component(i,c),inverted=component(i^2,c);
        palette[i][c]=((v*(16-inversion)+inverted*inversion)*17)/16;
    }
    for(int i=0;i<16;++i) {
        palette_[i]=RGB15(palette[i][0]>>3,palette[i][1]>>3,palette[i][2]>>3);
        sprite_colors_[(i+1)&15]=palette_[i];
    }
    // Only the117 shipped2x2 signatures need colors, not all65536 possible
    // combinations. BG3 holds the unchanged artwork throughout the entrance.
    for(unsigned i=0;i<signatures_;++i) {
        const unsigned signature=word(data_+signatures+i*2);
        unsigned rgb[3]{};
        for(int p=0;p<4;++p) for(int c=0;c<3;++c) rgb[c]+=palette[(signature>>(p*4))&15][c]/4;
        palette_[16+i]=RGB15(rgb[0]>>3,rgb[1]>>3,rgb[2]>>3);
    }
}
bool KikuriView::publish_palette()
{
    if(shown_white_==white_ && shown_inversion_==inversion_ && shown_lightball_==lightball_) return false;
    for(unsigned i=0;i<16+signatures_;++i) BG_PALETTE[i]=palette_[i];
    shown_white_=white_;shown_inversion_=inversion_;shown_lightball_=lightball_;return true;
}
bool KikuriView::begin()
{
    if(active_) return true;
    if(g_canvas_target) return false;
    FILE *f=std::fopen("nitro:/kikuri_art.bin","rb");if(!f) return false;
    if(std::fseek(f,0,SEEK_END)) {std::fclose(f);return false;}
    const long size=std::ftell(f);
    if(size<long(signatures+2+1216) || size>long(signatures+192*2+1216) || std::fseek(f,0,SEEK_SET)) {std::fclose(f);return false;}
    data_=static_cast<u8*>(std::malloc(size));
    const bool read=data_ && std::fread(data_,1,size,f)==size_t(size) && std::fgetc(f)==EOF && !std::ferror(f);
    const bool closed=std::fclose(f)==0;
    if(!read || !closed || !kikuri_art_asset_valid(data_,size)) {end();return false;}
    memory_=static_cast<u16*>(std::malloc(256*1024));
    if(!memory_ || !boss_gfx_init()) {end();return false;}
    for(unsigned i=0;i<65536;++i) memory_[i]=BG_BMP_RAM(4)[i];
    for(int i=0;i<208;++i) saved_palette_[i]=BG_PALETTE[i];
    saved_master_=REG_MASTER_BRIGHT;
    for(int i=0;i<16;++i) saved_soul_palette_[i]=SPRITE_PALETTE[KIKURI_SOUL_PALETTE*16+i];
    souls_uploaded_=false;
    for(int i=0;i<2;++i) {souls_[i]={};clipped_cel_[i]=clipped_rows_[i]=-1;}
    signatures_=word(data_+14);sprites_=dword(data_+28);
    white_=inversion_=shown_white_=shown_inversion_=shroud_camera_=-1;
    lightball_=shown_lightball_=-1;
    installed_=shrouded_=clear_canvas_=defeated_=false;brightness_=0;
    std::memset(changed_rows_,0,sizeof(changed_rows_));
    colors(0);
    if(!canvas_buffer_begin(memory_+65536)) {end();return false;}
    for(auto &ripple:ripples_) ripple=RippleCache{};
    boss_render_hide();oamSetHidden(&oamMain,BOSS_OAM_SLOT+1,true);
    active_=true;return true;
}
bool KikuriView::entrance(const KikuriEntrance &timeline,const KikuriEntranceFrame &f)
{
    if(!active_ || defeated_ || camera.left<0 || camera.left>64 || unsigned(f.phase)>KikuriEntranceFrame::Complete || f.white<0 || f.white>15 ||
       f.inversion<0 || f.inversion>16 || f.brightness<0 || f.brightness>16) return false;
    colors(f.white,f.inversion);brightness_=f.brightness;
    // Reuse the canvas lease's RAM for a retained8bpp BG3 shroud. BG2 stays
    // transparent: no averaged/half-transparent dots, full RGB recomposition,
    // or full-screen per-frame transfers. HUD windows already allow BG3.
    auto *staging=reinterpret_cast<u8*>(memory_+65536);
    for(int y=0;y<192;++y) {
        const uint16_t mask=f.phase==KikuriEntranceFrame::Barn ? timeline.rows()[y] : 0xffff;
        if(shrouded_ && shroud_rows_[y]==mask) continue;
        for(int x=0;x<320;++x)
            staging[y*320+x]=(mask&(0x8000>>(x&15))) ? data_[indices+y*320+x] : 0;
        shroud_rows_[y]=mask;changed_rows_[y]=true;
    }
    if(!shrouded_) clear_canvas_=true;
    shrouded_=true;shroud_camera_=camera.left;
    installed_=f.phase!=KikuriEntranceFrame::Barn;
    return true;
}
bool KikuriView::present_entrance()
{
    if(!active_ || defeated_ || !shrouded_ || REG_VCOUNT<192 || REG_VCOUNT>=200) return false;
    publish_palette();
    if(!installed_) BG_PALETTE[0]=0; // shroud is true black, not source color0
    if(clear_canvas_) {dmaFillHalfWords(0,BG_BMP_RAM(16),256*192*2);clear_canvas_=false;}
    const auto *staging=reinterpret_cast<const u8*>(memory_+65536);
    for(int y=0;y<192;++y) if(changed_rows_[y]) {
        // At most eight320-byte rows change per music-paced reveal step.
        // Flush only these DMA-readable heap rows, never a DTCM scratch array.
        DC_FlushRange(staging+y*320,320);
        dmaCopyHalfWords(3,staging+y*320,BG_BMP_RAM(4)+y*256,320);
        changed_rows_[y]=false;
    }
    REG_MASTER_BRIGHT=brightness_ ? (1<<14)|brightness_ : 0;
    bgSetScroll(g_bg_id_background,shroud_camera_,0);bgShow(g_bg_id_background);bgUpdate();
    return true;
}
void KikuriView::prepare_still(bool flash)
{
    if(!active_ || defeated_) return;
    if(shrouded_) {canvas_clear();shrouded_=false;}
    colors(flash?15:0);
}
bool KikuriView::still(bool flash)
{
    if(!active_ || defeated_ || camera.left<0 || camera.left>64) return false;
    prepare_still(flash);publish_palette();REG_MASTER_BRIGHT=0;
    install_background();return true;
}
void KikuriView::install_background()
{
    if(!installed_) {
        DC_FlushRange(data_+indices,61440);
        for(int y=0;y<192;++y)
            dmaCopyHalfWords(3,data_+indices+y*320,BG_BMP_RAM(4)+y*256,320);
        installed_=true;
    }
    bgSetScroll(g_bg_id_background,camera.left,0);bgShow(g_bg_id_background);bgUpdate();
}
void KikuriView::prepare_combat(const KikuriPhase1State &state)
{
    if(!active_ || defeated_) return;
    if(shrouded_) {canvas_clear();shrouded_=false;}
    colors(state.flash?15:0,0,view_clamp(state.lightball_step,0,5));
    for(int i=0;i<2;++i) souls_[i]=state.souls[i];
}
bool KikuriView::present_combat(int death_shake)
{
    if(!active_ || defeated_ || camera.left<0 || camera.left>64 || !canvas_buffer_ready() ||
       REG_VCOUNT<192 || REG_VCOUNT>=200) return false;
    const bool changed=publish_palette();
    if(changed) video_bomb_palette_changed();
    // Kikuri's brown/red-plane colors belong to BG3 and soul OBJ palette3,
    // not Reimu/orb's shared resident OBJ palette. Keep their normal colors
    // through hit flashes and the lightball transition; native bomb brightness
    // still treats the complete top screen exactly as before.
    // Publish the already-flushed canvas BEFORE an optional first BG3 upload
    // flushes the separate asset range. Both transfers stay in this VBlank.
    if(!canvas_buffer_present(true)) return false;
    install_background();video_bomb_commit();present_souls();
    // This static bitmap boss follows the camera1:1, like Konngara, not the
    // regular-stage parallax of the background currently borrowed by review.
    video_apply_death_shake(death_shake<0 ? video_bomb_shake() : death_shake,true);
    oamUpdate(&oamMain);video_stage_reveal(true);
    return true;
}
void KikuriView::draw_combat(const KikuriPhase1State &state)
{
    if(!active_ || defeated_) return;
    // Source updates tears before souls, and the global lasers after the boss.
    for(int i=0;i<KIKURI_TEAR_COUNT;++i) {
        const auto &tear=state.tears[i];
        if(tear.frame==1 && tear.visible) sprite(1,0,(tear.left/8)*8,tear.top);
        else if(tear.frame>=2 && tear.frame<75) ripple(i,tear);
    }
    for(const auto &soul:state.souls) if(soul.visible)
        soul_cutout(soul);
    u16 ink=palette_[10];
    for(const auto &laser:state.lasers) shootout_laser_visit(laser,laser_pixels,&ink);
}
void KikuriView::upload_soul(u16 *target,int cel,int clip_rows)
{
    const u8 *pixels=data_+sprites_+cel*256;
    // Source is row-major indexed ink; DS16x16/4bpp is four8x8 tiles.
    // Halfword CPU stores also work for DTCM sources: no byte VRAM writes/DMA.
    for(int tile=0;tile<4;++tile) for(int y=0;y<8;++y) for(int word=0;word<2;++word) {
        const int py=(tile/2)*8+y,px=(tile%2)*8+word*4;
        u16 ink=0;
        if(py>=clip_rows) for(int x=0;x<4;++x) ink|=pixels[py*16+px+x]<<(x*4);
        target[tile*16+y*2+word]=ink;
    }
}
void KikuriView::soul_cutout(const KikuriSoul &soul)
{
    if(soul.shown_image<0 || soul.shown_image>=3) return;
    const int left=camera.x((soul.left/8)*8),top=Camera::y(soul.top);
    if(!view_visible(left,top,16,16)) return;
    const u8 *pixels=data_+sprites_+soul.shown_image*256;
    // Tears/ripples/sparkles were put BEFORE the souls; lasers/pellets AFTER.
    // Only punch existing early ink out behind opaque OBJ pixels. No colored
    // soul pixels are rasterized or uploaded to BG2, and empty space is free.
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        const int px=left+x,py=top+y;
        if(px>=0 && px<256 && py>=UPPER_HUD_H && py<192 && pixels[y*16+x] && *canvas_at(px,py))
            canvas_clear_rect(px,py,1,1);
    }
}
void KikuriView::soul_actor_priority(int priority)
{
    // Preserve soul-over-player/orb/shot/item ordering, but leave priority-0
    // death puffs, offscreen markers and HUD alone. Stage15's BG1 is empty.
    for(int slot=0;slot<=10;++slot) oamSetPriority(&oamMain,slot,priority);
    for(int slot=24;slot<38;++slot) oamSetPriority(&oamMain,slot,priority);
}
void KikuriView::present_souls()
{
    bool visible=false;
    for(const auto &soul:souls_) visible|=soul.visible && soul.shown_image>=0 && soul.shown_image<3 &&
        view_visible(camera.x((soul.left/8)*8),Camera::y(soul.top),16,16);
    if(visible && !souls_uploaded_) {
        for(int cel=0;cel<3;++cel) upload_soul(boss_gfx()+cel*64,cel);
        souls_uploaded_=true;
    }
    if(souls_uploaded_) for(int i=0;i<16;++i) {
        // BG raw slots already include the approved flash/lightball/bomb
        // treatment. Copy ONLY our bank, never Reimu/orb/shot/HUD colors.
        const u16 color=BG_PALETTE[(i+15)&15];
        if(SPRITE_PALETTE[KIKURI_SOUL_PALETTE*16+i]!=color)
            SPRITE_PALETTE[KIKURI_SOUL_PALETTE*16+i]=color;
    }
    for(int i=0;i<2;++i) {
        const auto &soul=souls_[i];const int cel=soul.shown_image;
        const int x=camera.x((soul.left/8)*8),y=Camera::y(soul.top);
        const bool show=soul.visible && cel>=0 && cel<3 && view_visible(x,y,16,16);
        u16 *gfx=boss_gfx()+(show ? cel*64 : 0);
        if(show && y<UPPER_HUD_H) {
            const int rows=UPPER_HUD_H-y;gfx=boss_gfx()+(3+i)*64;
            if(clipped_cel_[i]!=cel || clipped_rows_[i]!=rows) {
                upload_soul(gfx,cel,rows);clipped_cel_[i]=cel;clipped_rows_[i]=rows;
            }
        }
        // Source puts the second soul last. Lower DS OAM index wins ties.
        oamSet(&oamMain,BOSS_OAM_SLOT+1-i,x,y,OAM_PRIORITY_GAMEPLAY,KIKURI_SOUL_PALETTE,
            SpriteSize_16x16,SpriteColorFormat_16Color,gfx,-1,false,!show,false,false,false);
    }
    soul_actor_priority(visible ? OAM_PRIORITY_GAMEPLAY+1 : OAM_PRIORITY_GAMEPLAY);
}
void KikuriView::ripple_stamp(u8 *pixels,int left,int column,int phase)
{
    const int base=((left-32)/16)*16,x=left+column*16;
    const int offset=phase==0 || phase>=10 ? 0 : phase<=5 ? 8*(phase-1) : 8*(9-phase);
    const int top=16-offset/2,clear_x=((x/16)*16-base)/2;
    const int clear_w=(((x+31)/16)*16-(x/16)*16)/2;
    // Match the source EGC word-rounded unput, followed by byte-aligned ink.
    // This retains its characteristic empty inside stripes at unaligned X.
    for(int y=0;y<8;++y) std::memset(pixels+(top+y)*ripple_w+clear_x,0,clear_w);
    const int cel=phase==0 || (phase>=5 && phase<=9) ? 0 : phase<5 ? 1 : 3;
    const auto *src=data_+sprites_+960+cel*64;
    const int draw_x=((x/8)*8-base)/2;
    for(int y=0;y<8;++y) for(int dx=0;dx<8;++dx) if(src[y*8+dx])
        pixels[(top+y)*ripple_w+draw_x+dx]=src[y*8+dx];
}
void KikuriView::ripple(int index,const KikuriTear &tear)
{
    // Rows192..255 of the leased canvas RAM are never drawn or DMA-published.
    // Keep10 independent indexed histories there:11,520 bytes, no new lease.
    auto *pixels=reinterpret_cast<u8*>(memory_+65536+256*192)+index*ripple_size;
    auto &cache=ripples_[index];
    if(cache.generation!=tear.generation || cache.left!=tear.left || cache.frame>tear.frame) {
        std::memset(pixels,0,ripple_size);cache={tear.generation,tear.left,1};
    }
    while(cache.frame<tear.frame) {
        const int stage=++cache.frame/5;
        if(stage<=10) ripple_stamp(pixels,tear.left,0,stage);
        if(stage>=2 && stage<=12) {
            ripple_stamp(pixels,tear.left,-1,stage-2);ripple_stamp(pixels,tear.left,1,stage-2);
        }
        if(stage>=4 && stage<=14) {
            ripple_stamp(pixels,tear.left,-2,stage-4);ripple_stamp(pixels,tear.left,2,stage-4);
        }
    }
    canvas_put_indexed(camera.x(((tear.left-32)/16)*16),Camera::y(352),ripple_w,ripple_h,pixels,sprite_colors_);
}
bool KikuriView::sprite(int group,int cel,int left,int top)
{
    const int counts[]={3,3,4},widths[]={16,8,8},offsets[]={0,768,960};
    if(!active_ || defeated_ || group<1 || group>2 || cel<0 || cel>=counts[group]) return false;
    const int w=widths[group];
    canvas_put_indexed(camera.x(left),Camera::y(top),w,w,data_+sprites_+offsets[group]+cel*w*w,sprite_colors_);
    return true;
}
void KikuriView::end(bool hold_black)
{
    if(active_) {
        boss_render_hide();oamSetHidden(&oamMain,BOSS_OAM_SLOT+1,true);
        soul_actor_priority(OAM_PRIORITY_GAMEPLAY);
        for(int i=0;i<16;++i) SPRITE_PALETTE[KIKURI_SOUL_PALETTE*16+i]=saved_soul_palette_[i];
        canvas_buffer_end();canvas_clear();
        DC_FlushRange(memory_,128*1024);
        dmaCopyHalfWords(3,memory_,BG_BMP_RAM(4),128*1024);
        for(int i=0;i<208;++i) BG_PALETTE[i]=saved_palette_[i];
        REG_MASTER_BRIGHT=hold_black && defeated_ ? (2<<14)|16 : saved_master_;
        video_apply_camera();active_=false;
    }
    std::free(memory_);std::free(data_);
    memory_=nullptr;data_=nullptr;
}
