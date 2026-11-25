// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <vector>

// A sample's note (engine/root_find.cpp), found when it is read, beside its hits: the pitch it holds, when it holds
// one, as a pitch class and how far off the equal-tempered note (A = 440 Hz) it sits. A kit's 16 Levels Scale and
// Chords and a KEYGROUP track of one WAV play it from that note (model/scales.h) instead of from C.
namespace rhythmer {

struct SampleRoot {
    int pitch_class = -1; // 0 C .. 11 B; -1 none: a drum, a noise, a phrase of notes, a chord, silence
    int cents = 0;        // -50 .. 50 over that note
    bool found() const { return pitch_class >= 0; }
};

// From the sample's sides summed at 44.1 kHz (model_mono(), engine/onsets.h); its first 8 seconds are enough.
SampleRoot find_root(const std::vector<float>& model_mono);

} // namespace rhythmer
