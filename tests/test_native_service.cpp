// Runs the actual ARM7 command code on a host register model, not an emulator.
#ifndef NS_DIAGNOSTIC_CAPTURE
#define NS_DIAGNOSTIC_CAPTURE 0
#endif
#include "arm7/service.hpp"
#include <cassert>
#include <fstream>
#include <vector>
#include <sys/mman.h>

using namespace native_audio::service;
static uint32_t send(unsigned op, unsigned arg = 0) { return command((op << 24) | arg); }

static std::vector<uint32_t> song(uint32_t used) {
    unsigned notes = 0;
    for (unsigned i = 0; i < NS_SLOTS; ++i) notes += bool(used & (1u << i));
    const unsigned eventOffset = sizeof(NsHeader) + sizeof(NsSample) + NS_SLOTS * sizeof(NsTrack);
    const unsigned gainOffset = eventOffset + (NS_SLOTS + notes) * sizeof(NsEvent);
    const unsigned pcmOffset = gainOffset + 288;
    std::vector<uint32_t> data((pcmOffset + 16) / 4);
    auto *h = reinterpret_cast<NsHeader *>(data.data());
    std::memcpy(h->magic, "HRNSQ01\0", 8);
    h->version = 1; h->bytes = data.size() * 4; h->samples = 1; h->tracks = NS_SLOTS;
    h->events = NS_SLOTS + notes; h->sampleOffset = 64; h->trackOffset = 128;
    h->eventOffset = eventOffset; h->gainOffset = gainOffset; h->pcmOffset = pcmOffset;
    h->tempo = 76; h->commonPeriod = 8; h->comparisonTicks = 16;
    auto *sample = reinterpret_cast<NsSample *>(data.data() + 16);
    sample->offset = pcmOffset; sample->length = 16; sample->c5 = 22050;
    sample->envelope[0] = 255; sample->envelope[3] = 255;
    auto *tracks = reinterpret_cast<NsTrack *>(reinterpret_cast<char *>(data.data()) + h->trackOffset);
    auto *events = reinterpret_cast<NsEvent *>(reinterpret_cast<char *>(data.data()) + h->eventOffset);
    unsigned first = 0;
    for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
        auto &track = tracks[slot];
        track.slot = slot; track.first = first; track.loopIndex = NS_NO_LOOP; track.endTick = 16;
        // Even unused sources may carry metadata: they must not write another
        // source's remapped hardware channel (especially FM3 versus SSG0).
        events[first++] = NsEvent{0, NS_PAN, uint8_t(slot), 3, 0, 0};
        if (used & (1u << slot))
            events[first++] = NsEvent{0, NS_NOTE, uint8_t(slot), 60, 760u << 16, (617u << 16) | 16};
        track.count = first - track.first;
    }
    auto *gains = reinterpret_cast<uint16_t *>(reinterpret_cast<char *>(data.data()) + gainOffset);
    for (unsigned i = 0; i < 144; ++i) gains[i] = 65535;
    return data;
}

static std::vector<uint32_t> bank() {
    std::vector<uint32_t> data((32 + 17 * 8 + 17 * 16) / 4);
    auto *h = reinterpret_cast<NsSfxHeader *>(data.data());
    std::memcpy(h->magic, "HRNSFX1\0", 8);
    h->version = 1; h->bytes = data.size() * 4; h->count = 17;
    h->tableOffset = 32; h->period = 760; h->gain = 46079;
    auto *samples = reinterpret_cast<NsSfxSample *>(data.data() + 8);
    for (unsigned i = 0; i < 17; ++i) samples[i] = NsSfxSample{32 + 17 * 8 + i * 16, 16};
    return data;
}

struct SfxSnapshot {
    unsigned access[2];
    uint32_t control[2], address[2], loop[2], length[2];
    uint16_t timer[2];
    SfxSnapshot() {
        for (unsigned i = 0; i < 2; ++i) {
            const unsigned ch = i ? 8 : 3;
            access[i] = testAccess[ch]; control[i] = testSound[ch]; address[i] = testAddress[ch];
            loop[i] = testLoop[ch]; length[i] = testLength[ch]; timer[i] = testTimer[ch];
        }
    }
    void unchanged() const {
        for (unsigned i = 0; i < 2; ++i) {
            const unsigned ch = i ? 8 : 3;
            assert(testAccess[ch] == access[i] && testSound[ch] == control[i]);
            assert(testAddress[ch] == address[i] && testLoop[ch] == loop[i]);
            assert(testLength[ch] == length[i] && testTimer[ch] == timer[i]);
        }
    }
};

static void exerciseSong(NsShared *mailbox, const void *data, unsigned bytes) {
    std::memcpy(reinterpret_cast<void *>(0x02000000), data, bytes);
    mailbox->blobAddress = 0x02000000; mailbox->blobBytes = bytes;
    sfxHandler(1, nullptr); sfxHandler(9, nullptr);
    const SfxSnapshot snapshot;
    assert(send(NS_PLAY) == 0 && nativeMode && !paused);
    assert(!captureAvailable && send(NS_CAPTURE) == 8);
    const auto *h = engine.header;
    unsigned expectedNotes[NS_SLOTS] = {};
    for (unsigned slot = 0; slot < NS_SLOTS; ++slot) {
        for (uint32_t i = 0; i < engine.tracks[slot].count; ++i)
            expectedNotes[slot] += engine.events[engine.tracks[slot].first + i].op == NS_NOTE;
        assert(mapping[slot] != 3 && mapping[slot] != 8);
        assert((mapping[slot] == 255) == !expectedNotes[slot]);
    }
    if (expectedNotes[6]) assert(mapping[6] == 5 && !expectedNotes[3] && mapping[3] == 255);
    // Seek/resume replay the real NOTE stream without touching either SFX.
    assert(send(NS_MASK, 1) == 0);
    assert(send(NS_MASK, 0x7fff) == 0);
    assert(send(NS_SEEK, 1) == 0);
    const auto tick = engine.tick;
    assert(send(NS_PAUSE) == 0 && paused);
    snapshot.unchanged();
    assert(send(NS_RESUME) == 0 && !paused && engine.tick == tick);
    for (unsigned i = engine.tick; i + 1 < h->comparisonTicks; ++i) engine.step();
    assert(engine.errors == 0);
    for (unsigned slot = 0; slot < NS_SLOTS; ++slot) assert(engine.voices[slot].starts >= expectedNotes[slot]);
    assert(send(NS_RELEASE) == 0 && !nativeMode && !engine.header);
    assert(send(NS_MASK, 1) != 0 && send(NS_RESUME) == 0);
    snapshot.unchanged();
    for (unsigned ch = 0; ch < 16; ++ch) if (ch != 3 && ch != 8) assert(testSound[ch] == 0);
    assert(send(NS_PLAY) == 0 && engine.tick == 0);
    reinterpret_cast<NsHeader *>(0x02000000)->version = 99;
    assert(send(NS_PLAY) != 0 && !nativeMode && !engine.header);
    assert(send(NS_MASK, 1) != 0);
    snapshot.unchanged();
}

int main(int argc, char **argv) {
    // Match the validated main-RAM address window without touching real DS RAM.
    void *ram = mmap(reinterpret_cast<void *>(0x02000000), 0x400000,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(ram != MAP_FAILED);
    auto *mailbox = new (reinterpret_cast<void *>(0x023f0000)) NsShared{};
    addressHandler(mailbox, nullptr);
    assert(shared == mailbox);
    init();
    const auto sfxBank = bank();
    std::memcpy(reinterpret_cast<void *>(0x02300000), sfxBank.data(), sfxBank.size() * 4);
    mailbox->sfxAddress = 0x02300000; mailbox->sfxBytes = sfxBank.size() * 4;
    sfxHandler(1, nullptr); // ignored before load
    assert(testSound[3] == 0 && testSound[8] == 0);
    assert(send(NS_SFX_LOAD) == 0 && sfx.ready());
    sfxHandler(1, nullptr); sfxHandler(9, nullptr);
    assert(testSound[3] & SOUNDXCNT_ENABLE); assert(testSound[8] & SOUNDXCNT_ENABLE);
    assert((testSound[3] & SOUNDXCNT_FORMAT_16BIT) && testLoop[3] == 0 && testLength[3] == 4);
    assert(testTimer[3] == uint16_t(-760));
    assert((testSound[3] & 0x37f) == native_audio::hardware_gain(46079));
    sfxHandler(1, nullptr); // same ID retriggers DS3, keeping ID9 on DS8
    assert(testAddress[3] == 0x02300000 + 32 + 17 * 8);
    assert(testAddress[8] == 0x02300000 + 32 + 17 * 8 + 8 * 16);
    sfxHandler(17, nullptr); // ID9 is now oldest
    assert(testAddress[8] == 0x02300000 + 32 + 17 * 8 + 16 * 16);
    testSound[3] = 0; // natural hardware completion makes DS3 free first
    sfxHandler(2, nullptr);
    assert(testAddress[3] == 0x02300000 + 32 + 17 * 8 + 16);
    for (unsigned ch = 0; ch < 16; ++ch) if (ch != 3 && ch != 8) assert(testAccess[ch] == 0);
    {
        const SfxSnapshot snapshot;
        sfxHandler(18, nullptr); sfxHandler(0xffffffff, nullptr);
        mailbox->sfxAddress = 0x01ffffff;
        assert(send(NS_SFX_LOAD) == 2);
        mailbox->sfxAddress = 0x02300000; mailbox->sfxBytes = 0xffffffff;
        assert(send(NS_SFX_LOAD) == 2);
        mailbox->sfxBytes = sfxBank.size() * 4;
        mailbox->sfxAddress = 0x02300001;
        assert(send(NS_SFX_LOAD) == 2);
        mailbox->sfxAddress = 0x02400000 - 4;
        assert(send(NS_SFX_LOAD) == 2);
        std::memcpy(reinterpret_cast<void *>(0x02302000), sfxBank.data(), sfxBank.size() * 4);
        mailbox->sfxAddress = 0x02302000;
        reinterpret_cast<NsSfxHeader *>(0x02302000)->version = 99;
        assert(send(NS_SFX_LOAD) == 2);
        snapshot.unchanged();
    }
    sfxHandler(2, nullptr); // a rejection cannot replace the acknowledged bank
    assert(testAddress[3] == 0x02300000 + 32 + 17 * 8 + 16);
    // A successful replacement stops old DMA before its ACK/free boundary.
    std::memcpy(reinterpret_cast<void *>(0x02301000), sfxBank.data(), sfxBank.size() * 4);
    mailbox->sfxAddress = 0x02301000;
    assert(send(NS_SFX_LOAD) == 0 && !testSound[3] && !testSound[8]);
    {
        sfxHandler(1, nullptr); sfxHandler(9, nullptr);
        const SfxSnapshot snapshot;
        const auto full = song(0x7fff & ~(1u << 6));
        std::memcpy(ram, full.data(), full.size() * 4);
        mailbox->blobAddress = 0x02000000; mailbox->blobBytes = full.size() * 4;
        assert(send(NS_PLAY) == 0);
        const auto sparse = song(1u << 6);
        std::memcpy(ram, sparse.data(), sparse.size() * 4);
        mailbox->blobBytes = sparse.size() * 4;
        assert(send(NS_PLAY) == 0 && mapping[6] == 5 && mapping[3] == 255);
        // Stopping after committing the sparse map would leave old music DMA
        // running on channels that are now unmapped.
        for (unsigned ch = 0; ch < 16; ++ch)
            if (ch != 3 && ch != 5 && ch != 8) assert(testSound[ch] == 0);
        assert(testSound[5] & SOUNDXCNT_ENABLE);
        assert(send(NS_RELEASE) == 0);
        snapshot.unchanged();
    }
    if (argc == 1) {
        const auto full = song(0x7fff & ~(1u << 6));
        exerciseSong(mailbox, full.data(), full.size() * 4);
        const auto st2 = song(0x7fff & ~(1u << 3));
        exerciseSong(mailbox, st2.data(), st2.size() * 4);
    } else for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i], std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(file)), {});
        assert(data.size() < 0x300000 && !data.empty());
        exerciseSong(mailbox, data.data(), data.size());
    }
    {
        const auto collision = song(0x7fff);
        std::memcpy(ram, collision.data(), collision.size() * 4);
        mailbox->blobBytes = collision.size() * 4;
        const SfxSnapshot snapshot;
        assert(send(NS_PLAY) == 2 && !nativeMode && !engine.header);
        snapshot.unchanged();
    }
    sfxHandler(0, nullptr);
    assert(!testSound[3] && !testSound[8]);
    sfxHandler(17, nullptr); sfxHandler(9, nullptr);
    assert(testSound[3] && testSound[8]);
    shutdown();
    for (auto value : testSound) assert(value == 0);
    assert(!sfx.ready());
    sfxHandler(1, nullptr);
    for (auto value : testSound) assert(value == 0);
    assert(munmap(ram, 0x400000) == 0);
    std::puts("ARM7 game service: reserved DS3/8, SSG0 remap/collision, SFX bank, music lifecycle isolation and shutdown passed");
}
