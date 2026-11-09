#include "native_audio/engine.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <string>

struct FakeHardware {
    bool silent = false;
    uint32_t starts = 0;
    struct Channel { bool enabled = false, loop = false; uint16_t period = 1, gain = 0; uint64_t phase = 0, end = 0; } channels[NS_SLOTS];
    unsigned channel(unsigned slot) const { return slot; }
    bool busy(unsigned slot) const { return channels[slot].enabled; }
    void stop(unsigned slot) { if (!silent) channels[slot].enabled = false; }
    void stopAll() { for (auto &c : channels) c.enabled = false; }
    void start(unsigned slot, const NsSample &sample, uint32_t offset, uint16_t period) {
        if (silent) return;
        ++starts;
        auto &c = channels[slot]; c.enabled = true; c.loop = sample.flags;
        c.phase = uint64_t(offset) << 16; c.end = uint64_t(sample.length) << 16; c.period = period;
    }
    void control(unsigned slot, uint16_t period, uint16_t gain, uint8_t) {
        if (silent) return;
        channels[slot].period = period; channels[slot].gain = gain;
    }
    void elapse(unsigned tempo) {
        for (auto &c : channels) {
            if (!c.enabled || c.loop) continue;
            c.phase += ((uint64_t(16756991) * 60) << 16) / (uint64_t(c.period) * tempo * 48);
            if (c.phase >= c.end) c.enabled = false;
        }
    }
};

int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<char> blob((std::istreambuf_iterator<char>(f)), {});
    assert(!blob.empty());
    FakeHardware hardware;
    native_audio::Engine<FakeHardware> engine(hardware);
    assert(!engine.load(blob.data(), 20));
    assert(engine.load(blob.data(), blob.size()));
    const auto *header = engine.header;
    assert(header->comparisonTicks > header->commonIntro && header->samples > 0);
    const char saved = blob[0]; blob[0] = 'X';
    assert(!engine.load(blob.data(), blob.size())); blob[0] = saved;
    assert(engine.load(blob.data(), blob.size()));
    engine.reset();
    const std::string prefix = argc == 3 ? argv[2] : "build/native_audio/native";
    FILE *trace = std::fopen((prefix + "_trace.csv").c_str(), "w");
    assert(trace);
    FILE *mix = std::fopen((prefix + "_mix_trace.csv").c_str(), "w");
    assert(mix);
    std::fputs("tick,slot,key,patch,volume,envelope,stage,gain,active,sample,hardware_gain_bits\n", mix);
    uint32_t starts[NS_SLOTS] = {};
    std::vector<NsEvent> scheduled;
    for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
        const auto &t = engine.tracks[slot];
        uint32_t index = 0, epoch = 0;
        do {
            for (uint32_t i = index; i < t.count; ++i) {
                auto e = engine.events[t.first + i];
                e.tick += epoch;
                if (e.tick < header->comparisonTicks) scheduled.push_back(e);
            }
            if (t.loopIndex == NS_NO_LOOP || t.endTick + epoch >= header->comparisonTicks) break;
            index = t.loopIndex;
            epoch += t.endTick - t.loopTick;
        } while (true);
    }
    std::stable_sort(scheduled.begin(), scheduled.end(), [](const NsEvent &a, const NsEvent &b) {
        return a.tick < b.tick || (a.tick == b.tick && a.slot < b.slot);
    });
    size_t scheduledIndex = 0;
    unsigned expectedTempo = header->tempo;
    unsigned expectedVolume[NS_SLOTS];
    for (unsigned slot = 0; slot < NS_SLOTS; ++slot)
        expectedVolume[slot] = slot < 6 ? 127 : slot < 9 ? 15 : 1953;
    for (;;) {
        // Check the runtime clock's tempo against the timestamped global
        // source stream, including mid-song changes (not just header tempo).
        const NsEvent *lastPitch[NS_SLOTS] = {};
        for (; scheduledIndex < scheduled.size() && scheduled[scheduledIndex].tick <= engine.tick; ++scheduledIndex) {
            const auto &event = scheduled[scheduledIndex];
            if (event.op == NS_TEMPO) expectedTempo = event.a;
            if (event.op == NS_VOLUME) expectedVolume[event.slot] = event.a;
            if (event.op == NS_RHYTHM_LEVEL)
                expectedVolume[event.slot] = 94 - event.a - event.b;
            if (event.op == NS_REL_VOLUME) {
                int delta = event.a < 32768 ? event.a : int(event.a) - 65536;
                int volume = int(expectedVolume[event.slot]) + delta;
                if (volume < 0) volume = 0;
                const int maximum = event.slot < 6 ? 127 : 15;
                if (volume > maximum) volume = maximum;
                expectedVolume[event.slot] = volume;
            }
            if (event.op == NS_NOTE || event.op == NS_PITCH) lastPitch[event.slot] = &event;
        }
        assert(engine.tempo == expectedTempo);
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
            const NsEvent *last = lastPitch[slot];
            if (last && last->op == NS_PITCH && engine.voices[slot].active)
                assert(engine.voices[slot].period == last->a);
        }
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
            const auto &v = engine.voices[slot];
            assert(v.volume == expectedVolume[slot]);
            if (slot >= 6 && slot < 9) {
                // Independent transcription of the driver's repeated-add
                // high-byte scaling (11dcc..11de0), rather than a separate
                // subjective per-channel gain correction.
                unsigned sum = 0;
                for (unsigned i = 0; i <= v.volume; ++i) sum += v.level;
                int dac = std::clamp(int(sum >> 8) - (v.amp.value >> 3), 0, 15);
                if (v.active) assert(v.gain == engine.gains[128 + dac]);
                std::fprintf(mix, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", engine.tick, slot,
                    v.key, v.patch, v.volume, v.level, v.stage, v.gain,
                    unsigned(v.active), v.sample, native_audio::hardware_gain(v.gain));
            }
            if (starts[slot] != v.starts) {
                starts[slot] = v.starts;
                std::fprintf(trace, "%u,%u,%u,%u,%u,%u,%u\n", engine.tick, slot, v.key,
                    v.gate, v.sample, v.gain, v.period);
            }
        }
        if (engine.tick == header->comparisonTicks - 1) break;
        hardware.elapse(engine.tempo); engine.step();
    }
    std::fclose(trace);
    std::fclose(mix);
    assert(engine.notes > 0 && engine.errors == 0);
    assert(hardware.starts == engine.notes); // automation never restarts an attack
    // Exercise early-end telemetry independently of any particular patch's
    // duration. The former @7 donor was entirely silent, not a short decay.
    {
        FakeHardware endedHardware;
        native_audio::Engine<FakeHardware> ended(endedHardware);
        assert(ended.load(blob.data(), blob.size()));
        ended.reset();
        unsigned slot = NS_SLOTS;
        for (unsigned i = 0; i < header->comparisonTicks && slot == NS_SLOTS; ++i) {
            for (unsigned s = 0; s < 6; ++s)
                if (ended.voices[s].active && ended.voices[s].gate > 2) { slot = s; break; }
            if (slot == NS_SLOTS) ended.step();
        }
        assert(slot < 6);
        endedHardware.channels[slot].enabled = false;
        ended.step(); ended.step();
        assert(ended.voices[slot].earlyEnds == 1);
        ended.step();
        assert(ended.voices[slot].earlyEnds == 1);
    }
    const uint32_t before = hardware.starts;
    const uint64_t position = engine.voices[1].position;
    engine.setMask(0);
    for (const auto &c : hardware.channels) assert(c.gain == 0 || !c.enabled);
    assert(hardware.starts == before && engine.voices[1].position == position);
    engine.setMask(0x7fff);
    assert(hardware.starts == before);
    for (unsigned i = 0; i < header->commonPeriod * 3; ++i) { hardware.elapse(engine.tempo); engine.step(); }
    assert(engine.errors == 0 && engine.loops > 0);

    // Long-run exact timer phase: no VBlank clock and no rounded 61-Hz tempo.
    native_audio::Clock clock;
    uint64_t delivered = 0;
    constexpr uint32_t interrupts = 1000000;
    for (uint32_t i = 0; i < interrupts; ++i) delivered += clock.interrupt(76);
    const uint64_t expected = uint64_t(interrupts) * 76 * 48 * 32768 / clock.denominator;
    assert(delivered == expected);
    assert(native_audio::hardware_gain(0) == 0);
    assert(native_audio::hardware_gain(65535) == 64); // half headroom
    assert(native_audio::rhythm_gain(0) == 65535);
    assert(native_audio::rhythm_gain(8) == 32767);
    assert(native_audio::rhythm_gain(63) == 0);
    assert(native_audio::rhythm_gain(94) == 0);
    const unsigned divider[] = {1, 2, 4, 16};
    unsigned last = 0;
    for (unsigned gain = 0; gain <= 65535; ++gain) {
        const uint16_t bits = native_audio::hardware_gain(gain);
        const unsigned mul = (bits & 127) == 127 ? 128 : bits & 127;
        const unsigned actual = mul * 65535 / (128 * divider[(bits >> 8) & 3]);
        assert(actual >= last);
        assert(std::abs(int(actual) - int(gain / 2)) <= 256);
        last = actual;
    }
    std::printf("Native engine: trace emitted; live tempo/pitch, no automation retriggers, independent loops, mute, early-end telemetry, clock and gain checks passed\n");
}
