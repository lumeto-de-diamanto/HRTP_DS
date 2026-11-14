#include <nds.h>
#include <cstring>

#include "round_results.hpp"
#include "background.hpp"
#include "canvas.hpp"
#include "font16x16.hpp"
#include "hud_render.hpp"
#include "sound_effects.hpp"
#include "subscreen.hpp"
#include "text_layer.hpp"
#include "video.hpp"
#include "backgrounds/HudPattern.h"
#include "backgrounds/RoundResults.h"

namespace {
constexpr int results_map=31, results_tiles=4, pattern_block=16, art_base=32;
constexpr char alphabet[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";
constexpr int font_words=(1+(sizeof(alphabet)-1)*4)*16;
static_assert(font_words*2<16*1024,"results font must fit the saved BG3 scratch region");

void frame()
{
    do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
}

void hold(int frames) { while(frames-->0) frame(); }

void bake_font()
{
    u16 *out=BG_TILE_RAM(results_tiles);
    dmaFillHalfWords(0,out,font_words*2);
    for(unsigned c=0;c<sizeof(alphabet)-1;++c) {
        const unsigned jis=alphabet[c]=='/' ? 0x213f : 0x2300+alphabet[c];
        const u16 *glyph=font16x16_find(jis);
        if(!glyph) continue;
        for(int tile=0;tile<4;++tile) for(int y=0;y<8;++y) for(int half=0;half<2;++half) {
            u16 packed=0;
            for(int x=0;x<4;++x)
                if(glyph[(tile/2)*8+y]&(0x8000>>((tile%2)*8+half*4+x))) packed|=1<<(x*4);
            out[(1+c*4+tile)*16+y*2+half]=packed;
        }
    }
}

void letter(int x,int y,char c)
{
    const char *found=c==' ' ? nullptr : std::strchr(alphabet,c);
    const int tile=found ? 1+(found-alphabet)*4 : 0;
    for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx)
        BG_MAP_RAM(results_map)[(y/8+dy)*32+x/8+dx]=tile ?
            (tile+dy*2+dx)|TILE_PALETTE(TEXT_PAL_RED) : 0;
}

void text(int x,int y,const char *s) { for(;*s;++s,x+=16) letter(x,y,*s); }

void palette_step(int step)
{
    for(int i=0;i<16;++i) {
        const u16 start=i==4 ? RGB15(1,1,1) : i==15 ? RGB15(3,2,2) : 0;
        const u16 target=RoundResults_palette[i];
        u16 color=0;
        for(int shift=0;shift<=10;shift+=5) {
            const int a=(start>>shift)&31,b=(target>>shift)&31;
            color|=(a+(b-a)*step/15)<<shift;
        }
        BG_PALETTE_SUB[art_base+i]=color;
    }
    BG_PALETTE[4]=BG_PALETTE_SUB[4]=BG_PALETTE_SUB[art_base+4];
    BG_PALETTE[5]=BG_PALETTE_SUB[5]=BG_PALETTE_SUB[art_base+15];
}

int row_amount(int tick,int y)
{
    const int age=tick-(y*400/192)/8;
    return age<=0 ? 0 : age>=20 ? 256 : age*256/20;
}

void top_pattern_row(int y,int amount)
{
    volatile u16 *out=BG_BMP_RAM(pattern_block)+y*256;
    const u16 colors[]={u16(0x8000|BG_PALETTE[4]),u16(0x8000|BG_PALETTE[5])};
    const int first=(y&1) ? 0 : 256-amount,last=(y&1) ? amount : 256;
    for(int x=first;x<last;++x) {
        const int source=(y&1) ? x+256-amount : x-(256-amount);
        out[x]=colors[HudPattern_pixels[(y&127)*128+(source&127)]==5];
    }
}

void top_zipper_row(int tick,int y,int amount)
{
    volatile u16 *out=BG_BMP_RAM(pattern_block)+y*256;
    const int delta=amount-row_amount(tick-1,y);
    // Preserve the actual composed boss-end pixels, shifting the old image
    // out in opposite directions. In-place traversal must follow the shift.
    if(y&1) for(int x=255;x>=amount;--x) out[x]=out[x-delta]|0x8000;
    else for(int x=0;x<256-amount;++x) out[x]=out[x+delta]|0x8000;
    top_pattern_row(y,amount);
}

void recolor_top(int tick)
{
    // RGB capture pixels don't follow palette writes. Recolour only the
    // incoming pattern, including completed rows; never tint the frozen boss.
    for(int y=0;y<192;++y) top_pattern_row(y,row_amount(tick,y));
}

void zipper()
{
    // Original: 400 rows, eight activated per iteration, twenty32px shifts
    // across640px, with a two-frame delay. Retain its70-iteration cadence;
    // resample progress, not the approved unscaled256x192 crop itself.
    int active=0,done_before=1,palette=0;
    for(int tick=0;tick<70;++tick) {
        int done=0;
        for(int y=0;y<192;++y) {
            const int birth=1+(y*400/192)/8,age=tick-birth+1;
            if(age>20) { ++done;continue; }
            if(age<=0) continue;
            if(age==20) ++done;
            // Even rows shift left and inject the source prefix at the right;
            // odd rows shift right and inject the source suffix at the left.
            const int amount=age*256/20;
            top_zipper_row(tick,y,amount);
            // ARM9's stack is in DTCM, which DMA cannot read. Publish paired
            // pixels with CPU halfword stores, like the gameplay sub renderer.
            // Volatile prevents the compiler from turning these into STRB.
            volatile u16 *out=BG_BMP_RAM_SUB(0)+y*128;
            for(int x=0;x<256;x+=2) {
                u16 packed=0;
                for(int half=0;half<2;++half) {
                    const int px=x+half,source=(y&1) ? px+256-amount : px-(256-amount);
                    const bool incoming=(y&1) ? px<amount : px>=256-amount;
                    const int shifted=(y&1) ? px-amount : px+amount;
                    const u8 index=incoming ? art_base+RoundResults_pixels[y*256+source] :
                        HudPattern_pixels[(y%128)*128+(shifted&127)];
                    packed|=u16(index)<<(half*8);
                }
                out[x/2]=packed;
            }
        }
        hold(2);
        if(done>=192) break;
        if(active<400) active+=8;
        if(done!=done_before && !(active&15) && palette<15) {
            palette_step(++palette);recolor_top(tick);
        }
        done_before=done;
    }
    palette_step(15);
    recolor_top(69);
}

void roll_number(int right,int y,int value,int digits,int &clock)
{
    int divisor=1;
    for(int i=1;i<digits;++i) divisor*=10;
    for(int place=0;place<digits;++place,divisor/=10) {
        const int digit=value/divisor%10,x=right-(digits-place)*16;
        // Original: stop on the second visit to the target digit,4ms per
        // nonterminal step. Quantize to60Hz without lengthening every4ms
        // step into a whole frame or flooding the native SFX queue: one
        // SFX2 restart per visible update, never for invisible intermediate steps.
        for(int step=0;step<=10+digit;++step) {
            letter(x,y,'0'+step%10);
            if(step==10+digit) break;
            clock+=240;
            if(clock>=1000) { clock-=1000;frame();mdrv2_se_play(SFX_CARD); }
        }
    }
    hold(5);
}
}

void round_results_run(const StageBonus &bonus,int stage)
{
    // Scratch lives in already-resident BG3 memory, not a new framebuffer or
    // OAM allocation. Its original pixels and the unused map are restored.
    alignas(4) u16 old_font[font_words],old_map[1024];
    u16 main_palette[256],sub_palette[256];
    const u32 display=REG_DISPCNT;
    const u32 capture=REG_DISPCAPCNT;
    const u16 blend=REG_BLDCNT,alpha=REG_BLDALPHA,fade=REG_BLDY;
    const u16 master=REG_MASTER_BRIGHT,sub_master=REG_MASTER_BRIGHT_SUB;
    for(int i=0;i<font_words;++i) old_font[i]=BG_TILE_RAM(results_tiles)[i];
    for(int i=0;i<1024;++i) old_map[i]=BG_MAP_RAM(results_map)[i];
    for(int i=0;i<256;++i) {main_palette[i]=BG_PALETTE[i];sub_palette[i]=BG_PALETTE_SUB[i];}
    hold(50);
    frame();
    // The caller has an empty unbuffered canvas. Borrow bank D for one
    // hardware capture of the composed top screen (HUD/OAM included), not a
    // software reconstruction or a heap framebuffer. Its old BG2 was empty.
    REG_DISPCNT&=~(1u<<10);
    vramSetBankD(VRAM_D_LCD);
    REG_DISPCAPCNT=DCAP_ENABLE|DCAP_MODE(DCAP_MODE_A)|DCAP_SRC_A(DCAP_SRC_A_COMPOSITED)|
        DCAP_SIZE(DCAP_SIZE_256x192)|DCAP_BANK(DCAP_BANK_VRAM_D);
    do { frame(); } while(REG_DISPCAPCNT&DCAP_ENABLE);
    vramSetBankD(VRAM_D_MAIN_BG_0x06040000);
    // BG2 now holds that capture. Keep it visible until the zipper has
    // replaced every pixel; the temporary results text map stays blank.
    REG_DISPCNT=(display&~0xff00u)|(1u<<10);
    REG_BLDCNT=0;REG_MASTER_BRIGHT=REG_MASTER_BRIGHT_SUB=0;
    subscreen_clear(SUB_COL_BG);hud_invalidate();
    bgInitHidden(0,BgType_Text4bpp,BgSize_T_256x256,results_map,results_tiles);
    bgInitHidden(3,BgType_Bmp8,BgSize_B8_128x128,pattern_block,0);
    bgSetPriority(0,0);bgSetPriority(3,3);bgWrapOn(3);
    bake_font();dmaFillHalfWords(0,BG_MAP_RAM(results_map),2048);
    BG_PALETTE[TEXT_PAL_RED*16+1]=RGB15(31,0,0);
    palette_step(0);bgSetScroll(3,0,0);bgUpdate();
    zipper();
    // Identical final pattern, now hardware-repeated. Upload and switch in
    // one fresh VBlank so the bitmap/atlas handoff cannot expose a hard cut.
    frame();
    DC_FlushRange(HudPattern_pixels,sizeof(HudPattern_pixels));
    dmaCopyHalfWords(3,HudPattern_pixels,BG_BMP_RAM(pattern_block),sizeof(HudPattern_pixels));
    bgShow(0);bgShow(3);
    REG_DISPCNT=(display&~0xff00u)|(1<<8)|(1<<11);
    text(64,8,"STAGE");letter(160,8,'0'+stage/10%10);letter(176,8,'0'+stage%10);
    text(64,24,"COMPLETE");
    text(8,56,"TIME");text(8,80,"MAXCOMBO");text(8,104,"BOMB/LIFE");
    text(8,128,"CLEAR");text(8,152,"TOTAL");
    hold(30);
    int clock=0;
    roll_number(248,56,bonus.time*10,5,clock);
    roll_number(248,80,bonus.combo*10,5,clock);
    roll_number(248,104,bonus.resources*10,5,clock);
    roll_number(248,128,bonus.stage*10,6,clock);
    roll_number(248,152,bonus.total,6,clock);
    text(80,176,"A OR B");
    // A held route confirmation (including presses during rolling) must be
    // released before a new edge can acknowledge the finished results.
    bool released=false;
    while(true) {
        frame();scanKeys();
        const u16 held=keysHeld(),down=keysDown();
        if(!(held&(KEY_A|KEY_B))) released=true;
        if(released && (down&(KEY_A|KEY_B))) break;
    }
    frame();REG_DISPCNT&=~0xff00u;
    // These backups are also on the DTCM stack: restoring via DMA would
    // corrupt scenery/map memory even if the range had been cache-flushed.
    volatile u16 *font_out=BG_TILE_RAM(results_tiles),*map_out=BG_MAP_RAM(results_map);
    for(int i=0;i<font_words;++i) font_out[i]=old_font[i];
    for(int i=0;i<1024;++i) map_out[i]=old_map[i];
    canvas_clear();subscreen_clear(SUB_COL_BG);hud_invalidate();
    bgInitHidden(0,BgType_Text4bpp,BgSize_T_256x256,30,2);bgSetPriority(0,0);
    bgInitHidden(3,BgType_Bmp8,BgSize_B8_512x256,4,0);bgSetPriority(3,3);
    if(display&(1<<8)) bgShow(0);
    if(display&(1<<11)) bgShow(3);
    bgSetScroll(3,background_scroll_x(),0);bgUpdate();
    for(int i=0;i<256;++i) {BG_PALETTE[i]=main_palette[i];BG_PALETTE_SUB[i]=sub_palette[i];}
    REG_BLDCNT=blend;REG_BLDALPHA=alpha;REG_BLDY=fade;
    REG_MASTER_BRIGHT=master;REG_MASTER_BRIGHT_SUB=sub_master;
    REG_DISPCAPCNT=capture;
    REG_DISPCNT=display;
}
