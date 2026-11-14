#include <nds.h>

#include "hud_render.hpp"
#include "sound_effects.hpp"
#include "subscreen.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"

#include "main/stage/stageobj.hpp"
#include "main/stage/item.hpp"
#include "main/stage/timer.hpp"
#include "rank.h"
#include "main/player/player.hpp"
#include "main/player/orb.hpp"
#include <cstring>

#include "fonts/font8x16.h"
#include "font16x16.hpp"

// Mockup layout: compact statistics down the left, small upper-right radar,
// native fullwidth stage/rank below. The source-art pattern stays unchanged.

static const char *rank_name()
{
    static const char *names[] = { "EASY", "NORMAL", "HARD", "LUNATIC" };
    int r = (int)rank;
    return (r >= 0 && r < 4) ? names[r] : "?";
}

namespace {
bool initialized;
bool radar_valid;
struct HudValues { int score, hi, combo, maximum, stage, rank; } previous;

void number_if_changed(int x, int y, int value, int old, int digits, bool force)
{
    if (!force && value == old) return;
    subscreen_fill_rect(x, y, digits * 8, 16, SUB_COL_BG);
    subscreen_number(x, y, value, digits, SUB_COL_FG);
}
}

void hud_invalidate() { initialized = false;radar_valid=false; }

void hud_render(int /*lives*/, int stage)
{
    const bool force = !initialized;
    const HudValues now{stage_score, hiscore, stage_cardcombo_cur, stage_cardcombo_max, stage, int(rank)};
    if (force) {
        subscreen_clear(SUB_COL_BG);
        // Static frame and labels are drawn once, not every game frame.
        subscreen_fill_rect(HUD_RADAR_LEFT-1,HUD_RADAR_TOP-1,HUD_RADAR_W+2,1,SUB_COL_ACCENT);
        subscreen_fill_rect(HUD_RADAR_LEFT-1,HUD_RADAR_TOP+HUD_RADAR_H,HUD_RADAR_W+2,1,SUB_COL_ACCENT);
        subscreen_fill_rect(HUD_RADAR_LEFT-1,HUD_RADAR_TOP,1,HUD_RADAR_H,SUB_COL_ACCENT);
        subscreen_fill_rect(HUD_RADAR_LEFT+HUD_RADAR_W,HUD_RADAR_TOP,1,HUD_RADAR_H,SUB_COL_ACCENT);
        subscreen_text(8,16,"SCORE",SUB_COL_DIM);
        subscreen_text(8,56,"HI SCORE",SUB_COL_DIM);
        subscreen_text(8,96,"COMBO",SUB_COL_DIM);
        subscreen_text(8,136,"MAX COMBO",SUB_COL_DIM);
        subscreen_fullwidth_text(104,112,"STAGE",SUB_COL_DIM);
        subscreen_fullwidth_text(104,144,"RANK",SUB_COL_DIM);
    }
    number_if_changed(24,32,now.score,previous.score,7,force);
    number_if_changed(24,72,now.hi,previous.hi,7,force);
    number_if_changed(56,112,now.combo,previous.combo,3,force);
    number_if_changed(56,152,now.maximum,previous.maximum,3,force);
    if (force || now.stage != previous.stage) {
        subscreen_fill_rect(200,128,32,16,SUB_COL_BG);
        subscreen_fullwidth_number(200,128,now.stage,2,SUB_COL_FG);
    }
    if (force || now.rank != previous.rank) {
        subscreen_fill_rect(136,160,112,16,SUB_COL_BG);
        subscreen_fullwidth_text(136,160,rank_name(),SUB_COL_FG);
    }
    previous = now;
    initialized = true;
}

namespace {
enum RadarShape { R_NONE,R_CARD,R_CROSS,R_BAR,R_PORTAL,R_POINT,R_BOMB,R_BOSS,R_PLAYER,R_ORB };
struct RadarMark { int16_t x,y;u8 w,h,shape,color; };
static_assert(sizeof(RadarMark)==8,"radar snapshots must stay compact");
constexpr int item_first=STAGE_MAX_CELLS*2,boss_slot=item_first+ITEM_BOMB_COUNT+ITEM_POINT_COUNT;
constexpr int player_slot=boss_slot+1,orb_slot=boss_slot+2,mark_count=orb_slot+1;
RadarMark previous_marks[mark_count],marks[mark_count];
u32 radar_dirty[(HUD_RADAR_H+7)/8];

int radar_x(int x) { return (view_clamp(x,PLAYFIELD_LEFT,PLAYFIELD_RIGHT-1)-PLAYFIELD_LEFT)*(HUD_RADAR_W-1)/(PLAYFIELD_W-1); }
int radar_y(int y) { return (view_clamp(y,PLAYFIELD_TOP,PLAYFIELD_BOTTOM-1)-PLAYFIELD_TOP)*(HUD_RADAR_H-1)/(PLAYFIELD_H-1); }

RadarMark marker(int x,int y,int w,int h,RadarShape shape,u8 color)
{
    return {int16_t(radar_x(x)-w/2),int16_t(radar_y(y)-h/2),u8(w),u8(h),u8(shape),color};
}

void dirty_mark(const RadarMark &m)
{
    if(!m.shape) return;
    const int left=view_clamp(m.x,0,HUD_RADAR_W),right=view_clamp(m.x+m.w,0,HUD_RADAR_W);
    const int top=view_clamp(m.y,0,HUD_RADAR_H),bottom=view_clamp(m.y+m.h,0,HUD_RADAR_H);
    if(right<=left || bottom<=top) return;
    for(int y=top/8;y<=(bottom-1)/8;++y) for(int x=left/8;x<=(right-1)/8;++x)
        radar_dirty[y]|=u32(1)<<x;
}

bool mark_ink(const RadarMark &m,int x,int y)
{
    const int dx=x-int(m.w)/2,dy=y-int(m.h)/2;
    const int distance=(dx<0?-dx:dx)+(dy<0?-dy:dy);
    switch(m.shape) {
    case R_CARD:case R_BOSS: return x==0 || y==0 || x==m.w-1 || y==m.h-1;
    case R_CROSS: return dx==0 || dy==0;
    case R_PORTAL: return distance==2;
    case R_POINT: return distance<=1;
    case R_PLAYER: return (dx<0?-dx:dx)<=y/2;
    case R_ORB: return dx*dx+dy*dy<=1;
    default: return true;
    }
}
}

void hud_radar_render(const HudRadarBoss &boss)
{
    std::memset(marks,0,sizeof(marks));
    for(int i=0;i<stage_card_total && i<STAGE_MAX_CELLS;++i) {
        const auto &c=stage_cards[i];
        if(c.flag!=CARD_REMOVED) marks[i]=marker(c.left+16,c.top+16,5,3,R_CARD,SUB_COL_RADAR_CARD);
    }
    for(int i=0;i<stage_obstacle_total && i<STAGE_MAX_CELLS;++i) {
        const auto &o=stage_obstacles[i];
        auto &m=marks[STAGE_MAX_CELLS+i];
        m=marker(o.left+16,o.top+16,3,3,R_CROSS,SUB_COL_RADAR_OBSTACLE);
        if(o.type==OT_PORTAL) m=marker(o.left+16,o.top+16,5,5,R_PORTAL,SUB_COL_RADAR_PORTAL);
        else if(o.type>=OT_BAR_TOP && o.type<=OT_BAR_RIGHT) {
            m=marker(o.left+16,o.top+16,8,8,R_BAR,SUB_COL_RADAR_OBSTACLE);
            if(o.type==OT_BAR_TOP) m.h=2;
            if(o.type==OT_BAR_BOTTOM) { m.y+=6;m.h=2; }
            if(o.type==OT_BAR_LEFT) m.w=2;
            if(o.type==OT_BAR_RIGHT) { m.x+=6;m.w=2; }
        } else if(o.type==OT_NONE) m={};
    }
    for(int i=0;i<ITEM_BOMB_COUNT;++i) if(CItems::is_visible(Items.bomb[i]))
        marks[item_first+i]=marker(Items.bomb[i].left+16,Items.bomb[i].top+16,3,3,R_BOMB,SUB_COL_RADAR_ITEM);
    for(int i=0;i<ITEM_POINT_COUNT;++i) if(CItems::is_visible(Items.point[i]))
        marks[item_first+ITEM_BOMB_COUNT+i]=marker(Items.point[i].left+16,Items.point[i].top+16,3,3,R_POINT,SUB_COL_RADAR_ITEM);
    if(boss.visible) marks[boss_slot]=marker(boss.x,boss.y,5,3,R_BOSS,SUB_COL_RADAR_BOSS);
    const auto &player=player_get_state();const auto &orb=orb_get_state();
    marks[player_slot]=marker(player.x_position+PLAYER_W/2,player.y_position+PLAYER_H/2,3,3,R_PLAYER,SUB_COL_RADAR_PLAYER);
    marks[orb_slot]=marker(orb.x_position+ORB_W/2,orb.y_position+ORB_H/2,3,3,R_ORB,SUB_COL_FG);
    bool changed=!radar_valid;
    std::memset(radar_dirty,0,sizeof(radar_dirty));
    if(!radar_valid) for(auto &row:radar_dirty) row=(u32(1)<<(HUD_RADAR_W/8))-1;
    else for(int i=0;i<mark_count;++i) if(std::memcmp(&marks[i],&previous_marks[i],sizeof(RadarMark))) {
        dirty_mark(previous_marks[i]);dirty_mark(marks[i]);changed=true;
    }
    if(!changed) return;
    u16 active[mark_count];int active_count=0;
    for(int i=0;i<mark_count;++i) if(marks[i].shape) active[active_count++]=i;
    for(int row=0;row<int(sizeof(radar_dirty)/sizeof(*radar_dirty));++row) for(int col=0;col<HUD_RADAR_W/8;++col) {
        if(!(radar_dirty[row]&(u32(1)<<col))) continue;
        const int left=col*8,top=row*8,height=top+8>HUD_RADAR_H ? HUD_RADAR_H-top : 8;
        u8 tile[64];std::memset(tile,SUB_COL_RADAR_BG,sizeof(tile));
        for(int i=0;i<active_count;++i) {
            const auto &m=marks[active[i]];
            if(m.x<left+8 && m.x+m.w>left && m.y<top+height && m.y+m.h>top)
            for(int y=0;y<height;++y) for(int x=0;x<8;++x) {
                const int mx=left+x-m.x,my=top+y-m.y;
                if(mx>=0 && mx<m.w && my>=0 && my<m.h && mark_ink(m,mx,my)) tile[y*8+x]=m.color;
            }
        }
        subscreen_put_indexed(HUD_RADAR_LEFT+left,HUD_RADAR_TOP+top,8,height,tile,0);
    }
    std::memcpy(previous_marks,marks,sizeof(marks));radar_valid=true;
}

// ReC98's banners (harryup_animate, stage_num_animate): blocking two-phase
// giant letters over the playfield, with fixed pauses between phases. The
// original's "HARRY UP" typo is fixed per the quirk policy (docs/
// ds-port-plan.md section 5).

// PC-98 glyphs carry 2-3 columns of left padding, so text centered by
// advance width sits visibly right of center. Center on the ink instead.
static int ink_span(const char *s, int scale, int &left_pad)
{
    int min_x = 8, max_x = -1, i = 0;
    for (; *s; s++, i++) {
        const unsigned char *glyph = font8x16[(unsigned char)*s - 0x20];
        for (int y = 0; y < 16; y++) {
            unsigned char bits = glyph[y];
            for (int x = 0; x < 8; x++) {
                if (bits & (0x80 >> x)) {
                    int gx = i * 8 + x;
                    if (gx < min_x)
                        min_x = gx;
                    if (gx > max_x)
                        max_x = gx;
                }
            }
        }
    }
    left_pad = min_x * scale;
    return (max_x - min_x + 1) * scale;
}

static void banner_line(const char *s)
{
    int left_pad;
    int w = ink_span(s, 6, left_pad);
    canvas_text_scaled((CANVAS_W - w) / 2 - left_pad, 48, s, 6, RGB15(31, 0, 0));
}

void banner_two_phase(const char *line1, const char *line2, int pause_frames)
{
    CanvasForegroundScope foreground;
    canvas_playfield_clip(false);
    canvas_clear_rect(0, UPPER_HUD_H, VIEW_W, VIEW_PLAY_H);
    banner_line(line1);
    for (int i = 0; i < pause_frames; i++) {
        if (i==0 && g_canvas_target) {
            canvas_buffer_prepare();
            do { swiWaitForVBlank(); } while(!canvas_buffer_present());
        } else swiWaitForVBlank();
    }

    canvas_clear_rect(0, UPPER_HUD_H, VIEW_W, VIEW_PLAY_H);
    banner_line(line2);
    for (int i = 0; i < pause_frames; i++) {
        if (i==0 && g_canvas_target) {
            canvas_buffer_prepare();
            do { swiWaitForVBlank(); } while(!canvas_buffer_present());
        } else swiWaitForVBlank();
    }

    canvas_clear_rect(0, UPPER_HUD_H, VIEW_W, VIEW_PLAY_H);
}

void harryup_animate()
{
    mdrv2_se_play(SFX_HURRY_UP);
    banner_two_phase("HURRY", "UP", 27);
}

static void prepare_title_mask(int page, u16 jis)
{
    // BG2's invisible rows 192..255 are already allocated VRAM. Two compact
    // 32x24 masks fit side by side there. Hardware magnification turns each
    // source pixel into an 8x8 screen block, with a centered 16x16 glyph.
    // Prepare only the INACTIVE mask; never black out the displayed glyph.
    const u16 *glyph = font16x16_find(jis);
    for (int y = 0; y < 24; ++y) {
        u16 *out = canvas_at(page * 32, CANVAS_H + y);
        for (int x = 0; x < 32; ++x) {
            const bool ink = glyph && x >= 8 && x < 24 && y >= 4 && y < 20 &&
                (glyph[y - 4] & (0x8000 >> (x - 8)));
            out[x] = ink ? 0 : 0x8000;
        }
    }
}

static void touhou_reiiden_animate()
{
    // Original: each 16x16 CG-ROM glyph is expanded to 256x256 in PC-98
    // TRAM. The DS backend is 50%, so scale 8 reproduces its 128x128 size.
    // Reverse-video spaces mask the screen while the glyph's ink acts as a
    // cut-out to the stage beneath; on BG2, opaque black + transparent glyph
    // pixels reproduces that composition directly.
    // Repainting a full visible black field before punching the next window
    // could expose the reported blank fields even with valid glyphs. Flip a small
    // affine source window instead: each complete mask follows the last one
    // directly at the VBlank ending its 8-frame hold. No DMA/cache or RAM buffer.
    prepare_title_mask(0, JIS_TOUHOU_REIIDEN[0]);
    bgSetScale(g_bg_id_canvas, 32, 32); // inverse scale: 256/8
    for (int i = 0; i < 6; i++) {
        bgSetScroll(g_bg_id_canvas, (i % 2) * 32, CANVAS_H);
        bgUpdate();
        mdrv2_se_play(SFX_TITLE_GLYPH);
        if (i + 1 < 6)
            prepare_title_mask((i + 1) % 2, JIS_TOUHOU_REIIDEN[i + 1]);
        for (int frame = 0; frame < 8; frame++)
            swiWaitForVBlank();
    }
    // Clear normal effects while they are still offscreen, then restore the
    // usual 1:1 canvas before STAGE text. Scenery/camera/HUD layers never move.
    canvas_clear();
    bgSetScale(g_bg_id_canvas, 256, 256);
    bgSetScroll(g_bg_id_canvas, 0, 0);
    bgUpdate();
}

void stage_num_animate(int stage_num, bool show_title)
{
    CanvasForegroundScope foreground;
    canvas_playfield_clip(false);
    if (show_title) {
        // Scene entrances in ReC98 contain only the background/stage field.
        // Suppress stale OAM from the previous scene while the BG2 windows
        // reveal the layers underneath; later stage banners intentionally
        // retain the player, matching the original's alternate path.
        // Hide gameplay only, not the fixed HUD icons/HP OAM at slots48+.
        for (int slot = 0; slot <= 40; ++slot) oamSetHidden(&oamMain, slot, true);
        oamUpdate(&oamMain);
        touhou_reiiden_animate();
    }
    char digits[3];
    digits[0] = '0' + ((stage_num / 10) % 10);
    digits[1] = '0' + (stage_num % 10);
    digits[2] = '\0';
    banner_two_phase("STAGE", digits, 35);
}
