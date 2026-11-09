// ARM9 native playback client. The ARM7 ACK is the blob lifetime boundary.
#pragma once
#include <nds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include "format.h"

namespace native_audio {
class Client {
public:
    bool init() {
        if (shared) return healthy;
        allocation = static_cast<NsShared *>(memalign(32, (sizeof(NsShared) + 31) & ~31u));
        if (!allocation) return false;
        std::memset(allocation, 0, sizeof(NsShared));
        DC_FlushRange(allocation, sizeof(NsShared));
        shared = static_cast<volatile NsShared *>(memUncached(allocation));
        healthy = fifoSendAddress(FIFO_USER_01, allocation);
        return healthy;
    }
    bool send(unsigned op, uint32_t arg = 0) {
        lastResult = 0;
        acknowledged = false;
        if (!healthy || !shared || !fifoSendValue32(FIFO_USER_01, (op << 24) | (arg & 0xffffff))) {
            healthy = false; return false;
        }
        for (unsigned frames = 0; !fifoCheckValue32(FIFO_USER_01); ++frames) {
            if (frames >= 180) {
                // Outcome is unknown: never free/reuse the DMA buffer or accept
                // a late ACK as the response to some later command.
                healthy = false; return false;
            }
            swiWaitForVBlank();
        }
        lastResult = fifoGetValue32(FIFO_USER_01);
        acknowledged = true;
        return lastResult == 0;
    }
    bool stop() {
        if (!send(NS_RELEASE)) return false;
        return true;
    }
    bool unload() {
        if (!stop()) return false;
        std::free(blob); blob = nullptr; bytes = 0;
        shared->blobAddress = 0;
        shared->blobBytes = 0;
        return true;
    }
    bool load(const char *path) {
        if (!unload()) return false;
        FILE *file = std::fopen(path, "rb");
        if (!file) return false;
        const bool seekOK = std::fseek(file, 0, SEEK_END) == 0;
        const long size = seekOK ? std::ftell(file) : -1;
        std::rewind(file);
        if (size < long(sizeof(NsHeader)) || size > 3 * 1024 * 1024) {
            std::fclose(file); return false;
        }
        blob = static_cast<uint8_t *>(memalign(32, size));
        const bool readOK = blob && std::fread(blob, 1, size, file) == unsigned(size);
        const bool closed = std::fclose(file) == 0;
        if (!readOK || !closed) { std::free(blob); blob = nullptr; return false; }
        bytes = size;
        DC_FlushRange(blob, bytes);
        shared->blobAddress = uint32_t(reinterpret_cast<uintptr_t>(blob));
        shared->blobBytes = bytes;
        return true;
    }
    bool play() { return blob && send(NS_PLAY); }
    bool loadSfx(const char *path) {
        if (!healthy || !shared) return false;
        FILE *file = std::fopen(path, "rb");
        if (!file) return false;
        const bool seekOK = std::fseek(file, 0, SEEK_END) == 0;
        const long size = seekOK ? std::ftell(file) : -1;
        std::rewind(file);
        if (size < long(sizeof(NsSfxHeader)) || size > 3 * 1024 * 1024) {
            std::fclose(file); return false;
        }
        auto *candidate = static_cast<uint8_t *>(memalign(32, size));
        const bool readOK = candidate && std::fread(candidate, 1, size, file) == unsigned(size);
        const bool closed = std::fclose(file) == 0;
        if (!readOK || !closed) { std::free(candidate); return false; }
        DC_FlushRange(candidate, size);
        shared->sfxAddress = uint32_t(reinterpret_cast<uintptr_t>(candidate));
        shared->sfxBytes = size;
        if (!send(NS_SFX_LOAD)) {
            if (acknowledged) {
                // A rejection is transactional on ARM7: it retained the old
                // acknowledged bank, never this candidate's DMA pointers.
                shared->sfxAddress = uint32_t(reinterpret_cast<uintptr_t>(sfxBlob));
                shared->sfxBytes = sfxBytes;
                std::free(candidate);
            } else {
                // Either bank may still be referenced by DMA. Keep both and
                // prohibit later commands from confusing a late ACK.
                uncertainSfx = candidate;
            }
            return false;
        }
        std::free(sfxBlob);
        sfxBlob = candidate; sfxBytes = size;
        return true;
    }
    bool playSfx(unsigned id) {
        return healthy && shared && sfxBlob && fifoSendValue32(FIFO_USER_02, id);
    }
    bool stopSfx() { return playSfx(0); }
    bool readStatus(NsStatus &out) const {
        if (!shared) return false;
        for (unsigned retry = 0; retry < 4; ++retry) {
            const uint32_t before = shared->status.serial;
            if (before & 1) continue;
            auto *src = reinterpret_cast<const volatile uint32_t *>(&shared->status);
            auto *dst = reinterpret_cast<uint32_t *>(&out);
            for (unsigned i = 0; i < sizeof(out) / 4; ++i) dst[i] = src[i];
            asm volatile("" ::: "memory");
            if (before == shared->status.serial) return true;
        }
        return false;
    }
    const uint8_t *data() const { return blob; }
    uint32_t size() const { return bytes; }
    volatile NsShared *shared = nullptr;
    bool healthy = false;
    uint32_t lastResult = 0;
private:
    NsShared *allocation = nullptr;
    uint8_t *blob = nullptr;
    uint32_t bytes = 0;
    uint8_t *sfxBlob = nullptr, *uncertainSfx = nullptr;
    uint32_t sfxBytes = 0;
    bool acknowledged = false;
};
} // namespace native_audio
