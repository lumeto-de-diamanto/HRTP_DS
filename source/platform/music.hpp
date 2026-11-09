#pragma once

#include "native_catalog.h"

// Native MDT-tick BGM lifecycle. All soundtrack entries are available for
// forthcoming title/boss/ending flows; current regular stages select by scene.
bool music_init();
bool music_play_track(NativeTrackId track);
void music_play_scene(int scene);
void music_pause();
void music_resume();
void music_stop();
const char *music_error();
