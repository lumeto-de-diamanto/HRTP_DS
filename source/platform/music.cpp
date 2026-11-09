#include <nds.h>
#include <cstdio>

#include "client.hpp"
#include "music.hpp"
#include "sound_effects.hpp"

namespace {

bool initialized;
bool playing;
bool paused;
int loaded_track = -1;
native_audio::Client player;
const char *error_message = "";
bool effects_ready;
uint32_t dropped_effects;

// ReC98's default regular-stage filename is ST<scene>.MDT. ST6.MDT is
// byte-identical to ST0.MDT, so scene 6 intentionally reuses that sequence.
const NativeTrackId scene_tracks[] = {
    NATIVE_ST0, NATIVE_ST1, NATIVE_ST2, NATIVE_ST3,
    NATIVE_ST4, NATIVE_ST5, NATIVE_ST0,
};

bool fail(const char *message)
{
    error_message = message;
    playing = false;
    paused = false;
    return false;
}

}

bool music_init()
{
    if (initialized)
        return true;
    initialized = player.init();
    if (!initialized)
        return fail("Native audio IPC initialization failed");

    return true;
}

void music_stop()
{
    if (initialized && !player.stop())
        error_message = "Native audio stop failed; buffer retained";
    playing = false;
    paused = false;
}

bool music_play_track(NativeTrackId track)
{
    if (!initialized)
        return fail("Native audio not initialized");
    if (track < 0 || track >= NATIVE_TRACK_COUNT) {
        music_stop();
        return fail("Invalid native track");
    }
    if (playing && loaded_track == track) {
        if (paused)
            music_resume();
        return playing && !paused;
    }
    if (loaded_track != track) {
        char path[80];
        std::snprintf(path, sizeof(path), "nitro:/audio/native/%s.nsq", nativeTracks[track].id);
        loaded_track = -1;
        playing = paused = false;
        if (!player.load(path))
            return fail("Native sequence/PCM load failed");
        loaded_track = track;
    }
    if (!player.play())
        return fail("ARM7 rejected native playback");
    playing = true;
    paused = false;
    error_message = "";
    return true;
}

void music_play_scene(int scene)
{
    if (scene < 0 || scene >= int(sizeof(scene_tracks) / sizeof(scene_tracks[0]))) {
        music_stop();
        return;
    }
    music_play_track(scene_tracks[scene]);
}

void music_pause()
{
    if (initialized && playing && !paused) {
        if (player.send(NS_PAUSE))
            paused = true;
        else
            error_message = "Native audio pause failed";
    }
}

void music_resume()
{
    if (initialized && playing && paused) {
        if (player.send(NS_RESUME))
            paused = false;
        else
            error_message = "Native audio resume failed";
    }
}

const char *music_error() { return error_message; }

bool sfx_init()
{
    if (effects_ready)
        return true;
    if (!initialized || !player.loadSfx("nitro:/audio/sfx.bin")) {
        error_message = "Native SFX bank load failed";
        return false;
    }
    effects_ready = true;
    return true;
}

void mdrv2_se_play(int id)
{
    if (!effects_ready || id <= 0 || id > SFX_HURRY_UP)
        return;
    if (!player.playSfx(unsigned(id)))
        ++dropped_effects; // never wait/stall the game or retry old effects later
}

void sfx_stop()
{
    if (effects_ready && !player.stopSfx())
        ++dropped_effects;
}

uint32_t sfx_dropped_requests() { return dropped_effects; }
