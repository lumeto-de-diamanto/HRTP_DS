#include <nds.h>
#include <filesystem.h>
#include <fat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <algorithm>
#include <unistd.h>
#include <cerrno>
#include "format.h"
#include "client.hpp"
#include "native_catalog.h"
#include "native_track.h"

namespace {
native_audio::Client player;
NsHeader songHeader{};
NsSample songSamples[256]{};
NativeTrackId track = NS_INITIAL_TRACK;
int loadedNativeTrack = -1;
bool paused = false;
unsigned selected = 1, cue = 0;
uint32_t muteMask = 0x7fff;
bool solo = false;
unsigned cues[6] = {};
const char *labels[] = {"F0", "F1", "F2", "F3", "F4", "F5", "S0", "S1", "S2", "BD", "SN", "CY", "HH", "TM", "RM"};
char message[96] = "Native soundtrack diagnostics";
unsigned captureNumber = 0;

bool send(unsigned op, uint32_t arg = 0) {
    if (player.send(op, arg)) return true;
    if (!player.healthy) std::snprintf(message, sizeof(message), "ARM7 timeout; buffer retained");
    else if (player.lastResult == 8) std::snprintf(message, sizeof(message), "FM5 active: capture unavailable");
    else std::snprintf(message, sizeof(message), "ARM7 command %u error %lu", op, (unsigned long)player.lastResult);
    return false;
}

bool start() {
    if (player.data() && loadedNativeTrack == track) {
        if (!send(NS_PLAY) || !send(NS_MASK, solo ? 1u << selected : muteMask)) return false;
        paused = false; cue = 0;
        return true;
    }
    // Stop/ACK before freeing the current DMA buffers; one song resident.
    if (!player.unload()) return false;
    loadedNativeTrack = -1;
    char path[96];
    std::snprintf(path, sizeof(path), "nitro:/%s/song.nsq", nativeTracks[track].id);
    if (!player.load(path)) { std::snprintf(message, sizeof(message), "Native load failed: %s", nativeTracks[track].id); return false; }
    std::memcpy(&songHeader, player.data(), sizeof(songHeader));
    if (songHeader.samples > 256 || songHeader.sampleOffset > player.size() ||
        songHeader.samples * sizeof(NsSample) > player.size() - songHeader.sampleOffset) return false;
    std::memcpy(songSamples, player.data() + songHeader.sampleOffset, songHeader.samples * sizeof(NsSample));
    if (!send(NS_PLAY)) return false;
    loadedNativeTrack = track;
    send(NS_MASK, solo ? 1u << selected : muteMask);
    paused = false; cue = 0;
    return true;
}

bool readStatus(NsStatus &out) {
    return player.readStatus(out);
}

void le16(FILE *file, uint16_t value) { std::fputc(value & 255, file); std::fputc(value >> 8, file); }
void le32(FILE *file, uint32_t value) { le16(file, value & 65535); le16(file, value >> 16); }

void saveCapture(const NsStatus &status) {
    // Only called after an explicit SELECT press. NitroFS remains read-only.
    if (!fatInitDefault()) { std::snprintf(message, sizeof(message), "Capture in RAM; no writable SD found"); return; }
    char filename[96];
    const char *root = access("sd:/", F_OK) == 0 ? "sd:" : "fat:";
    FILE *f = nullptr;
    for (unsigned attempt = 0; attempt < 10000; ++attempt) {
        std::snprintf(filename, sizeof(filename), "%s/native_%s_%04u.wav", root, nativeTracks[track].id, captureNumber++);
        f = std::fopen(filename, "wbx"); // exclusive creation: never overwrite a user's capture
        if (f || errno != EEXIST) break;
    }
    if (!f) { std::snprintf(message, sizeof(message), "Cannot save captured audio"); return; }
    const uint32_t rate = 33513982u / 1024;
    const uint32_t size = NS_CAPTURE_SAMPLES * 4;
    std::fwrite("RIFF", 1, 4, f); le32(f, 36 + size); std::fwrite("WAVEfmt ", 1, 8, f);
    le32(f, 16); le16(f, 1); le16(f, 2); le32(f, rate); le32(f, rate * 4); le16(f, 4); le16(f, 16);
    std::fwrite("data", 1, 4, f); le32(f, size);
    for (unsigned i = 0; i < NS_CAPTURE_SAMPLES; ++i) {
        le16(f, uint16_t(player.shared->capture[0][i])); le16(f, uint16_t(player.shared->capture[1][i]));
    }
    const bool error = std::ferror(f);
    const int closed = std::fclose(f);
    if (error || closed) std::snprintf(message, sizeof(message), "Capture write failed");
    else std::snprintf(message, sizeof(message), "Saved %.60s @ beat %lu", filename,
                       (unsigned long)(status.captureTick / 48));
    if (!error && !closed) send(NS_CAPTURE, 1);
}

void draw(const NsStatus &status) {
    const auto *header = &songHeader;
    const auto *samples = songSamples;
    const uint32_t musicalTick = status.tick < header->commonIntro ? status.tick :
        header->commonIntro + (status.tick - header->commonIntro) % header->commonPeriod;
    std::printf("\x1b[1;1H%s NATIVE %-6s\x1b[K\n", nativeTracks[track].id, paused ? "PAUSED" : "");
    std::printf("Beat %3lu.%02lu notes %lu\x1b[K\n", (unsigned long)(musicalTick / 48),
                (unsigned long)(musicalTick % 48), (unsigned long)status.notes);
    std::printf("    Pt Key Gate Gain GB HW End\x1b[K\n");
    for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
        const auto &v = status.voices[slot];
        std::printf("%c%s %02u %3u %4u %4u %u%u %2u %3lu\x1b[K\n", selected == slot ? '>' : ' ', labels[slot],
            v.patch, v.key, v.gate, v.gain >> 8, v.active, v.busy,
            v.hardware, (unsigned long)std::min<uint32_t>(v.earlyEnds, 999));
    }
    const auto &v = status.voices[selected];
    std::printf("s%u V%u E%u pos~%lu %s\x1b[K\n", v.sample, v.volume, v.envelope,
                (unsigned long)v.position, solo ? "SOLO" : v.muted ? "MUTE" : "");
    std::printf("%-31.31s\x1b[K\n", v.sample < header->samples ? samples[v.sample].name : "");
    std::printf("A restart START pause\x1b[K\n");
    std::printf("UP/DN voice X solo Y mute\x1b[K\n");
    std::printf("LEFT/RIGHT song L/R cue\x1b[K\n");
    std::printf("%-31.31s\x1b[K", message);
}
} // namespace

bool chooseTrack(NativeTrackId next) {
    track = next;
    if (!start()) return false;
    cues[0] = 0;
    cues[1] = songHeader.commonIntro / 48;
    for (unsigned i = 2; i < 6; ++i)
        cues[i] = (songHeader.commonIntro + songHeader.commonPeriod * (i - 1) / 5) / 48;
    std::snprintf(message, sizeof(message), "%u slides; SELECT capture/save", nativeTracks[track].slides);
    return true;
}

int main() {
    consoleDemoInit();
    if (!nitroFSInit(nullptr)) { std::printf("NitroFS failed\n"); while (true) swiWaitForVBlank(); }
    if (!player.init() || !chooseTrack(track)) {
        std::printf("Audio initialization failed: %s\n", message); while (true) swiWaitForVBlank();
    }
    NsStatus status{};
    unsigned frames = 0;
    while (true) {
        swiWaitForVBlank(); scanKeys();
        const uint16_t down = keysDown();
        if (down & (KEY_LEFT | KEY_RIGHT)) {
            const int next = (int(track) + ((down & KEY_RIGHT) ? 1 : NATIVE_TRACK_COUNT - 1)) % NATIVE_TRACK_COUNT;
            if (!chooseTrack(NativeTrackId(next))) {
                std::printf("\x1b[2JTrack load failed: %s\n", message);
                while (true) swiWaitForVBlank();
            }
        }
        if (down & KEY_UP) selected = (selected + NS_SLOTS - 1) % NS_SLOTS;
        if (down & KEY_DOWN) selected = (selected + 1) % NS_SLOTS;
        if (solo && (down & (KEY_UP | KEY_DOWN))) send(NS_MASK, 1u << selected);
        if (down & KEY_A) start();
        if (down & KEY_START) {
            if (send(paused ? NS_RESUME : NS_PAUSE)) paused = !paused;
        }
        if (down & KEY_X) { solo = !solo; send(NS_MASK, solo ? 1u << selected : muteMask); }
        if (down & KEY_Y) { solo = false; muteMask ^= 1u << selected; send(NS_MASK, muteMask); }
        if (down & (KEY_L | KEY_R)) {
            cue = (cue + ((down & KEY_R) ? 1 : 5)) % 6;
            send(NS_SEEK, cues[cue] * 48);
            std::snprintf(message, sizeof(message), "Seek resets loop PCM phase");
        }
        if (readStatus(status) && (down & KEY_SELECT)) {
            if (status.captureState == 2) saveCapture(status);
            else if (send(NS_CAPTURE)) std::snprintf(message, sizeof(message), "Capturing 0.125s; SELECT saves SD");
        }
        if (status.captureState == 2 && std::strncmp(message, "Capturing", 9) == 0)
            std::snprintf(message, sizeof(message), "Capture ready; SELECT saves SD");
        if (down || ++frames % 6 == 0) { readStatus(status); draw(status); }
    }
}
