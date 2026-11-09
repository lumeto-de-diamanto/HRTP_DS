// Render original MDRV2 OPNA patches into a small tracker sample bank.
// ymfm is an external BSD-3-Clause build dependency; no emulator runs on DS.
#include "ymfm_opn.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kClock = 7987200;
constexpr uint32_t kOutputRate = 22050;
constexpr double kRenderSeconds = 1.50;
constexpr int kLoopCycles = 64;

enum class SampleKind : uint8_t { Fm = 0, Ssg = 1 };

struct Operator {
    int ar = 0, dr = 0, sr = 0, rr = 0, sl = 0, tl = 0;
    int ks = 0, mul = 0, dt = 0, am = 0;
};

struct Patch {
    int feedback = 0, algorithm = 0, ams = 0, pms = 0;
    uint8_t key_mask = 0xf0;
    std::array<Operator, 4> op;
    int operator_count = 0;
};

struct Usage { int program, low, high; };

struct SsgUsage {
    int program = 0, low = 0, high = 0;
    int mixer = 1, noise = 0;
    std::array<int, 6> envelope{};
};

struct Rendered {
    SampleKind kind = SampleKind::Fm;
    int program = 0, root = 60, low = 0, high = 119;
    uint32_t c5speed = kOutputRate;
    uint32_t loop_begin = 0, loop_end = 0;
    int mixer = 0, noise = 0;
    std::array<int, 6> envelope{};
    std::vector<int32_t> pcm;
};

class Interface : public ymfm::ymfm_interface { };

std::vector<int> parse_numbers(const std::string &line)
{
    std::vector<int> values;
    std::istringstream in(line.substr(line.find(':') + 1));
    int value;
    while (in >> value)
        values.push_back(value);
    return values;
}

std::map<int, Patch> read_opm(const char *path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error(std::string("cannot read OPM: ") + path);
    std::map<int, Patch> result;
    Patch *current = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("@:", 0) == 0) {
            int number = -1;
            if (std::sscanf(line.c_str(), "@:%d", &number) != 1 || number < 0 || number > 127)
                throw std::runtime_error("bad OPM patch number");
            current = &result[number];
        } else if (current && line.rfind("CH:", 0) == 0) {
            auto v = parse_numbers(line);
            if (v.size() != 7)
                throw std::runtime_error("bad OPM channel line");
            current->feedback = v[1];
            current->algorithm = v[2];
            current->ams = v[3];
            current->pms = v[4];
            current->key_mask = uint8_t((v[5] & 0x78) << 1);
        } else if (current && (line.rfind("M1:", 0) == 0 ||
                               line.rfind("C1:", 0) == 0 ||
                               line.rfind("M2:", 0) == 0 ||
                               line.rfind("C2:", 0) == 0)) {
            auto v = parse_numbers(line);
            if (v.size() != 11 || current->operator_count >= 4)
                throw std::runtime_error("bad OPM operator line");
            Operator &op = current->op[current->operator_count++];
            op.ar = v[0]; op.dr = v[1]; op.sr = v[2]; op.rr = v[3];
            op.sl = v[4]; op.tl = v[5]; op.ks = v[6]; op.mul = v[7];
            op.dt = v[8]; op.am = v[10];
        }
    }
    for (const auto &entry : result)
        if (entry.second.operator_count != 4)
            throw std::runtime_error("incomplete OPM patch");
    return result;
}

std::vector<Usage> read_usage(const char *path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error(std::string("cannot read FM usage: ") + path);
    std::vector<Usage> result;
    Usage u;
    while (in >> u.program >> u.low >> u.high) {
        if (u.program < 0 || u.program > 127 || u.low < 0 || u.high > 119 || u.low > u.high)
            throw std::runtime_error("bad FM usage line");
        result.push_back(u);
    }
    if (result.empty())
        throw std::runtime_error("FM usage is empty");
    return result;
}

std::vector<SsgUsage> read_ssg_usage(const char *path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error(std::string("cannot read SSG usage: ") + path);
    std::vector<SsgUsage> result;
    SsgUsage u;
    while (in >> u.program >> u.low >> u.high >> u.mixer >> u.noise
              >> u.envelope[0] >> u.envelope[1]
              >> u.envelope[2] >> u.envelope[3] >> u.envelope[4] >> u.envelope[5]) {
        if (u.program < 0 || u.program > 127 || u.low < 0 || u.high > 119 || u.low > u.high ||
            u.mixer < 1 || u.mixer > 3 || u.noise < 0 || u.noise > 31)
            throw std::runtime_error("bad SSG usage line");
        for (int value : u.envelope)
            if (value < 0 || value > 255)
                throw std::runtime_error("bad SSG envelope byte");
        result.push_back(u);
    }
    return result;
}

void write_reg(ymfm::ym2608 &chip, uint8_t address, uint8_t value)
{
    chip.write_address(address);
    chip.write_data(value);
}

std::pair<int, int> frequency_registers(double hz)
{
    // YM2608 manual: FNUM=(144*f*2^20/clock)/2^(block-1).
    int block = 4;
    double fnum = 144.0 * hz * std::pow(2.0, 20) / kClock /
                  std::pow(2.0, block - 1);
    while (fnum > 2047 && block < 7) { block++; fnum *= 0.5; }
    while (fnum < 512 && block > 0) { block--; fnum *= 2.0; }
    int rounded = std::clamp((int)std::lround(fnum), 0, 2047);
    return {block, rounded};
}

double register_frequency(int block, int fnum)
{
    return fnum * (double)kClock * std::pow(2.0, block - 1) /
           (144.0 * std::pow(2.0, 20));
}

template<class Writer> void program_patch(const Patch &patch, Writer write)
{
    // OPM labels are in logical connection order: M1,C1,M2,C2. OPN's
    // physical register order is S1,S3,S2,S4. The MDT exporter has ALREADY
    // permuted the raw slots [0,2,1,3] into those labels; undo that permutation
    // here. Sequential offsets silently swap the middle operators, and can
    // route algorithm-4 patches into carriers with AR=0 (complete silence).
    static constexpr int offsets[4] = {0, 8, 4, 12};
    for (int i = 0; i < 4; i++) {
        const Operator &op = patch.op[i];
        int o = offsets[i];
        write(0x30 + o, ((op.dt & 7) << 4) | (op.mul & 15));
        write(0x40 + o, op.tl & 127);
        write(0x50 + o, ((op.ks & 3) << 6) | (op.ar & 31));
        write(0x60 + o, ((op.am & 1) << 7) | (op.dr & 31));
        write(0x70 + o, op.sr & 31);
        write(0x80 + o, ((op.sl & 15) << 4) | (op.rr & 15));
        write(0x90 + o, 0);
    }
    write(0xb0, ((patch.feedback & 7) << 3) | (patch.algorithm & 7));
    write(0xb4, 0xc0 | ((patch.ams & 3) << 4) | (patch.pms & 7));
}

std::vector<int32_t> render_native(const Patch &patch, int root)
{
    Interface intf;
    ymfm::ym2608 chip(intf);
    chip.set_fidelity(ymfm::OPN_FIDELITY_MIN);
    chip.reset();
    write_reg(chip, 0x29, 0x80); // six-channel mode
    write_reg(chip, 0x22, 0x00); // hardware LFO is not baked into samples
    program_patch(patch, [&](uint8_t reg, uint8_t value) { write_reg(chip, reg, value); });
    double hz = 440.0 * std::pow(2.0, (root - 69) / 12.0);
    auto [block, fnum] = frequency_registers(hz);
    write_reg(chip, 0xa4, (block << 3) | ((fnum >> 8) & 7));
    write_reg(chip, 0xa0, fnum & 255);
    write_reg(chip, 0x28, patch.key_mask); // source operator enable mask, channel 0

    uint32_t native_rate = chip.sample_rate(kClock);
    size_t native_count = (size_t)std::ceil(kRenderSeconds * native_rate);
    std::vector<int32_t> native(native_count);
    ymfm::ym2608::output_data output;
    for (size_t i = 0; i < native_count; i++) {
        chip.generate(&output);
        native[i] = (output.data[0] + output.data[1]) / 2;
    }

    size_t output_count = (size_t)std::floor(kRenderSeconds * kOutputRate);
    std::vector<int32_t> result(output_count);
    for (size_t i = 0; i < output_count; i++) {
        size_t a = i * native_count / output_count;
        size_t b = std::max(a + 1, (i + 1) * native_count / output_count);
        int64_t sum = 0;
        for (size_t j = a; j < b; j++)
            sum += native[j];
        result[i] = (int32_t)(sum / (int64_t)(b - a));
    }
    return result;
}

double rms(const std::vector<int32_t> &pcm, size_t begin, size_t end)
{
    begin = std::min(begin, pcm.size());
    end = std::min(end, pcm.size());
    if (begin >= end)
        return 0;
    long double sum = 0;
    for (size_t i = begin; i < end; i++)
        sum += (long double)pcm[i] * pcm[i];
    return std::sqrt((double)(sum / (end - begin)));
}

struct LoopChoice {
    uint32_t begin = 0, end = 0;
    int cycles = 0;
    long double error = 0;
};

LoopChoice find_phase_locked_loop(const std::vector<int32_t> &pcm, double actual_hz)
{
    LoopChoice best;
    best.error = std::numeric_limits<long double>::infinity();
    constexpr uint32_t window = 256;
    uint32_t begin_min = (uint32_t)(0.82 * kOutputRate) & ~3u;
    uint32_t begin_max = (uint32_t)(1.02 * kOutputRate) & ~3u;
    // Very low notes cannot fit 64 complete cycles in the rendered tail.
    // Retain 64 where possible (the approved ST0 path), otherwise choose the
    // largest whole-cycle neighborhood that fits without lengthening every
    // sample bank.
    int fit_cycles = (int)std::floor((pcm.size() - begin_max - window) *
                                     actual_hz / kOutputRate);
    int nominal_cycles = std::clamp(fit_cycles, 4, kLoopCycles);
    int radius = std::min(12, std::max(2, nominal_cycles / 4));
    for (int cycles = std::max(1, nominal_cycles - radius);
         cycles <= nominal_cycles + radius; cycles++) {
        int expected = (int)std::lround(cycles * kOutputRate / actual_hz);
        for (int delta = -32; delta <= 32; delta += 4) {
            uint32_t period = (uint32_t)std::max(8, expected + delta);
            period = (period + 2) & ~3u;
            // The +/-32-frame seam search can span more than a whole cycle
            // at high roots. A good seam from the neighboring cycle count
            // must not inherit this candidate's count: c5speed divides by
            // it below, turning a one-cycle mislabel into ~27 cents of error.
            // Adjacent counts are searched independently; keep only lengths
            // that actually belong to this count after hardware alignment.
            if (std::lround(period * actual_hz / kOutputRate) != cycles)
                continue;
            for (uint32_t begin = begin_min; begin <= begin_max; begin += 4) {
                uint32_t end = begin + period;
                if (end + window > pcm.size())
                    continue;
                long double error = 0;
                for (uint32_t i = 0; i < window; i++) {
                    long double difference = (long double)pcm[begin + i] - pcm[end + i];
                    error += difference * difference;
                }
                // Prefer the nominal cycle count when seams are effectively tied.
                error *= 1.0L + 0.00001L * std::abs(cycles - nominal_cycles);
                if (error < best.error)
                    best = {begin, end, cycles, error};
            }
        }
    }
    if (!best.end)
        throw std::runtime_error("could not find an aligned FM loop");
    return best;
}

Rendered render_zone(int program, const Patch &patch, int root, int low, int high)
{
    Rendered out;
    out.program = program; out.root = root; out.low = low; out.high = high;
    out.pcm = render_native(patch, root);
    if (std::none_of(out.pcm.begin(), out.pcm.end(), [](int32_t value) { return value != 0; }))
        throw std::runtime_error("used FM patch @" + std::to_string(program) +
            " root " + std::to_string(root) + " rendered silence; verify operator mapping/key-on mask");
    double hz = 440.0 * std::pow(2.0, (root - 69) / 12.0);
    size_t early = (size_t)(0.70 * kOutputRate);
    size_t late = (size_t)(1.15 * kOutputRate);
    double early_rms = rms(out.pcm, early, early + kOutputRate / 5);
    double late_rms = rms(out.pcm, late, late + kOutputRate / 5);
    bool sustained = late_rms > 2 && early_rms > 0 && late_rms / early_rms > 0.70;
    if (sustained) {
        auto [block, fnum] = frequency_registers(hz);
        LoopChoice loop = find_phase_locked_loop(out.pcm, register_frequency(block, fnum));
        out.loop_begin = loop.begin;
        out.loop_end = loop.end;
        out.pcm.resize(out.loop_end);
        double exact_rate = hz * (out.loop_end - out.loop_begin) / loop.cycles;
        out.c5speed = (uint32_t)std::lround(exact_rate * std::pow(2.0, (60 - root) / 12.0));
    } else {
        out.c5speed = (uint32_t)std::lround(kOutputRate * std::pow(2.0, (60 - root) / 12.0));
        // Keep the natural decay, but remove only a clearly silent tail.
        size_t keep = out.pcm.size();
        int32_t peak = 0;
        for (int32_t value : out.pcm) peak = std::max(peak, std::abs(value));
        int32_t threshold = std::max(2, peak / 2000);
        while (keep > kOutputRate / 4 && std::abs(out.pcm[keep - 1]) < threshold)
            keep--;
        out.pcm.resize(keep);
    }
    return out;
}

int32_t measure_ssg_peak()
{
    Interface intf;
    ymfm::ym2608 chip(intf);
    chip.set_fidelity(ymfm::OPN_FIDELITY_MIN);
    chip.reset();
    // Tone A only, noise disabled, fixed SSG volume 15.
    write_reg(chip, 0x00, 1);
    write_reg(chip, 0x01, 0);
    write_reg(chip, 0x07, 0x3e);
    write_reg(chip, 0x08, 15);
    ymfm::ym2608::output_data output;
    int32_t peak = 0;
    for (int i = 0; i < 4096; i++) {
        chip.generate(&output);
        peak = std::max(peak, std::abs(output.data[2]));
    }
    if (!peak)
        throw std::runtime_error("YM2608 SSG calibration was silent");
    return peak;
}

Rendered render_ssg_zone(const SsgUsage &usage, int root, int low, int high,
                          int32_t ssg_peak)
{
    Rendered out;
    out.kind = SampleKind::Ssg;
    out.program = usage.program; out.root = root; out.low = low; out.high = high;
    out.mixer = usage.mixer; out.noise = usage.noise; out.envelope = usage.envelope;
    double hz = 440.0 * std::pow(2.0, (root - 69) / 12.0);
    int tone_period = std::clamp((int)std::lround(kClock / (64.0 * hz)), 1, 4095);
    double actual_hz = kClock / (64.0 * tone_period);

    if (usage.mixer & 2) {
        // Noise and tone+noise use the YM2608's actual 17-bit generator. A
        // half-second loop is long enough to avoid a tonal short-noise cycle;
        // a discontinuity at its boundary is itself noise-like. Envelope
        // evolution is intentionally left to tracker volume rows.
        Interface intf;
        ymfm::ym2608 chip(intf);
        chip.set_fidelity(ymfm::OPN_FIDELITY_MIN);
        chip.reset();
        write_reg(chip, 0x00, tone_period & 255);
        write_reg(chip, 0x01, (tone_period >> 8) & 15);
        write_reg(chip, 0x06, usage.noise & 31);
        int mixer = 0x36;
        if (!(usage.mixer & 1)) mixer |= 0x01;
        if (!(usage.mixer & 2)) mixer |= 0x08;
        write_reg(chip, 0x07, mixer);
        write_reg(chip, 0x08, 15);
        uint32_t native_rate = chip.sample_rate(kClock);
        size_t native_count = native_rate / 2;
        size_t output_count = (kOutputRate / 2) & ~3u;
        std::vector<int32_t> native(native_count);
        ymfm::ym2608::output_data output;
        for (size_t i = 0; i < native_count; i++) {
            chip.generate(&output);
            native[i] = output.data[2] / 2;
        }
        out.pcm.resize(output_count);
        for (size_t i = 0; i < output_count; i++) {
            size_t a = i * native_count / output_count;
            size_t b = std::max(a + 1, (i + 1) * native_count / output_count);
            int64_t sum = 0;
            for (size_t j = a; j < b; j++) sum += native[j];
            out.pcm[i] = (int32_t)(sum / (int64_t)(b - a));
        }
        out.loop_begin = 0;
        out.loop_end = (uint32_t)out.pcm.size();
        out.c5speed = (uint32_t)std::lround(kOutputRate * std::pow(2.0, (60 - root) / 12.0));
        return out;
    }

    uint32_t period = (uint32_t)std::lround(kLoopCycles * kOutputRate / actual_hz);
    period = std::max<uint32_t>(8, (period + 2) & ~3u);
    out.pcm.resize(period);
    int32_t amplitude = std::max<int32_t>(1, ssg_peak / 2);
    for (uint32_t i = 0; i < period; i++) {
        uint64_t phase = (uint64_t)i * kLoopCycles * 2 / period;
        out.pcm[i] = (phase & 1) ? -amplitude : amplitude;
    }
    out.loop_begin = 0;
    out.loop_end = period;
    double exact_rate = hz * period / kLoopCycles;
    out.c5speed = (uint32_t)std::lround(exact_rate * std::pow(2.0, (60 - root) / 12.0));
    return out;
}

template<typename T> void write_le(std::ofstream &out, T value)
{
    for (size_t i = 0; i < sizeof(T); i++)
        out.put((char)((uint64_t)value >> (i * 8)));
}

void write_bank(const char *path, std::vector<Rendered> &samples)
{
    int32_t peak = 0;
    for (const auto &sample : samples)
        for (int32_t value : sample.pcm)
            peak = std::max(peak, std::abs(value));
    if (!peak)
        throw std::runtime_error("all rendered FM patches are silent");
    double scale = 118.0 / peak; // common gain preserves patch-to-patch balance
    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error(std::string("cannot write FM bank: ") + path);
    out.write("OPNABK3\0", 8);
    write_le<uint16_t>(out, (uint16_t)samples.size());
    for (const auto &sample : samples) {
        out.put((char)sample.kind); out.put((char)sample.program); out.put((char)sample.root);
        out.put((char)sample.low); out.put((char)sample.high);
        out.put((char)sample.mixer); out.put((char)sample.noise);
        for (int value : sample.envelope) out.put((char)value);
        write_le<uint32_t>(out, sample.c5speed);
        write_le<uint32_t>(out, (uint32_t)sample.pcm.size());
        write_le<uint32_t>(out, sample.loop_begin);
        write_le<uint32_t>(out, sample.loop_end);
        bool audible = false;
        std::vector<int8_t> quantized_pcm;
        quantized_pcm.reserve(sample.pcm.size());
        for (int32_t value : sample.pcm) {
            int quantized = (int)std::lround(value * scale);
            const int8_t pcm = std::clamp(quantized, -127, 127);
            quantized_pcm.push_back(pcm);
            audible |= pcm != 0;
        }
        if (!audible)
            throw std::runtime_error("used OPNA sample @" + std::to_string(sample.program) +
                " root " + std::to_string(sample.root) + " is silent after PCM8 quantization");
        out.write(reinterpret_cast<const char *>(quantized_pcm.data()), quantized_pcm.size());
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 5) {
        std::fprintf(stderr, "usage: render_opna_fm <patches.opm> <fm_usage.txt> <ssg_usage.txt> <out.bank>\n");
        return 1;
    }
    try {
        auto patches = read_opm(argv[1]);
        auto usage = read_usage(argv[2]);
        auto ssg_usage = read_ssg_usage(argv[3]);
        std::vector<Rendered> samples;
        for (const Usage &u : usage) {
            auto found = patches.find(u.program);
            if (found == patches.end())
                throw std::runtime_error("used FM patch absent from OPM");
            int first = ((u.low + 6) / 12) * 12;
            int last = ((u.high + 6) / 12) * 12;
            // Preserve the approved root-zone policy and tuning grid. Render
            // at root >=24 and let the native event timer reach lower keys.
            first = std::clamp(first, 24, 108);
            last = std::clamp(last, first, 108);
            std::vector<int> roots;
            for (int root = first; root <= last; root += 12)
                roots.push_back(root);
            for (size_t i = 0; i < roots.size(); i++) {
                int low = i == 0 ? u.low : (roots[i - 1] + roots[i]) / 2;
                int high = i + 1 == roots.size() ? u.high : (roots[i] + roots[i + 1]) / 2 - 1;
                samples.push_back(render_zone(u.program, found->second, roots[i], low, high));
            }
        }
        int32_t ssg_peak = measure_ssg_peak();
        for (const SsgUsage &u : ssg_usage) {
            int first = ((u.low + 6) / 12) * 12;
            int last = ((u.high + 6) / 12) * 12;
            first = std::clamp(first, 24, 108);
            last = std::clamp(last, first, 108);
            std::vector<int> roots;
            for (int root = first; root <= last; root += 12)
                roots.push_back(root);
            for (size_t i = 0; i < roots.size(); i++) {
                int low = i == 0 ? u.low : (roots[i - 1] + roots[i]) / 2;
                int high = i + 1 == roots.size() ? u.high : (roots[i] + roots[i + 1]) / 2 - 1;
                samples.push_back(render_ssg_zone(u, roots[i], low, high, ssg_peak));
            }
        }
        if (samples.size() > 256)
            throw std::runtime_error("OPNA bank exceeds native sample budget");
        write_bank(argv[4], samples);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "render_opna_fm: %s\n", e.what());
        return 1;
    }
    return 0;
}
