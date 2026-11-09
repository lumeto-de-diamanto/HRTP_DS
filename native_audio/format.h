// Little-endian, pointer-free native sequence/PCM container shared by both ROMs.
#pragma once
#include <stdint.h>

enum { NS_VERSION = 1, NS_SLOTS = 15, NS_CAPTURE_SAMPLES = 4096 };
enum NsOpcode { NS_NOTE = 1, NS_VOLUME, NS_LFO, NS_PAN, NS_PATCH,
                NS_MIXER, NS_NOISE, NS_DETUNE, NS_OFF, NS_TEMPO, NS_PITCH,
                NS_RHYTHM_LEVEL, NS_REL_VOLUME };
enum NsCommand { NS_PLAY = 1, NS_RELEASE, NS_PAUSE, NS_RESUME, NS_MASK,
                  NS_SEEK, NS_CAPTURE, NS_SFX_LOAD };
enum { NS_NO_LOOP = 0xffffffffu, NS_NO_GATE = 0xffffu };

struct NsHeader {
    char magic[8];
    uint32_t version, bytes, samples, tracks, events;
    uint32_t sampleOffset, trackOffset, eventOffset, pcmOffset;
    uint32_t tempo, commonIntro, commonPeriod, comparisonTicks, gainOffset;
};
struct NsSample {
    uint32_t offset, length, loopStart, loopEnd, c5, flags;
    char name[32];
    uint8_t envelope[6];
    uint16_t reserved;
};
struct NsTrack {
    uint32_t source, slot, first, count, loopIndex, loopTick, endTick, reserved;
};
struct NsEvent {
    uint32_t tick;
    uint8_t op, slot;
    uint16_t a;
    uint32_t b, c;
};
// Resident mono signed PCM16 one-shots. IDs are table indices plus one.
struct NsSfxHeader {
    char magic[8];
    uint32_t version, bytes, count, tableOffset, period, gain;
};
struct NsSfxSample {
    uint32_t offset, length; // bytes, both multiples of four
};
struct NsVoiceStatus {
    uint32_t starts, earlyEnds, position; // position is an estimate, not a hardware readback
    uint16_t sample, gate, gain, period;
    uint8_t key, patch, volume, envelope, active, busy, hardware, muted;
};
struct NsStatus {
    uint32_t serial, tick, notes, loops, errors, irqCycles, native, paused, mask;
    uint32_t captureState, captureTick; // 0 idle, 1 recording, 2 ready
    NsVoiceStatus voices[NS_SLOTS];
};
struct NsShared {
    uint32_t blobAddress, blobBytes;
    uint32_t sfxAddress, sfxBytes;
    NsStatus status;
    int16_t capture[2][NS_CAPTURE_SAMPLES];
};

static_assert(sizeof(NsHeader) == 64, "NS header layout");
static_assert(sizeof(NsSample) == 64, "NS sample layout");
static_assert(sizeof(NsTrack) == 32, "NS track layout");
static_assert(sizeof(NsEvent) == 16, "NS event layout");
static_assert(sizeof(NsSfxHeader) == 32, "NS SFX header layout");
static_assert(sizeof(NsSfxSample) == 8, "NS SFX sample layout");
