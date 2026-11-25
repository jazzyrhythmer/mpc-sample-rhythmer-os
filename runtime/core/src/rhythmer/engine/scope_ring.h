// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <array>
#include <atomic>
#include <cstdint>

// One channel's sound for a picture on the screen (the mixer's EQ spectrum, rhythmer-sample docs/MIXER_PLAN.md step
// 5): the audio thread writes it as mono into a ring, a block at a time; the screen thread reads the newest frames.
// One writer a block (the channel is processed on one thread), any number of readers. Each sample is an atomic, so a
// read beside a write sees old and new frames at its edge, never a torn value.
namespace rhythmer {

class ScopeRing {
public:
    static constexpr unsigned size = 4096; // a power of two: about 85 ms at 48 kHz

    void put(const float* l, const float* r, unsigned frames) {
        const uint32_t w = at.load(std::memory_order_relaxed);
        for (unsigned f = 0; f < frames; ++f) ring[(w + f) & (size - 1)].store(0.5f * (l[f] + r[f]), std::memory_order_relaxed);
        at.store(w + frames, std::memory_order_release);
    }
    // The newest `count` frames (at most `size`), oldest first.
    void latest(float* out, unsigned count) const {
        if (count > size) count = size;
        const uint32_t w = at.load(std::memory_order_acquire);
        for (unsigned n = 0; n < count; ++n) out[n] = ring[(w - count + n) & (size - 1)].load(std::memory_order_relaxed);
    }
    // How many frames it has taken in all (wraps): a reader can tell whether anything came since it last looked.
    uint32_t written() const { return at.load(std::memory_order_acquire); }

private:
    std::array<std::atomic<float>, size> ring {};
    std::atomic<uint32_t> at {0};
};

} // namespace rhythmer
