// Actual split-OBJ renderer and interactive art review; no fake combat loop.
#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "mima_render.hpp"
#include "main/boss/mima_intro.hpp"
#include "main/boss/singyoku.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/stage/timer.hpp"
#include "main/stage/stageobj.hpp"
#include "main/random.hpp"
#include "boss_gfx.hpp"
#include "boss_render.hpp"
#include "konngara_combat_render.hpp"
#include "background.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "heap_probe.hpp"
#include "sprites/BossArrow.h"

namespace {
std::vector<u8> art;
int ticks,exit_tick;
size_t steady_heap,steady_objects;
unsigned le16(const u8 *p) { return p[0]|(p[1]<<8); }
void write_art(const std::vector<u8> &bytes) {
    std::ofstream out("nitro:/mima_art.bin",std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()),bytes.size());assert(out.good());
}
void matches(int frame) {
    assert(!std::memcmp(boss_gfx(),art.data()+48+frame*3072,3072));
    assert(!std::memcmp(boss_gfx()+1536,BossArrow_tiles,64));
}
void observe() {
    ++ticks;assert(ticks<=exit_tick);
    assert(test_obj_count==79 && test_obj_used==27840 && test_obj_attempts==steady_objects);
    assert(test_heap_attempts==steady_heap);
    test_keys_down=test_keys_held=0;
    if(ticks>1 && ticks<=102) {
        const int source=(ticks-2)/2+1;
        const int frame=source<=10 ? 0 : source<=50 ? source-10 : 41;
        matches(frame);
        assert(test_objs[BOSS_OAM_SLOT].hidden==(frame==0));
        assert(test_objs[OAM_SLOT_BOSS_ARROW].hidden);
    }
    if(ticks>=103 && ticks<=135) matches(43+((ticks-103)/8)%4);
    if(ticks>=135 && ticks<=140) test_keys_down=KEY_A;
    if(ticks>=136 && ticks<=141) matches(41+(ticks-135)%6);
    if(ticks==141) test_keys_down=KEY_B;
    if(ticks==142) matches(0);
    if(ticks>=242 && ticks<=249) test_keys_held=KEY_LEFT|KEY_L;
    if(ticks>=250 && ticks<=257) test_keys_held=KEY_RIGHT|KEY_R;
    if(ticks==250) assert(camera.left==24 && test_objs[40].x==72);
    if(ticks==258) assert(camera.left==32 && test_objs[40].x==96);
    if(ticks==exit_tick) test_keys_down=KEY_START;
}
}

void test_mima_review()
{
    std::ifstream input("nitro:/mima_art.bin",std::ios::binary);
    art={std::istreambuf_iterator<char>(input),{}};
    assert(art.size()==144432 && mima_art_asset_valid(art.data(),art.size()));
    assert(!mima_art_asset_valid(nullptr,art.size()));
    assert(!mima_art_asset_valid(art.data()+1,art.size()));
    for(size_t size:{0u,15u,16u,47u,48u,144431u,144433u}) assert(!mima_art_asset_valid(art.data(),size));
    const auto objects=test_obj_attempts,heap=test_heap_count,bytes=test_heap_bytes;
    const std::vector<u16> resident(std::begin(test_obj_vram),std::end(test_obj_vram));
    const std::vector<u16> palette(std::begin(SPRITE_PALETTE),std::end(SPRITE_PALETTE));
    const auto changes=test_oam_changes;
    MimaView view;assert(!view.frame(41));
    test_heap_fail_after=0;assert(!view.begin());test_heap_fail_after=-1;
    assert(test_heap_count==heap && test_heap_bytes==bytes && test_oam_changes==changes);
    // Corruption, short reads, trailing bytes and missing files never publish
    // OAM/palette/VRAM or retain the bounded asset allocation.
    for(int mutation=0;mutation<7;++mutation) {
        auto broken=art;
        if(mutation==0) broken[0]='?';
        if(mutation==1) broken[12]=0xff;
        if(mutation==2) broken[4]=63;
        if(mutation==3) broken[19]|=0x80;
        if(mutation==4) broken.resize(art.size()-1);
        if(mutation==5) broken.push_back(0);
        if(mutation==6) broken.resize(15);
        write_art(broken);assert(!view.begin());
        assert(test_heap_count==heap && test_heap_bytes==bytes && test_oam_changes==changes);
        assert(std::equal(resident.begin(),resident.end(),test_obj_vram));
        assert(std::equal(palette.begin(),palette.end(),SPRITE_PALETTE));
    }
    assert(std::remove("nitro:/mima_art.bin")==0);assert(!view.begin());write_art(art);
    for(int replay=0;replay<20;++replay) {
        assert(view.begin());const auto attempts=test_heap_attempts;
        assert(view.begin() && test_heap_attempts==attempts);
        assert(!view.frame(-1) && !view.frame(47));
        MimaEntrance timeline;MimaEntranceFrame frame{};
        for(int tick=0;tick<101;++tick) {
            assert(timeline.next(frame));
            const int source=tick/2+1;
            assert(frame.source_frame==source && frame.complete==(tick==100));
            assert(frame.asset_frame==(source<=10 ? 0 : source<=50 ? source-10 : 41));
            assert(view.frame(frame.asset_frame));matches(frame.asset_frame);
        }
        assert(!timeline.next(frame));
        for(int cel=41;cel<47;++cel) {
            assert(view.frame(cel));matches(cel);
            for(int i=0;i<16;++i) assert(SPRITE_PALETTE[48+i]==le16(art.data()+16+i*2));
            assert(test_objs[40].size==SpriteSize_64x64 && test_objs[41].size==SpriteSize_64x32);
            assert(test_objs[40].x==camera.x(256) && test_objs[40].y==52);
            assert(test_objs[41].y==116 && test_objs[41].gfx==boss_gfx()+1024);
        }
        for(int cam=0;cam<=64;++cam) for(int left=-129;left<=641;++left) {
            camera.left=cam;assert(view.frame(41,left));
            const int x=half_floor(left)-cam;
            const int side=x+64<=0 ? -1 : x>=256 ? 1 : 0;
            const auto &arrow=test_objs[OAM_SLOT_BOSS_ARROW];
            assert(arrow.hidden==!side);
            if(side) assert(arrow.hflip==(side<0) && arrow.gfx==boss_gfx()+1536);
        }
        // Hop/spread-in cels are real boss visuals too. Previously only the
        // full still/meteor frames could show the marker, hiding it mid-hop.
        for(int cam:{0,32,64}) for(int left:{-128,-127,0,128,384,512,640})
            for(int cel=0;cel<47;++cel) for(bool hidden:{false,true}) {
                camera.left=cam;assert(view.frame(cel,left,96,hidden));
                const int x=half_floor(left)-cam;
                const int side=x+64<=0 ? -1 : x>=256 ? 1 : 0;
                const auto &arrow=test_objs[OAM_SLOT_BOSS_ARROW];
                assert(arrow.hidden==(hidden || cel==0 || !side));
                if(!arrow.hidden) {
                    assert(arrow.hflip==(side<0) && arrow.gfx==boss_gfx()+1536);
                    assert(arrow.palette==OBJ_PAL_OVERLAY && arrow.priority==0);
                    assert(arrow.y==Camera::y(176)-7);
                }
            }
        assert(test_heap_attempts==attempts && test_obj_attempts==objects);
        view.end();view.end();
        assert(test_objs[40].hidden && test_objs[41].hidden && test_objs[17].hidden);
        assert(test_heap_count==heap && test_heap_bytes==bytes);
        camera.snap_center();
    }
    // Prove writes stayed inside the shared allocation, including the marker.
    for(size_t i=0;i<resident.size();++i) if(test_obj_vram+i<boss_gfx() || test_obj_vram+i>=boss_gfx()+2048)
        assert(resident[i]==test_obj_vram[i]);
    // Both existing bosses must take the full cache back cleanly.
    load_boss_background();boss_render_hide();singyoku_init();boss_render_frame();
    assert(!std::memcmp(boss_gfx()+1024,BossArrow_tiles,64));
    boss_render_hide();
    { KonngaraCombatView konngara;assert(konngara.begin());konngara.end(); }
    assert(view.begin() && view.frame(41));matches(41);view.end();
    const auto player=player_get_state();
    const auto orb=orb_get_state();
    const auto boss=singyoku_state();const auto timer=stage_timer;const auto score=stage_score;
    initialize_irand(531);const int random=irand();initialize_irand(531);
    // Entry allocates art + a temporary background only, then is allocation-free.
    steady_objects=test_obj_attempts;
    for(int replay=0;replay<5;++replay) {
        steady_heap=test_heap_attempts+2;ticks=0;exit_tick=replay==4 ? 180 : 260;
        test_keys_down=test_keys_held=0;test_vblank_hook=observe;
        assert(mima_art_review(3,2,timer) && ticks==exit_tick);
        test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
        assert(test_heap_count==heap && test_heap_bytes==bytes && test_obj_attempts==steady_objects);
        assert(!std::memcmp(&player,&player_get_state(),sizeof(player)));
        assert(!std::memcmp(&orb,&orb_get_state(),sizeof(orb)));
        assert(!std::memcmp(&boss,&singyoku_state(),sizeof(boss)));
        assert(stage_timer==timer && stage_score==score);
        assert(test_objs[40].hidden && test_objs[41].hidden && test_objs[17].hidden);
    }
    assert(irand()==random);
    camera.snap_center();load_boss_background();boss_render_hide();boss_render_frame();
    assert(test_obj_count==79 && test_obj_used==27840);
    std::puts("Mima art: all47 split-OBJ frames/palette, source100-frame spread-in plus final commit, all camera edges, malformed/missing assets and denied allocation, 20 cache lifecycles, five interactive replays/pose/meteor/early exit, frozen gameplay/RNG and SinGyoku/Konngara handoffs passed");
}
