// Host-only YM2608 SSG one-shot renderer. Whole effect, one mono PCM16 sample.
#include "ymfm_opn.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t clockRate = 7987200, outputRate = 22050;
class Interface : public ymfm::ymfm_interface {};
uint32_t le32(std::istream &f) {
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        int c = f.get();
        if (c < 0) throw std::runtime_error("truncated SFX registers");
        result |= unsigned(c) << (i * 8);
    }
    return result;
}
void write(ymfm::ym2608 &chip, uint8_t reg, uint8_t value) {
    chip.write_address(reg); chip.write_data(value);
}
int peakCalibration() {
    Interface interface;
    ymfm::ym2608 chip(interface);
    chip.set_fidelity(ymfm::OPN_FIDELITY_MIN); chip.reset();
    // A resolved tone, not period 1 (which the SSG resampler averages toward
    // DC and therefore cannot measure the full DAC excursion).
    write(chip, 0, 0); write(chip, 1, 1); write(chip, 7, 0x3e); write(chip, 8, 15);
    ymfm::ym2608::output_data output;
    int peak = 0;
    for (unsigned i = 0; i < 4096; ++i) {
        chip.generate(&output); peak = std::max(peak, std::abs(output.data[2]));
    }
    if (!peak) throw std::runtime_error("SSG calibration was silent");
    return peak;
}
}

int main(int argc, char **argv) try {
    if (argc != 3) throw std::runtime_error("usage: render_opna_sfx <events.bin> <out.pcm>");
    std::ifstream f(argv[1], std::ios::binary);
    if (!f || le32(f) != 0x31584653) throw std::runtime_error("invalid SFX register input");
    const uint32_t tickClocks = le32(f), ticks = le32(f), count = le32(f);
    if (!tickClocks || ticks > 4096 || count > 1000000) throw std::runtime_error("SFX budget exceeded");
    struct Event { uint32_t tick, reg, value; };
    std::vector<Event> events;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t tick = le32(f), packed = le32(f);
        if (tick >= ticks || (packed & 255) > 10 || packed > 65535 ||
            (!events.empty() && tick < events.back().tick)) throw std::runtime_error("bad SFX register event");
        events.push_back({tick, packed & 255, packed >> 8});
    }
    Interface interface;
    ymfm::ym2608 chip(interface);
    chip.set_fidelity(ymfm::OPN_FIDELITY_MIN); chip.reset();
    write(chip, 7, 0x3f);
    const uint32_t rate = chip.sample_rate(clockRate);
    const double seconds = double(tickClocks) * ticks / clockRate + .1;
    const size_t frames = size_t(std::ceil(seconds * rate));
    std::vector<double> native(frames);
    // One common gain, never per-effect normalization. A centered full-level
    // SSG tone maps to PCM16 peak 12544, matching the approved KAMI2 tone zone's
    // PCM8 peak 49. Runtime applies the shared SSG mix/headroom once.
    const double gain = 25088.0 / peakCalibration();
    const double coefficient = std::exp(-2 * 3.141592653589793 * 10 / rate);
    double previousInput = 0, filtered = 0;
    size_t event = 0;
    unsigned mixer = 0x3f, volumes[3]{};
    constexpr unsigned dac[16] = {0, 141, 222, 306, 441, 585, 836, 1112,
        1595, 2146, 3081, 4135, 6006, 8155, 11976, 16382};
    ymfm::ym2608::output_data output;
    for (size_t i = 0; i < frames; ++i) {
        while (event < events.size() && uint64_t(i) * clockRate >= uint64_t(events[event].tick) * tickClocks * rate) {
            if (events[event].reg == 7) mixer = events[event].value;
            if (events[event].reg >= 8) volumes[events[event].reg - 8] = events[event].value & 15;
            write(chip, events[event].reg, events[event].value); ++event;
        }
        chip.generate(&output);
        // Center the unipolar DAC before filtering. Tone AND noise has a 1/4
        // expected duty, either alone 1/2. ymfm's mixed SSG output has 2/3 gain.
        // This is shared DAC-level centering, not per-effect normalization.
        double center = 0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const bool tone = !(mixer & (1u << channel));
            const bool noise = !(mixer & (8u << channel));
            const double duty = tone && noise ? .25 : tone || noise ? .5 : 1.;
            center += dac[volumes[channel]] * duty * 2 / 3;
        }
        const double input = output.data[2] - center;
        filtered = input - previousInput + coefficient * filtered;
        previousInput = input;
        native[i] = filtered * gain;
    }
    std::ofstream out(argv[2], std::ios::binary);
    if (!out) throw std::runtime_error("cannot write SFX PCM");
    const size_t samples = (size_t(std::ceil(seconds * outputRate)) + 1) & ~size_t(1);
    int peak = 0;
    for (size_t i = 0; i < samples; ++i) {
        size_t a = i * frames / samples, b = std::max(a + 1, (i + 1) * frames / samples);
        double sum = 0;
        for (size_t j = a; j < b; ++j) sum += native[j];
        double value = sum / (b - a);
        // Only the final 5 ms of the fixed DC-filter tail fades to zero.
        if (samples - i < outputRate / 200) value *= double(samples - i - 1) / (outputRate / 200);
        const long rounded = std::lround(value);
        if (rounded < -32767 || rounded > 32767) throw std::runtime_error("SFX common gain clips; lower the common gain, not individual effects");
        int16_t pcm = int16_t(rounded);
        peak = std::max(peak, std::abs(int(pcm)));
        out.put(char(uint16_t(pcm) & 255)); out.put(char(uint16_t(pcm) >> 8));
    }
    if (!out || !peak) throw std::runtime_error("SFX output failed/silent");
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what()); return 1;
}
