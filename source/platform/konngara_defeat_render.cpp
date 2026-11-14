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
#include <cerrno>
#ifdef __NDS__
#include <malloc.h>
#endif

namespace {
constexpr size_t header=232,page_bytes=320*200,max_size=header+10*(510+page_bytes);
// Two indexed pages (including their dictionaries) fit in the existing128KiB
// effects staging half. The other half still preserves the old BG3 backdrop.
constexpr size_t cache_stride=(page_bytes+510+31)&~size_t(31),read_chunk=8192;
static_assert(cache_stride*2<=128*1024,"defeat caches exceed existing staging RAM");
KonngaraDefeatDiagnostics diagnostics;

void record_failure(KonngaraDefeatFailure failure,int error)
{
    diagnostics.failure=failure;diagnostics.error_number=error;
#ifdef __NDS__
    const auto info=mallinfo();
    diagnostics.heap_available=true;
    diagnostics.heap_start=reinterpret_cast<uintptr_t>(getHeapStart());
    diagnostics.heap_end=reinterpret_cast<uintptr_t>(getHeapEnd());
    diagnostics.heap_limit=reinterpret_cast<uintptr_t>(getHeapLimit());
    // mallinfo free bytes are only INSIDE the committed heap. The remaining
    // growable space is reported separately; neither is a largest-block probe.
    diagnostics.heap_used=info.uordblks;diagnostics.heap_free=info.fordblks;
#endif
}
unsigned word(const u8 *p) { return p[0]|(p[1]<<8); }
unsigned dword(const u8 *p) { return word(p)|(word(p+2)<<16); }
const u8 *entry(const u8 *p,int page) { return p+112+page*12; }

bool header_valid(const u8 *p,size_t size)
{
    if(std::memcmp(p,"KGD1",4) || word(p+4)!=320 || word(p+6)!=200 || word(p+8)!=10 || word(p+10) || dword(p+12)!=size) return false;
    for(int i=16;i<112;++i) if(p[i]>15) return false;
    size_t offset=header;
    for(int page=0;page<10;++page) {
        const u8 *d=entry(p,page);unsigned count=word(d);
        if(!count || count>255 || (page==9 && count>207) || word(d+2) || dword(d+4)!=offset ||
           dword(d+8)!=offset+count*2 || offset+count*2+page_bytes>size) return false;
        offset+=count*2+page_bytes;
    }
    return offset==size;
}

bool page_valid(const u8 *p,unsigned count)
{
    for(unsigned i=0;i<count;++i) {
        unsigned s=word(p+i*2);
        if((s&15)>((s>>4)&15) || ((s>>4)&15)>((s>>8)&15) || ((s>>8)&15)>(s>>12)) return false;
        if(i && word(p+(i-1)*2)>=s) return false;
    }
    const u8 *pixels=p+count*2;
    for(size_t i=0;i<page_bytes;++i) if(!pixels[i] || pixels[i]>count) return false;
    return true;
}

void palette(u16 (&out)[256],const u8 *data,const u8 *signatures,const KonngaraDefeatFrame &frame,bool hit_flash)
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
        unsigned signature=word(signatures+i*2);int sums[3]={};
        for(int pixel=0;pixel<4;++pixel) for(int c=0;c<3;++c)
            sums[c]+=components[(signature>>(pixel*4))&15][c];
        out[i+1]=RGB15(sums[0]>>3,sums[1]>>3,sums[2]>>3);
    }
}
}

const KonngaraDefeatDiagnostics &konngara_defeat_diagnostics() { return diagnostics; }

const char *konngara_defeat_failure_name(KonngaraDefeatFailure failure)
{
    switch(failure) {
    case KonngaraDefeatFailure::None: return "none";
    case KonngaraDefeatFailure::State: return "inactive/already defeated";
    case KonngaraDefeatFailure::Open: return "file open";
    case KonngaraDefeatFailure::HeaderRead: return "header read";
    case KonngaraDefeatFailure::HeaderSize: return "header size";
    case KonngaraDefeatFailure::Allocation: return "asset allocation";
    case KonngaraDefeatFailure::BodyRead: return "asset read";
    case KonngaraDefeatFailure::EndRead: return "end-of-file read";
    case KonngaraDefeatFailure::TrailingData: return "unexpected trailing data";
    case KonngaraDefeatFailure::Validation: return "asset validation";
    case KonngaraDefeatFailure::Seek: return "page seek";
    case KonngaraDefeatFailure::Prefetch: return "page prefetch incomplete";
    }
    return "unknown";
}

bool konngara_defeat_asset_valid(const void *data,size_t size)
{
    if(!data || size<header || size>max_size) return false;
    const auto *p=static_cast<const u8*>(data);
    if(!header_valid(p,size)) return false;
    for(int page=0;page<10;++page) {
        const u8 *d=entry(p,page);
        if(!page_valid(p+dword(d+4),word(d))) return false;
    }
    return true;
}

bool KonngaraCombatView::defeat(bool hit_flash)
{
    diagnostics={};diagnostics.active=active_;diagnostics.already_defeated=defeated_;
    FILE *file=nullptr;
    auto fail=[&](KonngaraDefeatFailure failure,int error=0) {
        record_failure(failure,error); // Capture before fclose/free or caller cleanup.
        if(file) std::fclose(file);
        return false;
    };
    if(!active_ || defeated_) return fail(KonngaraDefeatFailure::State);
    // Stop ALICE at the lethal hit, before potentially slow NitroFS reads.
    // A load failure still leaves live graphics/palettes untouched for cleanup.
    music_stop();
    bgSetScroll(g_bg_id_background,0,0);
    errno=0;file=std::fopen("nitro:/konngara_defeat.bin","rb");
    if(!file) return fail(KonngaraDefeatFailure::Open,errno);
    u8 h[header];
    errno=0;diagnostics.read_bytes=std::fread(h,1,16,file);
    if(diagnostics.read_bytes!=16) return fail(KonngaraDefeatFailure::HeaderRead,errno);
    size_t size=dword(h+12);
    diagnostics.requested_bytes=size;
    if(size<header || size>max_size) return fail(KonngaraDefeatFailure::HeaderSize);
    errno=0;diagnostics.read_bytes+=std::fread(h+16,1,header-16,file);
    if(diagnostics.read_bytes!=header) return fail(KonngaraDefeatFailure::HeaderRead,errno);
    if(!header_valid(h,size)) return fail(KonngaraDefeatFailure::Validation);
    u8 *cache[2]={reinterpret_cast<u8*>(background_backup_+65536),nullptr};
    cache[1]=cache[0]+cache_stride;
    auto read=[&](u8 *out,size_t bytes) {
        errno=0;size_t count=std::fread(out,1,bytes,file);diagnostics.read_bytes+=count;
        return count==bytes;
    };
    // Validate the whole file before touching live graphics, just as before,
    // but reuse one existing staging page instead of malloc'ing the full file.
    for(int page=0;page<10;++page) {
        diagnostics.page=page;
        unsigned count=word(entry(h,page));
        if(!read(cache[0],count*2+page_bytes)) return fail(KonngaraDefeatFailure::BodyRead,errno);
        if(!page_valid(cache[0],count)) return fail(KonngaraDefeatFailure::Validation);
    }
    errno=0;
    if(std::fgetc(file)!=EOF) return fail(KonngaraDefeatFailure::TrailingData);
    if(std::ferror(file)) return fail(KonngaraDefeatFailure::EndRead,errno);
    // Have the first two pages ready before the first cinematic frame. Later
    // pages are prefetched in <=8KiB pieces over the preceding ten-frame hold,
    // rather than stalling for a whole64KiB read at a visible page boundary.
    errno=0;if(std::fseek(file,header,SEEK_SET)) return fail(KonngaraDefeatFailure::Seek,errno);
    for(int page=0;page<2;++page) {
        diagnostics.page=page;unsigned count=word(entry(h,page));
        if(!read(cache[page],count*2+page_bytes)) return fail(KonngaraDefeatFailure::BodyRead,errno);
        if(!page_valid(cache[page],count)) return fail(KonngaraDefeatFailure::Validation);
    }
    text_layer_clear();
    defeated_=true;pending_=false;
    int cached[2]={0,1},loading=-1;size_t loaded=0;
    KonngaraDefeat timeline(h[16+48+7*3]);
    KonngaraDefeatFrame frame{},previous{};bool first=true;
    u16 colors[256];
    while(timeline.next(frame)) {
        diagnostics.page=frame.page;
        const int slot=frame.page&1,next=frame.page+1;
        if(cached[slot]!=frame.page) return fail(KonngaraDefeatFailure::Prefetch);
        if(next<10 && cached[slot^1]!=next) {
            unsigned count=word(entry(h,next));size_t bytes=count*2+page_bytes;
            diagnostics.page=next;
            if(loading!=next) {
                loading=next;loaded=0;
                errno=0;if(std::fseek(file,dword(entry(h,next)+4),SEEK_SET)) return fail(KonngaraDefeatFailure::Seek,errno);
            }
            size_t piece=bytes-loaded;if(piece>read_chunk) piece=read_chunk;
            if(!read(cache[slot^1]+loaded,piece)) return fail(KonngaraDefeatFailure::BodyRead,errno);
            loaded+=piece;
            if(loaded==bytes) {
                if(!page_valid(cache[slot^1],count)) return fail(KonngaraDefeatFailure::Validation);
                cached[slot^1]=next;loading=-1;
            }
        }
        const u8 *page=cache[slot]+word(entry(h,frame.page))*2;
        bool image=first || frame.page!=previous.page || frame.line_on_top!=previous.line_on_top;
        bool recolor=first || frame.page!=previous.page || frame.ramp!=previous.ramp || frame.fade!=previous.fade;
        // DMA reads the CURRENT page directly from heap-backed staging, never
        // the DTCM header. Flush after all prefetch writes, before presentation.
        if(image) DC_FlushRange(page,page_bytes);
        if(recolor) palette(colors,h,cache[slot],frame,hit_flash);
        do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
        if(first) {
            for(int i=0;i<128;++i) oamSetHidden(&oamMain,i,true);
            oamUpdate(&oamMain);
            bgHide(0);bgHide(g_bg_id_tiles);bgHide(g_bg_id_canvas);
            video_bomb_reset(); // no palette/brightness ownership into the cinematic
            bgSetScroll(g_bg_id_background,0,0);bgUpdate();
        }
        if(image) for(int y=0;y<VIEW_H;++y) {
            int row=((16+frame.line_on_top)/2+y)%200;
            dmaCopyHalfWords(3,page+row*320+VIEW_MAX_LEFT/2,BG_BMP_RAM(4)+y*256,256);
        }
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
    std::fclose(file);
    // Last scroll/reset is black. Restore unused text inks only so the practice
    // result can be shown, retaining the black scene until its normal cleanup.
    for(int i=208;i<256;++i) BG_PALETTE[i]=bg_palette_[i];
    bgShow(0);
    return true;
}
