#include <nds.h>

#include "menu_render.hpp"
#include "text_layer.hpp"
#include "palette_fx.hpp"

namespace {
// Source JIS, not captured font artwork: every pixel still comes from the
// user's selected font.bmp. Full-size pause JIS and native ANK results fit the
//256px viewport; all modal ink stays below the approved fixed upper HUD.
const u16 resume_label[]={0x3a46,0x332b}; // 再開
const u16 quit_label[]={0x3d2a,0x4e3b}; // 終了
const u16 quit_title[]={0x4b5c,0x4576,0x244b,0x3d2a,0x4e3b,0x2437,0x2441,0x2463,0x2426,0x244e,0x2129};
const u16 quit_no[]={0x2426,0x243d,0x2447,0x2439,0x2425}; // うそですぅ
const u16 quit_yes[]={0x244f,0x2424,0x2443}; // はいっ

template<size_t N> void jis_text(int x,int y,const u16 (&text)[N],int weight=3)
{
    for(unsigned i=0;i<N;++i) text_layer_modal_glyph(x+i*16,y,text[i],TEXT_MODAL_WHITE,weight);
}

void result_number(int x,int y,int value,int digits)
{
    char text[7];text[digits]='\0';
    for(int i=digits-1;i>=0;--i) { text[i]='0'+value%10;value/=10; }
    text_layer_modal_text(x,y,text,TEXT_MODAL_WHITE);
}

void menu_vblank()
{
    do { swiWaitForVBlank(); } while(REG_VCOUNT<192 || REG_VCOUNT>=200);
}

void pause_cursor(bool confirmation,int selection)
{
    const int first=confirmation ? 36 : 68,second=confirmation ? 148 : 132;
    text_layer_modal_clear_rect(first,72,16,16);
    text_layer_modal_clear_rect(second,72,16,16);
    text_layer_modal_glyph(selection ? second : first,72,0x217c,TEXT_MODAL_WHITE,0);
}

//0=first option,1=second, -1=Esc/START. Original navigation is horizontal;
// B is source Z/shot, A is the DS menu-confirm alternative to Return.
int pause_select(bool confirmation)
{
    int selection=0;
    pause_cursor(confirmation,selection);
    while(true) {
        menu_vblank();scanKeys();
        const u16 down=keysDown(),directions=(keysHeld()|down)&(KEY_LEFT|KEY_RIGHT);
        const int next=directions==KEY_LEFT ? 0 : directions==KEY_RIGHT ? 1 : selection;
        if(next!=selection) { selection=next;pause_cursor(confirmation,selection); }
        if(down&KEY_START) return -1;
        if(down&(KEY_A|KEY_B)) return selection;
    }
}
}

static void menu_text(int gx, int gy, const char *s, int bank)
{
    text_layer_puts(gx, gy, s, bank);
}

static void menu_choice(int gy, const char *s, bool selected)
{
    // Cursor column 8, label from column 10, centered-ish layout.
    menu_text(8, gy, selected ? ">" : " ", TEXT_PAL_RED);
    menu_text(10, gy, s, selected ? TEXT_PAL_WHITE : TEXT_PAL_RED);
}

bool pause_menu_run(void)
{
    palette_dim(40);
    menu_vblank();text_layer_modal_begin();
    // Pause title/choices use source BLACK weight, not its stage-bonus BOLD.
    for(int i=0;i<5;++i) text_layer_modal_glyph(84+i*16,56,0x2300+"PAUSE"[i],TEXT_MODAL_WHITE,3);
    jis_text(84,72,resume_label);jis_text(148,72,quit_label);
    const int choice=pause_select(false);
    text_layer_clear();
    for(int i=0;i<20;++i) menu_vblank(); // source blank, dimmed selection delay
    bool quit=false;
    if(choice==1) {
        text_layer_modal_begin(); // discard old glyph pixels, not just map entries
        jis_text(36,56,quit_title);
        jis_text(52,72,quit_no);jis_text(164,72,quit_yes);
        quit=pause_select(true)==1;
    }
    text_layer_clear();palette_restore();return quit;
}

bool gameover_menu_run()
{
    palette_dim(40);
    text_layer_clear();
    menu_text(11,3,"GAME OVER",TEXT_PAL_WHITE);
    menu_text(3,10,"A/START: CHOOSE  B: NEW GAME",TEXT_PAL_RED);
    int selected=0;
    bool released=false;
    while(true) {
        menu_choice(5,"RETRY STAGE",selected==0);
        menu_choice(6,"NEW GAME",selected==1);
        swiWaitForVBlank();scanKeys();
        const uint16_t held=keysHeld(),down=keysDown();
        if(!(held&(KEY_A|KEY_B|KEY_START))) released=true;
        if(!released) continue; // a held attack must not auto-select a new run
        if(down&(KEY_UP|KEY_DOWN)) selected=1-selected;
        if(down&(KEY_A|KEY_B|KEY_START)) {
            const bool retry=!(down&KEY_B) && selected==0;
            text_layer_clear();palette_restore();return retry;
        }
    }
}

void stagebonus_run(const StageBonus &bonus, int stage_num)
{
    palette_dim(100); // source restores the normal palette, not pause dimming
    menu_vblank();text_layer_modal_begin();
    const u16 control=REG_BLDCNT,alpha=REG_BLDALPHA,window=REG_WINOUT;
    // BG0 remains in front of BG2/OBJ.16+8 dims the scene under black by50%
    // while saturating white lettering stays fully bright. The HUD windows
    // disable effects, so its existing BG0 timer/divider are not blended.
    REG_BLDCNT=BLEND_ALPHA|BLEND_SRC_BG0|BLEND_DST_BG1|BLEND_DST_BG2|
        BLEND_DST_BG3|BLEND_DST_SPRITE|BLEND_DST_BACKDROP;
    REG_BLDALPHA=16|(8<<8);REG_WINOUT=window|0x20;
    // Keep the24-frame centre-out reveal, but fill a continuous solid panel.
    // Only newly exposed rows are written; no full-canvas blit or new OAM.
    int previous_top=108,previous_bottom=108;
    for(int frame=1;frame<=24;++frame) {
        const int half_height=(84*frame+23)/24,top=108-half_height,bottom=108+half_height;
        text_layer_modal_fill_rect(16,top,224,previous_top-top,TEXT_MODAL_BLACK);
        text_layer_modal_fill_rect(16,previous_bottom,224,bottom-previous_bottom,TEXT_MODAL_BLACK);
        previous_top=top;previous_bottom=bottom;
        menu_vblank();
    }
    char title[]="STAGE 00 COMPLETE";
    title[6]='0'+stage_num/10;title[7]='0'+stage_num%10;
    text_layer_modal_text(60,32,title,TEXT_MODAL_RED);
    text_layer_modal_text(108,56,"BONUS",TEXT_MODAL_WHITE);
    text_layer_modal_text(32,72,"Time",TEXT_MODAL_WHITE);
    text_layer_modal_text(32,88,"Continuous",TEXT_MODAL_WHITE);
    text_layer_modal_text(32,104,"Bomb&Player",TEXT_MODAL_WHITE);
    text_layer_modal_text(32,120,"STAGE",TEXT_MODAL_WHITE);
    text_layer_modal_text(32,144,"BONUS Point",TEXT_MODAL_WHITE);
    text_layer_modal_text(72,168,"Hit A or B Key",TEXT_MODAL_RED);
    result_number(176,72,bonus.time*10,5);
    result_number(176,88,bonus.combo*10,5);
    result_number(176,104,bonus.resources*10,5);
    result_number(176,120,bonus.stage*10,5);
    result_number(168,144,bonus.total,6);
    // Original input-reset uses held shot/Return; accept their DS press events
    // too, so a fresh press during the reveal can dismiss without auto-firing.
    do { menu_vblank();scanKeys(); } while(!((keysDown()|keysHeld())&(KEY_A|KEY_B)));
    text_layer_clear();REG_BLDCNT=control;REG_BLDALPHA=alpha;REG_WINOUT=window;
    palette_restore();
}
