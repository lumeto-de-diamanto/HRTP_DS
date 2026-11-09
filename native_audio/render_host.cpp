// Analysis-only PCM stem renderer for comparing recordings. Never linked into
// a ROM and never used as a music asset. Reuses the actual native sequencer.
#include "engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

struct HostSound {
    bool silent = false;
    const uint8_t *blob = nullptr;
    struct Voice {
        const int8_t *pcm = nullptr;
        uint32_t length = 0, loop = 0;
        bool active = false, looped = false;
        double position = 0;
        uint16_t period = 1, gain = 0;
    } voices[NS_SLOTS];
    unsigned channel(unsigned slot) const { return slot; }
    bool busy(unsigned slot) const { return voices[slot].active; }
    void stop(unsigned slot) { voices[slot].active = false; }
    void stopAll() { for (auto &v : voices) v.active = false; }
    void start(unsigned slot, const NsSample &s, uint32_t offset, uint16_t period) {
        auto &v = voices[slot];
        v.pcm = reinterpret_cast<const int8_t *>(blob + s.offset);
        v.length = s.length; v.loop = s.loopStart; v.looped = s.flags & 1;
        v.position = offset; v.period = period; v.active = true;
    }
    void control(unsigned slot, uint16_t period, uint16_t gain, uint8_t) {
        voices[slot].period = period;
        voices[slot].gain = native_audio::hardware_gain(gain);
    }
    void sample(float *out, unsigned rate) {
        constexpr unsigned divisors[] = {1, 2, 4, 16};
        for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
            auto &v = voices[slot];
            out[slot] = 0;
            if (!v.active) continue;
            if (v.position >= v.length) {
                if (!v.looped) { v.active = false; continue; }
                v.position = v.loop + std::fmod(v.position - v.loop, v.length - v.loop);
            }
            unsigned mul = v.gain & 127;
            if (mul == 127) mul = 128;
            // Mono mean of centered left/right outputs, before master mixer
            // clipping. DS nearest-held PCM and multiplier/divider modeled;
            // analogue filtering, MP3 and sample-accurate DAC timing are not.
            out[slot] = v.pcm[unsigned(v.position)] / 128.0f * mul /
                (128.0f * divisors[(v.gain >> 8) & 3]) * 0.5f;
            v.position += 16756991.0 / (v.period * double(rate));
        }
    }
};

int main(int argc, char **argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: render_host <song.nsq> <out.f32> <first-beat> <end-beat>\n");
        return 1;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(f)), {});
    HostSound sound;
    native_audio::Engine<HostSound> engine(sound);
    if (!engine.load(data.data(), data.size())) return 2;
    sound.blob = reinterpret_cast<const uint8_t *>(data.data());
    const unsigned first = std::strtoul(argv[3], nullptr, 10) * 48;
    const unsigned end = std::strtoul(argv[4], nullptr, 10) * 48;
    if (first >= end || end > engine.header->comparisonTicks) return 3;
    FILE *output = std::fopen(argv[2], "wb");
    if (!output) return 4;
    engine.reset();
    native_audio::Clock phase;
    constexpr unsigned rate = 22050;
    constexpr double interrupt = 32768.0 / 33513982.0;
    double next = interrupt, start = -1;
    uint64_t frames = 0;
    float samples[NS_SLOTS];
    for (uint64_t i = 0; engine.tick < end; ++i) {
        const double time = double(i) / rate;
        while (time >= next) {
            if (phase.interrupt(engine.tempo)) engine.step();
            next += interrupt;
        }
        sound.sample(samples, rate);
        if (engine.tick >= first && engine.tick < end) {
            if (start < 0) start = time;
            if (std::fwrite(samples, sizeof(float), NS_SLOTS, output) != NS_SLOTS) return 5;
            ++frames;
        }
    }
    if (std::fclose(output)) return 6;
    std::printf("{\"rate\":%u,\"channels\":%u,\"frames\":%llu,\"source_start_seconds\":%.9f,\"source_end_seconds\":%.9f}\n",
        rate, NS_SLOTS, (unsigned long long)frames, start, start + double(frames) / rate);
}
