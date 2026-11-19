// Included by test_viewport.cpp to inspect native sub-screen pixels/writes.
#include "yuugenmagan_combat_render.hpp"

static void test_yuugenmagan_radar()
{
    const auto old_player=player;const auto old_orb=orb_get_state();const auto old_items=Items;
    const int cards=stage_card_total,obstacles=stage_obstacle_total;
    stage_card_total=stage_obstacle_total=0;Items.init();
    player.x_position=304;player.y_position=368;orb_set_position(48,368);
    hud_invalidate();hud_render(3,10);
    const auto art=read_bytes("radar-icons.bin");constexpr int stride=260,w=160,h=84;
    const auto *vram=reinterpret_cast<const u8*>(BG_BMP_RAM_SUB(0));
    const std::vector<u8> outside(vram,vram+256*192);std::vector<u8> expected(w*h);
    const u8 colors[]={SUB_COL_RADAR_PLAYER,SUB_COL_RADAR_ORB,0,0,0,0,0,0,0,SUB_COL_RADAR_BOSS};
    auto draw=[&](int wx,int wy,int icon) {
        const int iw=art[icon*stride],ih=art[icon*stride+1];
        const int left=std::clamp(wx,0,639)*159/639-iw/2,top=(std::clamp(wy,64,399)-64)*83/335-ih/2;
        for(int y=0;y<ih;++y) for(int x=0;x<iw;++x)
            if(left+x>=0 && left+x<w && top+y>=0 && top+y<h && art[icon*stride+4+y*iw+x])
                expected[(top+y)*w+left+x]=colors[icon];
    };
    YuugenMaganPhase1 boss;boss.reset();auto s=boss.state();
    const int xs[]={96,544,224,416,320},ys[]={150,150,198,198,97};
    const auto heap=test_heap_attempts,objects=test_obj_attempts;
    sub_calls.reserve(256);
    for(unsigned mask=0;mask<32;++mask) {
        std::fill(expected.begin(),expected.end(),SUB_COL_RADAR_BG);
        for(int i=0;i<5;++i) {
            s.eyes[i].orb_active=mask&(1u<<i);if(s.eyes[i].orb_active) draw(xs[i],ys[i],9);
            s.visual.image[i]=0;s.visual.shown[i]=YMG_AHEAD; // marker follows hitbox, not either cel
        }
        draw(player.x_position+16,player.y_position+16,0);draw(64,384,1);
        yuugenmagan_radar_render(s);
        for(int y=0;y<192;++y) for(int x=0;x<256;++x) {
            const bool inside=x>=88 && x<248 && y>=19 && y<103;
            assert(vram[y*256+x]==(inside ? expected[(y-19)*w+x-88] : outside[y*256+x]));
        }
        sub_calls.clear();s.health.invincible=true;s.flash=true;camera.left=64;
        const auto frozen=s;yuugenmagan_radar_render(s);
        assert(sub_calls.empty() && !std::memcmp(&frozen,&s,sizeof(s)));
    }
    assert(hud_radar_boss(s.eyes[4]).y==86 && (97-64)*83/335-(86-64)*83/335==3);
    const int north_left=88+320*159/639-8;
    for(int y=0;y<16;++y) for(int x=0;x<16;++x)
        assert(sub_index(north_left+x,19+y)==(art[9*stride+4+y*16+x] ? SUB_COL_RADAR_BOSS : SUB_COL_RADAR_BG));
    hud_radar_render();sub_calls.clear();hud_radar_render();assert(sub_calls.empty());
    assert(test_heap_attempts==heap && test_obj_attempts==objects);
    player=old_player;orb_set_position(old_orb.x_position,old_orb.y_position);Items=old_items;
    stage_card_total=cards;stage_obstacle_total=obstacles;camera.snap_center();hud_radar_render();hud_invalidate();
    std::puts("YuugenMagan radar:32 independent hitbox masks, unchanged collider/3px north-only nudge/full glyph, cel/flash/camera independence, idle zero-write and cleanup budgets passed");
}
