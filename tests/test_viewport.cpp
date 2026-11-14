// Actual renderers + original-sized engine, with host VRAM/OAM/window models.
#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "heap_probe.hpp"
#include "camera.hpp"
#include "canvas.hpp"
#include "video.hpp"
#include "tiles.hpp"
#include "stage_render.hpp"
#include "background.hpp"
#include "upper_hud.hpp"
#include "orb_render.hpp"
#include "player_render.hpp"
#include "shot_render.hpp"
#include "item_render.hpp"
#include "pellet_render.hpp"
#include "popup.hpp"
#include "boss_render.hpp"
#include "text_layer.hpp"
#include "hud_render.hpp"
#include "subscreen.hpp"
#include "palette_fx.hpp"
#include "menu_render.hpp"
#include "main/boss/singyoku.hpp"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include "main/player/shot.hpp"
#include "main/stage/stageobj.hpp"
#include "main/stage/item.hpp"
#include "main/stage/timer.hpp"
#include "main/bullet/pellet.hpp"
#include "main/random.hpp"
#include "stagedat/stagedat0.h"
#include "font16x16.hpp"
#include "sprites/HudIcons.h"
#include "fonts/font8x16.h"

std::vector<int> test_effects;
void mdrv2_se_play(int id) { test_effects.push_back(id); }
void test_stage_entrances();
void test_konngara_review();
void test_konngara_stage20();
void test_stage_bonus_awards();
void test_player_death();
void test_bombs();
extern PlayerState player;

// Exercise the actual sub-screen VRAM backend while counting its drawing seam:
// unchanged frames must issue NO writes, not just rewrite equal bytes.
namespace sub_backend {
#include "../source/platform/subscreen.cpp"
}
struct SubCall { char type; int x, y, w, h; };
static std::vector<SubCall> sub_calls;
std::string test_practice_label;
void subscreen_init() { sub_backend::subscreen_init(); }
void subscreen_clear(u8 c) { sub_calls.push_back({'C',0,0,256,192});sub_backend::subscreen_clear(c); }
void subscreen_fill_rect(int x, int y, int w, int h, u8 c) { sub_calls.push_back({'F',x,y,w,h});sub_backend::subscreen_fill_rect(x,y,w,h,c); }
void subscreen_text(int x, int y, const char *s, u8 color) {
    sub_calls.push_back({'T',x,y,0,16});
    if (x == 8 && y == 140) test_practice_label = s;
    sub_backend::subscreen_text(x,y,s,color);
}
void subscreen_number(int x, int y, int value, int digits, u8 c) { sub_calls.push_back({'N',x,y,digits*8,16});sub_backend::subscreen_number(x,y,value,digits,c); }
void subscreen_fullwidth_number(int x, int y, int value, int digits, u8 c) { sub_calls.push_back({'W',x,y,digits*16,16});sub_backend::subscreen_fullwidth_number(x,y,value,digits,c); }
void subscreen_fullwidth_text(int x,int y,const char *s,u8 c) {
    sub_calls.push_back({'L',x,y,int(std::strlen(s))*16,16});sub_backend::subscreen_fullwidth_text(x,y,s,c);
}
void subscreen_put_indexed(int x,int y,int w,int h,const u8 *pixels,int pal) {
    sub_calls.push_back({'I',x,y,w,h});sub_backend::subscreen_put_indexed(x,y,w,h,pixels,pal);
}

static std::vector<u8> read_bytes(const char *path) {
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static void check_background(const std::vector<u8> &source) {
    const auto *out = static_cast<const u8 *>(bgGetGfxPtr(g_bg_id_background));
    for (int y = 0; y < VIEW_H; ++y)
        for (int x = 0; x < VIEW_W; ++x)
            assert(out[y * 512 + test_bgs[3].x + x] == source[y * BACKGROUND_W + test_bgs[3].x + x]);
}

static int obj_pixel(int slot, int x, int y) {
    const auto &obj = test_objs[slot];
    const int widths[] = {8,16,32,32,64,8,32,64,8,16,16,32};
    const int width = widths[obj.size];
    const int word = ((y / 8) * (width / 8) + x / 8) * 16 + (y % 8) * 2 + (x % 8) / 4;
    return (obj.gfx[word] >> ((x % 4) * 4)) & 15;
}

static u8 sub_index(int x,int y) {
    return reinterpret_cast<const u8*>(BG_BMP_RAM_SUB(0))[y*256+x];
}

static void check_sub_text(int x,int y,const char *s,u8 color) {
    for(;*s;++s,x+=8) for(int gy=0;gy<16;++gy) for(int gx=0;gx<8;++gx)
        assert(sub_index(x+gx,y+gy)==((font8x16[*s-0x20][gy]&(0x80>>gx)) ? color : SUB_COL_BG));
}

static void check_sub_wide(int x,int y,const char *s,u8 color) {
    for(;*s;++s,x+=16) {
        const auto *glyph=font16x16_find(*s=='?' ? 0x2129 : 0x2300+*s);
        assert(glyph);
        for(int gy=0;gy<16;++gy) for(int gx=0;gx<16;++gx)
            assert(sub_index(x+gx,y+gy)==((glyph[gy]&(0x8000>>gx)) ? color : SUB_COL_BG));
    }
}

static void check_bottom_hud() {
    assert(HUD_RADAR_LEFT==88 && HUD_RADAR_TOP==19 && HUD_RADAR_W==160 && HUD_RADAR_H==84);
    assert(test_bgs[6].type==BgType_Bmp8 && test_bgs[6].size==BgSize_B8_256x256 && test_bgs[6].map==0);
    assert(test_bgs[7].type==BgType_Bmp8 && test_bgs[7].size==BgSize_B8_128x128 && test_bgs[7].map==4);
    assert(test_bgs[6].priority==0 && test_bgs[7].priority==3 && test_bgs[7].wrap);
    assert(BG_PALETTE_SUB[SUB_COL_DIM]==RGB15(31,0,0) && BG_PALETTE_SUB[SUB_COL_FG]==RGB15(31,31,31));
    assert(BG_PALETTE_SUB[4]==RGB15(1,1,1) && BG_PALETTE_SUB[5]==RGB15(3,2,2));
    const auto *pattern=reinterpret_cast<const u8*>(BG_BMP_RAM_SUB(4));
    assert(std::equal(pattern,pattern+128*128,sub_backend::HudPattern_pixels));
    const auto *vram=reinterpret_cast<const u8*>(BG_BMP_RAM_SUB(0));
    const std::vector<u8> tail(vram+256*192,vram+128*1024);
    const auto old_player=player;const auto old_orb=orb_get_state();const auto old_items=Items;const auto old_pellets=Pellets;
    const auto old_card_total=stage_card_total,old_obstacle_total=stage_obstacle_total;
    const std::vector<StageCard> old_cards(stage_cards,stage_cards+STAGE_MAX_CELLS);
    const std::vector<StageObstacle> old_obstacles(stage_obstacles,stage_obstacles+STAGE_MAX_CELLS);
    const int old_score=stage_score,old_hi=hiscore,old_combo=stage_cardcombo_cur,old_max=stage_cardcombo_max;
    const auto old_rank=rank;
    stage_score=1234567;hiscore=7654321;stage_cardcombo_cur=123;stage_cardcombo_max=456;rank=RANK_LUNATIC;
    hud_invalidate();hud_render(3,20);
    for(const auto &label:std::vector<std::pair<int,const char*>>{{16,"SCORE"},{56,"HI SCORE"},{96,"COMBO"},{136,"MAX COMBO"}})
        check_sub_text(8,label.first,label.second,SUB_COL_DIM);
    check_sub_wide(104,112,"STAGE",SUB_COL_DIM);check_sub_wide(104,144,"RANK",SUB_COL_DIM);
    check_sub_text(24,32,"1234567",SUB_COL_FG);check_sub_text(24,72,"7654321",SUB_COL_FG);
    check_sub_text(56,112,"123",SUB_COL_FG);check_sub_text(56,152,"456",SUB_COL_FG);
    check_sub_wide(200,128,"20",SUB_COL_FG);check_sub_wide(136,160,"LUNATIC",SUB_COL_FG);
    stage_score=0;hud_render(3,20);check_sub_text(24,32,"0000000",SUB_COL_FG);
    rank=RANK_EASY;hud_render(3,20);check_sub_wide(136,160,"EASY",SUB_COL_FG);
    for(int y=160;y<176;++y) for(int x=200;x<248;++x) assert(sub_index(x,y)==0);

    // Odd/even paired writes, transparency and clipping must not touch the
    // neighbour pixel, hidden overlay rows, resident tile or remaining VRAM C.
    subscreen_fill_rect(0,0,256,192,13);
    subscreen_fill_rect(-3,-2,6,4,14);subscreen_fill_rect(255,191,8,8,15);
    assert(sub_index(2,1)==14 && sub_index(3,1)==13 && sub_index(255,191)==15 && sub_index(254,191)==13);
    const u8 pixels[]={1,2,0,3,4,5};
    subscreen_put_indexed(-1,190,3,2,pixels,10);
    assert(sub_index(0,190)==12 && sub_index(1,190)==13 && sub_index(0,191)==14 && sub_index(1,191)==15);
    subscreen_put_indexed(254,191,3,2,pixels,10);
    assert(sub_index(254,191)==11 && sub_index(255,191)==12);
    subscreen_text(-4,-8,"EDGE",SUB_COL_FG);subscreen_text(252,188,"EDGE",SUB_COL_FG);
    subscreen_fullwidth_text(-4,-8,"EDGE",SUB_COL_FG);subscreen_fullwidth_text(252,188,"EDGE",SUB_COL_FG);
    assert(std::equal(tail.begin(),tail.end(),vram+256*192));

    hud_invalidate();hud_render(3,20);
    const std::vector<u8> outside(vram,vram+256*192);
    stage_card_total=stage_obstacle_total=0;Items.init();
    player.x_position=48;player.y_position=344;orb_set_position(544,104);
    HudRadarBoss boss{320,160,true};
    constexpr int w=HUD_RADAR_W,h=HUD_RADAR_H;
    std::vector<u8> expected(w*h);
    auto center_x=[](int x) { return std::max(0,std::min(639,x))*159/639; };
    auto center_y=[](int y) { return (std::max(64,std::min(399,y))-64)*83/335; };
    auto draw=[&](int wx,int wy,int mw,int mh,u8 color,auto ink) {
        const int left=center_x(wx)-mw/2,top=center_y(wy)-mh/2;
        for(int y=0;y<mh;++y) for(int x=0;x<mw;++x)
            if(left+x>=0 && left+x<w && top+y>=0 && top+y<h && ink(x,y)) expected[(top+y)*w+left+x]=color;
    };
    auto rebuild=[&] {
        std::fill(expected.begin(),expected.end(),SUB_COL_RADAR_BG);
        if(stage_card_total && stage_cards[0].flag!=CARD_REMOVED)
            draw(stage_cards[0].left+16,stage_cards[0].top+16,5,3,SUB_COL_RADAR_CARD,[](int x,int y){return x==0||x==4||y==0||y==2;});
        for(int i=0;i<stage_obstacle_total;++i) {
            const auto &o=stage_obstacles[i];
            if(o.type>=OT_BAR_TOP && o.type<=OT_BAR_RIGHT)
                draw(o.left+16,o.top+16,8,8,SUB_COL_RADAR_OBSTACLE,[&](int x,int y){
                    return o.type==OT_BAR_TOP ? y<2 : o.type==OT_BAR_BOTTOM ? y>=6 : o.type==OT_BAR_LEFT ? x<2 : x>=6;
                });
            else if(o.type==OT_PORTAL) draw(o.left+16,o.top+16,5,5,SUB_COL_RADAR_PORTAL,[](int x,int y){return abs(x-2)+abs(y-2)==2;});
            else if(o.type==OT_NONE) continue;
            else draw(o.left+16,o.top+16,3,3,SUB_COL_RADAR_OBSTACLE,[](int x,int y){return x==1||y==1;});
        }
        if(CItems::is_visible(Items.bomb[0])) draw(Items.bomb[0].left+16,Items.bomb[0].top+16,3,3,SUB_COL_RADAR_ITEM,[](int,int){return true;});
        if(CItems::is_visible(Items.point[0])) draw(Items.point[0].left+16,Items.point[0].top+16,3,3,SUB_COL_RADAR_ITEM,[](int x,int y){return abs(x-1)+abs(y-1)<=1;});
        if(boss.visible) draw(boss.x,boss.y,5,3,SUB_COL_RADAR_BOSS,[](int x,int y){return x==0||x==4||y==0||y==2;});
        draw(player.x_position+16,player.y_position+16,3,3,SUB_COL_RADAR_PLAYER,[](int x,int y){return abs(x-1)<=y/2;});
        const auto &orb=orb_get_state();
        draw(orb.x_position+16,orb.y_position+16,3,3,SUB_COL_FG,[](int x,int y){return (x-1)*(x-1)+(y-1)*(y-1)<=1;});
    };
    auto verify=[&] {
        rebuild();
        for(int y=0;y<192;++y) for(int x=0;x<256;++x) {
            const bool inside=x>=HUD_RADAR_LEFT && x<HUD_RADAR_LEFT+w && y>=HUD_RADAR_TOP && y<HUD_RADAR_TOP+h;
            assert(sub_index(x,y)==(inside ? expected[(y-HUD_RADAR_TOP)*w+x-HUD_RADAR_LEFT] : outside[y*256+x]));
        }
        assert(std::equal(tail.begin(),tail.end(),vram+256*192));
    };
    sub_calls.reserve(1024);sub_calls.clear();hud_radar_render(boss);verify();
    assert(sub_calls.size()==20*11); // one initial opaque raster, no camera frame
    const auto copies=test_dma_copy_calls,fills=test_dma_fill_bytes;
    sub_calls.clear();
    initialize_irand(4567);const int next_random=irand();initialize_irand(4567);
    const auto attempts=test_heap_attempts;
    for(int frame=0;frame<120;++frame) {camera.left=frame%65;hud_radar_render(boss);}
    assert(sub_calls.empty() && irand()==next_random && test_heap_attempts==attempts);
    assert(test_dma_copy_calls==copies && test_dma_fill_bytes==fills);

    // Populate scenery/items, then overlap every priority at the card centre.
    stage_card_total=1;stage_cards[0]={112,128,CARD_ALIVE,0,0};
    stage_obstacle_total=2;stage_obstacles[0]={240,192,OT_BUMPER,0,0,0};stage_obstacles[1]={400,224,OT_PORTAL,0,0,0};
    Items.bomb[0]={176,256,0,IF_FALL,0,0};Items.point[0]={336,288,0,IF_BOUNCE,0,0};
    // engine states must remain read-only
    const auto card=stage_cards[0];const auto obstacle=stage_obstacles[1];const auto items=Items;
    hud_radar_render(boss);verify();
    assert(!std::memcmp(&card,&stage_cards[0],sizeof(card)) && !std::memcmp(&obstacle,&stage_obstacles[1],sizeof(obstacle)) && !std::memcmp(&items,&Items,sizeof(items)));
    for(int type:{OT_BAR_TOP,OT_BAR_BOTTOM,OT_BAR_LEFT,OT_BAR_RIGHT,OT_TURRET_SLOW_1_AIMED,OT_NONE,OT_BUMPER}) {
        stage_obstacles[0].type=type;hud_radar_render(boss);verify();
    }
    boss={128,144,true};player.x_position=112;player.y_position=128;orb_set_position(112,128);
    hud_radar_render(boss);verify();
    orb_set_position(608,368);hud_radar_render(boss);verify(); // exposes player/boss/card beneath orb
    player.x_position=0;player.y_position=368;boss.visible=false;
    stage_cards[0].flag=CARD_REMOVED;stage_obstacle_total=0;
    Items.bomb[0].flag=IF_SPLASH;Items.point[0].flag=IF_COLLECTED;
    sub_calls.clear();hud_radar_render(boss);verify();
    assert(!sub_calls.empty() && sub_calls.size()<60);
    for(const auto &call:sub_calls) assert(call.type=='I' && call.w==8 && call.h<=8);
    for(int x:{-100,0,639,800}) for(int y:{0,64,399,500}) {
        orb_set_position(x-16,y-16);player.x_position=623-x;player.y_position=447-y;
        boss={x,y,true};hud_radar_render(boss);verify();
    }
    sub_calls.clear();Pellets.add_single(300,200,0,TO_SP(2));hud_radar_render(boss);
    assert(sub_calls.empty()); // projectiles never enter the snapshot
    Pellets.init();
    hud_invalidate();hud_render(3,20);sub_calls.clear();hud_radar_render(boss);verify();
    assert(sub_calls.size()==20*11); // correct full restoration after reinitialization
    player=old_player;orb_set_position(old_orb.x_position,old_orb.y_position);Items=old_items;Pellets=old_pellets;
    std::copy(old_cards.begin(),old_cards.end(),stage_cards);std::copy(old_obstacles.begin(),old_obstacles.end(),stage_obstacles);
    stage_card_total=old_card_total;stage_obstacle_total=old_obstacle_total;
    stage_score=old_score;hiscore=old_hi;stage_cardcombo_cur=old_combo;stage_cardcombo_max=old_max;rank=old_rank;
    camera.snap_center();hud_invalidate();
    std::puts("Bottom HUD: source pattern/layer bounds, red labels/white values, exact world radar shapes/priority, removal/overlap/edge restoration, camera-independent idle writes and read-only/RNG/heap checks passed");
}

static int ink_count() {
    const u16 *px = canvas_at(0, 0);
    return std::count_if(px, px + VIEW_W * VIEW_H, [](u16 v) { return v != 0; });
}

static int foreground_frames, covered_hud_frames, title_frames;
static u16 displayed_canvas_pixel(int x, int y) {
    const auto &bg = test_bgs[2];
    return *canvas_at(bg.x + x * bg.sx / 256, bg.y + y * bg.sy / 256);
}
static void observe_foreground() {
    assert(++foreground_frames < 2000);
    if(g_canvas_target && !canvas_buffer_ready())
        assert(std::equal(canvas_at(0,0),canvas_at(0,VIEW_H),BG_BMP_RAM(CANVAS_BMP_BLOCK)));
    assert(REG_WININ == 0x1D1D && test_bgs[0].priority == 1 && test_bgs[2].priority == 0);
    assert(test_objs[OAM_SLOT_HUD_LIVES].priority == 1 && test_objs[OAM_SLOT_HUD_HP].priority == 1);
    for (int i = 0; i < OAM_HUD_HP_COUNT; ++i) assert(test_objs[OAM_SLOT_HUD_HP + i].priority == 1);
    covered_hud_frames += (displayed_canvas_pixel(0, 0) & 0x8000) != 0;
    if (test_bgs[2].sy == 32) {
        assert(test_bgs[2].sx == 32 && test_bgs[2].y == CANVAS_H);
        assert(title_frames < 48);
        const int index = title_frames / 8;
        assert(test_bgs[2].x == (index % 2) * 32);
        const u16 *glyph = font16x16_find(JIS_TOUHOU_REIIDEN[index]);
        assert(glyph);
        int windows = 0;
        for (int y = 0; y < VIEW_H; ++y)
            for (int x = 0; x < VIEW_W; ++x) {
                const bool ink = x >= 64 && x < 192 && y >= 32 && y < 160 &&
                    (glyph[(y - 32) / 8] & (0x8000 >> ((x - 64) / 8)));
                assert(displayed_canvas_pixel(x, y) == (ink ? 0 : 0x8000));
                windows += ink;
            }
        assert(windows > 0); // Never a full-screen black field between glyphs.
        ++title_frames;
    }
}

static int bg0_pixel(int x, int y) {
    const int tile = BG_MAP_RAM(30)[(y / 8) * 32 + x / 8] & 1023;
    const u16 word = BG_TILE_RAM(2)[tile * 16 + (y % 8) * 2 + (x % 8) / 4];
    return (word >> ((x % 4) * 4)) & 15;
}

static void check_modal_glyph(int left,int top,unsigned jis,int ink,int weight)
{
    const u16 *glyph=font16x16_find(jis);assert(glyph);
    for(int y=0;y<16;++y) {
        u16 bits=glyph[y];
        if(weight==1 || weight==3) bits=u16(bits|(bits<<1));
        if(weight>=2) {
            const u16 added=u16(bits<<1)&~bits;
            bits=u16((bits|(bits<<1))&~u16(added<<1));
        }
        for(int x=0;x<16;++x)
            assert(bg0_pixel(left+x,top+y)==(bits&(0x8000>>x) ? ink : 0));
    }
}

static void check_modal_ank(int left,int top,const char *text,int ink)
{
    for(;*text;++text,left+=8) for(int y=0;y<16;++y) for(int x=0;x<8;++x)
        assert(bg0_pixel(left+x,top+y)==(font8x16[*text-0x20][y]&(0x80>>x) ? ink : TEXT_MODAL_BLACK));
}

namespace {
struct MenuProbe {
    int scenario=0,pause_frames=0,confirm_frames=0,blank_frames=0,opening_frames=0,result_frames=0,waits=0;
    unsigned timer=0;int score=0,stage=1;
    StageBonus bonus{};
    u16 hud[96]{};
} menu_probe;

void observe_source_menu()
{
    auto &p=menu_probe;assert(++p.waits<200);
    assert(stage_timer==p.timer && stage_score==p.score);
    assert(std::equal(std::begin(p.hud),std::end(p.hud),BG_MAP_RAM(30)));
    test_keys_down=test_keys_held=0;
    if(p.scenario<5) {
        bool confirmation=false,pause=false;
        for(int y=56;y<72;++y) {
            for(int x=36;x<52;++x) confirmation|=bg0_pixel(x,y)!=0;
            for(int x=84;x<164;++x) pause|=bg0_pixel(x,y)!=0;
        }
        pause&=!confirmation;
        if(pause) {
            ++p.pause_frames;
            check_modal_glyph(84,56,0x2350,TEXT_MODAL_WHITE,3);
            check_modal_glyph(84,72,0x3a46,TEXT_MODAL_WHITE,3);
            if(p.scenario==4) {
                if(p.pause_frames==1) test_keys_down=KEY_UP; // vertical navigation is not the source menu
                if(p.pause_frames==2) test_keys_down=KEY_RIGHT;
                if(p.pause_frames==3) {
                    check_modal_glyph(132,72,0x217c,TEXT_MODAL_WHITE,0);
                    for(int y=72;y<88;++y) for(int x=68;x<84;++x) assert(!bg0_pixel(x,y));
                    test_keys_down=KEY_LEFT;
                }
                if(p.pause_frames==4) { check_modal_glyph(68,72,0x217c,TEXT_MODAL_WHITE,0);test_keys_down=KEY_B; }
            } else test_keys_down=p.scenario==3 ? KEY_START : p.scenario==0 ? KEY_B : KEY_RIGHT|KEY_B;
        } else if(confirmation) {
            ++p.confirm_frames;
            check_modal_glyph(36,56,0x4b5c,TEXT_MODAL_WHITE,3);
            check_modal_glyph(52,72,0x2426,TEXT_MODAL_WHITE,3);
            check_modal_glyph(164,72,0x244f,TEXT_MODAL_WHITE,3);
            check_modal_glyph(36,72,0x217c,TEXT_MODAL_WHITE,0); // No is first/default
            test_keys_down=p.scenario==2 ? KEY_RIGHT|KEY_B : KEY_B;
        } else if(p.pause_frames && !p.confirm_frames) ++p.blank_frames;
        return;
    }
    if((REG_BLDCNT&0x3f)!=BLEND_SRC_BG0) return; // first VBlank before modal ownership
    assert(REG_BLDCNT==(BLEND_ALPHA|BLEND_SRC_BG0|BLEND_DST_BG1|BLEND_DST_BG2|BLEND_DST_BG3|BLEND_DST_SPRITE|BLEND_DST_BACKDROP));
    assert(REG_BLDALPHA==0x0810 && (REG_WINOUT&0x20) && !(REG_WININ&0x2020));
    bool results=false;
    for(int y=32;y<48;++y) for(int x=60;x<196;++x) results|=bg0_pixel(x,y)==TEXT_MODAL_RED;
    if(results) {
        ++p.result_frames;
        char title[]="STAGE 00 COMPLETE";title[6]='0'+p.stage/10;title[7]='0'+p.stage%10;
        check_modal_ank(60,32,title,TEXT_MODAL_RED);
        check_modal_ank(108,56,"BONUS",TEXT_MODAL_WHITE);
        check_modal_ank(32,72,"Time",TEXT_MODAL_WHITE);
        check_modal_ank(32,88,"Continuous",TEXT_MODAL_WHITE);
        check_modal_ank(32,104,"Bomb&Player",TEXT_MODAL_WHITE);
        check_modal_ank(32,120,"STAGE",TEXT_MODAL_WHITE);
        check_modal_ank(32,144,"BONUS Point",TEXT_MODAL_WHITE);
        const int metrics[]={p.bonus.time*10,p.bonus.combo*10,p.bonus.resources*10,p.bonus.stage*10,p.bonus.total};
        for(int row=0;row<5;++row) {
            const int digits=row==4 ? 6 : 5,left=row==4 ? 168 : 176,top=row==4 ? 144 : 72+row*16;
            int divisor=row==4 ? 100000 : 10000;
            for(int d=0;d<digits;++d,divisor/=10) {
                const char digit[]={char('0'+(metrics[row]/divisor)%10),'\0'};
                check_modal_ank(left+d*8,top,digit,TEXT_MODAL_WHITE);
            }
        }
        check_modal_ank(72,168,"Hit A or B Key",TEXT_MODAL_RED);
        // Black halves the next visible scene layer; white text stays white.
        const auto add=[](int source,int scene){return std::min(31,source+scene/2);};
        assert(add(BG_PALETTE[TEXT_PAL_RED*16+TEXT_MODAL_BLACK]&31,20)==10);
        assert(add(BG_PALETTE[TEXT_PAL_RED*16+TEXT_MODAL_WHITE]&31,20)==31);
        if(p.result_frames==3) test_keys_down=p.scenario==5 ? KEY_B : KEY_A;
        return;
    }
    assert(++p.opening_frames<=24);
    const int half_height=(84*p.opening_frames+23)/24;
    for(int y=24;y<192;++y) for(int x=0;x<256;++x)
        assert(bg0_pixel(x,y)==(x>=16 && x<240 && y>=108-half_height && y<108+half_height ? TEXT_MODAL_BLACK : 0));
    test_keys_down=KEY_A; // cannot skip the source opening animation
}
}

static void check_source_menus()
{
    text_layer_clear();canvas_playfield_clip(false);canvas_clear();
    canvas_fill_rect(12,80,8,8,RGB15(12,20,6));canvas_fill_rect(24,80,4,4,0x7fff);
    const std::vector<u16> pixels(canvas_at(0,0),canvas_at(0,192));
    const std::vector<u16> bg(BG_PALETTE,BG_PALETTE+256),obj(SPRITE_PALETTE,SPRITE_PALETTE+256);
    const std::vector<u16> font(BG_TILE_RAM(2),BG_TILE_RAM(2)+224*16);
    const auto heap=test_heap_attempts,objects=test_obj_attempts,oam=test_oam_changes;
    const u16 control=REG_BLDCNT,alpha=REG_BLDALPHA,window=REG_WINOUT;
    for(int scenario=0;scenario<7;++scenario) {
        menu_probe={};auto &p=menu_probe;p.scenario=scenario;p.timer=stage_timer;p.score=stage_score;
        std::copy(BG_MAP_RAM(30),BG_MAP_RAM(30)+96,p.hud);
        test_vblank_hook=observe_source_menu;
        if(scenario<5) {
            assert(pause_menu_run()==(scenario==2));
            assert(p.pause_frames==(scenario==4 ? 4 : 1) && p.blank_frames==20);
            assert(p.confirm_frames==(scenario==1 || scenario==2 ? 1 : 0));
        } else {
            p.stage=scenario==5 ? 20 : 1;
            p.bonus=scenario==5 ? StageBonus{6553,6553,6553,6553,262120} : StageBonus{};
            REG_BLDCNT=0x0123;REG_BLDALPHA=0x0506;REG_WINOUT=0x401f;
            stagebonus_run(p.bonus,p.stage);
            assert(p.opening_frames==24 && p.result_frames==3);
            assert(REG_BLDCNT==0x0123 && REG_BLDALPHA==0x0506 && REG_WINOUT==0x401f);
        }
        test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
        assert(std::equal(bg.begin(),bg.end(),BG_PALETTE) && std::equal(obj.begin(),obj.end(),SPRITE_PALETTE));
        assert(std::equal(pixels.begin(),pixels.end(),canvas_at(0,0)));
        assert(std::equal(font.begin(),font.end(),BG_TILE_RAM(2)));
        assert(std::all_of(BG_MAP_RAM(30)+96,BG_MAP_RAM(30)+1024,[](u16 v){return v==0;}));
    }
    assert(test_heap_attempts==heap && test_obj_attempts==objects && test_oam_changes==oam);
    REG_BLDCNT=control;REG_BLDALPHA=alpha;REG_WINOUT=window;
    canvas_clear();
    std::puts("Menus: native16x16 pause JIS, horizontal/default-No controls, native8x16 results, solid translucent BG0 reveal/targets, A-or-B prompt, max/zero bonuses and exact HUD/palette/blend restoration passed");
}

static void check_white_projectiles()
{
    camera.snap_center();
    for (int scene = -1; scene < 7; ++scene) {
        if (scene < 0) load_boss_background(); else load_background(scene);
        palette_apply_round();
        Pellets.init();
        Pellets.add_group(300, 120, PG_1, 3 * 16);
        auto &pellet = Pellets.at(0);
        for (int decay : {0, 1, 11, 19}) {
            pellet.decay_frame = decay;
            canvas_clear(); pellet_render_frame();
            assert(ink_count() > 0);
            for (const u16 *px = canvas_at(0, 0); px < canvas_at(0, VIEW_H); ++px)
                assert(*px == 0 || *px == 0xFFFF); // opaque pure white, never scene palette gray
        }
        pellet.decay_frame = 20;
        canvas_clear(); pellet_render_frame(); assert(ink_count() == 0);
    }
    std::puts("Projectile rendering: white intact/shatter cels on all seven scene palettes and SinGyoku passed");
}

static void check_ofuda_rendering()
{
    shots_initialize(); Pellets.init(); camera.snap_center();
    orb_set_position(10000, 10000);
    shots_add(300, 160); shots_render_frame();
    assert(!test_objs[1].hidden);
    const u16 *normal = test_objs[1].gfx;
    Pellets.add_group(300, 160, PG_1, 3 * 16);
    canvas_clear(); pellet_render_frame(); assert(ink_count() > 0);
    assert(!Pellets.update());
    canvas_begin_frame(); pellet_render_frame();
    assert(ink_count() == 0); // no pellet/shatter remains in the rendered hit frame
    shots_render_frame();
    assert(!test_objs[1].hidden && test_objs[1].gfx != normal);
    const u16 *impact = test_objs[1].gfx;
    const int x = test_objs[1].x, y = test_objs[1].y;
    for (int frame = 1; frame <= 7; ++frame) {
        shots_update(); shots_render_frame();
        assert(test_objs[1].hidden == (frame == 7));
        if (frame < 7) {
            assert(test_objs[1].x == x && test_objs[1].y == y);
            assert(test_objs[1].gfx != normal);
            assert((test_objs[1].gfx == impact) == (frame < 3));
        }
    }
    std::puts("Ofuda rendering: pellet disappears immediately, stationary OAM impact cels then hide passed");
}

static void check_portal_rendering()
{
    camera.snap_center();
    for (int count : {1, 2}) for (int entry = 0; entry < count; ++entry) {
        uint8_t obstacles[STAGE_ROWS][STAGE_COLS] = {};
        const uint8_t cards[STAGE_ROWS][STAGE_COLS] = {};
        for (int i = 0; i < count; ++i) obstacles[2][6 + i * 4] = OT_PORTAL;
        stage_init(obstacles, cards); stage_reset(); stage_render_clear();
        initialize_irand(1234); orb_initialize();
        const int destination = count == 1 ? entry : 1 - entry;
        orb_set_position(stage_obstacle(entry).left, stage_obstacle(entry).top);
        for (int frame = 1; frame <= 60; ++frame) {
            if (frame > 40) orb_set_position(10000, 10000);
            stage_obstacles_update(); stage_render_frame(); orb_render_frame();
            if (frame <= 40) assert(test_objs[10].hidden == (frame < 40));
            for (int slot = 0; slot < count; ++slot) {
                StgObj expected = STGOBJ_PORTAL;
                if (slot == entry && frame < 10) expected = STGOBJ_PORTAL_ANIM;
                else if (slot == entry && frame < 20) expected = STGOBJ_PORTAL_ANIM2;
                else if (slot == destination && frame >= 20 && frame < 30) expected = STGOBJ_PORTAL_ANIM2;
                else if (slot == destination && frame >= 30 && frame < 40) expected = STGOBJ_PORTAL_ANIM;
                for (int q = 0; q < 4; ++q) {
                    const int mx = (6 + slot * 4) * 2 + (q & 1);
                    const int my = 2 * 2 + 3 + (q >> 1);
                    const u16 tile = BG_MAP_RAM(8)[(mx / 32) * 1024 + my * 32 + mx % 32];
                    assert((tile & 1023) == 1 + expected * 4 + q);
                }
            }
            if (frame == 40) {
                assert(test_objs[10].x == camera.x(stage_obstacle(destination).left));
                assert(test_objs[10].y == Camera::y(stage_obstacle(destination).top));
            }
        }
        stage_reset(); stage_render_frame();
        assert(!orb_get_in_portal());
    }
    std::puts("Portal rendering: actual BG1 entrance/exit cels and OAM orb reappearance at 40 passed");
}

static void check_obj_allocator_model()
{
    oamInit(&oamMain, SpriteMapping_1D_32, false);
    auto *small = oamAllocateGfx(&oamMain, SpriteSize_8x8, SpriteColorFormat_16Color);
    auto *large = oamAllocateGfx(&oamMain, SpriteSize_64x64, SpriteColorFormat_16Color);
    assert(small == test_obj_vram && large == test_obj_vram + 1024);
    // The alignment gap remains usable, rather than silently consumed.
    auto *gap = oamAllocateGfx(&oamMain, SpriteSize_8x8, SpriteColorFormat_16Color);
    assert(gap == test_obj_vram + 16);
    oamFreeGfx(&oamMain, large); oamFreeGfx(&oamMain, small); oamFreeGfx(&oamMain, gap);
    std::vector<u16 *> full;
    while (auto *p = oamAllocateGfx(&oamMain, SpriteSize_8x8, SpriteColorFormat_16Color)) full.push_back(p);
    assert(full.size() == 1024 && test_obj_used == 32768);
    // Free alternating blocks: enough total space, but no contiguous 16x16.
    for (size_t i = 0; i < full.size(); i += 2) oamFreeGfx(&oamMain, full[i]);
    assert(!oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_16Color));
    for (size_t i = 1; i < full.size(); i += 2) oamFreeGfx(&oamMain, full[i]);
    assert(test_obj_used == 0 && test_obj_count == 0);
    large = oamAllocateGfx(&oamMain, SpriteSize_64x64, SpriteColorFormat_16Color);
    assert(large == test_obj_vram); oamFreeGfx(&oamMain, large);
    auto *indexed = oamAllocateGfx(&oamMain, SpriteSize_64x64, SpriteColorFormat_256Color);
    assert(indexed == test_obj_vram && test_obj_used == 4096);
    oamFreeGfx(&oamMain, indexed);
    oamInit(&oamMain, SpriteMapping_1D_32, false);
}

static void check_sprite_initializers()
{
    struct Owner { bool (*init)(); size_t count, bytes; };
    const Owner owners[] = {
        {player_render_init, 62, 19456}, {shots_render_init, 4, 128},
        {orb_render_init, 5, 576}, {item_render_init, 2, 256},
        {boss_render_init, 1, 4096}, {upper_hud_init, 5, 3328}
    };
    for (const auto &owner : owners) {
        const size_t bytes = test_obj_used, count = test_obj_count;
        const std::vector<size_t> lengths(std::begin(test_obj_lengths), std::end(test_obj_lengths));
        const std::vector<u16> resident(std::begin(test_obj_vram), std::end(test_obj_vram));
        const std::vector<u16> palette(std::begin(SPRITE_PALETTE), std::end(SPRITE_PALETTE));
        const auto changes = test_oam_changes;
        for (size_t fail = 0; fail < owner.count; ++fail) {
            const size_t attempts = test_obj_attempts, frees = test_obj_frees;
            test_obj_fail_after = fail;
            assert(!owner.init());
            assert(test_obj_attempts == attempts + fail + 1 && test_obj_frees == frees + fail);
            assert(test_obj_used == bytes && test_obj_count == count);
            assert(std::equal(lengths.begin(), lengths.end(), test_obj_lengths));
            for (size_t i = 0; i < lengths.size(); ++i)
                if (lengths[i]) assert(std::equal(resident.begin() + i * 16,
                    resident.begin() + i * 16 + lengths[i] / 2, test_obj_vram + i * 16));
            assert(test_oam_changes == changes && std::equal(palette.begin(), palette.end(), SPRITE_PALETTE));
        }
        test_obj_fail_after = -1;
        // Also exercise real capacity exhaustion, not just injected failures.
        std::vector<u16 *> blockers;
        while (auto *p = oamAllocateGfx(&oamMain, SpriteSize_8x8, SpriteColorFormat_16Color)) blockers.push_back(p);
        assert(!owner.init());
        for (auto *p : blockers) oamFreeGfx(&oamMain, p);
        assert(test_obj_used == bytes && test_obj_count == count);
        assert(owner.init()); // retry after every partial-allocation failure
        assert(test_obj_used == bytes + owner.bytes && test_obj_count == count + owner.count);
    }
    assert(test_obj_count == 79 && test_obj_used == 27840);
    const auto attempts = test_obj_attempts, changes = test_oam_changes;
    const std::vector<u16> resident(std::begin(test_obj_vram), std::end(test_obj_vram));
    const std::vector<u16> palette(std::begin(SPRITE_PALETTE), std::end(SPRITE_PALETTE));
    test_obj_fail_after = 0;
    for (int repeat = 0; repeat < 2000; ++repeat)
        for (const auto &owner : owners) assert(owner.init());
    test_obj_fail_after = -1;
    assert(test_obj_attempts == attempts && test_oam_changes == changes);
    assert(std::equal(resident.begin(), resident.end(), test_obj_vram));
    assert(std::equal(palette.begin(), palette.end(), SPRITE_PALETTE));
    size_t end = 0;
    for (size_t i = 0; i < 1024; ++i) if (test_obj_lengths[i]) end = i * 32 + test_obj_lengths[i];
    assert(end == 28672 && test_obj_capacity - end == 4096);
    // Prove the hybrid's temporary4KiB lease fits beside
    // the real79 production owners, without freeing or resizing their caches.
    auto *hybrid = oamAllocateGfx(&oamMain,SpriteSize_64x64,SpriteColorFormat_256Color);
    assert(hybrid == test_obj_vram + 28672/2 && test_obj_used == 31936 && test_obj_count == 80);
    assert(std::equal(palette.begin(),palette.end(),SPRITE_PALETTE) && test_oam_changes == changes);
    for (size_t i = 0; i < 1024; ++i) if (test_obj_lengths[i] && i*32 < 28672)
        assert(std::equal(resident.begin()+i*16,resident.begin()+i*16+test_obj_lengths[i]/2,test_obj_vram+i*16));
    oamFreeGfx(&oamMain,hybrid);
    assert(test_obj_used == 27840 && test_obj_count == 79);
    std::printf("Sprites: 79 allocation-failure positions rolled back; 2000 reinitializations stable; %zu allocations/%zu bytes, aligned end %zu of 32768\n",
        test_obj_count, test_obj_used, end);
}

static void check_sprite_lifecycles()
{
    // The pause/bonus canvas snapshot is intentionally retained after first use.
    palette_dim(50); palette_restore();
    assert(test_heap_count == 1 && test_heap_bytes == 98304);
    const auto heap_attempts = test_heap_attempts, obj_attempts = test_obj_attempts;
    for (int transition = 0; transition < 1000; ++transition) {
        const bool boss = transition % 8 == 7;
        boss_render_hide();
        camera.snap_center();
        if (boss) load_boss_background(); else load_background(transition % 7);
        palette_apply_round();
        const auto &stage = ds_stagedat0[transition % 5];
        stage_init(stage.obstacle, stage.card); stage_reset(); stage_render_clear();
        player_initialize(); orb_initialize(); shots_initialize(); Pellets.init(); Items.init();
        timer_init_for(0, transition % 5);
        if (boss) singyoku_init();
        Items.add_bomb(300, 120); Items.add_point(340, 120); Items.update();
        shots_add(300, 160); shots_update();
        player_is_hit = true; player_miss();
        stage_render_frame(); player_render_frame(); shots_render_frame(); orb_render_frame();
        item_render_frame(); item_rings_frame(); orb_indicator_render();
        upper_hud_render(transition % 13, transition % 6, stage_timer,
            boss ? UpperHudMode::Boss : UpperHudMode::Regular);
        if (boss) { boss_render_frame(); upper_hud_boss(18, 18, 6, 12); }
        else upper_hud_boss_hide();
        palette_dim(50); palette_restore();
        assert(test_obj_count == 79 && test_obj_used == 27840 && test_obj_attempts == obj_attempts);
        // Each background load uses one temporary buffer and frees it.
        assert(test_heap_count == 1 && test_heap_bytes == 98304);
        assert(test_heap_attempts == heap_attempts + transition + 1);
    }
    std::puts("Sprites: 1000 stage/life/HUD/boss reset cycles with flat OBJ usage and one retained 98304-byte dim snapshot passed");
}

int main()
{
    static_assert(PLAYFIELD_W == 640 && PLAYFIELD_H == 336);
    static_assert(PLAYFIELD_TOP == 64 && PLAYFIELD_BOTTOM == 400);
    static_assert(STAGE_COLS == 20 && STAGE_ROWS == 10);
    static_assert(half_floor(-1) == -1 && half_floor(-2) == -1);
    camera.snap_center();
    assert(camera.left == 32 && camera.x(320) == 128 && camera.y(64) == 24 && camera.y(400) == 192);
    for (int center = 16; center <= 624; ++center) {
        camera.follow(center);
        assert(camera.left >= 0 && camera.left <= 64);
        if (center >= 256 && center <= 384) assert(camera.x(center) == 128);
    }
    camera.follow(16); assert(camera.left == 0);
    camera.follow(624); assert(camera.left == 64);
    camera.snap_center(); assert(camera.left == 32);

    video_init();text_layer_init();subscreen_init();tiles_init();
    check_obj_allocator_model();
    assert(test_bgs[1].size == BgSize_T_512x256);
    assert(test_bgs[3].size == BgSize_B8_512x256 && test_bgs[2].map == 16);
    assert(test_vram_d_mode == VRAM_D_MAIN_BG_0x06040000);
    assert(test_bgs[2].priority == 0 && test_bgs[1].priority == 2 && test_bgs[3].priority == 3);
    assert(REG_WININ == 0x1919 && REG_WINOUT == 0x001F);
    assert(REG_WIN0H == 128 && REG_WIN1H == 128 * 256 && REG_WIN0V == 24 && REG_WIN1V == 24);
    assert((REG_DISPCNT & (DISPLAY_WIN0_ON | DISPLAY_WIN1_ON)) == (DISPLAY_WIN0_ON | DISPLAY_WIN1_ON));

    const auto full = read_bytes("nitro:/st0.bin");
    assert(full.size() == 320 * 192);
    for (bool boss : {false, true, false}) {
        if (boss) load_boss_background(); else load_background(0);
        const auto source = boss ? read_bytes("nitro:/boss1.bin") : full;
        const std::vector<u16> resident(BG_BMP_RAM(4), BG_BMP_RAM(4) + 512 * 256 / 2);
        const auto upload_bytes = test_dma_fill_bytes;
        const auto upload_copies = test_dma_copy_calls;
        for (int pass = 0; pass < 4; ++pass) {
            for (int step = 0; step <= 64; ++step) {
                const int left = pass & 1 ? 64 - step : step;
                camera.left = left;
                video_apply_camera(); assert(test_bgs[1].x == left && test_bgs[1].y == 0);
                // Current checked-in camera uses centered three-fifths scenery
                // motion; keep it unchanged rather than restoring an old test's
                // half-speed policy during unrelated title/build fixes.
                assert(test_bgs[3].x == (boss ? left : 32 + ((left - 32) * 3) / 5) && test_bgs[3].y == 0);
                check_background(source);
                assert(std::equal(resident.begin(), resident.end(), BG_BMP_RAM(4)));
            }
        }
        assert(test_dma_fill_bytes == upload_bytes && test_dma_copy_calls == upload_copies);
        camera.snap_center(); video_apply_camera();
        assert(test_bgs[1].x == 32 && test_bgs[3].x == 32); // unchanged entrance framing
    }
    camera.snap_center(); load_boss_background(); check_background(read_bytes("nitro:/boss1.bin"));

    stage_init(ds_stagedat0[0].obstacle, ds_stagedat0[0].card);
    for (int i = 0; i < stage_card_count(); ++i) {
        const auto &c = stage_card(i);
        assert(c.top >= 64 && c.top <= 352);
        assert(stage_cell_x(c.left) >= 0 && stage_cell_x(c.left) < 20);
        assert(stage_cell_y(c.top) >= 0 && stage_cell_y(c.top) < 10);
    }
    tiles_clear_all();
    tiles_put(0, 0, STGOBJ_BUMPER);
    tiles_put(19, 9, STGOBJ_BUMPER);
    auto *map = BG_MAP_RAM(8);
    assert(map[3 * 32] && map[1024 + 21 * 32 + 6] && map[1024 + 22 * 32 + 7]);
    for (int block = 0; block < 2; ++block)
        for (int i = 0; i < 3 * 32; ++i) assert(map[block * 1024 + i] == 0);
    stage_render_clear(); stage_render_frame();

    initialize_irand(1234); player_initialize(); orb_initialize(); shots_initialize(); Pellets.init(); Items.init();
    check_sprite_initializers();
    const auto sprite_attempts = test_obj_attempts;
    assert(test_obj_used < test_obj_capacity && player.x_position == 304 && player.y_position == 368);
    assert(orb_get_state().x_position == 600 && orb_get_state().y_position == 280);
    for (int i = 0; i < 200; ++i) player_update(KEY_LEFT, 0);
    assert(player.x_position == 0);
    for (int i = 0; i < 200; ++i) player_update(KEY_RIGHT, 0);
    assert(player.x_position == 608);
    player_initialize(); camera.snap_center();

    // Run real timer and real pellet updates: the old y=0 spawn disappeared
    // on the first update after restoring the playfield ceiling to64.
    orb_set_position(10000, 10000);
    for (int difficulty = RANK_EASY; difficulty <= RANK_LUNATIC; ++difficulty) {
        rank = static_cast<rank_t>(difficulty);
        Pellets.init(); timer_init_for(0, 0); stage_timer = 2;
        assert(timer_tick() == TIMER_EVENT_HARRYUP && stage_timer == 0);
        for (int tick = 0; tick < 4; ++tick) assert(timer_tick() == TIMER_EVENT_NONE);
        assert(Pellets.alive_count() == 0);
        timer_tick(); assert(Pellets.alive_count() == difficulty + 1);
        for (int i = 0; i <= difficulty; ++i) assert(TO_PIXEL(Pellets.at(i).cur_top.v) == PLAYFIELD_TOP);
        Pellets.update(); assert(Pellets.alive_count() == difficulty + 1);
        for (int i = 0; i <= difficulty; ++i) assert(TO_PIXEL(Pellets.at(i).cur_top.v) > PLAYFIELD_TOP);
        camera.follow(TO_PIXEL(Pellets.at(0).cur_left.v) + PELLET_W / 2);
        canvas_clear(); canvas_playfield_clip(true); pellet_render_frame();
        assert(ink_count() > 0);
        assert(std::all_of(canvas_at(0, 0), canvas_at(0, UPPER_HUD_H), [](u16 pixel) { return pixel == 0; }));
        canvas_playfield_clip(false); camera.snap_center();
        player_is_hit = true;
        player_miss(); // real gameplay miss hook, not a direct helper call
        assert(stage_timer == 1000 && !player_is_hit && player_is_invincible());
    }
    // Surviving misses preserve the countdown and add time on both card and
    // boss stages. Fresh-stage/game-over initialization must NOT extend it.
    for (int stage : {0, 4}) {
        for (unsigned remaining : {1000u, 198u, 2u, 0u}) {
            timer_init_for(0, stage);
            const unsigned initial = stage_timer;
            player_initialize(); assert(stage_timer == initial);
            stage_timer = remaining;
            player_is_hit = true;
            player_miss();
            assert(!player_is_hit && player_is_invincible());
            assert(stage_timer == (remaining ? remaining + 200 : 1000));
            player_update(0, 0); // respawn updates do not repeatedly add time
            assert(stage_timer == (remaining ? remaining + 200 : 1000));
            timer_init_for(0, stage);
            player_initialize(); assert(stage_timer == initial);
        }
    }
    rank = RANK_NORMAL; pellet_speed = 0; Pellets.init(); orb_initialize();
    player_render_frame(); assert(test_objs[0].x == 120 && test_objs[0].y == 176);
    orb_set_position(600, 280); orb_render_frame(); assert(test_objs[10].hidden);
    canvas_clear(); canvas_playfield_clip(true); orb_indicator_render();
    assert(ink_count() == 0 && !test_objs[OAM_SLOT_ORB_ARROW].hidden);
    assert(test_objs[OAM_SLOT_ORB_ARROW].priority == 0 && test_objs[OAM_SLOT_ORB_ARROW].size == SpriteSize_8x16);
    auto marker = orb_indicator(camera, 600, 280, 32, 32);
    assert(marker.direction == 1 && marker.x == 253 && marker.y == 140);
    orb_set_position(0, 64); marker = orb_indicator(camera, 0, 64, 32, 32);
    assert(marker.direction == -1 && marker.y >= 29);
    orb_indicator_render(); assert(test_objs[OAM_SLOT_ORB_ARROW].hflip);
    orb_set_position(40, 64); orb_render_frame(); assert(!test_objs[10].hidden); // partial visibility
    canvas_clear(); orb_indicator_render(); assert(ink_count() == 0 && test_objs[OAM_SLOT_ORB_ARROW].hidden);
    orb_set_in_portal(true); orb_set_position(600, 100);
    canvas_clear(); orb_indicator_render(); assert(ink_count() == 0 && test_objs[OAM_SLOT_ORB_ARROW].hidden); orb_set_in_portal(false);
    shots_add(600, 180); shots_render_frame(); assert(test_objs[1].hidden);
    Items.point[0] = {600, 180, 0, IF_FALL, 0, 0}; item_render_frame(); assert(test_objs[28].hidden);
    camera.follow(624); orb_render_frame(); shots_render_frame(); item_render_frame();
    assert(!test_objs[10].hidden && !test_objs[1].hidden && !test_objs[28].hidden);

    // Camera movement never despawns offscreen bullets; they reappear in view.
    camera.left = 0; orb_set_position(300, 300);
    Pellets.add_single(600, 100, 64, 16); Pellets.update(); assert(Pellets.alive_count() == 1);
    canvas_clear(); pellet_render_frame(); assert(ink_count() == 0);
    camera.left = 64; canvas_clear(); pellet_render_frame(); assert(ink_count() > 0);
    singyoku_init(); assert(singyoku_state().entity.left == 272 && singyoku_state().entity.top == 96);
    boss_render_frame(); assert(test_objs[40].x == 64 && test_objs[40].y == 24 && test_objs[40].priority == 1);

    // HUD/arrow issue zero software canvas writes; BG0 tile rows + OAM only.
    canvas_playfield_clip(false); canvas_clear(); upper_hud_render(6, 5, 1000);
    assert(ink_count() == 0);
    assert(!test_objs[OAM_SLOT_HUD_LIVES + 5].hidden && test_objs[OAM_SLOT_HUD_LIVES + 6].hidden);
    assert(!test_objs[OAM_SLOT_HUD_BOMBS + 4].hidden);
    assert(test_objs[OAM_SLOT_HUD_LIVES + 5].x == 80 && test_objs[OAM_SLOT_HUD_BOMBS + 4].x == 168);
    for (int i = 0; i < OAM_HUD_HP_COUNT; ++i) assert(test_objs[OAM_SLOT_HUD_HP + i].hidden);
    assert(test_objs[OAM_SLOT_HUD_LIVES].size == SpriteSize_16x16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            assert(obj_pixel(OAM_SLOT_HUD_LIVES, x, y) == HudIcons_pixels[0][y * 16 + x]);
    // Switching modes MUST redraw resources even when counts/timer are identical.
    upper_hud_render(6, 5, 1000, UpperHudMode::Boss); upper_hud_boss(8, 8);
    assert(!test_objs[OAM_SLOT_HUD_LIVES].hidden && test_objs[OAM_SLOT_HUD_LIVES + 1].hidden);
    assert(!test_objs[OAM_SLOT_HUD_BOMBS].hidden && test_objs[OAM_SLOT_HUD_BOMBS + 1].hidden);
    assert(test_objs[OAM_SLOT_HUD_BOMBS].x == 32 && test_objs[OAM_SLOT_HUD_HP].x == 64);
    assert(obj_pixel(OAM_SLOT_HUD_HP, 0, 0) == 1 && obj_pixel(OAM_SLOT_HUD_HP, 48, 0) == 2);
    assert(obj_pixel(OAM_SLOT_HUD_HP, 7, 0) == 0); // source FE mask gap
    assert(obj_pixel(OAM_SLOT_HUD_HP, 0, 14) == 1 && obj_pixel(OAM_SLOT_HUD_HP, 0, 15) == 0);
    assert(obj_pixel(OAM_SLOT_HUD_HP, 16, 0) == 1 && obj_pixel(OAM_SLOT_HUD_HP, 17, 0) == 2);
    assert(obj_pixel(OAM_SLOT_HUD_HP, 17, 1) == 2); // source A7 columns, not checkerboard
    auto *text_map = BG_MAP_RAM(30);
    const auto divider = text_map[64];
    assert(divider && text_map[28]);
    // Warning ink is pure red below 200, including zero as in timer_put().
    // Check the actual palette bank used by all four displayed digits.
    assert(BG_PALETTE[TEXT_PAL_RED * 16 + 1] == RGB15(31, 0, 0));
    for (unsigned remaining : {1000u, 9999u, 10000u, ~0u, 200u, 199u, 198u, 42u, 2u, 0u, 1000u}) {
        text_layer_hud_timer(remaining);
        for (int y = 0; y < 2; ++y)
            for (int x = 28; x < 32; ++x)
                assert((text_map[y * 32 + x] >> 12) ==
                       (remaining < 200 ? TEXT_PAL_RED : TEXT_PAL_WHITE));
        // Every digit is an unmodified native ANK glyph, including leading
        // zeroes and9999 saturation. No baked/squeezed fullwidth digits remain.
        const unsigned shown=remaining>9999 ? 9999 : remaining;
        unsigned divisor=1000;
        for(int digit=0;digit<4;++digit,divisor/=10)
            for(int y=0;y<16;++y) for(int x=0;x<8;++x)
                assert(bg0_pixel(224+digit*8+x,y)==!!(font8x16['0'+(shown/divisor)%10-0x20][y]&(0x80>>x)));
    }
    palette_dim(40);
    assert(BG_PALETTE[TEXT_PAL_RED * 16 + 1] == RGB15(12, 0, 0));
    palette_restore();
    assert(BG_PALETTE[TEXT_PAL_RED * 16 + 1] == RGB15(31, 0, 0));
    for (int x = 0; x < 32; ++x) assert(text_map[64 + x] == divider);
    const u16 *divider_gfx = BG_TILE_RAM(2) + (divider & 1023) * 16;
    assert(divider_gfx[0] == 0 && divider_gfx[14] == 0x2222);
    const std::vector<u16> hud_map(text_map, text_map + 96);
    text_layer_puts(10, 3, "PAUSE", TEXT_PAL_WHITE); text_layer_clear();
    assert(std::equal(hud_map.begin(), hud_map.end(), text_map));
    upper_hud_render(0, 0, 0, UpperHudMode::Boss); upper_hud_boss(0, 0);
    assert(!test_objs[OAM_SLOT_HUD_LIVES].hidden && !test_objs[OAM_SLOT_HUD_BOMBS].hidden);
    assert(test_objs[OAM_SLOT_HUD_HP].hidden);
    assert((text_map[28] & 1023) == (hud_map[28] & 1023) - 2); // timer 1000 -> 0000
    for (unsigned digit = 0; digit < 10; ++digit) {
        text_layer_hud_timer(digit * 1111);
        const u8 *glyph = font8x16['0' + digit - 0x20];
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 8; ++x)
                assert(bg0_pixel(224 + x, y) == !!(glyph[y] & (0x80 >> x)));
    }
    upper_hud_render(12, 5, 100); // regular-stage overflow is still represented exactly
    assert(!test_objs[OAM_SLOT_HUD_LIVES].hidden && test_objs[OAM_SLOT_HUD_LIVES + 1].hidden);
    assert(!test_objs[OAM_SLOT_HUD_BOMBS + 4].hidden);
    assert(text_map[2] && text_map[3] && text_map[4]); // x12 beside one life icon
    assert(test_objs[OAM_SLOT_HUD_BOMBS + 4].x == 168);
    upper_hud_render(7, 5, 100);
    assert(!test_objs[OAM_SLOT_HUD_LIVES].hidden && test_objs[OAM_SLOT_HUD_LIVES + 1].hidden && text_map[2]);
    upper_hud_render(6, 5, 100);
    assert(!test_objs[OAM_SLOT_HUD_LIVES + 5].hidden && !text_map[2] && !text_map[3] && !text_map[4]);
    std::vector<u16> timer_map;
    for (int y = 0; y < 2; ++y)
        for (int x = 28; x < 32; ++x) timer_map.push_back(text_map[y * 32 + x]);
    for (int count = 0; count <= 12; ++count) {
        upper_hud_render(count, 5, 100, UpperHudMode::Boss);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 8; ++x) {
                const int digit = count < 10 ? count : x < 4 ? count / 10 : count % 10;
                const int source_x = count < 10 ? x : (x % 4) * 2;
                assert(bg0_pixel(16 + x, y) == !!(font8x16['0' + digit - 0x20][y] & (0x80 >> source_x)));
            }
    }
    // Three original-size HP strips support every shipped boss's <=18 points.
    upper_hud_boss(18, 18);
    for (int i = 0; i < OAM_HUD_HP_COUNT; ++i) {
        assert(!test_objs[OAM_SLOT_HUD_HP + i].hidden);
        assert(test_objs[OAM_SLOT_HUD_HP + i].x == 64 + i * 64);
    }
    assert(obj_pixel(OAM_SLOT_HUD_HP + 2, 8, 14) == 2);
    assert(obj_pixel(OAM_SLOT_HUD_HP + 2, 15, 0) == 0 && obj_pixel(OAM_SLOT_HUD_HP + 2, 16, 0) == 0);
    for (int y = 0; y < 32; ++y)
        for (int x = 16; x < 64; ++x) assert(obj_pixel(OAM_SLOT_HUD_HP + 2, x, y) == 0);
    upper_hud_boss(8, 8);
    assert(test_objs[OAM_SLOT_HUD_HP + 1].hidden && test_objs[OAM_SLOT_HUD_HP + 2].hidden);
    upper_hud_boss(18, 18);
    assert(!text_map[3] && !text_map[4]); // no stale regular-stage x12 text in the HP area
    upper_hud_render(12, 5, 100); // same values, different layout; hide every strip
    for (int i = 0; i < OAM_HUD_HP_COUNT; ++i) assert(test_objs[OAM_SLOT_HUD_HP + i].hidden);
    upper_hud_render(12, 5, 100, UpperHudMode::Boss); upper_hud_boss(18, 18);
    for (int i = 0; i < OAM_HUD_HP_COUNT; ++i) assert(!test_objs[OAM_SLOT_HUD_HP + i].hidden);
    int timer_index = 0;
    for (int y = 0; y < 2; ++y)
        for (int x = 28; x < 32; ++x) assert(timer_map[timer_index++] == text_map[y * 32 + x]);
    // Maxima: six lives/five bombs, every boss health value through18, timer
    //9999. Check ACTUAL ink (not64px HP sprite bounds with transparent padding).
    const auto check_separation=[]() {
        for(int y=0;y<16;++y) for(int x=0;x<256;++x) {
            int ink=bg0_pixel(x,y)!=0;
            for(int slot=OAM_SLOT_HUD_LIVES;slot<OAM_SLOT_HUD_HP+OAM_HUD_HP_COUNT;++slot) {
                if(slot>=OAM_SLOT_HUD_BOMBS+5 && slot<OAM_SLOT_HUD_HP) continue;
                const auto &obj=test_objs[slot];
                const int width=slot>=OAM_SLOT_HUD_HP ? 64 : 16;
                if(!obj.hidden && x>=obj.x && x<obj.x+width && y>=obj.y)
                    ink+=obj_pixel(slot,x-obj.x,y-obj.y)!=0;
            }
            assert(ink<=1);
        }
    };
    for(int count:{0,6,12}) for(int bombs:{0,5}) {
        upper_hud_render(count,bombs,9999,UpperHudMode::Regular);check_separation();
        for(int hp=0;hp<=18;++hp) {
            upper_hud_render(count,bombs,9999,UpperHudMode::Boss);upper_hud_boss(hp,hp);check_separation();
        }
    }
    // Re-enter maximum-HP mode with the same timer, including invalidation
    // after cinematic OAM hiding. Preserve the cache and clean every old cell.
    upper_hud_invalidate();upper_hud_render(12,5,100,UpperHudMode::Boss);upper_hud_boss(18,18);
    for(int x=26;x<28;++x) for(int y=0;y<2;++y) assert(!text_map[y*32+x]);
    const auto idle_fills = test_dma_fill_bytes, idle_oam = test_oam_changes;
    for (int i = 0; i < 120; ++i) {
        upper_hud_render(12, 5, 100, UpperHudMode::Boss); upper_hud_boss(18, 18);
    }
    assert(idle_fills == test_dma_fill_bytes && idle_oam == test_oam_changes);
    for (int y = 0; y < 24; ++y) {
        int visible = 0;
        for (int slot = 48; slot <= 77; ++slot)
            visible += !test_objs[slot].hidden && y >= test_objs[slot].y && y < test_objs[slot].y +
                (slot >= OAM_SLOT_HUD_HP && slot < OAM_SLOT_HUD_HP + OAM_HUD_HP_COUNT ? 32 : 16);
        assert(visible <= 32);
    }
    const u16 color = SPRITE_PALETTE[OBJ_PAL_OVERLAY * 16 + 1];
    palette_dim(40); palette_restore(); assert(SPRITE_PALETTE[OBJ_PAL_OVERLAY * 16 + 1] == color);
    video_canvas_foreground_begin(); video_canvas_foreground_begin();
    assert(REG_WININ == 0x1D1D && test_bgs[0].priority == 1);
    video_canvas_foreground_end(); assert(REG_WININ == 0x1D1D);
    video_canvas_foreground_end(); assert(REG_WININ == 0x1919 && test_bgs[0].priority == 0);
    foreground_frames = covered_hud_frames = title_frames = 0; test_vblank_hook = observe_foreground;
    stage_num_animate(1, true);
    test_vblank_hook = nullptr;
    assert(foreground_frames == 118 && covered_hud_frames == 48 && title_frames == 48);
    assert(test_bgs[2].sx == 256 && test_bgs[2].sy == 256 && test_bgs[2].x == 0 && test_bgs[2].y == 0);
    assert(!test_objs[OAM_SLOT_HUD_LIVES].hidden && !test_objs[OAM_SLOT_HUD_HP].hidden);
    assert(text_map[64] == divider && REG_WININ == 0x1919 && test_bgs[0].priority == 0);
    assert(test_objs[OAM_SLOT_HUD_LIVES].priority == 0 && test_objs[OAM_SLOT_HUD_HP].priority == 0);
    // The same blocking banner path must publish each phase when BG2 is RAM
    // buffered, rather than leave the previously displayed laser frame live.
    static u16 buffered_canvas[256*256];
    assert(canvas_buffer_begin(buffered_canvas));
    foreground_frames = covered_hud_frames = 0;test_vblank_hook=observe_foreground;
    harryup_animate();test_vblank_hook=nullptr;
    assert(foreground_frames==54 && !canvas_buffer_ready());
    canvas_buffer_prepare();REG_VCOUNT=192;assert(canvas_buffer_present());
    canvas_buffer_end();
    assert(ink_count()==0 && REG_WININ==0x1919);
    foreground_frames = covered_hud_frames = 0; test_vblank_hook = observe_foreground;
    test_keys_down = KEY_B;
    assert(boss_defeat_and_select_route() == ROUTE_MAKAI);
    test_keys_down = 0; test_vblank_hook = nullptr;
    assert(covered_hud_frames > 0 && REG_WININ == 0x1919 && test_bgs[0].priority == 0);
    assert(test_objs[OAM_SLOT_HUD_HP].priority == 0);
    check_background(read_bytes("nitro:/boss1.bin"));

    // Bottom HUD: one static frame, changed fields only, zero idle writes.
    hud_invalidate(); sub_calls.clear(); hud_render(3, 1);
    assert(sub_calls.front().type == 'C');
    sub_calls.clear();
    for (int frame = 0; frame < 120; ++frame) hud_render(3, 1);
    assert(sub_calls.empty());
    stage_score += 10; hud_render(3, 1);
    assert(sub_calls.size() == 2 && sub_calls[0].type == 'F' && sub_calls[0].w == 56 && sub_calls[1].type == 'N');
    sub_calls.clear(); stage_cardcombo_cur = 0; stage_cardcombo_max = 0; hud_render(3, 1); sub_calls.clear();
    stage_cardcombo_cur = 12; hud_render(3, 1);
    assert(sub_calls.size() == 2 && sub_calls[0].w == 24);
    sub_calls.clear(); hud_render(3, 6); assert(sub_calls.size() == 2 && sub_calls[0].w == 32 && sub_calls[1].type=='W');
    sub_calls.clear(); rank = RANK_LUNATIC; hud_render(3, 6);
    assert(sub_calls.size() == 2 && sub_calls[0].w == 112 && sub_calls[1].type == 'L');
    sub_calls.clear(); hud_render(3, 6); assert(sub_calls.empty());
    hud_invalidate(); hud_render(3, 6); assert(sub_calls.front().type == 'C');
    check_bottom_hud();

    // BG2 only erases tiles touched by the previous frame, not all 98KB.
    canvas_clear(); canvas_playfield_clip(true);
    canvas_fill_rect(10, 40, 4, 4, 0x7FFF);
    const auto before_clear = test_dma_fill_bytes;
    canvas_begin_frame(); assert(ink_count() == 0);
    assert(test_dma_fill_bytes - before_clear == 8 * 8 * sizeof(u16));
    const auto empty = test_dma_fill_bytes;
    canvas_begin_frame(); assert(test_dma_fill_bytes == empty);
    canvas_playfield_clip(true); canvas_clear();
    canvas_fill_rect(-2, 20, 8, 8, 0x7FFF); assert(*canvas_at(0,23) == 0 && *canvas_at(0,24) == 0xFFFF);
    const auto obj_used = test_obj_used;
    check_white_projectiles();
    check_ofuda_rendering();
    check_portal_rendering();
    assert(test_obj_used == obj_used);
    assert(test_obj_attempts == sprite_attempts && test_obj_count == 79 && test_obj_used == 27840);
    check_sprite_lifecycles();
    check_source_menus();
    test_stage_entrances();
    test_konngara_review();
    test_stage_bonus_awards();
    test_konngara_stage20();
    test_player_death();
    test_bombs();
    assert(test_vram.before == 0xDEADBEEF && test_vram.after == 0xDEADBEEF);
    assert(test_sub_vram.before==0xC0FFEE11 && test_sub_vram.after==0xC0FFEE11);
    std::puts("Viewport: center-anchored parallax/boss 1:1, native halfwidth timer, six-life/five-bomb and18HP ink separation, cached HUD handoffs, fullscreen transitions, HURRY UP and unchanged gameplay passed");
}
