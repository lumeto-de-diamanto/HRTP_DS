#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "yuugenmagan_render.hpp"
#include "boss_gfx.hpp"
#include "boss_render.hpp"
#include "background.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "video.hpp"
#include "heap_probe.hpp"

namespace {
std::vector<u8> bytes(const char *name)
{
    std::ifstream f(name,std::ios::binary);assert(f.good());
    return {std::istreambuf_iterator<char>(f),{}};
}
void write_art(const std::vector<u8> &data)
{
    std::ofstream f("nitro:/yuugenmagan_art.bin",std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()),data.size());assert(f.good());
}
int eye_cel(int eye,int frame)
{
    const int first=eye<2 ? 100 : eye<4 ? 120 : 140;
    int cel=frame<first ? 0 : frame<first+20 ? 1 : frame<first+40 ? 2 : frame<first+60 ? 6 : 3;
    if(frame>=240) cel=frame<260 ? 2 : frame<280 ? 1 : 0;
    if(eye<2 && frame>=300) cel=frame<320 ? 1 : frame<331 ? 2 : eye==0 ? 4 : 3;
    return cel;
}
void background_pixels(const std::vector<u8> &source,int step)
{
    const auto *bitmap=static_cast<const u8*>(bgGetGfxPtr(g_bg_id_background));
    for(int y=0;y<192;++y) for(int x=0;x<320;++x) {
        int sum[3]{};
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
            const int color=source[48+(16+y*2+dy)*640+x*2+dx];
            for(int c=0;c<3;++c) sum[c]+=(source[color*3+c]+(color==15 && c<2 ? step : 0))*17/4;
        }
        assert(BG_PALETTE[bitmap[y*512+x]]==RGB15(sum[0]/8,sum[1]/8,sum[2]/8));
    }
}
}

void test_yuugenmagan()
{
    const auto art=bytes("nitro:/yuugenmagan_art.bin"),source=bytes("yuugenmagan-source.bin"),eyes=bytes("yuugenmagan-eyes.bin");
    assert(yuugenmagan_art_asset_valid(art.data(),art.size()));
    assert(!yuugenmagan_art_asset_valid(nullptr,art.size()));
    for(size_t size:{size_t(0),size_t(31),size_t(80),art.size()-1,art.size()+1})
        assert(!yuugenmagan_art_asset_valid(art.data(),size));
    YuugenMaganOpening opening;
    for(int tick=1;tick<=331;++tick) {
        assert(opening.next());const auto &s=opening.state();
        assert(s.frame==tick && s.recolor==tick/40 && s.complete==(tick==331));
        for(int i=0;i<5;++i) {
            assert(s.image[i]==eye_cel(i,tick));
            assert(s.shown[i]==eye_cel(i,((tick-1)/4)*4));
        }
        const unsigned mask=(tick>160 && tick<300 ? 3u : 0u) | (tick>180 && tick<320 ? 12u : 0u) |
            (tick>200 && tick<331 ? 16u : 0u);
        assert(s.lasers==mask);
    }
    assert(!opening.next());
    const auto heap=test_heap_count,allocated=test_heap_bytes,objects=test_obj_attempts;
    const std::vector<u16> resident(std::begin(test_obj_vram),std::end(test_obj_vram));
    const std::vector<u16> palette(SPRITE_PALETTE,SPRITE_PALETTE+256);
    YuugenMaganView view;assert(!view.frame({}));
    test_heap_fail_after=0;assert(!view.begin());test_heap_fail_after=-1;
    for(int mutation=0;mutation<8;++mutation) {
        auto broken=art;
        if(mutation==0) broken[0]='?';
        if(mutation==1) broken[14]=193;
        if(mutation==2) broken[32]=16;
        if(mutation==3) broken[80]=15;
        if(mutation==4) broken.resize(art.size()-1);
        if(mutation==5) broken.push_back(0);
        if(mutation==6) broken[16]=79;
        if(mutation==7) broken.back()=1;
        write_art(broken);assert(!view.begin());
        assert(test_heap_count==heap && test_heap_bytes==allocated);
        assert(std::equal(resident.begin(),resident.end(),test_obj_vram));
    }
    assert(std::remove("nitro:/yuugenmagan_art.bin")==0);assert(!view.begin());write_art(art);
    for(int replay=0;replay<2;++replay) {
        camera.snap_center();assert(view.begin());const auto attempts=test_heap_attempts;
        assert(view.begin() && test_heap_attempts==attempts);
        YuugenMaganOpeningFrame frame;
        for(int step:{0,4,8}) {frame.recolor=step;assert(view.frame(frame));background_pixels(source,step);}
        for(int cel=0;cel<7;++cel) {
            const auto *gfx=reinterpret_cast<const u8*>(boss_gfx())+cel*512;
            for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
                const int address=((y/8)*4+x/8)*32+(y%8)*4+(x%8)/2;
                assert(((gfx[address]>>((x%2)*4))&15)==(y<24 ? eyes[cel*768+y*32+x] : 0));
            }
            for(auto &shown:frame.shown) shown=cel;
            for(int cam:{0,32,64}) {
                camera.left=cam;assert(view.frame(frame));assert(test_bgs[3].x==cam);
                for(int i=0;i<5;++i) {
                    const auto &obj=test_objs[40+i];const int x=YUUGENMAGAN_LEFT[i]/2-cam;
                    assert(obj.x==x && obj.y==YUUGENMAGAN_TOP[i]/2-8 && obj.size==SpriteSize_32x32 && obj.palette==3);
                    assert(obj.gfx==boss_gfx()+cel*256 && obj.hidden==(cel==0 || x+32<=0 || x>=256));
                }
            }
        }
        assert(test_heap_attempts==attempts && test_obj_attempts==objects);
        view.end();view.end();
        assert(std::equal(palette.begin(),palette.end(),SPRITE_PALETTE));
        assert(test_heap_count==heap && test_heap_bytes==allocated);
        for(int i=40;i<45;++i) assert(test_objs[i].hidden);
    }
    camera.snap_center();load_boss_background();boss_render_hide();boss_render_frame();
    std::puts("YuugenMagan art: source opening/cel latency, pixels/palettes, camera edges, invalid/missing/denied assets and allocation-free resident-cache cleanup passed");
}
