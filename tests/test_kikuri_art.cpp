// Real retained BG renderer + production entrance, independent mask oracle.
#include <nds.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "kikuri_render.hpp"
#include "heap_probe.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "video.hpp"
#include "background.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/stage/timer.hpp"
#include "main/stage/stageobj.hpp"
#include "main/boss/singyoku.hpp"
#include "main/random.hpp"
#include "music.hpp"
extern bool test_kikuri_music_allowed;
extern int test_kikuri_music_starts;
extern unsigned test_kikuri_music_tick;
extern bool test_kikuri_tick_available;
extern bool test_kikuri_manual_ticks;

namespace {
std::vector<u8> read(const char *path) {
    std::ifstream f(path,std::ios::binary);assert(f.good());
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
void write(const std::vector<u8> &data) {
    std::ofstream f("nitro:/kikuri_art.bin",std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()),data.size());assert(f.good());
}
u16 rgb(const std::vector<u8> &source,const int *pixels,int white,int inversion=0) {
    unsigned components[3]{};
    for(int i=0;i<4;++i) for(int c=0;c<3;++c) {
        const int p=pixels[i];int v=source[p*3+c],inverse=source[(p^2)*3+c];
        if(p==2 || p==6 || p==8 || p==11) v=std::max(v,white);
        if((p^2)==2 || (p^2)==6 || (p^2)==8 || (p^2)==11) inverse=std::max(inverse,white);
        components[c]+=(v*17*(16-inversion)+inverse*17*inversion)/16/4;
    }
    return 0x8000|RGB15(components[0]>>3,components[1]>>3,components[2]>>3);
}
void matches(const std::vector<u8> &art,const std::vector<u8> &source,const KikuriEntranceFrame &frame,const uint16_t *rows) {
    const auto *bgpixels=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
    // Check the upper24 HUD rows too: the background and black shroud are
    // on BG3, which BOTH HUD windows already admit, not the excluded BG2.
    assert((REG_WININ&0x0808)==0x0808 && test_bgs[3].visible);
    for(int y=0;y<192;++y) for(int x=0;x<320;++x) {
        const bool visible=frame.phase!=KikuriEntranceFrame::Barn || (rows[y]&(0x8000>>(x%16)));
        assert(bgpixels[y*512+x]==(visible ? art[128080+y*320+x] : 0));
    }
    for(int y=0;y<192;++y) for(int x=0;x<256;++x) {
        const int hx=camera.left+x;
        const unsigned index=bgpixels[y*512+hx];
        if(!index) {assert(BG_PALETTE[0]==0);continue;}
        int pixels[4];
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
            pixels[dy*2+dx]=source[48+(16+y*2+dy)*640+hx*2+dx];
        }
        assert(BG_PALETTE[index]==(rgb(source,pixels,frame.white,frame.inversion)&0x7fff));
    }
    assert(REG_MASTER_BRIGHT==(frame.brightness ? (1<<14)|frame.brightness : 0));
    for(int i=0;i<256*192;++i) assert(BG_BMP_RAM(16)[i]==0);
}
int ticks,exit_at,flashes;unsigned shroud_cleared;bool was_white;size_t steady_heap,steady_obj;
void observe_flash() {
    if(!shroud_cleared && test_kikuri_music_tick>=880 && test_kikuri_music_tick<909) {
        const auto *bg=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
        bool clear=true;
        for(int y=95;y<=96;++y) for(int x=0;x<320;++x) if(!bg[y*512+x]) clear=false;
        if(clear) shroud_cleared=test_kikuri_music_tick;
    }
    const bool white=REG_MASTER_BRIGHT==((1<<14)|16);
    if(white && !was_white) {
        ++flashes;
        // Includes the one-VBlank read->commit latency, not just controller time.
        assert(test_kikuri_music_tick>=909 && test_kikuri_music_tick<=912);
        assert(shroud_cleared && test_kikuri_music_tick-shroud_cleared>=22 &&
               test_kikuri_music_tick-shroud_cleared<=26); // half-beat gap including commits
    }
    was_white=white;
}
void observe() {
    ++ticks;assert(test_heap_attempts==steady_heap && test_obj_attempts==steady_obj);
    test_kikuri_music_tick+=ticks%5 ? 1 : 2; //72 musical ticks/sec vs60 frames/sec
    test_kikuri_tick_available=ticks<250 || ticks>252; // raced snapshots, then catch-up
    test_keys_down=0;test_keys_held=0;
    if(ticks==exit_at) test_keys_down=KEY_START;
    else if(ticks==850) test_keys_down=KEY_X;
    else if(ticks==852) test_keys_down=KEY_A;
    else if(ticks==854) test_keys_down=KEY_Y;
    else if(ticks==950) test_keys_down=KEY_B;
    if(ticks>880 && ticks<949) test_keys_held=KEY_RIGHT;
}
}
void test_kikuri_art()
{
    const auto art=read("nitro:/kikuri_art.bin"),source=read("kikuri-source.bin"),sprites=read("kikuri-sprites.bin");
    assert(source.size()==256048 && sprites.size()==1216);
    camera.snap_center();canvas_playfield_clip(false);canvas_clear();
    const u16 master=REG_MASTER_BRIGHT,sub_master=REG_MASTER_BRIGHT_SUB;
    REG_MASTER_BRIGHT=(2<<14)|2;REG_MASTER_BRIGHT_SUB=(1<<14)|3;
    const auto heap=test_heap_count,bytes=test_heap_bytes,objects=test_obj_attempts;
    const std::vector<u16> bg(BG_BMP_RAM(4),BG_BMP_RAM(4)+65536);
    const std::vector<u16> palette(BG_PALETTE,BG_PALETTE+256);
    const std::vector<u16> obj(test_obj_vram,test_obj_vram+32768/2);
    auto restored=[&]() {
        assert(!g_canvas_target && test_heap_count==heap && test_heap_bytes==bytes);
        assert(test_obj_attempts==objects);
        assert(std::equal(bg.begin(),bg.end(),BG_BMP_RAM(4)));
        assert(std::equal(palette.begin(),palette.end(),BG_PALETTE));
        assert(std::equal(obj.begin(),obj.end(),test_obj_vram));
        assert(REG_MASTER_BRIGHT==((2<<14)|2) && REG_MASTER_BRIGHT_SUB==((1<<14)|3));
    };
    KikuriView view;
    assert(kikuri_art_asset_valid(art.data(),art.size()));
    for(int mutation=0;mutation<9;++mutation) {
        auto broken=art;
        if(mutation==0) broken[0]^=1;
        if(mutation==1) broken[14]=193;
        if(mutation==2) broken[16]^=1;
        if(mutation==3) broken[32]=16;
        if(mutation==4) broken[128080]=255;
        if(mutation==5) broken.back()=16;
        if(mutation==6) broken.pop_back();
        if(mutation==7) broken.push_back(0);
        if(mutation==8) broken.resize(31);
        assert(!kikuri_art_asset_valid(broken.data(),broken.size()));
        write(broken);assert(!view.begin());restored();
    }
    assert(std::remove("nitro:/kikuri_art.bin")==0);assert(!view.begin());write(art);
    for(int fail=0;fail<2;++fail) {
        test_heap_fail_after=fail;assert(!view.begin());test_heap_fail_after=-1;restored();
    }
    assert(view.begin());const auto allocations=test_heap_attempts;
    assert(test_heap_bytes-bytes==art.size()+256*1024); // no128KiB RGB lookup
    assert(view.begin() && test_heap_attempts==allocations);
    { KikuriView other;assert(!other.begin()); }
    KikuriEntrance timeline;KikuriEntranceFrame frame{};
    uint16_t rows[192]{};int tick=0,y=0,counter=0;
    // Finish the source-mask sweep at885, half a beat before the unchanged
    // tick909 flash. The native diagnostic's19.00 is tick912.
    static_assert(KIKURI_FLASH_TICK==909);
    static_assert(KIKURI_SHROUD_CLEAR_TICK==885);
    for(unsigned musical=0;musical<909;++musical) {
        const int previous_y=y;
        const int until=musical<885 ? musical*360/885 : 360;
        while(counter<until) {
            ++counter;
            if(counter%3==0) {
                auto copy=[&](int off,uint16_t mask) {
                    if(y-off>=0 && y-off<192) rows[y-off]=mask;
                    if(191-y+off>=0 && 191-y+off<192) rows[191-y+off]=mask;
                };
                if(y<96) copy(0,0x2222);
                if(counter*2>8 && y<104) copy(8,0xaaaa);
                if(counter*2>16 && y<112) copy(16,0xeeee);
                if(counter*2>24 && y<=120) copy(24,0xffff);
                ++y;
            }
        }
        assert(timeline.next(frame,musical));++tick;
        assert(frame.phase==KikuriEntranceFrame::Barn && frame.source_frame==counter);
        assert(std::equal(rows,rows+192,timeline.rows()));
        const bool clear=std::all_of(rows,rows+192,[](uint16_t row) {return row==0xffff;});
        assert(clear==(musical>=885)); // not a partial mask or an early final snap
        const size_t uploads=test_dma_copy_bytes;
        camera.left=musical%65;assert(view.entrance(timeline,frame));
        REG_VCOUNT=191;assert(!view.present_entrance());
        REG_VCOUNT=192;assert(view.present_entrance());
        const size_t uploaded=test_dma_copy_bytes-uploads;
        if(!musical) assert(uploaded==61440);
        else if(previous_y==y) assert(uploaded==0); // stalled/unchanged ticks do no bitmap work
        else assert(uploaded<=8*320); // changed rows only, never a full image
        if(musical%37==0 || musical<4 || musical>904) matches(art,source,frame,rows);
    }
    for(int flash=0;flash<=48;++flash) {
        assert(timeline.next(frame,909));++tick;
        assert(frame.source_frame==flash && frame.white==0);
        assert(frame.phase==(flash==48 ? KikuriEntranceFrame::Complete : KikuriEntranceFrame::Flash));
        assert(frame.brightness==(flash<8 ? 16-flash*2 : 0));
        assert(frame.inversion==(flash<16 ? 16 : flash<48 ? 15-(flash-16)/2 : 0));
        assert(std::equal(rows,rows+192,timeline.rows()));
        camera.left=flash%65;const auto uploads=test_dma_copy_bytes;
        assert(view.entrance(timeline,frame) && view.present_entrance());matches(art,source,frame,rows);
        assert(test_dma_copy_bytes==uploads); // flash/recovery are palette/register only
        assert(REG_MASTER_BRIGHT_SUB==((1<<14)|3));
    }
    assert(!timeline.next(frame,909) && tick==958);
    for(unsigned cadence:{1u,2u,7u,48u,909u}) {
        KikuriEntrance varied;KikuriEntranceFrame state{};
        for(unsigned musical=0;musical<909;musical+=cadence) {
            assert(varied.next(state,musical) && state.phase==KikuriEntranceFrame::Barn);
            const auto previous=state.source_frame;
            const std::array<uint16_t,192> snapshot=[&]() {
                std::array<uint16_t,192> value{};std::copy(varied.rows(),varied.rows()+192,value.begin());return value;
            }();
            assert(varied.next(state,musical) && state.source_frame==previous);
            assert(varied.next(state,0) && state.source_frame==previous); // stale clock cannot rewind
            assert(std::equal(snapshot.begin(),snapshot.end(),varied.rows()));
        }
        assert(varied.next(state,909) && state.phase==KikuriEntranceFrame::Flash && state.brightness==16);
    }
    { KikuriEntrance skipped;assert(skipped.next(frame,~0u) && frame.phase==KikuriEntranceFrame::Flash); }
    // Restore the completed frame used by the renderer bounds checks below.
    frame={KikuriEntranceFrame::Complete,48,0,0,0};
    size_t still_uploads=0;
    for(int cam=0;cam<=64;++cam) {
        camera.left=cam;view.prepare_still();assert(view.still());
        if(!cam) still_uploads=test_dma_copy_calls;
        else assert(test_dma_copy_calls==still_uploads); // hardware scroll only
        assert(test_bgs[3].x==cam);
        const auto *bgpixels=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
        for(int y=0;y<192;++y) for(int x=0;x<256;++x) {
            const int sx=(cam+x)*2,sy=16+y*2;
            const int pixels[]={source[48+sy*640+sx],source[48+sy*640+sx+1],
                                source[48+(sy+1)*640+sx],source[48+(sy+1)*640+sx+1]};
            assert(BG_PALETTE[bgpixels[y*512+cam+x]]==(rgb(source,pixels,0)&0x7fff));
        }
    }
    for(int cam:{-1,65}) {camera.left=cam;assert(!view.still() && !view.entrance(timeline,frame));}
    camera.snap_center();
    view.prepare_still(true);assert(view.still(true));
    for(int i=0;i<16;++i) {
        // Original palette entries use direct RGB4->RGB15, unlike averaging.
        int rgb4[3];for(int c=0;c<3;++c) rgb4[c]=(i==2 || i==6 || i==8 || i==11) ? 15 : source[i*3+c];
        assert(BG_PALETTE[i]==RGB15(rgb4[0]*17>>3,rgb4[1]*17>>3,rgb4[2]*17>>3));
    }
    view.prepare_still();assert(view.still());
    assert(!view.sprite(-1,0,0,0) && !view.sprite(0,3,0,0) && !view.sprite(2,4,0,0));
    for(int group=0;group<3;++group) for(int cel=0;cel<(group==2?4:3);++cel) {
        canvas_clear();camera.snap_center();assert(view.sprite(group,cel,288,280));
        int w=group==0?16:8,offset=(group==0?0:group==1?768:960)+cel*w*w;
        for(int y=0;y<w;++y) for(int x=0;x<w;++x) {
            const int p=sprites[offset+y*w+x];const int raw=(p+15)%16;
            const u16 expected=p ? 0x8000|RGB15(source[raw*3]*17>>3,source[raw*3+1]*17>>3,source[raw*3+2]*17>>3) : 0;
            assert(*canvas_at(camera.x(288)+x,Camera::y(280)+y)==expected);
        }
    }
    assert(test_heap_attempts==allocations);view.end();view.end();restored();
    for(int replay=0;replay<8;++replay) {assert(view.begin());view.prepare_still(replay%2);assert(view.still(replay%2));view.end();restored();}
    { KikuriView scoped;assert(scoped.begin());scoped.prepare_still();assert(scoped.still()); }
    restored();
    const auto player=player_get_state();const auto orb=orb_get_state();
    const auto boss=singyoku_state();const auto timer=stage_timer;const auto score=stage_score;
    initialize_irand(123);const int random=irand();initialize_irand(123);
    const int starts=test_kikuri_music_starts;
    test_kikuri_manual_ticks=true;
    { // One production replay; exhaustive mask/tick/cel oracles are above.
        ticks=flashes=0;shroud_cleared=0;was_white=false;exit_at=40;camera.snap_center();
        test_kikuri_tick_available=true;
        steady_heap=test_heap_attempts+2;steady_obj=test_obj_attempts;
        assert(view.begin());music_stop();assert(music_play_track(NATIVE_KAMI));
        test_keys_down=test_keys_held=0;test_vblank_hook=observe;test_bg_update_hook=observe_flash;
        assert(kikuri_entrance_play(view));
        test_vblank_hook=nullptr;test_bg_update_hook=nullptr;test_keys_down=test_keys_held=0;
        view.end();
        assert(flashes==1 && ticks>750 && camera.left==32); // no dev-key cancellation/replay
        assert(std::equal(palette.begin(),palette.begin()+208,BG_PALETTE));
        assert(std::equal(bg.begin(),bg.end(),BG_BMP_RAM(4)));
        assert(test_heap_count==heap && test_heap_bytes==bytes && test_obj_attempts==objects);
        assert(!std::memcmp(&player,&player_get_state(),sizeof(player)));
        assert(!std::memcmp(&orb,&orb_get_state(),sizeof(orb)));
        assert(!std::memcmp(&boss,&singyoku_state(),sizeof(boss)));
        assert(stage_timer==timer && stage_score==score && !g_canvas_target);
        assert(REG_MASTER_BRIGHT==((2<<14)|2) && REG_MASTER_BRIGHT_SUB==((1<<14)|3));
    }
    assert(irand()==random);camera.snap_center();
    test_kikuri_manual_ticks=false;
    assert(test_kikuri_music_starts==starts+1 && !g_canvas_target);
    assert(test_heap_count==heap && test_heap_bytes==bytes && test_obj_attempts==objects);
    REG_MASTER_BRIGHT=master;REG_MASTER_BRIGHT_SUB=sub_master;
    std::puts("Kikuri art: shroud clear885/unchanged909 flash, VBlank/whole-pixel masks/upper HUD, stale/missing/skipped ticks, sparse uploads, unchanged49-frame recovery/all cels, production entrance ignores dev keys, frozen gameplay/RNG and complete restoration passed");
}
