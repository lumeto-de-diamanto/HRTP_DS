#include "audio_lfo.hpp"
#include <cassert>
#include <cstdio>

int main()
{
    MdrvSoftwareLfo pitch(1, {200, 5, 7, 30});
    for (int clock = 1; clock <= 31; clock++)
        assert(pitch.tick() == 0);
    const int triangle[] = {1, 2, 1, 1, 0, -1, -2, -3, -3, -2, -1, 0};
    for (int expected : triangle)
        assert(pitch.tick() == expected);

    MdrvSoftwareLfo saw(4, {60, 2, 5, 0});
    for (int clock = 1; clock <= 120; clock++) {
        const int raw = (127 - (clock * 60 / 256) * 5) & 255;
        assert(saw.tick(true) == raw);
        assert(mdrv_ssg_lfo_attenuation(raw) == raw / 8);
    }
    MdrvSoftwareLfo amp(3, {20, 10, 1, 0});
    for (int clock = 1; clock <= 12; clock++)
        assert(amp.tick() == 0);
    assert(amp.tick() == 255); // subtract one, wrap; not 31 dB-step attenuation
    for (int clock = 14; clock <= 25; clock++)
        assert(amp.tick() == 255);
    assert(amp.tick() == 254);
    assert(mdrv_fm_lfo_volume(107, 255) > mdrv_fm_lfo_volume(107, 0));
    assert(mdrv_fm_lfo_volume(107, 254) == 64);
    assert(mdrv_fm_lfo_volume(107, 253) == 0); // TL underflow wraps, then clips
    assert(mdrv_fm_lfo_volume(107, 5) < mdrv_fm_lfo_volume(107, 0));
    MdrvSoftwareLfo disabled(1, {0, 0, 0, 0});
    for (int i = 0; i < 300; i++)
        assert(disabled.tick() == 0);
    std::puts("LFO checks: pitch delay/carry/triangle, SSG saw /8, FM raw-byte TL wrap passed");
}
