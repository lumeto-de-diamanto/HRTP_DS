// Verify the real renderer directly against raw MDT patch bytes.
#define main renderer_program_main
#include "render_opna_fm.cpp"
#undef main
#include <cassert>

static std::vector<uint8_t> read_bytes(const char *path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

int main(int argc, char **argv)
{
    assert(argc == 4); // MDT, its OPM export, actually used FM programs
    const auto bytes = read_bytes(argv[1]);
    assert(bytes.size() >= 12);
    auto le16 = [&](size_t p) { assert(p + 2 <= bytes.size()); return bytes[p] | bytes[p + 1] << 8; };
    const unsigned channels = le16(2);
    const unsigned table = le16(6 + channels * 4), tableEnd = le16(8 + channels * 4);
    assert(table < tableEnd && tableEnd <= bytes.size() && (tableEnd - table) % 32 == 0);
    const auto patches = read_opm(argv[2]);
    const auto usage = read_usage(argv[3]);
    unsigned checked = 0;
    for (const Usage &u : usage) {
        assert(table + (u.program + 1) * 32 <= tableEnd);
        const auto *raw = bytes.data() + table + u.program * 32;
        std::map<int, int> programmed;
        const Patch &patch = patches.at(u.program);
        program_patch(patch, [&](uint8_t address, uint8_t value) { programmed[address] = value; });
        // MDRV98's EB handler consumes four physical slots consecutively:
        // 0x30,34,38,3c, then 0x40,44,48,4c, etc. Do not repeat the OPM
        // exporter's logical operator permutation in this reference path.
        for (unsigned physical = 0; physical < 4; ++physical) {
            const unsigned offset = physical * 4;
            assert(programmed.at(0x30 + offset) == (raw[6 + physical] & 0x7f));
            assert(programmed.at(0x40 + offset) == (raw[10 + physical] & 0x7f));
            assert(programmed.at(0x50 + offset) == (raw[14 + physical] & 0xdf));
            assert(programmed.at(0x60 + offset) == (raw[18 + physical] & 0x9f));
            assert(programmed.at(0x70 + offset) == (raw[22 + physical] & 0x1f));
            assert(programmed.at(0x80 + offset) == raw[26 + physical]);
            checked += 6;
        }
        assert(programmed.at(0xb0) == (raw[4] & 63));
        assert(patch.key_mask == ((raw[3] & 0x78) << 1));
    }
    // A two-operator algorithm-4 instrument makes a useful sentinel: placing
    // its carrier into slot 3 instead of slot 2 silences both output carriers.
    Patch sentinel;
    sentinel.algorithm = 4;
    sentinel.op[0].ar = sentinel.op[1].ar = 31;
    sentinel.op[0].mul = sentinel.op[1].mul = 1;
    sentinel.op[0].tl = 24;
    const auto correct = render_native(sentinel, 72);
    assert(std::any_of(correct.begin(), correct.end(), [](int32_t v) { return v != 0; }));
    std::swap(sentinel.op[1], sentinel.op[2]);
    const auto swapped = render_native(sentinel, 72);
    assert(std::all_of(swapped.begin(), swapped.end(), [](int32_t v) { return v == 0; }));
    bool rejected = false;
    try { render_zone(0, sentinel, 72, 72, 72); }
    catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
}
