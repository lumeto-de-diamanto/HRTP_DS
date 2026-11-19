// Actual bitmap renderer pixel oracles; normal lifecycle is tested in Stage15.
#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "kikuri_render.hpp"
#include "heap_probe.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "boss_render.hpp"
#include "sound_effects.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/bullet/pellet.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/item.hpp"
#include "main/stage/timer.hpp"
#include "main/boss/singyoku.hpp"

namespace {
u16 actor_palette[16]{};
std::vector<u8> read(const char *path) {
    std::ifstream f(path,std::ios::binary);assert(f.good());
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
int component(const std::vector<u8> &source,int index,int channel,bool flash,int step) {
    int value=source[index*3+channel];
    if(index==5) {if(channel==0) value+=step;else if(channel==2) value-=step;}
    value=std::clamp(value,0,15);
    if(flash && (index==2 || index==6 || index==8 || index==11)) value=15;
    return value*17;
}
int obj_pixel(const TestObj &obj,int x,int y) {
    const auto *bytes=reinterpret_cast<const u8*>(obj.gfx);
    const int address=((y/8)*2+x/8)*32+(y%8)*4+(x%8)/2;
    return (bytes[address]>>((x%2)*4))&15;
}
std::vector<u16> composed_souls() {
    std::vector<u16> pixels(256*192,0);
    // Low OAM index wins; draw slot41 first, then40, then the BG2 layer.
    for(int slot=BOSS_OAM_SLOT+1;slot>=BOSS_OAM_SLOT;--slot) {
        const auto &obj=test_objs[slot];if(obj.hidden) continue;
        assert(obj.size==SpriteSize_16x16 && obj.format==SpriteColorFormat_16Color);
        assert(obj.palette==KIKURI_SOUL_PALETTE && obj.priority==OAM_PRIORITY_GAMEPLAY);
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
            const int px=obj.x+x,py=obj.y+y,ink=obj_pixel(obj,x,y);
            if(px>=0 && px<256 && py>=0 && py<192 && ink)
                pixels[py*256+px]=0x8000|SPRITE_PALETTE[obj.palette*16+ink];
        }
    }
    for(int i=0;i<256*192;++i) if(BG_BMP_RAM(16)[i]) pixels[i]=BG_BMP_RAM(16)[i];
    return pixels;
}
void match(const std::vector<u8> &source,const KikuriPhase1State &state,bool indigo=false) {
    const auto *bg=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
    for(int y=0;y<192;++y) for(int x=0;x<256;++x) {
        unsigned rgb[3]{};
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
            const int p=source[48+(16+y*2+dy)*640+(camera.left+x)*2+dx];
            for(int c=0;c<3;++c) rgb[c]+=component(source,p,c,state.flash,state.lightball_step)/4;
        }
        const u16 expected=RGB15(rgb[0]>>3,rgb[1]>>3,rgb[2]>>3);
        assert(BG_PALETTE[bg[y*512+camera.left+x]]==(indigo ? video_bomb_indigo(expected) : expected));
    }
    assert(std::equal(actor_palette,actor_palette+16,SPRITE_PALETTE));
}
void ripple_oracle(std::vector<u8> &world,const std::vector<u8> &sprites,int left,int age) {
    auto stamp=[&](int dx,int phase) {
        const int x=left+dx,offset=phase==0 || phase==10 ? 0 : phase<=5 ? (phase-1)*8 : (9-phase)*8;
        const int top=16-offset/2,from=(x&~15)/2,to=((x+31)&~15)/2;
        for(int y=top;y<top+8;++y) for(int px=from;px<to;++px) world[y*320+px]=0;
        const int cel=phase==0 || (phase>=5 && phase<=9) ? 0 : phase<5 ? 1 : 3;
        for(int y=0;y<8;++y) for(int px=0;px<8;++px) {
            const int p=sprites[960+cel*64+y*8+px];
            if(p) world[(top+y)*320+(x&~7)/2+px]=p;
        }
    };
    auto center=[&](int p) {stamp(0,p);};
    auto inner=[&](int p) {stamp(-16,p);stamp(16,p);};
    auto outer=[&](int p) {stamp(-32,p);stamp(32,p);};
    if(age<5) center(0);
    else if(age<10) center(1);
    else if(age<15) {center(2);inner(0);}
    else if(age<20) {center(3);inner(1);}
    else if(age<25) {center(4);inner(2);outer(0);}
    else if(age<30) {center(5);inner(3);outer(1);}
    else if(age<35) {center(6);inner(4);outer(2);}
    else if(age<40) {center(7);inner(5);outer(3);}
    else if(age<45) {center(8);inner(6);outer(4);}
    else if(age<50) {center(9);inner(7);outer(5);}
    else if(age<55) {center(10);inner(8);outer(6);}
    else if(age<60) {inner(9);outer(7);}
    else if(age<65) {inner(10);outer(8);}
    else if(age<70) outer(9);
    else if(age<75) outer(10);
}
void effects_pixels(KikuriView &view,const std::vector<u8> &source,const std::vector<u8> &sprites) {
    KikuriPhase1State s{};s.lightball_step=5;
    const auto allocations=test_heap_attempts,objects=test_obj_attempts;
    int generation=0;
    for(int top:{64,205,393}) for(int left:{32,301,576}) for(int cam:{0,32,64}) {
        s.tears[0]={left,top,1,1,++generation,true};camera.left=cam;
        std::vector<u16> expected(256*192,0);
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
            const int px=(left/8)*4-cam+x,py=top/2-8+y,p=sprites[768+y*8+x],raw=(p+15)&15;
            if(p && px>=0 && px<256 && py>=24 && py<192)
                expected[py*256+px]=0x8000|RGB15(component(source,raw,0,false,5)>>3,
                    component(source,raw,1,false,5)>>3,component(source,raw,2,false,5)>>3);
        }
        view.prepare_combat(s);canvas_begin_frame();canvas_playfield_clip(true);view.draw_combat(s);
        assert(std::equal(expected.begin(),expected.end(),g_canvas_target));
        canvas_playfield_clip(false);canvas_buffer_prepare();assert(view.present_combat());
    }
    for(int left:{32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
                  288,289,290,291,292,293,294,295,296,297,298,299,300,301,302,303,576}) {
        std::vector<u8> world(320*24,0);std::vector<u16> expected(256*192,0);
        const int slot=left%10;++generation;
        for(auto &tear:s.tears) tear={};
        for(int age=2;age<=74;++age) {
            ripple_oracle(world,sprites,left,age);
            s.tears[slot]={left,392,age,1,generation,true};
            s.flash=age%3==0;camera.left=(age*7)%65;
            std::fill(expected.begin(),expected.end(),0);
            for(int y=0;y<24;++y) for(int x=0;x<256;++x) {
                const int p=world[y*320+x+camera.left],raw=(p+15)&15;
                if(p) expected[(168+y)*256+x]=0x8000|RGB15(component(source,raw,0,s.flash,5)>>3,
                    component(source,raw,1,s.flash,5)>>3,component(source,raw,2,s.flash,5)>>3);
            }
            const auto frozen=s;
            for(int repeat=0;repeat<(age==25 ? 3 : 1);++repeat) {
                view.prepare_combat(s);canvas_begin_frame();canvas_playfield_clip(true);view.draw_combat(s);
                assert(std::equal(expected.begin(),expected.end(),g_canvas_target));
                canvas_playfield_clip(false);canvas_buffer_prepare();assert(view.present_combat());
                assert(std::equal(expected.begin(),expected.end(),BG_BMP_RAM(16)));
                assert(!std::memcmp(&frozen,&s,sizeof(s)));
            }
        }
    }
    for(auto &tear:s.tears) tear={};
    std::vector<u8> combined(320*24,0);
    for(int i=0;i<10;++i) {
        const int left=40+i*56,age=2+i*7;
        s.tears[i]={left,392,age,0,++generation,true};std::vector<u8> one(320*24,0);
        for(int f=2;f<=age;++f) ripple_oracle(one,sprites,left,f);
        for(size_t p=0;p<one.size();++p) if(one[p]) combined[p]=one[p];
    }
    for(int cam:{0,32,64}) {
        camera.left=cam;s.flash=false;std::vector<u16> expected(256*192,0);
        for(int y=0;y<24;++y) for(int x=0;x<256;++x) {
            const int p=combined[y*320+x+cam],raw=(p+15)&15;
            if(p) expected[(168+y)*256+x]=0x8000|RGB15(component(source,raw,0,false,5)>>3,
                component(source,raw,1,false,5)>>3,component(source,raw,2,false,5)>>3);
        }
        view.prepare_combat(s);canvas_begin_frame();canvas_playfield_clip(true);view.draw_combat(s);
        assert(std::equal(expected.begin(),expected.end(),g_canvas_target));
        canvas_playfield_clip(false);canvas_buffer_prepare();assert(view.present_combat());
    }
    for(auto &tear:s.tears) tear={};
    // Real eight-pixel-wide source lasers: independent byte/Y grouped oracle,
    // including the final group that the source intentionally never flushes.
    for(int width:{4,8}) for(int target:{-60,240,364,668}) for(int cam:{0,32,64}) {
        s.lasers[0]={};assert(shootout_laser_spawn(s.lasers[0],307,147,target,400,50,20,width));
        for(int f=0;f<40;++f) shootout_laser_update(s.lasers[0]);
        std::vector<u16> expected(256*192,0);const auto &l=s.lasers[0];
        int x=l.start_x,y=l.start_y,previous_x=x>>8,previous_y=y>>8;uint16_t dots=0;
        camera.left=cam;s.flash=false;
        for(int i=0;i<l.length;++i) {
            if((previous_x>>3)!=(x>>11) || previous_y!=(y>>8)) {
                for(int bit=0;bit<16;++bit) if(dots&(0x8000>>bit)) {
                    const int px=((previous_x&~7)+bit)/2-cam,py=previous_y/2-8;
                    if(px>=0 && px<256 && py>=24 && py<192)
                        expected[py*256+px]=0x8000|RGB15(component(source,10,0,false,5)>>3,
                            component(source,10,1,false,5)>>3,component(source,10,2,false,5)>>3);
                }
                previous_x=x>>8;previous_y=y>>8;dots=0;
                if(previous_x<0 || previous_x>638 || previous_y<64 || previous_y>399) break;
            }
            dots|=uint16_t(((0xffffu<<(16-width))&0xffffu)>>((x>>8)&7));x+=l.step_x;y+=l.step_y;
        }
        view.prepare_combat(s);canvas_begin_frame();canvas_playfield_clip(true);view.draw_combat(s);
        assert(std::equal(expected.begin(),expected.end(),g_canvas_target));
        canvas_playfield_clip(false);canvas_buffer_prepare();assert(view.present_combat());
        assert(std::equal(expected.begin(),expected.end(),BG_BMP_RAM(16)));
    }
    s.lasers[0]={};view.prepare_combat(s);canvas_begin_frame();view.draw_combat(s);
    canvas_buffer_prepare();assert(view.present_combat());
    assert(std::all_of(BG_BMP_RAM(16),BG_BMP_RAM(16)+256*192,[](u16 p) {return !p;}));
    assert(test_heap_attempts==allocations && test_obj_attempts==objects);
    std::puts("Kikuri effects: falling tear cel and ripple ages2..74/all16 X remainders/arena/camera edges, persistent high-cel fill/word-unput stripes, palette flashes, repeated frozen draws/ten simultaneous scratch caches/reuse, independent crossed8px laser ink and complete dirty cleanup with no allocations passed");
}
}

void test_kikuri_render()
{
    singyoku_init();boss_render_frame(); // prime the shared owner's cache key
    const std::vector<u16> singyoku_tiles(boss_gfx(),boss_gfx()+1024);
    const auto source=read("kikuri-source.bin"),sprites=read("kikuri-sprites.bin");
    const auto heap=test_heap_count,bytes=test_heap_bytes,objects=test_obj_attempts;
    const std::vector<u16> bg(BG_BMP_RAM(4),BG_BMP_RAM(4)+65536),palette(BG_PALETTE,BG_PALETTE+256);
    const std::vector<u16> obj_palette(SPRITE_PALETTE,SPRITE_PALETTE+256);
    std::copy(SPRITE_PALETTE,SPRITE_PALETTE+16,actor_palette);
    assert(actor_palette[3]==RGB15(31,0,0)); // Reimu's resident red bow, not Kikuri brown
    KikuriView view;assert(view.begin());KikuriPhase1State state{};state.health.reset(14);
    size_t copies=0;
    for(int step=0;step<=5;++step) for(bool flash:{false,true}) for(int cam:{0,32,64}) {
        camera.left=cam;state.lightball_step=step;state.flash=flash;
        view.prepare_combat(state);canvas_begin_frame();canvas_buffer_prepare();
        REG_VCOUNT=191;assert(!view.present_combat());REG_VCOUNT=192;assert(view.present_combat());
        match(source,state);assert(test_bgs[3].x==cam);
        if(!copies) copies=test_dma_copy_bytes;
        else assert(test_dma_copy_bytes==copies); // every camera/flash/fade is palette/register only
    }
    video_bomb_prepare(true,50,100);view.prepare_combat(state);canvas_begin_frame();canvas_buffer_prepare();assert(view.present_combat());
    assert(REG_MASTER_BRIGHT==((1<<14)|16));
    video_bomb_prepare(false,-1,200);state.flash=false;state.lightball_step=2;
    view.prepare_combat(state);canvas_begin_frame();canvas_buffer_prepare();assert(view.present_combat());match(source,state,true);
    video_bomb_reset();
    const auto allocations=test_heap_attempts;
    for(int cel=0;cel<3;++cel) for(bool flash:{false,true}) for(int cam:{0,32,64})
    for(int top:{60,94,205,393}) {
        state.lightball_step=5;state.flash=flash;camera.left=cam;
        state.souls[0]={cam*2-4,top,2,cel,0,true};
        state.souls[1]={cam*2+500,top,1,(cel+1)%3,1,true};
        view.prepare_combat(state);canvas_begin_frame();canvas_playfield_clip(true);view.draw_combat(state);
        std::vector<u16> expected(256*192,0);
        for(const auto &soul:state.souls) {
            const int left=(soul.left/8)*4-cam,y0=soul.top/2-8;
            for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
                const int dx=left+x,dy=y0+y,p=sprites[soul.shown_image*256+y*16+x],raw=(p+15)&15;
                if(!p || dx<0 || dx>=256 || dy<24 || dy>=192) continue;
                expected[dy*256+dx]=0x8000|RGB15(component(source,raw,0,flash,5)>>3,
                    component(source,raw,1,flash,5)>>3,component(source,raw,2,flash,5)>>3);
            }
        }
        assert(std::all_of(g_canvas_target,g_canvas_target+256*192,[](u16 p){return !p;}));
        canvas_playfield_clip(false);canvas_buffer_prepare();assert(view.present_combat());
        assert(composed_souls()==expected);match(source,state);
        for(int i=0;i<2;++i) {
            const auto &obj=test_objs[BOSS_OAM_SLOT+1-i];const auto &soul=state.souls[i];
            assert(obj.x==(soul.left/8)*4-cam && obj.y==top/2-8);
            assert(obj.hidden==!view_visible(obj.x,obj.y,16,16));
        }
    }
    // All normal cels stay resident: animation/camera/flash must not rewrite
    // their384 bytes, allocate, touch actor palettes or draw souls on BG2.
    const std::vector<u16> tiles(boss_gfx(),boss_gfx()+192);
    for(int cel=0;cel<3;++cel) {
        state.souls[0]={112,94,0,cel,0,true};state.souls[1]=state.souls[0];
        state.souls[1].shown_image=(cel+1)%3;
        camera.left=32;state.flash=cel%2;state.lightball_step=5;
        view.prepare_combat(state);canvas_begin_frame();canvas_playfield_clip(true);
        // Earlier tear/sparkle-like ink must remain behind the souls, but
        // later lasers/pellets must still cover them.
        canvas_fill_rect(24,39,16,16,RGB15(0,31,0));view.draw_combat(state);
        canvas_hline(24,44,16,RGB15(0,0,31));canvas_playfield_clip(false);canvas_buffer_prepare();
        REG_VCOUNT=191;assert(!view.present_combat());
        assert(std::equal(tiles.begin(),tiles.end(),boss_gfx()));
        REG_VCOUNT=192;assert(view.present_combat());
        const auto composed=composed_souls();
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
            int p=sprites[cel*256+y*16+x];const int upper=sprites[((cel+1)%3)*256+y*16+x];
            if(upper) p=upper;
            const int raw=(p+15)&15;
            const u16 ink=y==5 ? RGB15(0,0,31) : p ? RGB15(component(source,raw,0,state.flash,5)>>3,
                component(source,raw,1,state.flash,5)>>3,component(source,raw,2,state.flash,5)>>3) : RGB15(0,31,0);
            assert(composed[(39+y)*256+24+x]==(0x8000|ink));
        }
        assert(test_objs[0].priority==2 && test_objs[10].priority==2 && test_objs[24].priority==2);
        assert(std::equal(tiles.begin(),tiles.end(),boss_gfx()));
    }
    // Bomb inversion uses the exact software-cel RGB transform in bank3 only.
    video_bomb_prepare(true,58,200);view.prepare_combat(state);canvas_begin_frame();view.draw_combat(state);
    canvas_buffer_prepare();assert(view.present_combat());
    for(int i=1;i<16;++i) {
        const int raw=(i+15)&15;const u16 normal=RGB15(component(source,raw,0,state.flash,5)>>3,
            component(source,raw,1,state.flash,5)>>3,component(source,raw,2,state.flash,5)>>3);
        assert(SPRITE_PALETTE[KIKURI_SOUL_PALETTE*16+i]==video_bomb_indigo(normal));
    }
    assert(std::equal(actor_palette,actor_palette+16,SPRITE_PALETTE));video_bomb_reset();
    state.souls[0].visible=state.souls[1].visible=false;
    view.prepare_combat(state);canvas_begin_frame();view.draw_combat(state);canvas_buffer_prepare();assert(view.present_combat());
    assert(std::all_of(BG_BMP_RAM(16),BG_BMP_RAM(16)+256*192,[](u16 p) {return p==0;}));
    assert(test_objs[40].hidden && test_objs[41].hidden && test_objs[0].priority==OAM_PRIORITY_GAMEPLAY);
    assert(test_heap_attempts==allocations);effects_pixels(view,source,sprites);view.end();
    assert(test_heap_count==heap && test_heap_bytes==bytes && test_obj_attempts==objects && !g_canvas_target);
    assert(std::equal(bg.begin(),bg.end(),BG_BMP_RAM(4)) && std::equal(palette.begin(),palette.end(),BG_PALETTE));
    assert(std::equal(obj_palette.begin(),obj_palette.end(),SPRITE_PALETTE));
    assert(test_objs[40].hidden && test_objs[41].hidden);
    boss_render_frame(); // same original SinGyoku frame must be re-uploaded
    assert(std::equal(singyoku_tiles.begin(),singyoku_tiles.end(),boss_gfx()));boss_render_hide();
    camera.snap_center();
    std::puts("Kikuri renderer: hardware16x16/4bpp souls/all cels/clip/camera/overlap, unchanged tear/laser layering and actor palettes, resident384-byte animation/no new OBJ allocations, flash/bomb palette3, cache handoff and retained tear/ripple/laser pixels passed");
}
