#pragma once
#include <nds.h>
#include <cstddef>
#include "main/boss/yuugenmagan_opening.hpp"

constexpr int YUUGENMAGAN_FRAME_BYTES = 512;
bool yuugenmagan_art_asset_valid(const void *data,size_t size);
bool yuugenmagan_visual_valid(const YuugenMaganOpeningFrame &state);
void yuugenmagan_lasers_render(unsigned mask);

// Borrows the existing4KiB boss cache and OAM40..44. The first frame uploads
// scenery/cels under native black; later frames only change palette/OAM/BG2.
// Caller reloads scenery after end(), before revealing the next stage.
class YuugenMaganView {
    u8 *data_ = nullptr;
    unsigned signatures_ = 0,sprites_ = 0;
    int recolor_ = -1;
    bool installed_ = false;
    bool flash_ = false;
    bool color15_override_ = false;
    int color15_[3]{};
    u16 saved_obj_palette_[16]{};
    void colors(const YuugenMaganOpeningFrame &state,bool flash);
public:
    YuugenMaganView() = default;
    ~YuugenMaganView() { end(); }
    YuugenMaganView(const YuugenMaganView &) = delete;
    YuugenMaganView &operator=(const YuugenMaganView &) = delete;
    bool begin();
    bool frame(const YuugenMaganOpeningFrame &state,bool flash=false);
    bool sprite_palette(u16 (&out)[16],const YuugenMaganOpeningFrame &state,bool flash) const;
    // Eyes are put after missiles in the source. Remove early BG2 ink behind
    // opaque eye pixels, without reproducing destructive unblitting glitches.
    void cover_eyes(const YuugenMaganOpeningFrame &state) const;
    void end();
};

// Frozen visual inspection only: no attacks, collisions, score, RNG, or AI.
bool yuugenmagan_art_review(int lives,int bombs,unsigned timer);
