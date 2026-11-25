// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>

// A region's own tempo (PadSound::own_tempo, tenths of a BPM) for Follow and Seq warp when it has no # Beats: what
// Context::follow_tempos() sets. Pure, for the shell and the tests.
namespace rhythmer {

// From the sample's found tempo (BeatGrid::tempo, engine/onsets.h; 0: none): that tempo, half or twice it, whichever
// is nearest the project's tempo between 70 and 180 BPM (a loop read at 140 BPM plays as 70 in a 90 BPM project, as
// it would from its length), and exact where the region is 1, 2, 4 ... 64 bars at it (or one or two beats) within
// 4%, another whole number of bars within 1.5%. A wider reach for 3, 5, 6 ... bars cut tempos found in pieces of a
// song (not cut to bars) to a wrong one more often than it put a loop right, on 1739 Apple Loops and 22 beats.
// Without one, the whole number of bars (1/4 to 8) that puts the region nearest the project's tempo between 70
// and 180 BPM. 0 for a region under 0.1 s: a hit, not a loop, keeps its speed. All in tenths of a BPM, the region
// in 44.1 kHz frames.
inline uint16_t follow_tempo(uint32_t region_frames, uint16_t found, uint16_t project) {
    if (region_frames < 4410) return 0;
    const double target = project / 10.0, length = region_frames;
    double best = 0;
    if (found) {
        for (double times : {0.25, 0.5, 1.0, 2.0, 4.0}) {
            double bpm = found / 10.0 * times;
            if (bpm >= 70 && bpm <= 180 && (best == 0 || std::fabs(bpm - target) < std::fabs(best - target))) best = bpm;
        }
        if (best == 0) best = found / 10.0; // out of range every way (under 17.5 or over 720 BPM): as found
        const double beats = length * best / (60 * 44100.0);
        double whole = 0, off = 1;
        auto fit = [&](double count, double within) {
            const double by = std::fabs(beats / count - 1);
            if (by < within && by < off) { whole = count; off = by; }
        };
        for (double count = 1; count <= 256; count *= 2) fit(count, 0.04);
        for (unsigned bars = 3; bars <= 64; ++bars) if (bars & (bars - 1)) fit(bars * 4, 0.015);
        if (whole) best = whole * 60 * 44100.0 / length;
    } else {
        for (double bars : {0.25, 0.5, 1.0, 2.0, 4.0, 8.0}) {
            double bpm = bars * 4 * 60 * 44100.0 / length;
            if (bpm >= 70 && bpm <= 180 && (best == 0 || std::fabs(bpm - target) < std::fabs(best - target))) best = bpm;
        }
    }
    return uint16_t(std::lround(std::fmin(best * 10, 65535.0)));
}

} // namespace rhythmer
