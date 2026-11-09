// Actual native-only ARM7 diagnostic service on a host register model.
#define NS_DIAGNOSTIC_CAPTURE 1
#include "arm7/service.hpp"
#include <cassert>
#include <fstream>
#include <vector>
#include <sys/mman.h>

using namespace native_audio::service;
static uint32_t send(unsigned op, unsigned arg = 0) { return command((op << 24) | arg); }

int main(int argc, char **argv) {
    assert(argc == 2);
    void *ram = mmap(reinterpret_cast<void *>(0x02000000), 0x400000,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(ram != MAP_FAILED);
    auto *mailbox = new (reinterpret_cast<void *>(0x023f0000)) NsShared{};
    addressHandler(mailbox, nullptr); init();
    assert(send(NS_SFX_LOAD) == 8); // diagnostic owns capture clocks, never SFX
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
    assert(!bytes.empty() && bytes.size() < 0x300000);
    std::memcpy(ram, bytes.data(), bytes.size());
    mailbox->blobAddress = 0x02000000; mailbox->blobBytes = bytes.size();
    assert(send(NS_PLAY) == 0 && nativeMode && engine.header);
    assert(std::memcmp(mapping, originalMapping, sizeof(mapping)) == 0);
    bool fm5 = false;
    for (unsigned i = 0; i < engine.tracks[5].count; ++i)
        fm5 |= engine.events[engine.tracks[5].first + i].op == NS_NOTE;
    assert(captureAvailable == !fm5);
    if (fm5) {
        const auto before = testTimer[1];
        assert(send(NS_CAPTURE) == 8 && testTimer[1] == before);
    } else {
        assert(send(NS_CAPTURE) == 0 && mailbox->status.captureState == 1);
        assert(testTimer[1] == uint16_t(-512) && testTimer[3] == uint16_t(-512));
        assert(send(NS_CAPTURE, 1) == 0 && mailbox->status.captureState == 0);
    }
    const auto tick = engine.tick;
    assert(send(NS_PAUSE) == 0 && paused);
    assert(send(NS_RESUME) == 0 && !paused && engine.tick == tick);
    assert(send(NS_RELEASE) == 0 && !nativeMode && !engine.header);
    shutdown();
    for (auto value : testSound) assert(value == 0);
    assert(munmap(ram, 0x400000) == 0);
    std::puts("Native diagnostic: mapping, FM5 capture rejection, SFX exclusion and lifecycle passed");
}
