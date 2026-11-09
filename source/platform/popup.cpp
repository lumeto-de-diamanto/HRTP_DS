#include <stdio.h>

#include "popup.hpp"
#include "canvas.hpp"
#include "camera.hpp"

// ReC98 shows flip/collect scores as bold text that rises and fades above
// the scoring object. The world is displayed at 50%, so the original's
// 8x16 popups render as 4x8 here.

#define POPUP_COUNT 16
#define POPUP_LIFE 30 // frames
#define POPUP_RISE_DIV 2

struct Popup {
    bool alive;
    int x, y;      // world coordinates; projected anew as the camera moves
    int value;
    int age;
};

static Popup popups[POPUP_COUNT];

void popup_clear()
{
    for (auto &popup : popups) popup = {};
}

void popup_add(int world_x, int world_y, int value)
{
    for (int i = 0; i < POPUP_COUNT; i++) {
        if (popups[i].alive)
            continue;
        popups[i].alive = true;
        popups[i].x = world_x;
        popups[i].y = world_y;
        popups[i].value = value;
        popups[i].age = 0;
        return;
    }
}

void popup_render_frame()
{
    for (int i = 0; i < POPUP_COUNT; i++) {
        Popup &pu = popups[i];
        if (!pu.alive)
            continue;

        if (pu.age >= POPUP_LIFE) {
            pu.alive = false;
            continue;
        }

        char buf[12];
        int digits = 0;
        int v = pu.value;
        do {
            buf[digits++] = '0' + (v % 10);
            v /= 10;
        } while (v && digits < 11);
        for (int a = 0, b = digits - 1; a < b; a++, b--) {
            char t = buf[a];
            buf[a] = buf[b];
            buf[b] = t;
        }
        buf[digits] = '\0';

        int x = camera.x(pu.x) - (digits * 4) / 2;
        int y = camera.y(pu.y) - (pu.age / POPUP_RISE_DIV);

        canvas_text_3x5(x, y, buf, 0x7FFF);

        pu.age++;
    }
}
