// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/chop_slices.h"
#include "rhythmer/model/project.h"
#include <algorithm>
#include <functional>
#include <map>
#include <utility>
#include <vector>

// Issue 24: slice hits, kit notes played on Chop while Seq Record ran (Event::slice). Each plays a slice of its pad's
// sample's chop as the chop is now, the kit keeping no pad for it: a cut moved moves the hits on it, and a slice past
// the chop's end is silent. A sample's chop is the one Chop holds while the sample is Chop's, else the one the
// project keeps (Context::live_chop()). Pure.
namespace rhythmer {

using ChopOf = std::function<const Slices&(unsigned sample)>;

// The kit pad a note plays (-1: a note off the kit's pads).
inline int kit_pad_of(const Event& e) {
    return e.key >= sampler_first_note && e.key < sampler_first_note + sampler_pads ? int(e.key - sampler_first_note) : -1;
}

// Slice hits on pads holding `sample`, on slice `from` and after (what a change from there changes the sound of):
// the question before Chop renumbers slices (Ask::Rechop).
inline unsigned slice_hits(const Project& project, const PatternBank& patterns, unsigned sample, unsigned from = 0) {
    unsigned count = 0;
    for (unsigned t = 0; t < track_count; ++t) {
        const auto& track = project.tracks[t];
        if (track.engine != EngineKind::Sampler) continue;
        for (const auto& pattern : patterns.at[t])
            for (const auto& e : pattern.events) {
                if (!e.slice || unsigned(e.slice - 1) < from) continue;
                const int pad = kit_pad_of(e);
                if (pad >= 0 && track.pads[size_t(pad)].used() && track.pads[size_t(pad)].sample == sample) ++count;
            }
    }
    return count;
}

// The sample slots slice hits play, in order (their slices' starts held in memory when read from the card).
inline std::vector<uint16_t> sliced_samples(const Project& project, const PatternBank& patterns) {
    std::vector<uint16_t> out;
    for (unsigned t = 0; t < track_count; ++t) {
        const auto& track = project.tracks[t];
        if (track.engine != EngineKind::Sampler) continue;
        for (const auto& pattern : patterns.at[t])
            for (const auto& e : pattern.events) {
                const int pad = e.slice ? kit_pad_of(e) : -1;
                if (pad >= 0 && track.pads[size_t(pad)].used()) out.push_back(track.pads[size_t(pad)].sample);
            }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

inline bool any_slice_hits(const PatternBank& patterns) {
    for (const auto& track : patterns.at)
        for (const auto& pattern : track)
            for (const auto& e : pattern.events) if (e.slice) return true;
    return false;
}

// For a sampler that plays no slice hits (the MPC 3 export): each pad and slice the hits play laid on a free pad of
// their kit as TO PADS lays a slice (POLY: the pad's sound over the slice's region), the hits moved onto it as plain
// notes. A hit on a slice past its chop's end is dropped (it is silent); one with no free pad left plays its pad's
// region. How many were left on their pad so.
inline unsigned lay_slice_hits(Project& project, PatternBank& patterns, const ChopOf& chop_of) {
    unsigned left = 0;
    for (unsigned t = 0; t < track_count; ++t) {
        auto& track = project.tracks[t];
        if (track.engine != EngineKind::Sampler) continue;
        std::map<std::pair<int, int>, int> laid; // a pad and slice, the pad it went on (-1: none free, -2: past the chop)
        unsigned next = 0;                        // the free pads, from the first
        for (auto& pattern : patterns.at[t]) {
            bool changed = false;
            for (auto& e : pattern.events) {
                if (!e.slice) continue;
                const int pad = kit_pad_of(e), slice = e.slice - 1;
                e.slice = 0;
                changed = true;
                if (pad < 0) continue;
                auto at = laid.find({pad, slice});
                if (at == laid.end()) {
                    int to = -2;
                    const PadSound parent = track.pads[size_t(pad)];
                    if (parent.used()) {
                        const Slices& s = chop_of(parent.sample);
                        if (unsigned(slice) < s.count) {
                            while (next < sampler_pads && track.pads[next].used()) ++next;
                            to = -1;
                            if (next < sampler_pads) { track.pads[next] = slice_sound(parent, s.start[size_t(slice)], s.stop[size_t(slice)], region_frames(parent, project.samples[parent.sample].frames)); to = int(next); }
                        }
                    }
                    at = laid.emplace(std::make_pair(pad, slice), to).first;
                }
                if (at->second >= 0) e.key = uint8_t(sampler_first_note + unsigned(at->second));
                else if (at->second == -1) ++left;
                else e.velocity = 0; // past the chop's end: marked, dropped below
            }
            if (!changed) continue;
            pattern.events.erase(std::remove_if(pattern.events.begin(), pattern.events.end(), [](const Event& e) { return !e.velocity; }),
                                 pattern.events.end());
            pattern.sort();
        }
    }
    return left;
}

} // namespace rhythmer
