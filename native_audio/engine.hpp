// Portable native-tick sequencer. Hardware access is supplied by the backend.
#pragma once
#include "format.h"
#include "audio_lfo.hpp"
#include <cstring>
#include <limits>

namespace native_audio {

// Match ymfm's ADPCM-A attenuation approximation (not the linear GM policy).
// Caller-provided WAVs must retain common recording gain for this to preserve
// relative levels. Exact analogue FM/rhythm calibration is still unverified.
inline uint16_t rhythm_gain(unsigned attenuation) {
    if (attenuation >= 63) return 0;
    return uint32_t(65535) * (15 - (attenuation & 7)) / (15 * (1u << (attenuation >> 3)));
}

// A 512-count /64 ARM7 timer runs at BUS_CLOCK/32768 Hz, not exactly 1024.
// Retain the remainder against that exact clock instead of rounding tempo.
struct Clock {
    static constexpr uint32_t denominator = 33513982u * 60u;
    uint32_t phase = 0;
    bool interrupt(uint32_t tempo) {
        phase += tempo * 48u * 32768u;
        if (phase < denominator) return false;
        phase -= denominator;
        return true;
    }
};

// Find the closest DS volume multiplier/divider, with 127 meaning 128/128.
// Preserve the approved 1/2 music headroom before hardware quantization.
inline uint16_t hardware_gain(uint16_t gain) {
    const uint32_t divisors[] = {1, 2, 4, 16};
    uint32_t bestError = UINT32_MAX;
    uint16_t best = 0;
    const uint32_t target = gain / 2;
    for (unsigned d = 0; d < 4; ++d) {
        uint32_t n = (target * 128u * divisors[d] + 32767u) / 65535u;
        n = std::min<uint32_t>(n, 128);
        if (n == 127) n = 126; // there is no 127/128 setting
        const uint32_t actual = n * 65535u / (128u * divisors[d]);
        const uint32_t error = actual > target ? actual - target : target - actual;
        if (error < bestError) {
            bestError = error;
            best = (n == 128 ? 127 : n) | (d << 8);
        }
    }
    return best;
}

template<class Backend> class Engine {
public:
    struct Voice {
        bool active = false, releasing = false, countedEnd = false, rhythmLog = false;
        uint16_t sample = 0, key = 0, patch = 0, volume = 127, pan = 3, mixer = 1;
        uint16_t gate = 0, level = 0, stage = 0, basePeriod = 1, period = 1, fnum = 1, gain = 0;
        uint32_t age = 0, starts = 0, earlyEnds = 0;
        uint64_t position = 0;
        int pitchKind = 0, ampKind = 0;
        std::array<int, 4> pitchParams{}, ampParams{};
        MdrvSoftwareLfo pitch{0, {}}, amp{0, {}};
    };
    struct Cursor { uint32_t index = 0, epoch = 0; };
    explicit Engine(Backend &backend) : hw(backend) {}
    const NsHeader *header = nullptr;
    const NsSample *samples = nullptr;
    const NsTrack *tracks = nullptr;
    const NsEvent *events = nullptr;
    const uint16_t *gains = nullptr;
    const uint8_t *blob = nullptr;
    Voice voices[NS_SLOTS];
    Cursor cursors[NS_SLOTS];
    uint32_t tick = 0, notes = 0, loops = 0, errors = 0, mask = 0x7fff, tempo = 76;

    bool load(const void *data, uint32_t bytes) {
        header = nullptr;
        if (!data || (reinterpret_cast<uintptr_t>(data) & 3) || bytes < sizeof(NsHeader)) return false;
        const auto *h = static_cast<const NsHeader *>(data);
        auto range = [bytes](uint32_t offset, uint32_t count, uint32_t size) {
            return !(offset & 3) && offset <= bytes && count <= (bytes - offset) / size;
        };
        if (std::memcmp(h->magic, "HRNSQ01\0", 8) || h->version != NS_VERSION || h->bytes != bytes ||
            h->tracks != NS_SLOTS || !h->samples || h->samples > 256 || h->tempo < 16 || h->tempo > 127 ||
            !h->commonPeriod || h->comparisonTicks < h->commonIntro ||
            !range(h->sampleOffset, h->samples, sizeof(NsSample)) ||
            !range(h->trackOffset, h->tracks, sizeof(NsTrack)) ||
            !range(h->eventOffset, h->events, sizeof(NsEvent)) ||
            !range(h->gainOffset, 144, sizeof(uint16_t))) return false;
        blob = static_cast<const uint8_t *>(data);
        samples = reinterpret_cast<const NsSample *>(blob + h->sampleOffset);
        tracks = reinterpret_cast<const NsTrack *>(blob + h->trackOffset);
        events = reinterpret_cast<const NsEvent *>(blob + h->eventOffset);
        gains = reinterpret_cast<const uint16_t *>(blob + h->gainOffset);
        for (uint32_t i = 0; i < h->samples; ++i) {
            const auto &s = samples[i];
            if (s.length < 16 || (s.length & 3) || s.offset < h->pcmOffset ||
                !range(s.offset, s.length, 1) || !s.c5 || s.flags > 1 ||
                (s.flags && ((s.loopStart & 3) || s.loopEnd != s.length || s.loopStart >= s.loopEnd)) ||
                s.loopStart / 4 > 65535) return false;
        }
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
            const auto &t = tracks[slot];
            if (t.slot != slot || t.first > h->events || t.count > h->events - t.first ||
                (t.loopIndex != NS_NO_LOOP && (t.loopIndex >= t.count || t.endTick <= t.loopTick))) return false;
            uint32_t last = 0;
            for (uint32_t i = 0; i < t.count; ++i) {
                const auto &e = events[t.first + i];
                if (e.slot != slot || e.tick < last || e.tick > t.endTick || e.op < NS_NOTE || e.op > NS_REL_VOLUME)
                    return false;
                if (e.op == NS_NOTE && ((e.b & 65535) >= h->samples || !(e.b >> 16) ||
                    !(e.c & 65535) || e.a >= 120 || (slot < 6 && !(e.c >> 16)))) return false;
                if (e.op == NS_VOLUME && e.a > (slot >= 9 ? 1953 : slot >= 6 ? 15 : 127)) return false;
                if (e.op == NS_LFO && (e.a < 1 || e.a > 4 || (slot >= 6 && e.a <= 2 && e.b))) return false;
                if (e.op == NS_PAN && e.a > 3) return false;
                if (e.op == NS_MIXER && e.a > 3) return false;
                if (e.op == NS_TEMPO && (e.a < 16 || e.a > 127)) return false;
                if (e.op == NS_REL_VOLUME && slot >= 9) return false;
                if (e.op == NS_PITCH && !e.a) return false;
                if (e.op == NS_RHYTHM_LEVEL && (slot < 9 || e.a > 63 || e.b > 31)) return false;
                last = e.tick;
            }
        }
        header = h;
        return true;
    }

    void reset() {
        hw.stopAll();
        tick = notes = loops = errors = 0;
        tempo = header ? header->tempo : 76;
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
            voices[slot] = Voice{};
            if (slot >= 6 && slot < 9) voices[slot].volume = 15;
            if (slot >= 9) voices[slot].volume = 1953;
            cursors[slot] = Cursor{};
        }
        if (header) processEvents();
    }

    void step() {
        if (!header || tick == UINT32_MAX) { ++errors; return; }
        ++tick;
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
            auto &v = voices[slot];
            if (!v.active) continue;
            const auto &s = samples[v.sample];
            ++v.age;
            v.position += ((uint64_t(16756991) * 60) << 16) /
                          (uint64_t(v.period) * tempo * 48);
            const uint64_t end = uint64_t(s.length) << 16;
            if (s.flags && v.position >= end) {
                const uint64_t start = uint64_t(s.loopStart) << 16;
                v.position = start + (v.position - start) % (end - start);
            } else if (v.position > end) v.position = end;
            if (slot >= 9 && v.gate == NS_NO_GATE && !(s.flags & 1) && v.position >= end) {
                stop(slot); continue;
            }
            if (v.gate != NS_NO_GATE && v.gate) --v.gate;
            if (slot >= 6 && slot < 9) {
                if (!v.gate) {
                    v.releasing = true;
                    v.level = s.envelope[5] ? std::max(0, int(v.level) - s.envelope[5]) : 0;
                } else if (v.stage == 0) {
                    v.level = std::min(255, v.level + s.envelope[1]);
                    if (v.level == 255) v.stage = 1;
                } else if (v.stage == 1) {
                    v.level = std::max(int(s.envelope[3]), int(v.level) - s.envelope[2]);
                    if (v.level == s.envelope[3]) v.stage = 2;
                } else v.level = std::max(0, int(v.level) - s.envelope[4]);
                if (v.releasing && !v.level) stop(slot);
            } else if (!v.gate) stop(slot);
            if (!v.active) continue;
            const int delta = v.pitch.tick(slot >= 6);
            v.amp.tick(slot >= 6);
            if (slot < 6) {
                const int changed = std::max(1, int(v.fnum) + delta);
                v.period = std::clamp<uint32_t>((uint32_t(v.basePeriod) * v.fnum + changed / 2) / changed, 1, 65535);
            }
            if (!hw.silent && !hw.busy(slot) && !v.countedEnd && v.age > 1 && v.gate &&
                !(slot >= 9 && v.gate == NS_NO_GATE)) {
                ++v.earlyEnds;
                v.countedEnd = true;
            }
            update(slot);
        }
        processEvents();
    }

    void stop(unsigned slot) { voices[slot].active = false; hw.stop(slot); }

    void setMask(uint32_t value) {
        mask = value & 0x7fff;
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) if (voices[slot].active) update(slot);
    }

    void restoreHardware() {
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
            const auto &v = voices[slot];
            if (!v.active) continue;
            const auto &s = samples[v.sample];
            if (!(s.flags) && (v.position >> 16) >= s.length) continue;
            // The DS has no readable playback-position or phase-restore register.
            // One-shots can resume at an aligned offset; loops restart their
            // loop body on seek/resume. This is explicitly a diagnostic limit.
            hw.start(slot, s, uint32_t(v.position >> 16), v.period);
            update(slot);
        }
    }

    void publish(volatile NsStatus &out, bool native, bool paused, uint32_t cycles) {
        ++out.serial;
        barrier();
        out.tick = tick; out.notes = notes; out.loops = loops; out.errors = errors;
        out.irqCycles = cycles; out.native = native; out.paused = paused; out.mask = mask;
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
            const auto &v = voices[slot];
            auto &d = out.voices[slot];
            d.starts = v.starts; d.earlyEnds = v.earlyEnds; d.position = v.position >> 16;
            d.sample = v.sample; d.gate = v.gate; d.gain = v.gain; d.period = v.period;
            d.key = v.key; d.patch = v.patch; d.volume = std::min(255, int(v.volume));
            d.envelope = v.level; d.active = v.active; d.busy = hw.busy(slot);
            d.hardware = hw.channel(slot); d.muted = !(mask & (1u << slot));
        }
        barrier();
        ++out.serial;
    }

private:
    Backend &hw;
    static void barrier() {
#if defined(__GNUC__)
        asm volatile("" ::: "memory");
#endif
    }
    void update(unsigned slot) {
        auto &v = voices[slot];
        if (slot < 6) {
            int tl = std::min(127, ((127 - v.volume) + v.amp.value) & 255);
            v.gain = gains[127 - tl];
        } else if (slot < 9) {
            int level = v.level * (v.volume + 1) / 256 - (v.amp.value >> 3);
            v.gain = gains[128 + std::clamp(level, 0, 15)];
        } else v.gain = v.rhythmLog ? rhythm_gain(v.volume) : uint32_t(v.volume) * 65535 / 1953;
        const uint16_t gain = (mask & (1u << slot)) && v.pan && v.mixer ? v.gain : 0;
        hw.control(slot, v.period, gain, v.pan == 1 ? 0 : v.pan == 2 ? 127 : 64);
    }
    void apply(const NsEvent &e) {
        auto &v = voices[e.slot];
        switch (e.op) {
        case NS_NOTE:
            v.sample = e.b & 65535; v.key = e.a; v.gate = e.c & 65535;
            v.fnum = std::max<uint32_t>(1, e.c >> 16); v.basePeriod = v.period = e.b >> 16;
            v.position = 0; v.age = 0; v.active = true; v.releasing = v.countedEnd = false;
            v.level = samples[v.sample].envelope[0]; v.stage = 0;
            v.pitch = MdrvSoftwareLfo(v.pitchKind, v.pitchParams);
            v.amp = MdrvSoftwareLfo(v.ampKind, v.ampParams);
            ++v.starts; ++notes;
            hw.start(e.slot, samples[v.sample], 0, v.period);
            break;
        case NS_VOLUME: v.volume = e.a; break;
        case NS_REL_VOLUME:
            v.volume = std::clamp(int(v.volume) + int(int16_t(e.a)), 0, e.slot < 6 ? 127 : 15);
            break;
        case NS_TEMPO: tempo = e.a; break;
        case NS_PITCH: v.basePeriod = v.period = e.a; break;
        case NS_RHYTHM_LEVEL: v.rhythmLog = true; v.volume = (63 - e.a) + (31 - e.b); break;
        case NS_PATCH: v.patch = e.a; break;
        case NS_PAN: v.pan = e.a; break;
        case NS_MIXER: v.mixer = e.a; break;
        case NS_LFO: {
            std::array<int, 4> params{};
            for (unsigned i = 0; i < 4; ++i) params[i] = (e.b >> (i * 8)) & 255;
            if (e.a <= 2) {
                v.pitchKind = e.a; v.pitchParams = params;
                v.pitch = MdrvSoftwareLfo(e.a, params);
            } else {
                v.ampKind = e.a; v.ampParams = params;
                v.amp = MdrvSoftwareLfo(e.a, params);
            }
            break;
        }
        case NS_OFF: stop(e.slot); break;
        case NS_NOISE: case NS_DETUNE: break; // selection metadata, already resolved into NOTE
        default: ++errors; break;
        }
        if (v.active) update(e.slot);
    }
    void processEvents() {
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
            const auto &track = tracks[slot];
            auto &cursor = cursors[slot];
            for (;;) {
                while (cursor.index < track.count) {
                    const auto &event = events[track.first + cursor.index];
                    if (uint64_t(event.tick) + cursor.epoch > tick) break;
                    apply(event);
                    ++cursor.index;
                }
                if (cursor.index != track.count || track.loopIndex == NS_NO_LOOP ||
                    uint64_t(track.endTick) + cursor.epoch > tick) break;
                const uint32_t period = track.endTick - track.loopTick;
                if (cursor.epoch > UINT32_MAX - period) { ++errors; return; }
                cursor.epoch += period;
                cursor.index = track.loopIndex;
                ++loops;
            }
        }
    }
};
} // namespace native_audio
