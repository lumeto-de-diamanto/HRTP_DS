#include <nds.h>
#pragma once
#ifndef NS_DIAGNOSTIC_CAPTURE
#define NS_DIAGNOSTIC_CAPTURE 0
#endif
#include "engine.hpp"
#include "sfx.hpp"

namespace native_audio::service {
// Listening diagnostics keep capture. Gameplay owns exactly DS3/8
// for SFX and maps only music slots that actually contain NOTE events.
constexpr uint8_t originalMapping[NS_SLOTS] = {0, 2, 4, 5, 6, 1, 8, 9, 10, 7, 11, 12, 13, 14, 15};
#if NS_DIAGNOSTIC_CAPTURE
uint8_t mapping[NS_SLOTS] = {0, 2, 4, 5, 6, 1, 8, 9, 10, 7, 11, 12, 13, 14, 15};
#else
uint8_t mapping[NS_SLOTS] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
#endif
bool captureAvailable = true;

struct Hardware {
    bool silent = false;
    const uint8_t *blob = nullptr;
    uint8_t channel(unsigned slot) const { return mapping[slot]; }
    bool busy(unsigned slot) const {
        return mapping[slot] != 255 && (REG_SOUNDXCNT(mapping[slot]) & SOUNDXCNT_ENABLE);
    }
    void stop(unsigned slot) {
        if (!silent && mapping[slot] != 255) REG_SOUNDXCNT(mapping[slot]) = 0;
    }
    void stopAll() {
        if (silent) return;
#if NS_DIAGNOSTIC_CAPTURE
        for (unsigned i = 0; i < 16; ++i) REG_SOUNDXCNT(i) = 0;
#else
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) stop(slot);
#endif
    }
    void start(unsigned slot, const NsSample &sample, uint32_t offset, uint16_t period) {
        if (silent || mapping[slot] == 255) return;
        const unsigned hw = mapping[slot];
        REG_SOUNDXCNT(hw) = 0;
        uint32_t start = 0, loop = sample.loopStart;
        if (offset) {
            if (sample.flags && offset >= sample.loopStart) {
                start = sample.loopStart; loop = 0;
            } else {
                start = std::min(offset & ~3u, sample.length);
                loop = sample.flags ? sample.loopStart - start : 0;
            }
        }
        const uint32_t length = sample.length - start;
        if (length < 16) return;
        REG_SOUNDXSAD(hw) = uint32_t(reinterpret_cast<uintptr_t>(blob + sample.offset + start));
        REG_SOUNDXPNT(hw) = loop / 4;
        REG_SOUNDXLEN(hw) = (length - loop) / 4;
        REG_SOUNDXTMR(hw) = uint16_t(-period);
        REG_SOUNDXCNT(hw) = SOUNDXCNT_ENABLE | SOUNDXCNT_FORMAT_8BIT |
            (sample.flags ? SOUNDXCNT_REPEAT : SOUNDXCNT_ONE_SHOT) | SOUNDXCNT_PAN(64);
    }
    void control(unsigned slot, uint16_t period, uint16_t gain, uint8_t pan) {
        if (silent || mapping[slot] == 255) return;
        const unsigned hw = mapping[slot];
        REG_SOUNDXTMR(hw) = uint16_t(-period);
        const uint32_t mask = 0x7fu | (3u << 8) | (127u << 16);
        REG_SOUNDXCNT(hw) = (REG_SOUNDXCNT(hw) & ~mask) |
            native_audio::hardware_gain(gain) | SOUNDXCNT_PAN(pan);
    }
} hardware;

native_audio::Engine<Hardware> engine(hardware);
struct SfxHardware {
    static unsigned channel(unsigned voice) { return voice ? 8 : 3; }
    bool busy(unsigned voice) const { return REG_SOUNDXCNT(channel(voice)) & SOUNDXCNT_ENABLE; }
    void stop(unsigned voice) { REG_SOUNDXCNT(channel(voice)) = 0; }
    void start(unsigned voice, const uint8_t *pcm, uint32_t bytes, uint16_t period, uint16_t gain) {
        const unsigned hw = channel(voice);
        REG_SOUNDXCNT(hw) = 0;
        REG_SOUNDXSAD(hw) = uint32_t(reinterpret_cast<uintptr_t>(pcm));
        REG_SOUNDXPNT(hw) = 0;
        REG_SOUNDXLEN(hw) = bytes / 4;
        REG_SOUNDXTMR(hw) = uint16_t(-period);
        REG_SOUNDXCNT(hw) = SOUNDXCNT_ENABLE | SOUNDXCNT_FORMAT_16BIT | SOUNDXCNT_ONE_SHOT |
            SOUNDXCNT_PAN(64) | native_audio::hardware_gain(gain);
    }
} sfxHardware;
native_audio::SfxPlayer<SfxHardware> sfx(sfxHardware);

bool prepareMapping() {
    bool used[NS_SLOTS] = {};
    for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
        const auto &track = engine.tracks[slot];
        for (uint32_t i = 0; i < track.count; ++i)
            if (engine.events[track.first + i].op == NS_NOTE) used[slot] = true;
    }
#if NS_DIAGNOSTIC_CAPTURE
    captureAvailable = !used[5];
#else
    uint8_t next[NS_SLOTS];
    uint32_t owned = (1u << 3) | (1u << 8);
    for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
        next[slot] = used[slot] ? (slot == 6 ? 5 : originalMapping[slot]) : 255;
        if (next[slot] == 255) continue;
        if (owned & (1u << next[slot])) return false;
        owned |= 1u << next[slot];
    }
    std::memcpy(mapping, next, sizeof(mapping));
    captureAvailable = false; // stereo capture programs DS3's timer
#endif
    return true;
}
native_audio::Clock clockPhase;
volatile NsShared *shared = nullptr;
volatile uint32_t pending = 0;
volatile bool nativeMode = false, paused = false, seeking = false, exiting = false;
uint32_t maxCycles = 0;

void publish() {
    if (shared) engine.publish(shared->status, nativeMode, paused, maxCycles);
}

void captureStop() {
    REG_SNDCAP0CNT = REG_SNDCAP1CNT = 0;
    if (shared) shared->status.captureState = 0;
}

void audioInterrupt() {
    if (!nativeMode || paused || seeking || !shared) return;
    const uint16_t before = TIMER_DATA(2);
    if (clockPhase.interrupt(engine.tempo)) {
        engine.step();
        if (shared->status.captureState == 1 &&
            !(REG_SNDCAP0CNT & SNDCAPCNT_START_BUSY) && !(REG_SNDCAP1CNT & SNDCAPCNT_START_BUSY))
            shared->status.captureState = 2;
        const uint16_t elapsed = TIMER_DATA(2) - before;
        maxCycles = std::max(maxCycles, uint32_t(elapsed) * 64);
        publish();
    }
}

void addressHandler(void *address, void *) {
    const uintptr_t p = reinterpret_cast<uintptr_t>(address);
    if (!(p & 31) && p >= 0x02000000 && p <= 0x02400000 - sizeof(NsShared))
        shared = static_cast<volatile NsShared *>(address);
}

void commandHandler(uint32_t value, void *) {
    // Work (particularly seeking) runs in main(), never in this FIFO IRQ.
    if (!pending) pending = value;
    else fifoSendValue32(FIFO_USER_01, 3);
}

void sfxHandler(uint32_t value, void *) {
#if !NS_DIAGNOSTIC_CAPTURE
    if (sfx.ready() && !exiting) sfx.play(value);
#else
    (void)value;
#endif
}

uint32_t command(uint32_t value) {
    if (!shared) return 1;
    const unsigned op = value >> 24;
    const uint32_t arg = value & 0xffffff;
    irqDisable(IRQ_TIMER1);
    uint32_t result = 0;
    switch (op) {
    case NS_PLAY: {
        // This is still the previous song's ownership map. Stop it before
        // prepareMapping commits the replacement and reset starts any notes.
        hardware.stopAll();
        engine.header = nullptr;
        for (auto &voice : engine.voices) voice.active = false;
        nativeMode = true; paused = false;
        captureStop();
        const uint32_t address = shared->blobAddress;
        const uint32_t bytes = shared->blobBytes;
        if ((address & 3) || address < 0x02000000 || bytes > 0x400000 ||
            address > 0x02400000 - bytes || !engine.load(reinterpret_cast<const void *>(address), bytes) ||
            !prepareMapping()) {
            engine.header = nullptr;
            nativeMode = false; result = 2; break;
        }
        hardware.blob = reinterpret_cast<const uint8_t *>(address);
        clockPhase.phase = 0; maxCycles = 0;
        engine.reset();
        break;
    }
    case NS_RELEASE:
        captureStop(); hardware.stopAll();
        nativeMode = false; paused = false;
        engine.header = nullptr; // caller may free the old blob after this ACK
        for (auto &voice : engine.voices) voice.active = false;
        break;
    case NS_PAUSE:
        if (nativeMode) { paused = true; captureStop(); hardware.stopAll(); }
        break;
    case NS_RESUME:
        if (nativeMode) { engine.restoreHardware(); paused = false; }
        break;
    case NS_MASK:
        if (!nativeMode || !engine.header) { result = 4; break; }
        engine.setMask(arg);
        break;
    case NS_SEEK:
        if (!nativeMode || !engine.header || arg > engine.header->comparisonTicks) { result = 4; break; }
        captureStop(); hardware.stopAll(); seeking = true;
        hardware.silent = true; engine.reset();
        // Timer IRQ is enabled during replay but returns immediately. System,
        // storage, input and power IRQs remain serviced throughout the seek.
        irqEnable(IRQ_TIMER1);
        while (engine.tick < arg && !exiting) engine.step();
        irqDisable(IRQ_TIMER1);
        hardware.silent = false; engine.restoreHardware();
        clockPhase.phase = 0; seeking = false;
        break;
    case NS_SFX_LOAD: {
#if NS_DIAGNOSTIC_CAPTURE
        result = 8; // diagnostic capture/music retain exclusive channel ownership
#else
        // FIFO02 may run in an IRQ while main validates/commits the bank.
        // Keep both channels and the bank pointer atomic with respect to it.
        const int oldIME = enterCriticalSection();
        const uint32_t address = shared->sfxAddress;
        const uint32_t bytes = shared->sfxBytes;
        if ((address & 3) || address < 0x02000000 || bytes > 0x400000 ||
            address > 0x02400000 - bytes || !sfx.load(reinterpret_cast<const void *>(address), bytes)) result = 2;
        leaveCriticalSection(oldIME);
#endif
        break;
    }
    case NS_CAPTURE:
        if (arg == 1) { captureStop(); break; }
        if (!captureAvailable) { result = 8; break; }
        if (!nativeMode || paused || shared->status.captureState == 1) { result = 5; break; }
        captureStop();
        REG_SOUNDXTMR(1) = REG_SOUNDXTMR(3) = uint16_t(-512); // capture rate = BUS_CLOCK/1024
        REG_SNDCAP0DAD = uint32_t(reinterpret_cast<uintptr_t>(&shared->capture[0][0]));
        REG_SNDCAP1DAD = uint32_t(reinterpret_cast<uintptr_t>(&shared->capture[1][0]));
        REG_SNDCAP0LEN = REG_SNDCAP1LEN = NS_CAPTURE_SAMPLES / 2;
        shared->status.captureTick = engine.tick;
        shared->status.captureState = 1;
        REG_SNDCAP0CNT = SNDCAPCNT_START_BUSY | SNDCAPCNT_ONESHOT | SND0CAPCNT_SOURCE_LEFT_MIXER;
        REG_SNDCAP1CNT = SNDCAPCNT_START_BUSY | SNDCAPCNT_ONESHOT | SND1CAPCNT_SOURCE_RIGHT_MIXER;
        break;
    default: result = 6; break;
    }
    publish();
    irqEnable(IRQ_TIMER1);
    return result;
}

void init() {
    fifoSetAddressHandler(FIFO_USER_01, addressHandler, nullptr);
    fifoSetValue32Handler(FIFO_USER_01, commandHandler, nullptr);
    fifoSetValue32Handler(FIFO_USER_02, sfxHandler, nullptr);
    // Timer 2 belongs to libnds' system counter. Read it for profiling without
    // repurposing it (doing so would break filesystem/system timing).
    systemCounterSetup();
    timerStart(1, ClockDivider_64, uint16_t(-512), audioInterrupt);
}

void poll() {
    if (pending) {
        const uint32_t message = pending;
        const uint32_t result = command(message);
        pending = 0;
        fifoSendValue32(FIFO_USER_01, result);
    }
}

void shutdown() {
    exiting = true;
    timerStop(1);
    captureStop();
#if !NS_DIAGNOSTIC_CAPTURE
    sfx.unload();
#endif
    for (unsigned i = 0; i < 16; ++i) REG_SOUNDXCNT(i) = 0;
}
} // namespace native_audio::service
