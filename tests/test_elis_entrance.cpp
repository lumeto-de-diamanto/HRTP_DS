#include <nds.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iterator>
#include <vector>
#include "elis_render.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "heap_probe.hpp"
#include "boss_render.hpp"
#include "background.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/timer.hpp"
#include "main/random.hpp"

void test_elis_stage15();
void test_elis_phase1();
void test_elis_phase3();
void test_elis_phase5();
void test_elis_defeat();
namespace {
unsigned tick_for(int frame) {return (frame*6+4)/5;}
std::vector<u8> bytes(const char *name)
{
    std::ifstream f(name,std::ios::binary);assert(f.good());return {std::istreambuf_iterator<char>(f),{}};
}
void timing()
{
    std::array<int,120> starts{};
    for(int i=1;i<120;++i) starts[i]=starts[i-1]+(120-i)/12+1;
    assert(starts.back()==659);
    auto point=[](int tick,int i) {
        const unsigned char a=(tick-1)*24+i*128;
        return ElisSphere{304+((96*Cos8(a))>>8),114+((96*Sin8(a))>>8),3};
    };
    ElisEntrance clock;
    for(int frame=0;frame<=799;++frame) {
        const auto f=clock.at(tick_for(frame));
        const int k=int(std::upper_bound(starts.begin(),starts.end(),frame)-starts.begin());
        const int wave=frame<659 ? 0 : frame-658,moves=std::min(wave/4,8);
        assert(f.source_frame==frame && f.rotation_tick==k && f.wave_frame==wave && f.complete==(frame==799));
        const int girl=wave<24 ? 0 : wave<100 ? 1 : wave>=128 && wave<130 ? 3 : 2;
        assert(f.girl==girl);
        if(girl==1) assert(f.wave_put_frame==wave/4*4);
        for(int i=0;i<2;++i) for(int trail=0;trail<3;++trail) {
            ElisSphere expected{};
            if(k>trail) {
                expected=point(k-trail,i);expected.cel=3-trail;
                if(moves) {
                    const int previous=moves-trail;
                    expected=point(120+std::min(0,previous),i);expected.cel=3-trail;
                    expected.left+=(i ? -64 : 64)*std::max(0,previous);
                }
            }
            const auto &actual=f.spheres[i][trail];
            assert(actual.left==expected.left && actual.top==expected.top && actual.cel==expected.cel);
        }
        const auto held=clock.at(tick_for(frame));assert(held.revision==f.revision);
        assert(clock.at(frame ? tick_for(frame)-1 : 0).source_frame==frame); // regressed snapshots
    }
    assert(clock.at(~0u).source_frame==799);
    ElisEntrance cues;
    assert(cues.at(816).girl==ElisEntranceFrame::Hidden);
    assert(cues.at(819).wave_put_frame==24);
    assert(cues.at(910).girl==ElisEntranceFrame::Still);
    assert(cues.at(944).girl==ElisEntranceFrame::Ripple);
    assert(cues.at(947).girl==ElisEntranceFrame::Still && cues.at(959).complete);
}
u16 rgb(const std::vector<u8> &source,int index)
{
    return 0x8000|RGB15(source[index*3]*17/8,source[index*3+1]*17/8,source[index*3+2]*17/8);
}
void pixels(const std::vector<u8> &source,const ElisEntranceFrame &f,int cam)
{
    static u16 expected[256*192];std::fill(std::begin(expected),std::end(expected),0);
    if(f.girl) for(int y=0;y<48;++y) {
        int dx=0;
        if(f.girl!=ElisEntranceFrame::Still) {
            const int len=f.girl==ElisEntranceFrame::Ripple ? 3 : f.wave_put_frame+2;
            const int amp=f.girl==ElisEntranceFrame::Ripple ? 8 : 100-f.wave_put_frame;
            const int phase=f.girl==ElisEntranceFrame::Ripple ? 64 : f.wave_put_frame*6;
            dx=amp*Sin8((phase+2*y*(256/len))&255)/256;
        }
        for(int x=0;x<64;++x) {
            const int px=(256+dx)/2-cam+x,py=40+y,index=source[48+y*64+x];
            if(index && px>=0 && px<256) expected[py*256+px]=rgb(source,index-1);
        }
    }
    for(const auto &trail:f.spheres) for(const auto &s:trail) if(s.cel) {
        const int bx=int(std::floor(s.left/8.0))*4-cam,by=s.top/2-8;
        for(int y=0;y<16;++y) for(int x=0;x<16;++x)
            if(bx+x>=0 && bx+x<256 && by+y>=0 && by+y<192 && source[48+3072+(s.cel-1)*256+y*16+x])
                expected[(by+y)*256+bx+x]=rgb(source,8);
    }
    assert(std::equal(std::begin(expected),std::end(expected),g_canvas_target));
}
void renderer()
{
    const auto source=bytes("elis-expected.bin"),art=bytes("nitro:/elis_art.bin");
    const auto blocks=test_heap_count,allocated=test_heap_bytes,objects=test_obj_attempts;
    const std::vector<u16> actors(SPRITE_PALETTE,SPRITE_PALETTE+256);
    assert(elis_art_asset_valid(art.data(),art.size()) && !elis_art_asset_valid(nullptr,art.size()));
    auto broken=art;broken.back()=16;assert(!elis_art_asset_valid(broken.data(),broken.size()));
    assert(!elis_art_asset_valid(art.data(),art.size()-1));
    for(int deny:{0,1}) {
        ElisView view;test_heap_fail_after=deny;assert(!view.begin());test_heap_fail_after=-1;
        assert(!g_canvas_target && test_heap_count==blocks && test_heap_bytes==allocated);
    }
    for(int cam:{0,64}) {
        camera.left=cam;video_stage_fade_out();ElisView view;assert(view.begin());
        const auto heap=test_heap_attempts;
        {ElisView other;assert(!other.begin());}
        CanvasForegroundScope foreground;ElisEntrance clock;
        for(int frame:{0,10,200,658,659,680,682,683,690,698,758,786,788,799}) {
            const auto &f=clock.at(tick_for(frame));REG_VCOUNT=80;
            const auto writes=test_oam_changes;assert(view.compose(f));assert(!view.present() && test_oam_changes==writes);
            pixels(source,f,cam);REG_VCOUNT=192;assert(view.present());
            assert(std::equal(g_canvas_target,g_canvas_target+256*192,BG_BMP_RAM(16)));
            const auto dma=test_dma_copy_bytes;assert(view.compose(f) && view.present());assert(test_dma_copy_bytes==dma);
            if(frame==683) { // pan during a held wave: use put24, not clock25
                camera.left=32;assert(view.compose(f));pixels(source,f,32);assert(view.present());
                camera.left=cam;assert(view.compose(f));pixels(source,f,cam);assert(view.present());
            }
            assert(std::equal(actors.begin(),actors.end(),SPRITE_PALETTE));
            assert(test_heap_attempts==heap && test_obj_attempts==objects);
        }
        video_stage_fade_out();view.end();view.end();video_stage_reveal();
        assert(!g_canvas_target && test_heap_count==blocks && test_heap_bytes==allocated);
    }
    camera.snap_center();load_boss_background();boss_render_hide();boss_render_frame();
}
}
void test_elis_entrance()
{
    const int score=stage_score;const auto timer=stage_timer;const auto player=player_get_state();const auto orb=orb_get_state();
    initialize_irand(937);const int random=irand();initialize_irand(937);
    timing();renderer();assert(irand()==random && stage_score==score && stage_timer==timer);
    assert(!std::memcmp(&player,&player_get_state(),sizeof(player)) && !std::memcmp(&orb,&orb_get_state(),sizeof(orb)));
    test_elis_stage15();
    test_elis_phase1();
    test_elis_phase3();
    test_elis_phase5();
    test_elis_defeat();
    std::puts("Elis entrance:799-frame source geometry/trails/waves, KAMI2 tick cues/stale-clock catch-up, native source pixels/dither spheres/camera edges, zero held DMA/OBJ/palette churn and allocation cleanup passed");
}
