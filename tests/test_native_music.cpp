// Tests the actual gameplay music layer with a fake acknowledged ARM9 client.
#include "music.hpp"
#include "sound_effects.hpp"
#include "client.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using namespace native_audio;
    assert(music_init());
    assert(sfx_init());
    assert(calls.back() == "nitro:/audio/sfx.bin");
    calls.clear();
    mdrv2_se_play(SFX_SHOT); mdrv2_se_play(SFX_PICKUP);
    assert(calls.size() == 2 && calls[0] == "sfx:1" && calls[1] == "sfx:15");
    mdrv2_se_play(0); mdrv2_se_play(18);
    assert(calls.size() == 2);
    sfx_stop(); assert(calls.back() == "sfx:stop");
    calls.clear();
    music_play_scene(0);
    assert(calls.size() == 2 && calls[0] == "nitro:/audio/native/st0.nsq" && calls[1] == "play");
    calls.clear();
    music_play_scene(0); // adjacent regular stages retain the running song
    assert(calls.empty());
    music_pause(); music_pause();
    assert(calls.size() == 1 && calls[0] == std::to_string(NS_PAUSE));
    music_play_scene(0); // an idempotent play also resumes a paused scene
    assert(calls.size() == 2 && calls[1] == std::to_string(NS_RESUME));
    music_stop(); calls.clear(); music_play_scene(0);
    assert(calls.size() == 1 && calls[0] == "play"); // game-over restarts without reloading PCM
    calls.clear(); music_play_scene(1);
    assert(calls.size() == 2 && calls[0] == "nitro:/audio/native/st1.nsq");
    calls.clear(); music_play_scene(6);
    assert(calls.size() == 2 && calls[0] == "nitro:/audio/native/st0.nsq");
    for (int track = 0; track < NATIVE_TRACK_COUNT; ++track)
        assert(music_play_track(NativeTrackId(track)));
    music_stop();
    loadOK = false;
    assert(!music_play_track(NATIVE_ST1));
    assert(music_error()[0]);
    loadOK = true;
    assert(music_play_track(NATIVE_ST1));
    assert(!music_error()[0]);
    sendOK = false;
    music_pause();
    assert(music_error()[0]);
    sendOK = true;
    music_play_scene(-1); // invalid scenes stop, not an out-of-bounds catalog read
    assert(calls.back() == "stop");
    const auto dropped = sfx_dropped_requests();
    sendOK = false; mdrv2_se_play(SFX_CARD); sfx_stop(); sendOK = true;
    assert(sfx_dropped_requests() == dropped + 2);
    std::puts("Gameplay native music: all tracks, scene alias, continuity, restart, pause/resume and failure checks passed");
}
