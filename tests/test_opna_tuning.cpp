// Actual renderer pitch checks, independent of IT/MAS metadata preservation.
#define main renderer_program_main
#include "render_opna_fm.cpp"
#undef main
#include <cassert>

static double cents(double actual, double expected)
{
    return 1200 * std::log2(actual / expected);
}

static double crossing_frequency(const std::vector<int32_t> &pcm, size_t begin, size_t end)
{
    // The sentinel has exactly one undetuned carrier, so positive zero
    // crossings measure its fundamental without a harmonic-picking heuristic.
    double first = 0, last = 0;
    unsigned crossings = 0;
    for (size_t i = std::max<size_t>(1, begin); i < end; ++i) {
        if (pcm[i - 1] <= 0 && pcm[i] > 0) {
            double at = (i - 1) + double(-pcm[i - 1]) / (pcm[i] - pcm[i - 1]);
            if (!crossings) first = at;
            last = at;
            ++crossings;
        }
    }
    assert(crossings > 8 && last > first);
    return (crossings - 1) / (last - first); // cycles per PCM frame
}

int main()
{
    unsigned checked = 0;
    double worst = 0;
    for (int root : {48, 60, 72, 84, 96, 108}) {
        const double hz = 440 * std::pow(2.0, (root - 69) / 12.0);
        // Pure sine at a known frequency: the search must never attach an
        // adjacent cycle count to a good seam, even when +/-32 samples spans
        // multiple high-note cycles.
        std::vector<int32_t> sine(size_t(kRenderSeconds * kOutputRate));
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = std::lround(20000 * std::sin(2 * std::acos(-1) * hz * i / kOutputRate));
        const auto choice = find_phase_locked_loop(sine, hz);
        const double cycles = (choice.end - choice.begin) * hz / kOutputRate;
        if (std::lround(cycles) != choice.cycles) {
            std::fprintf(stderr, "FAIL: root %d loop contains %.4f cycles but claims %d\n",
                         root, cycles, choice.cycles);
            return 1;
        }
        ++checked;

        Patch patch;
        patch.algorithm = 7;
        patch.key_mask = 0x80;
        patch.op[3].ar = 31;
        patch.op[3].mul = 1;
        const auto zone = render_zone(0, patch, root, root, root);
        assert(zone.loop_end > zone.loop_begin);
        const double observed = crossing_frequency(zone.pcm, zone.loop_begin, zone.loop_end);
        const double rate = zone.c5speed * std::pow(2.0, (root - 60) / 12.0);
        const double error = std::abs(cents(observed * rate, hz));
        worst = std::max(worst, error);
        if (error > 2) {
            std::fprintf(stderr, "FAIL: actual rendered FM root %d is %.4f cents off\n", root, error);
            return 1;
        }
        ++checked;
        SsgUsage usage;
        const auto ssg = render_ssg_zone(usage, root, root, root, 1000);
        const double ssg_hz = double(kLoopCycles) * ssg.c5speed *
            std::pow(2.0, (root - 60) / 12.0) / (ssg.loop_end - ssg.loop_begin);
        assert(std::abs(cents(ssg_hz, hz)) < 2);
        assert(std::abs(cents(observed * rate, ssg_hz)) < 2);
        ++checked;
    }
}
