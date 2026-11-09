// Synthetic assets only: portable allocator/parser plus real ARM9 client.
#include "native_audio/sfx.hpp"
#include "native_audio/client.hpp"
#include <cassert>
#include <vector>
#include <fstream>
#include <unistd.h>
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#endif

struct FakeSfxHardware {
    struct Voice {
        bool enabled = false;
        const uint8_t *pcm = nullptr;
        uint32_t bytes = 0;
        uint16_t period = 0, gain = 0;
        unsigned starts = 0;
    } voices[2];
    unsigned writes = 0;
    bool busy(unsigned i) const { assert(i < 2); return voices[i].enabled; }
    void stop(unsigned i) { assert(i < 2); ++writes; voices[i].enabled = false; }
    void start(unsigned i, const uint8_t *pcm, uint32_t bytes, uint16_t period, uint16_t gain) {
        assert(i < 2); ++writes;
        auto &v = voices[i];
        v.enabled = true; v.pcm = pcm; v.bytes = bytes; v.period = period; v.gain = gain; ++v.starts;
    }
};

static std::vector<uint32_t> bank(unsigned count = 17) {
    std::vector<uint32_t> data((32 + count * 8 + count * 16) / 4);
    auto *h = reinterpret_cast<NsSfxHeader *>(data.data());
    std::memcpy(h->magic, "HRNSFX1\0", 8);
    h->version = 1; h->bytes = data.size() * 4; h->count = count;
    h->tableOffset = 32; h->period = 760; h->gain = 46079;
    auto *samples = reinterpret_cast<NsSfxSample *>(data.data() + 8);
    for (unsigned i = 0; i < count; ++i) {
        samples[i] = NsSfxSample{32 + count * 8 + i * 16, 16};
        auto *pcm = reinterpret_cast<int16_t *>(reinterpret_cast<char *>(data.data()) + samples[i].offset);
        for (unsigned j = 0; j < 8; ++j) pcm[j] = (j & 1) ? -int(i + j) : int(i + j);
    }
    return data;
}

static void portableTests() {
    FakeSfxHardware hw;
    native_audio::SfxPlayer<FakeSfxHardware> player(hw);
    assert(!player.ready() && !player.play(1) && hw.writes == 0);
    auto data = bank();
    auto *h = reinterpret_cast<NsSfxHeader *>(data.data());
    auto *samples = reinterpret_cast<NsSfxSample *>(data.data() + 8);
    const auto *base = reinterpret_cast<const uint8_t *>(data.data());
    assert(player.load(data.data(), data.size() * 4));
    assert(player.ready() && player.play(1) && player.play(9));
    assert(hw.voices[0].pcm == base + samples[0].offset);
    assert(hw.voices[1].pcm == base + samples[8].offset); // mixed layers stay one PCM/voice
    assert(hw.voices[0].bytes == 16 && hw.voices[0].period == 760 && hw.voices[0].gain == 46079);
    assert(player.play(1)); // same ID restarts existing DS voice, not a duplicate
    assert(hw.voices[0].starts == 2 && hw.voices[1].starts == 1);
    assert(player.play(17)); // 9 is now oldest
    assert(hw.voices[0].pcm == base + samples[0].offset && hw.voices[1].pcm == base + samples[16].offset);
    assert(player.play(2)); // 1 is oldest
    assert(hw.voices[0].pcm == base + samples[1].offset);
    hw.voices[1].enabled = false; // natural hardware completion
    assert(player.play(3));
    assert(hw.voices[0].pcm == base + samples[1].offset && hw.voices[1].pcm == base + samples[2].offset);
    const auto writes = hw.writes;
    assert(!player.play(18) && !player.play(0xffffffff) && hw.writes == writes);
    assert(player.play(0) && !hw.voices[0].enabled && !hw.voices[1].enabled);
    assert(player.play(9) && hw.voices[0].pcm == base + samples[8].offset);

    auto reject = [&](const void *ptr, uint32_t size) {
        const auto before = hw.writes;
        assert(!player.load(ptr, size) && player.ready() && hw.writes == before);
        // Every rejection leaves the previously valid bank usable.
        assert(player.play(9) && hw.voices[0].pcm == base + samples[8].offset);
    };
    reject(nullptr, 0);
    reject(base + 1, h->bytes - 1);
    for (unsigned size = 0; size < sizeof(NsSfxHeader); ++size) reject(base, size);
    auto malformed = [&](auto mutation) {
        auto bad = data;
        mutation(*reinterpret_cast<NsSfxHeader *>(bad.data()), reinterpret_cast<NsSfxSample *>(bad.data() + 8));
        reject(bad.data(), bad.size() * 4);
    };
    malformed([](auto &x, auto *) { x.magic[0] = 'X'; });
    malformed([](auto &x, auto *) { x.version = 2; });
    malformed([](auto &x, auto *) { --x.bytes; });
    malformed([](auto &x, auto *) { x.count = 0; });
    malformed([](auto &x, auto *) { x.count = 257; });
    malformed([](auto &x, auto *) { x.tableOffset = 0; });
    malformed([](auto &x, auto *) { x.tableOffset = 33; });
    malformed([](auto &x, auto *) { x.tableOffset = x.bytes - 4; });
    malformed([](auto &x, auto *) { x.tableOffset = 0xfffffffc; });
    malformed([](auto &x, auto *) { x.period = 0; });
    malformed([](auto &x, auto *) { x.period = 65536; });
    malformed([](auto &x, auto *) { x.gain = 65536; });
    malformed([](auto &, auto *s) { ++s[0].offset; });
    malformed([](auto &, auto *s) { s[0].offset = 0; });
    malformed([](auto &, auto *s) { s[0].offset = 32; });
    malformed([](auto &x, auto *s) { s[0].offset = x.bytes; });
    malformed([](auto &, auto *s) { s[0].offset = 0xfffffffc; });
    malformed([](auto &, auto *s) { s[0].length = 0; });
    malformed([](auto &, auto *s) { s[0].length = 12; });
    malformed([](auto &, auto *s) { s[0].length = 17; });
    malformed([](auto &, auto *s) { s[0].length = 0xfffffffc; });
    malformed([](auto &, auto *s) { s[1].offset = s[0].offset; });
    malformed([](auto &, auto *s) { s[1].offset = s[0].offset + 4; });
    malformed([](auto &, auto *s) { s[16].length += 4; });
    for (const auto count : {1u, 256u}) {
        auto replacement = bank(count);
        auto *rh = reinterpret_cast<NsSfxHeader *>(replacement.data());
        rh->gain = count == 1 ? 0 : 65535;
        rh->period = count == 1 ? 1 : 65535;
        assert(player.load(replacement.data(), replacement.size() * 4));
        assert(!hw.voices[0].enabled && !hw.voices[1].enabled);
        assert(player.play(count));
        assert(hw.voices[0].gain == rh->gain && hw.voices[0].period == rh->period);
        player.unload(); // before this local replacement goes out of scope
    }
    assert(!player.ready() && !player.play(1));
}

static void assertFreed(const void *pointer) {
#if defined(__SANITIZE_ADDRESS__)
    assert(__asan_address_is_poisoned(pointer));
#else
    (void)pointer;
#endif
}

static void clientTests() {
    char path[] = "/tmp/opencode/native-sfx-test-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "wb");
    const auto data = bank();
    assert(file && std::fwrite(data.data(), 4, data.size(), file) == data.size() && std::fclose(file) == 0);
    native_audio::Client client;
    assert(client.init());
    void *sharedAllocation = testSharedAddress;
    assert(!client.playSfx(1));
    testReply = 2;
    assert(!client.loadSfx(path) && client.healthy);
    assertFreed(testLastFlush);
    assert(!client.shared->sfxAddress && !client.shared->sfxBytes && !client.playSfx(1));
    testReply = 0;
    assert(client.loadSfx(path));
    const void *first = testLastFlush;
    assert(!(reinterpret_cast<uintptr_t>(first) & 31) && testFlushedBytes == data.size() * 4);
    assert(client.shared->sfxAddress == uint32_t(reinterpret_cast<uintptr_t>(first)));
    assert(testAck == uint32_t(NS_SFX_LOAD << 24));
    const unsigned sends = testSends[FIFO_USER_01], waits = testWaits;
    assert(client.playSfx(1) && testSfxValue == 1);
    assert(client.playSfx(9) && testSfxValue == 9);
    assert(client.stopSfx() && testSfxValue == 0);
    assert(testSends[FIFO_USER_01] == sends && testWaits == waits);
    assert(client.loadSfx(path));
    const void *second = testLastFlush;
    assertFreed(first); // successful replacement frees only after its ACK
    testReply = 2;
    assert(!client.loadSfx(path) && client.healthy);
    assertFreed(testLastFlush);
    assert(client.shared->sfxAddress == uint32_t(reinterpret_cast<uintptr_t>(second)));
    assert(client.shared->sfxBytes == data.size() * 4 && client.playSfx(17));
    testReply = 0; testAutoAck = false;
    assert(!client.loadSfx(path) && !client.healthy && testWaits == waits + 180);
    const void *uncertain = testLastFlush;
#if defined(__SANITIZE_ADDRESS__)
    assert(!__asan_address_is_poisoned(second) && !__asan_address_is_poisoned(uncertain));
#endif
    assert(!client.playSfx(1) && !client.loadSfx(path));
    // Test backend has no DMA: simulate shutdown to reclaim intentionally
    // retained unknown-outcome buffers and keep the sanitizer leak check useful.
    std::free(const_cast<void *>(second)); std::free(const_cast<void *>(uncertain));
    std::free(sharedAllocation);
    testAutoAck = true;

    native_audio::Client failedSend;
    assert(failedSend.init());
    sharedAllocation = testSharedAddress;
    testSendOK = false;
    const unsigned beforeWait = testWaits;
    assert(!failedSend.loadSfx(path) && !failedSend.healthy && testWaits == beforeWait);
#if defined(__SANITIZE_ADDRESS__)
    assert(!__asan_address_is_poisoned(testLastFlush));
#endif
    // Even an unsuccessful send conservatively retains a published buffer.
    std::free(const_cast<void *>(testLastFlush)); std::free(sharedAllocation);
    testSendOK = true;
    assert(unlink(path) == 0);
}

int main(int argc, char **argv) {
    portableTests();
    clientTests();
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
        FakeSfxHardware hw;
        native_audio::SfxPlayer<FakeSfxHardware> player(hw);
        assert(!bytes.empty() && player.load(bytes.data(), bytes.size()));
        const auto *h = reinterpret_cast<const NsSfxHeader *>(bytes.data());
        assert(h->count == 17 && h->period == 760 && h->gain == 46079);
        for (unsigned id = 1; id <= h->count; ++id) assert(player.play(id));
        assert(hw.voices[0].enabled && hw.voices[1].enabled);
        player.unload();
    }
    std::puts("Native SFX: strict parser, two voices, retrigger/oldest/completion, resident client and ACK lifetime checks passed");
}
