// Portable two-voice resident PCM16 player; backend voices are not music slots.
#pragma once
#include "format.h"
#include <cstring>

namespace native_audio {
template<class Backend> class SfxPlayer {
public:
    explicit SfxPlayer(Backend &backend) : hw(backend) {}

    // Transactional validation: rejected input changes neither hardware nor the
    // previously acknowledged bank. The caller may immediately free rejection.
    bool load(const void *data, uint32_t bytes) {
        if (!data || (reinterpret_cast<uintptr_t>(data) & 3) || bytes < sizeof(NsSfxHeader)) return false;
        const auto *h = static_cast<const NsSfxHeader *>(data);
        if (std::memcmp(h->magic, "HRNSFX1\0", 8) || h->version != 1 || h->bytes != bytes ||
            !h->count || h->count > 256 || !h->period || h->period > 65535 || h->gain > 65535 ||
            (h->tableOffset & 3) || h->tableOffset < sizeof(NsSfxHeader) ||
            h->tableOffset > bytes || h->count > (bytes - h->tableOffset) / sizeof(NsSfxSample)) return false;
        const auto *base = static_cast<const uint8_t *>(data);
        const auto *table = reinterpret_cast<const NsSfxSample *>(base + h->tableOffset);
        const uint32_t tableEnd = h->tableOffset + h->count * sizeof(NsSfxSample);
        for (uint32_t i = 0; i < h->count; ++i) {
            const auto &s = table[i];
            if ((s.offset & 3) || (s.length & 3) || s.length < 16 || s.offset < tableEnd ||
                s.offset > bytes || s.length > bytes - s.offset) return false;
            for (uint32_t j = 0; j < i; ++j)
                if (s.offset < table[j].offset + table[j].length && table[j].offset < s.offset + s.length)
                    return false;
        }
        stop(); // old DMA must be stopped before the successful load ACK
        blob = base; header = h; samples = table;
        return true;
    }
    bool ready() const { return header != nullptr; }
    void stop() {
        for (unsigned i = 0; i < 2; ++i) { hw.stop(i); voices[i] = Voice{}; }
    }
    void unload() { stop(); blob = nullptr; header = nullptr; samples = nullptr; }

    // No priority numbers: same-ID retrigger, free first, otherwise oldest.
    // Hardware clears its enable bit on natural completion; no timer is needed.
    bool play(uint32_t id) {
        if (!id) { stop(); return true; }
        if (!header || id > header->count) return false;
        for (unsigned i = 0; i < 2; ++i)
            if (voices[i].id && !hw.busy(i)) voices[i] = Voice{};
        unsigned chosen = 2;
        for (unsigned i = 0; i < 2; ++i) if (voices[i].id == id) { chosen = i; break; }
        if (chosen == 2)
            for (unsigned i = 0; i < 2; ++i) if (!voices[i].id) { chosen = i; break; }
        if (chosen == 2) chosen = voices[0].older ? 0 : 1;
        const auto &s = samples[id - 1];
        hw.start(chosen, blob + s.offset, s.length, uint16_t(header->period), uint16_t(header->gain));
        voices[chosen].id = id;
        voices[chosen].older = false;
        voices[chosen ^ 1].older = true;
        return true;
    }

private:
    // With exactly two voices a relative age bit cannot overflow.
    struct Voice { uint32_t id = 0; bool older = false; } voices[2];
    Backend &hw;
    const uint8_t *blob = nullptr;
    const NsSfxHeader *header = nullptr;
    const NsSfxSample *samples = nullptr;
};
} // namespace native_audio
