// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <cstdint>

// Seq Record's place for a hit (roadmap P0): the tick it was heard on, counted
// back from the playhead (EngineHost::heard_place), wrapped into the pattern;
// and, with Rec Quantize, whether the step it snaps to is still ahead of the
// playhead, so the audio thread skips it once (it was heard live). Plain
// arithmetic, for host tests.
namespace rhythmer {

// A position counted back from the playhead (it may be below 0, or past the end after the nearest tick), in the pattern.
inline uint32_t wrap_tick(int position, uint32_t length) {
    if (!length) return 0;
    int64_t at = int64_t(position) % int64_t(length);
    return uint32_t(at < 0 ? at + int64_t(length) : at);
}

// The same for a hit on a kit lane of its own length (`loop` ticks, shorter than the pattern's `length`): where the
// lane was. It loops from where the pattern began, so it is `pass` passes of the pattern and `position` ticks on
// (below 0: in the pass before), round the lane's loop.
inline uint32_t lane_tick(int position, uint32_t pass, uint32_t length, uint32_t loop) {
    if (!loop || loop >= length) return wrap_tick(position, length);
    int64_t at = (int64_t(pass) * int64_t(length) + int64_t(position)) % int64_t(loop);
    return uint32_t(at < 0 ? at + int64_t(loop) : at);
}

// `snapped` (the hit `heard` moved onto its grid) still to come for a playhead at `playhead` (the tick it last played).
inline bool still_ahead(uint32_t heard, uint32_t snapped, int playhead, uint32_t length) {
    if (!length || playhead < 0) return false;
    uint32_t to_snap = (snapped + length - heard % length) % length; // forward from the hit
    if (!to_snap || to_snap > length / 2) return false;              // on the hit, or moved back onto an earlier step
    int64_t passed = int64_t((uint32_t(playhead) + length - heard % length) % length); // how far the playhead went on
    if (passed > int64_t(length / 2)) passed -= int64_t(length);    // still short of the hit (it rounded up to the next tick)
    return int64_t(to_snap) > passed;
}

} // namespace rhythmer
