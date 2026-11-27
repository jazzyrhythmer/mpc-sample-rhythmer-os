// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/pattern.h"
#include <cstdint>

// A track's playback direction (Elektron's per track direction; Patterns, Shift + Loop): its patterns' steps of Q
// forward, backward, or each pass in an order of its own. A note keeps its place inside its step, and its length.
// The audio thread plays the pattern as written (scaled by the track's speed, speed_pattern()) and reads, on each
// tick, the ticks of the step this tick stands for (directed_tick()).
namespace rhythmer {

enum class PlayDirection : uint8_t { Forward, Reverse, Random, Count };
inline const char* direction_name(PlayDirection d) {
    static const char* const names[] {"FWD", "REV", "RND"};
    return names[unsigned(d) < unsigned(PlayDirection::Count) ? unsigned(d) : 0];
}
inline PlayDirection direction_of(unsigned value) {
    return value < unsigned(PlayDirection::Count) ? PlayDirection(value) : PlayDirection::Forward;
}

// A hash of 32 bits, each bit of the input moving about half of the output's.
constexpr uint32_t step_hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

// Step `k`'s place in an order of `count` steps that `seed` picks: every step once (a shuffle), the same for the same
// seed. A four-round Feistel permutation of the next even power of two, walked until it lands below `count`.
inline uint32_t shuffled_step(uint32_t k, uint32_t count, uint32_t seed) {
    if (count < 2 || k >= count) return count ? k % count : 0;
    unsigned bits = 2;
    while ((uint64_t(1) << bits) < count) ++bits;
    bits += bits & 1u;
    const unsigned half = bits / 2;
    const uint32_t mask = (uint32_t(1) << half) - 1;
    uint32_t x = k;
    do {
        uint32_t left = x >> half, right = x & mask;
        for (uint32_t round = 0; round < 4; ++round) {
            const uint32_t f = step_hash(right ^ seed ^ (round + 1) * 0x9e3779b9u) & mask;
            const uint32_t next = left ^ f;
            left = right; right = next;
        }
        x = left << half | right;
    } while (x >= count);
    return x;
}

// The seed of a track's pass for RND: the same orders each time the pattern starts from its top.
constexpr uint32_t direction_seed(unsigned track, uint32_t pass, unsigned lane = 0) {
    return step_hash(uint32_t(track) * 0x85ebca6bu ^ step_hash(pass + 0x632be5abu) ^ uint32_t(lane) * 0xc2b2ae35u);
}

// The ticks of a pattern as it plays (at `speed`) that step boundaries fall on: step `k` of Q (`q` ticks as written)
// from `base` (written ticks) starts where speed_pattern() puts a note written there.
struct StepClock {
    uint32_t base = 0;  // written
    unsigned q = sixteenth_ticks;
    Speed speed {1, 1};
    uint64_t start(uint64_t k) const { return (uint64_t(base + k * q) * speed.den + speed.num / 2) / speed.num; }
    // The step tick `at` (as played, at or after start(0)) falls in.
    uint64_t step_at(uint64_t at) const {
        const uint64_t written = at * speed.num / speed.den;
        uint64_t k = written > base ? (written - base) / q : 0;
        while (k && start(k) > at) --k;
        while (start(k + 1) <= at) ++k;
        return k;
    }
};

// What one tick of a directed loop plays: the notes (and Motion points) of the pattern as played from `from` up to,
// not including, `to`; `step` the step it stands for. A loop of steps from `clock`'s base up to `end` (as played);
// `at` one of its ticks. Forward: the tick itself. Otherwise the tick's place in its step, in the step the order puts
// there; the step's last tick also takes the rest of that step (a step one tick longer than this one at an odd speed).
struct DirectedTick { uint32_t from = 0, to = 0, step = 0; };
inline DirectedTick directed_tick(uint32_t at, uint32_t end, const StepClock& clock, PlayDirection direction, uint32_t seed) {
    if (direction == PlayDirection::Forward || end <= clock.start(0)) return {at, at + 1, uint32_t(clock.step_at(at))};
    uint64_t count = clock.step_at(end - 1) + 1; // the last step may be a part one
    const uint64_t k = std::min<uint64_t>(clock.step_at(at), count - 1);
    const uint64_t j = direction == PlayDirection::Reverse ? count - 1 - k : shuffled_step(uint32_t(k), uint32_t(count), seed);
    const uint64_t here = clock.start(k), here_end = std::min<uint64_t>(clock.start(k + 1), end);
    const uint64_t there = clock.start(j), there_end = std::min<uint64_t>(clock.start(j + 1), end);
    uint64_t from = std::min(there + (at - here), there_end);
    uint64_t to = at + 1 >= here_end ? there_end : std::min(from + 1, there_end);
    return {uint32_t(from), uint32_t(std::max(from, to)), uint32_t(j)};
}

} // namespace rhythmer
