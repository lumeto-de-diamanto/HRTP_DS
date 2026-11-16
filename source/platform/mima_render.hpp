#pragma once
#include <cstddef>
#include <nds.h>

constexpr int MIMA_FRAME_BYTES = 3072, MIMA_FRAME_COUNT = 47;
constexpr int MIMA_STILL_FRAME = 41, MIMA_CAST_FRAME = 42, MIMA_METEOR_FRAME = 43;
constexpr int MIMA_BASE_LEFT = 256, MIMA_BASE_TOP = 120;
bool mima_art_asset_valid(const void *data, size_t size);

// Review-only owner of immutable art; shares the permanent4KiB boss OBJ cache.
// Caller ends Konngara before begin(). No per-frame allocation/decoding/I/O.
class MimaView {
    u8 *data_ = nullptr;
    int last_frame_ = -1;
public:
    ~MimaView() { end(); }
    MimaView() = default;
    MimaView(const MimaView &) = delete;
    MimaView &operator=(const MimaView &) = delete;
    bool begin();
    void end();
    bool frame(int asset_frame, int left=MIMA_BASE_LEFT, int top=MIMA_BASE_TOP,bool hidden=false);
};

// Development-only art/entrance gallery. No combat, HP damage or Stage10 unlock.
// Caller reloads its current stage after return (including after failure).
bool mima_art_review(int lives, int bombs, unsigned timer);
