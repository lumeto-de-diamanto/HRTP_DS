#include "background.hpp"
#include "nds/arm9/background.h"
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>

#include "backgrounds/st0.h"
#include "backgrounds/st1.h"
#include "backgrounds/st2.h"
#include "backgrounds/st3.h"
#include "backgrounds/st4.h"
#include "backgrounds/st5.h"
#include "backgrounds/st6.h"
#include "backgrounds/boss1.h"

#include "file_load.c"
#include "video.hpp"
#include "camera.hpp"
static bool boss_background;

int background_scroll_x() { return camera.background_x(boss_background); }

static const unsigned short *const scene_palettes[7] = {
    st0_palette, st1_palette, st2_palette, st3_palette,
    st4_palette, st5_palette, st6_palette,
};

static const size_t scene_palette_sizes[7] = {
    sizeof(st0_palette), sizeof(st1_palette), sizeof(st2_palette),
    sizeof(st3_palette), sizeof(st4_palette), sizeof(st5_palette),
    sizeof(st6_palette),
};

static void load_named_background(const char *file_name, const unsigned short *palette, size_t palette_size, bool boss) {
    void *bin_buf;
    size_t bin_size;
    if (!file_load(file_name, &bin_buf, &bin_size)) {
        printf("Error opening %s!", file_name);
        return;
    }
    if (bin_size != BACKGROUND_W * VIEW_H) {
        printf("Invalid background size: %s", file_name);
        free(bin_buf);
        return;
    }
    // Palette budget: backgrounds use entries 0..207 only (palette.hpp).
    // Entries 0..15 hold the original GRP colors (the round palette).
    memcpy(BG_PALETTE, palette, palette_size);
    auto *dst = reinterpret_cast<u16 *>(bgGetGfxPtr(g_bg_id_background));
    const auto *src = static_cast<const u8 *>(bin_buf);
    dmaFillHalfWords(0, dst, 512 * 256);
    // CPU halfword stores are safe for both cached RAM and DTCM sources.
    // The old per-row DMA read a stack scratch buffer without cache handling.
    for (int y = 0; y < VIEW_H; ++y)
        for (int x = 0; x < BACKGROUND_W; x += 2)
            dst[(y * 512 + x) / 2] = src[y * BACKGROUND_W + x] |
                (src[y * BACKGROUND_W + x + 1] << 8);
    free(bin_buf);
    boss_background = boss;
    video_apply_camera();
    bgShow(g_bg_id_background);
}

void load_background(int scene) {
    if (scene < 0 || scene > 6)
        scene = 0;
    char file_name[32];
    snprintf(file_name, sizeof(file_name), "nitro:/st%d.bin", scene);
    load_named_background(file_name, scene_palettes[scene], scene_palette_sizes[scene], false);
}

void load_boss_background() {
    load_named_background("nitro:/boss1.bin", boss1_palette, sizeof(boss1_palette), true);
}
