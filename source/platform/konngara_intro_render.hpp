#pragma once
#include <stddef.h>

// Validation does not mutate video/audio state. Borrowed input must be aligned
// to a halfword (the file loader's heap buffer is suitably aligned).
bool konngara_intro_asset_valid(const void *data, size_t size);

// Development preview only: plays ALICE before the source entrance, holds the
// final composition for 120 frames, then restores normal BG2/HUD layering.
// Caller restarts its stage/music afterward; combat/progression is not faked.
// Asset/allocation failure leaves hardware/audio untouched. Playback failure
// clears the preview and restores ordinary layering before returning false.
bool konngara_intro_preview();

// Actual Stage20 entrance: no preview-only120-frame hold and no final clear.
// ALICE continues into combat; caller keeps a foreground mask through the first
// coherent combat presentation so the final intro image cannot expose old BG3.
bool konngara_intro_play();
