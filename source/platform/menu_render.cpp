#include <nds.h>

#include "menu_render.hpp"
#include "text_layer.hpp"
#include "palette_fx.hpp"

// Blocking pause and stage-bonus screens, drawn over the dimmed playfield
// on the top screen (BG0 text layer) -- same presentation as ReC98's
// th01/main/hud/menu.cpp (pause_menu: PAUSE -> choices, QUIT ->
// confirmation) and th01/main/bonus.cpp (stagebonus_animate), with the
// 8x16 ANK font standing in for the original's fullwidth characters.

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

// Draws a dimmed pause screen and waits; returns true if QUIT was
// confirmed. (No exit from the game loop exists -- quitting restarts at
// the first stage until the title screen arrives.)
static bool pause_confirm_quit(void);

bool pause_menu_run(void)
{
    palette_dim(40);
    text_layer_clear();
    menu_text(13, 3, "PAUSE", TEXT_PAL_WHITE);

    int sel = 0;
    bool quit = false;
    while (1) {
        menu_choice(5, "RESUME", sel == 0);
        menu_choice(6, "QUIT", sel == 1);

        while (1) {
            swiWaitForVBlank();
            scanKeys();
            uint16_t down = keysDown();
            if (down & KEY_UP) {
                sel = (sel + 1) % 2;
                break;
            }
            if (down & KEY_DOWN) {
                sel = (sel + 1) % 2;
                break;
            }
            if (down & (KEY_A | KEY_X)) {
                if (sel == 0) {
                    text_layer_clear();
                    palette_restore();
                    return false;
                }
                quit = pause_confirm_quit();
                if (quit) {
                    text_layer_clear();
                    palette_restore();
                    return true;
                }
                // Back from the confirmation: redraw the pause menu.
                text_layer_clear();
                menu_text(13, 3, "PAUSE", TEXT_PAL_WHITE);
                break;
            }
            if (down & KEY_B) {
                text_layer_clear();
                palette_restore();
                return false;
            }
        }
    }
}

static bool pause_confirm_quit(void)
{
    text_layer_clear();
    menu_text(11, 3, "QUIT GAME?", TEXT_PAL_WHITE);

    int sel = 1; // default NO
    while (1) {
        menu_choice(5, "YES", sel == 0);
        menu_choice(6, "NO", sel == 1);

        while (1) {
            swiWaitForVBlank();
            scanKeys();
            uint16_t down = keysDown();
            if (down & (KEY_UP | KEY_DOWN)) {
                sel = (sel + 1) % 2;
                break;
            }
            if (down & (KEY_A | KEY_X))
                return (sel == 0);
            if (down & KEY_B)
                return false;
        }
    }
}

void stagebonus_run(const StageBonus &bonus, int stage_num)
{
    palette_dim(40);
    text_layer_clear();

    char title[20];
    // "Stage n clear!" -- lowercase looks better in 8x16 (the original
    // uses 16x16 fullwidth graphics here).
    title[0] = 'S'; title[1] = 't'; title[2] = 'a'; title[3] = 'g';
    title[4] = 'e'; title[5] = ' ';
    title[6] = '0' + (stage_num / 10);
    title[7] = '0' + (stage_num % 10);
    title[8] = ' '; title[9] = 'c'; title[10] = 'l'; title[11] = 'e';
    title[12] = 'a'; title[13] = 'r'; title[14] = '!'; title[15] = '\0';
    menu_text(8, 2, title, TEXT_PAL_WHITE);

    menu_text(8, 4, "Time", TEXT_PAL_RED);
    text_layer_number(24, 4, bonus.time, 4, TEXT_PAL_WHITE);
    menu_text(8, 5, "Combo", TEXT_PAL_RED);
    text_layer_number(24, 5, bonus.combo, 4, TEXT_PAL_WHITE);
    menu_text(8, 6, "Miko", TEXT_PAL_RED);
    text_layer_number(24, 6, bonus.resources, 4, TEXT_PAL_WHITE);
    menu_text(8, 7, "Stage", TEXT_PAL_RED);
    text_layer_number(24, 7, bonus.stage, 4, TEXT_PAL_WHITE);

    menu_text(8, 9, "Total", TEXT_PAL_WHITE);
    text_layer_number(22, 9, bonus.total, 6, TEXT_PAL_RED);
    menu_text(17, 11, "press A", TEXT_PAL_RED);

    while (1) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & (KEY_A | KEY_B | KEY_X))
            break;
    }

    text_layer_clear();
    palette_restore();
}
