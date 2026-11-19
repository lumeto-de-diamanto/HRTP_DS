#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>
#include "yuugenmagan_combat_render.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "boss_gfx.hpp"
#include "boss_render.hpp"
#include "background.hpp"
#include "heap_probe.hpp"
#include "sprites/Missile.h"
#include "main/math/trig.hpp"

namespace {
u16 expected[256*192];
std::vector<u8> bytes(const char *path)
{
    std::ifstream f(path,std::ios::binary);assert(f.good());return {std::istreambuf_iterator<char>(f),{}};
}
void pixel(int x,int y,int cam,int dots=1)
{
    for(int d=0;d<dots;++d) {
        if(x+d<0 || x+d>=640 || y<0 || y>=400) continue;
        const int px=(x+d)/2-cam,py=y/2-8;
        if(px>=0 && px<256 && py>=24 && py<192) expected[py*256+px]=0xffff;
    }
}
// Independent closed-form major-axis samples, including the source's omitted
// final byte/row group. Crop emitted pixels, not the pentagram's endpoints.
void edge(YuugenMaganPoint a,YuugenMaganPoint b,int cam,int dots=1)
{
    if(a.x==b.x) {for(int y=std::min(a.y,b.y);y<=std::max(a.y,b.y);++y) pixel(a.x,y,cam,dots);return;}
    if(a.y==b.y) {for(int x=std::min(a.x,b.x);x<=std::max(a.x,b.x);++x) pixel(x,a.y,cam);return;}
    if(a.x>b.x) std::swap(a,b);
    const int dx=b.x-a.x,dy=std::abs(b.y-a.y),major=std::max(dx,dy),minor=std::min(dx,dy),dir=b.y>a.y ? 1 : -1;
    std::vector<std::pair<int,int>> points;
    for(int i=0;i<=major;++i) {
        const int n=(i*minor+(major-1)/2)/major;
        points.emplace_back(a.x+(dx>dy ? i : n),a.y+dir*(dx>dy ? n : i));
    }
    const auto last=points.back();int flushed=int(points.size());
    auto byte=[](int x) {return x/8-(x<0 && x%8!=0);};
    while(flushed && byte(points[flushed-1].first)==byte(last.first) && points[flushed-1].second==last.second) --flushed;
    for(int i=0;i<flushed;++i) pixel(points[i].first,points[i].second,cam,dots);
}
void beam(int endpoint,int cam)
{
    // Northern lasers alone retain integer-before-float endpoint clipping.
    if(endpoint==316) {edge({316,92},{316,399},cam,3);return;}
    if(endpoint<316) edge({endpoint+(endpoint==0),399},{316,92},cam,3);
    else edge({316,92},{endpoint>=624 ? (endpoint==640 ? 638 : endpoint-1) : endpoint,399},cam,3);
}
u16 color(const std::vector<u8> &art,int index,const YuugenMaganPhase1State &s)
{
    int c[3];
    for(int i=0;i<3;++i) {
        const int raw=index==15 ? s.visual.color15[i] : art[32+index*3+i];
        c[i]=(s.flash && (index==1 || index==11) ? 15 : std::clamp(raw,0,15))*17/8;
    }
    return RGB15(c[0],c[1],c[2]);
}
void palette(const std::vector<u8> &art,const YuugenMaganPhase1State &s,bool bomb)
{
    auto component=[&](int index,int c) {
        const int raw=index==15 ? s.visual.color15[c] : art[32+index*3+c];
        return (s.flash && (index==1 || index==11) ? 15 : std::clamp(raw,0,15))*17;
    };
    const unsigned count=art[14]|(art[15]<<8);
    for(unsigned i=0;i<count;++i) {
        const unsigned signature=art[61520+i*2]|(art[61521+i*2]<<8);int sum[3]{};
        for(int p=0;p<4;++p) for(int c=0;c<3;++c) sum[c]+=component((signature>>(p*4))&15,c)/4;
        const u16 normal=RGB15(sum[0]/8,sum[1]/8,sum[2]/8);
        assert(BG_PALETTE[16+i]==(bomb ? video_bomb_indigo(normal) : normal));
    }
    for(int i=0;i<16;++i) {
        const u16 normal=color(art,i,s);assert(BG_PALETTE[i]==(bomb ? video_bomb_indigo(normal) : normal));
        if(i<15) assert(SPRITE_PALETTE[49+i]==normal);
    }
}
void publish(YuugenMaganCombatView &view,const YuugenMaganPhase1State &s,const MissilePool *pool=nullptr)
{
    const auto before=s;canvas_begin_frame();canvas_playfield_clip(true);assert(view.compose(s,pool));
    const auto mismatch=std::mismatch(std::begin(expected),std::end(expected),g_canvas_target);
    if(mismatch.first!=std::end(expected)) {
        const int offset=int(mismatch.first-std::begin(expected));
        std::fprintf(stderr,"GPU mismatch (%d,%d): %04x != %04x, cam%d lasers%u star%d missile%d\n",
            offset%256,offset/256,*mismatch.first,*mismatch.second,camera.left,s.laser_count,s.pentagram.visible,pool ? pool->at(0).cel : -1);
    }
    assert(std::equal(std::begin(expected),std::end(expected),g_canvas_target));
    canvas_buffer_prepare();REG_VCOUNT=192;assert(view.present());
    assert(!std::memcmp(&before,&s,sizeof(s)));
    assert(std::equal(std::begin(expected),std::end(expected),BG_BMP_RAM(16)));
}
}

void test_yuugenmagan_combat()
{
    const auto art=bytes("nitro:/yuugenmagan_art.bin"),eyes=bytes("yuugenmagan-eyes.bin");
    const auto blocks=test_heap_count,allocated=test_heap_bytes,objects=test_obj_attempts;
    const std::vector<u16> actors(SPRITE_PALETTE,SPRITE_PALETTE+48);
    YuugenMaganCombatView view;assert(!view.compose({}) && !view.present());
    camera.snap_center();
    for(int deny:{0,1}) {
        test_heap_fail_after=deny;assert(!view.begin());test_heap_fail_after=-1;
        assert(!g_canvas_target && test_heap_count==blocks && test_heap_bytes==allocated);
    }
    video_stage_fade_out();assert(view.prepare_entrance() && !g_canvas_target);assert(view.begin());
    const auto heap=test_heap_attempts;const std::vector<u16> cache(boss_gfx(),boss_gfx()+2048);
    {YuugenMaganCombatView other;assert(!other.begin() && !other.prepare_entrance());}
    YuugenMaganPhase1 boss;boss.reset();auto s=boss.state();s.visual.color15_override=true;
    for(auto &cel:s.visual.shown) cel=YMG_DOWN;
    for(int red:{-67,-4,0,63,126}) for(bool flash:{false,true}) for(bool bomb:{false,true}) {
        video_bomb_reset();s.visual.color15[0]=red;s.visual.color15[1]=0;s.visual.color15[2]=68;s.flash=flash;
        if(bomb) video_bomb_prepare(true,60,100);
        canvas_begin_frame();const std::vector<u16> old(BG_PALETTE,BG_PALETTE+256);const auto changes=test_oam_changes;
        REG_VCOUNT=80;assert(view.compose(s));canvas_buffer_prepare();assert(!view.present());
        assert(test_oam_changes==changes && std::equal(old.begin(),old.end(),BG_PALETTE));
        REG_VCOUNT=192;assert(view.present());palette(art,s,bomb);
        assert(std::equal(actors.begin(),actors.end(),SPRITE_PALETTE));
    }
    video_bomb_reset();s.flash=false;MissilePool pool;
    for(int cam:{0,64}) for(int cel:{0,8,17}) for(int left:{-15,219,625}) {
        camera.left=cam;auto &m=const_cast<Missile&>(pool.at(0));
        m.flag=1;m.visible=true;m.cel=cel;m.draw_x=left;m.draw_y=left==625 ? 397 : 204;
        std::fill(std::begin(expected),std::end(expected),0);
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
            const int px=half_floor(left)-cam+x,py=m.draw_y/2-8+y,index=Missile_pixels[cel][y*8+x];
            if(index && px>=0 && px<256 && py>=24 && py<192) expected[py*256+px]=0x8000|color(art,index-1,s);
        }
        for(int i=0;i<5;++i) for(int y=0;y<24;++y) for(int x=0;x<32;++x) {
            const int px=YUUGENMAGAN_LEFT[i]/2-cam+x,py=YUUGENMAGAN_TOP[i]/2-8+y;
            if(px>=0 && px<256 && eyes[YMG_DOWN*768+y*32+x]) expected[py*256+px]=0;
        }
        publish(view,s,&pool); // opaque eyes cover early missile pixels
    }
    for(int cam:{0,32,64}) for(int endpoint:{0,16,288,316,320,624,640}) for(unsigned count:{1u,2u}) {
        camera.left=cam;s.laser_count=count;s.laser_targets[0]=endpoint;s.laser_targets[1]=640-endpoint;
        std::fill(std::begin(expected),std::end(expected),0);beam(endpoint,cam);if(count==2) beam(640-endpoint,cam);
        publish(view,s);
    }
    s.laser_count=0;s.pentagram.visible=true;
    for(int cam:{0,32,64}) for(int cx:{0,316,608}) for(int cy:{64,189,425}) for(int radius:{64,115}) for(int angle:{2,51,192}) {
        camera.left=cam;YuugenMaganPoint corners[5];
        for(int i=0;i<5;++i) {const unsigned char a=angle+i*51;corners[i]={cx+((radius*Cos8(a))>>8),cy+((radius*Sin8(a))>>8)};}
        const int path[]={0,2,4,1,3};for(int i=0;i<5;++i) s.pentagram.ink[i]=corners[path[i]];
        std::fill(std::begin(expected),std::end(expected),0);for(int i=0;i<5;++i) edge(s.pentagram.ink[i],s.pentagram.ink[(i+1)%5],cam);
        publish(view,s);
    }
    // The approved clipping fix: extending one straight edge below the screen
    // must not rotate its visible crop or lose its bottom row.
    for(int cam:{0,64}) {
        camera.left=cam;std::vector<u16> first;
        for(int bottom:{500,540}) {
            const YuugenMaganPoint a{120,100},b{120+(bottom-100)/4,bottom};
            s.pentagram.ink[0]=s.pentagram.ink[4]=a;for(int i=1;i<4;++i) s.pentagram.ink[i]=b;
            std::fill(std::begin(expected),std::end(expected),0);edge(a,b,cam);publish(view,s);
            if(first.empty()) first.assign(g_canvas_target,g_canvas_target+256*192);
            else assert(std::equal(first.begin(),first.end(),g_canvas_target));
            assert(std::count(g_canvas_target+191*256,g_canvas_target+192*256,u16(0xffff))>0);
        }
    }
    auto bad=s;bad.pentagram.ink[0].x=-1025;assert(!view.compose(bad));
    bad=s;bad.visual.color15[0]=128;assert(!view.compose(bad));
    bad=s;bad.laser_count=3;assert(!view.compose(bad));
    s.pentagram.visible=false;std::fill(std::begin(expected),std::end(expected),0);publish(view,s);
    assert(std::equal(cache.begin(),cache.end(),boss_gfx()) && std::equal(actors.begin(),actors.end(),SPRITE_PALETTE));
    assert(test_heap_attempts==heap && test_obj_attempts==objects);
    video_stage_fade_out();view.end();view.end();video_stage_reveal();
    assert(!g_canvas_target && test_heap_count==blocks && test_heap_bytes==allocated);
    camera.snap_center();load_boss_background();boss_render_hide();boss_render_frame();
    std::puts("YuugenMagan GPU: source-mixture flash/wrapped palettes and bombs, early-VBlank/lease guards, missile-eye layering, northern endpoint clipping vs straight pentagram crops, dirty cleanup and flat heap/OBJ/cache budgets passed");
}
