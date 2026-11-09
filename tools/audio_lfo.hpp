// Shared integer MDRV98 software LFO state for conversion and native playback.
// The floating-point scalar helpers below are host-conversion-only; no chip
// emulator or floating-point LFO calculation runs on the DS.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

struct MdrvSoftwareLfo
{
    int kind;
    std::array<int, 4> p;
    int accumulator = 0, phase = 0, value = 0, delay;

    MdrvSoftwareLfo(int shape, std::array<int, 4> parameters)
        : kind(shape), p(parameters), delay((p[3] + 1) & 255)
    {
        if (kind == 2 || kind == 4)
            p[1] = ((p[1] >> 1) | (p[1] << 7)) & 255;
    }

    // Call on clocks after key-on, not on the key-on clock itself.
    int tick(bool ssg = false)
    {
        const bool amplitude = kind >= 3;
        const bool saw = kind == 2 || kind == 4;
        if (!kind)
            return 0;
        if (delay) {
            if (--delay)
                return value;
            phase = ((p[1] >> 1) | (p[1] & 128)) & 0xbf; // SAR AL,1; AND AL,BF
            accumulator = 0;
            if (amplitude)
                value = saw && !(phase & 128) ? 127 : 0;
        }
        if (!p[0])
            return value;
        const int sum = accumulator + p[0];
        accumulator = sum & 255;
        if (sum <= 255)
            return value;
        const int depth = amplitude || ssg ? p[2] : p[2] >> 2;
        if (amplitude) {
            value = (value + ((phase & 128) ? depth : -depth)) & 255;
        } else {
            value += (phase & 128) ? -depth : depth;
        }
        if (!saw) {
            phase = (phase - 1) & 255;
            if (!(phase & 127))
                phase = (((p[1] & 127) + phase) ^ 128) & 255;
        }
        return value;
    }
};

inline int mdrv_ssg_lfo_attenuation(int raw) { return raw >> 3; }

// Scalar carrier-TL approximation: preserve the driver's unsigned addition,
// wrap and clamp, rather than interpreting the raw byte as unsigned gain.
// Different carrier TLs can clip differently; that timbre change is not modeled.
inline int mdrv_fm_lfo_volume(int midi_volume, int raw)
{
    if (!raw)
        return (midi_volume + 1) / 2;
    if (!midi_volume)
        return 0;
    const int base = std::clamp((int)std::lround(-20.0 / 0.75 *
                                      std::log10(midi_volume / 127.0)), 0, 127);
    const int attenuation = std::min(127, (base + raw) & 255);
    return std::clamp((int)std::lround(64.0 *
                         std::pow(10.0, -0.75 * attenuation / 20.0)), 0, 64);
}
