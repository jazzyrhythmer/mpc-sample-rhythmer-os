// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/project.h"
#include <algorithm>
#include <cstdint>

// Free length (2026-11-18). An empty pattern starts Free (Pattern::free): K1 shows Free, below 1 bar. A Seq Record
// take on it, with no notes on another track playing with it, grows it a bar at a time: the bar playing and the
// next are always in it. The take ends at the end of the bar playing; pressed in the first eighth of a bar after
// the first, at that bar line, and what was played since moves to the top. With notes on another track playing
// with it, the take is as long as the longest of them, and lines up with it (shell/sequences.h). Pure, for the
// shell, the cards and the tests.
namespace rhythmer {

// The longest pattern with notes that another used track than `t` is on (not off), and its track (-1: none): what
// plays with it in its sequence (model/scene_links.h).
struct ColumnFit { uint32_t length = 0; int track = -1; };
inline ColumnFit column_fit(const Project& project, const PatternBank& patterns, unsigned t) {
    ColumnFit fit;
    for (unsigned k = 0; k < track_count; ++k) {
        const auto& track = project.tracks[k];
        if (k == t % track_count || !track.used() || track.off) continue;
        const auto& pattern = patterns.at[k][track.pattern % pattern_count];
        if (pattern.empty() || pattern.length <= fit.length) continue;
        fit.length = pattern.length; fit.track = int(k);
    }
    return fit;
}

// What K1 shows for a pattern: Free (0), else the ticks a take on it would get.
inline uint32_t free_shown(const Pattern& pattern, uint32_t column) { return pattern.is_free() ? column : pattern.length; }
// K1 offers Free: the pattern is empty and no other track has notes in its sequence.
inline bool free_offered(const Pattern& pattern, uint32_t column) { return pattern.empty() && !column; }

// The length a growing take needs at `position` (ticks from its top): the bar playing and the next, `most` at most.
inline uint32_t free_grown(uint32_t position, uint32_t bar, uint32_t most) {
    bar = std::max<uint32_t>(bar, 1);
    return uint32_t(std::min<uint64_t>(most, (uint64_t(position) / bar + 2) * bar));
}

// Where a take ends at `position`: the end of the bar playing, or, in the first `grace` ticks of a bar after the
// first, that bar line (`wrap`: what lies past it moves to the top).
struct FreeEnd { uint32_t length = 0; bool wrap = false; };
inline FreeEnd free_end(uint32_t position, uint32_t bar, uint32_t grace, uint32_t most) {
    bar = std::max<uint32_t>(bar, 1);
    uint32_t done = position / bar, into = position % bar;
    FreeEnd end;
    if (done >= 1 && into < grace) { end.length = done * bar; end.wrap = true; }
    else end.length = (done + 1) * bar;
    end.length = std::clamp<uint32_t>(end.length, bar, std::max(most, bar));
    if (end.length >= most) end.wrap = false;
    return end;
}

// The pattern ends there: its notes, Motion and kept punches past the end move to the top with `wrap`.
inline void free_close(Pattern& p, FreeEnd end) {
    p.free = false;
    p.length = end.length;
    if (end.wrap) {
        bool moved = false;
        for (auto& e : p.events) if (e.tick >= end.length) { e.tick -= end.length; moved = true; }
        if (moved) p.sort();
        moved = false;
        for (auto& m : p.motion) if (m.tick >= end.length) { m.tick -= end.length; moved = true; }
        if (moved) p.sort_motion();
        for (auto& k : p.punches) if (k.tick >= end.length) k.tick -= end.length;
        std::sort(p.punches.begin(), p.punches.end(), [](const PatternPunch& a, const PatternPunch& b) { return a.tick < b.tick; });
    }
    p.fit_lanes();
}

} // namespace rhythmer
