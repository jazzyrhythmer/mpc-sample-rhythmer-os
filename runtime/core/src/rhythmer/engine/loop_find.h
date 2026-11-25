// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <cstdint>
#include <vector>

// A loop for a held note (the auto sampler, shell/auto_sample.h): in the part
// played while the key was held, an end on a rising zero crossing near where
// the key was let go, and the start (a rising zero crossing between 0.1 s and
// 1 s before it, never in the first quarter of the hold) whose frames before
// it sound most like those before the end. The frames before the end are
// then crossfaded into those before the start, so the jump back
// is seamless. Nothing is changed when no loop is found (a hold under 0.3 s,
// silence, or no start that matches well enough).
//
// A sound that moves while held (detuned oscillators beating, an LFO on its
// level or its filter, a chorus) is not given a short loop, which would
// freeze one moment of it: it loops a long stretch, from where it was last in
// the state it is in at the end (its level and brightness over 0.4 s), with a
// crossfade of up to a second. The longer it was held, the longer the loop.
//
// With `always` (a sound heard to sustain, so it must play on while held), a
// sound that never repeats closely (detuned unison saws, chorus, noise) still
// gets a loop: most of the held part, from a third of the way in, its join
// smeared by an equal-power crossfade of up to half a second (what the
// sample libraries do for pads), `match` then the poor match it had.
namespace rhythmer {

struct LoopPoints {
    uint32_t start = 0, end = 0; // end: the frame the loop jumps back from (start plays next)
    float match = 0;             // how alike the two places were, 0..1
    bool moving = false;         // the sound moves: the long loop
    bool found() const { return end > start; }
};

LoopPoints find_loop(std::vector<float>& left, std::vector<float>& right, uint64_t held, float rate, bool always = false);

// Whether a held sound (one channel, its first `held` frames) moves, as find_loop() counts it: past its first half
// second, its level or brightness climbs 1.5 dB or more back from a low (a note that only fades does not).
bool sound_moves(const std::vector<float>& sound, uint64_t held, float rate);

} // namespace rhythmer
