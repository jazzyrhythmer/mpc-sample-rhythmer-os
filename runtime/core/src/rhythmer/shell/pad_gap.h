// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/modulators.h"
#include "rhythmer/model/pad_params.h"
#include "rhythmer/model/pattern.h"
#include "rhythmer/model/project.h"
#include "rhythmer/model/project_file.h"
#include "rhythmer/model/sampler.h"
#include <algorithm>
#include <array>

// Erase + an empty kit pad (stopped): the pads after it move down one and close the gap, through its bank and the
// next one (bank A's gap takes B's pad 1 onto A16 and B's pads down one; C stays; bank H's gap stays in H, never
// A); the last pad of the two is left empty. What names a pad goes with it: its sound, chain and mute, its zone
// trims, its notes, motion, lane lengths and Euclidean rhythms in every pattern of the track, the modulators' and
// macros' pad lanes, Knob FX's pads and the compressor's key pad. Notes and lanes still on the empty pad (a pad
// deleted keeps its notes) go. Plain data, no Context, so the host tests run it.
namespace rhythmer {

// The pad after the last one a gap at `gap` moves: the end of the bank after its own, or of bank H.
constexpr unsigned gap_end(unsigned gap) {
    const unsigned end = (gap % sampler_pads / bank_pads + 2) * bank_pads;
    return end < sampler_pads ? end : sampler_pads;
}

// Kit pad `n` once the gap at `gap` is closed: one lower between it and gap_end(), -1 for the gap itself, else as it is.
constexpr int gap_closed(unsigned n, unsigned gap) {
    if (n == gap) return -1;
    return n > gap && n < gap_end(gap) ? int(n - 1) : int(n);
}

// Whether closing the gap at `gap` moves a pad: it is empty and a pad after it, up to gap_end(), holds a sample.
inline bool gap_closes(const Track& t, unsigned gap) {
    gap %= sampler_pads;
    if (t.pads[gap].used()) return false;
    for (unsigned n = gap + 1; n < gap_end(gap); ++n) if (t.pads[n].used()) return true;
    return false;
}

// The track's kit and its patterns.
inline void close_pad_gap(Track& t, std::array<Pattern, pattern_count>& patterns, unsigned gap) {
    gap %= sampler_pads;
    const unsigned end = gap_end(gap);
    for (unsigned n = gap; n + 1 < end; ++n) {
        t.pads[n] = t.pads[n + 1];
        t.pad_fx.at[n] = t.pad_fx.at[n + 1];
        t.set_pad_muted(n, t.pad_muted(n + 1));
    }
    t.pads[end - 1] = PadSound {};
    t.pad_fx.at[end - 1] = Chain {{}, pad_slots};
    t.set_pad_muted(end - 1, false);
    for (auto& z : t.zone_trims) {
        if (!z.used()) continue;
        int to = gap_closed(z.pad, gap);
        if (to < 0) z = ZoneTrim {};
        else z.pad = uint8_t(to);
    }
    if (int to = gap_closed(t.last_pad, gap); to >= 0) t.last_pad = uint8_t(to);
    if (int to = gap_closed(t.levels_pad, gap); to >= 0) t.levels_pad = uint8_t(to);
    // A kit's key: pad n is note sampler_first_note + n; -1 for the gap, -2 for a key that is no pad.
    auto key_to = [&](unsigned key) {
        if (key < sampler_first_note || key >= sampler_first_note + sampler_pads) return -2;
        int to = gap_closed(key - sampler_first_note, gap);
        return to < 0 ? -1 : int(sampler_first_note) + to;
    };
    for (auto& p : patterns) {
        p.events.erase(std::remove_if(p.events.begin(), p.events.end(), [&](const Event& e) { return key_to(e.key) == -1; }), p.events.end());
        for (auto& e : p.events) if (int to = key_to(e.key); to >= 0) e.key = uint8_t(to);
        std::stable_sort(p.events.begin(), p.events.end(), [](const Event& a, const Event& b) { return a.tick != b.tick ? a.tick < b.tick : a.key < b.key; });
        p.motion.erase(std::remove_if(p.motion.begin(), p.motion.end(),
                                      [&](const MotionPoint& m) { return is_pad_lane(m.param) && gap_closed(m.pad, gap) < 0; }), p.motion.end());
        for (auto& m : p.motion) if (is_pad_lane(m.param)) m.pad = uint8_t(gap_closed(m.pad, gap));
        std::stable_sort(p.motion.begin(), p.motion.end(), [](const MotionPoint& a, const MotionPoint& b) { return a.tick != b.tick ? a.tick < b.tick : a.param != b.param ? a.param < b.param : a.pad < b.pad; });
        p.lanes.erase(std::remove_if(p.lanes.begin(), p.lanes.end(), [&](const LaneLength& l) { return key_to(l.key) == -1; }), p.lanes.end());
        for (auto& l : p.lanes) if (int to = key_to(l.key); to >= 0) l.key = uint8_t(to);
        std::stable_sort(p.lanes.begin(), p.lanes.end(), [](const LaneLength& a, const LaneLength& b) { return a.key < b.key; });
        p.euclids.erase(std::remove_if(p.euclids.begin(), p.euclids.end(), [&](const EuclidLane& l) { return key_to(l.key) == -1; }), p.euclids.end());
        for (auto& l : p.euclids) if (int to = key_to(l.key); to >= 0) l.key = uint8_t(to);
        std::stable_sort(p.euclids.begin(), p.euclids.end(), [](const EuclidLane& a, const EuclidLane& b) { return a.key < b.key; });
    }
}

// What names track `track`'s pads in the rest of the project: the modulators' and macros' pad lanes (a lane on the
// gap goes), Knob FX's pads and the compressor's key pad (the gap: the whole kit again).
inline void close_pad_gap_names(Project& project, unsigned track, unsigned gap) {
    track %= track_count; gap %= sampler_pads;
    for (auto& mod : project.modulators)
        for (auto& g : mod.targets) {
            if (!g.used() || g.track != track || pad_param_of(g.param) < 0) continue;
            int to = gap_closed(g.pad, gap);
            if (to < 0) g = ModTarget {};
            else g.pad = uint8_t(to);
        }
    for (auto& macro : project.macros)
        for (auto& g : macro.targets) {
            if (!g.used() || g.track != track || pad_param_of(g.param) < 0) continue;
            int to = gap_closed(g.pad, gap);
            if (to < 0) g = MacroTarget {};
            else g.pad = uint8_t(to);
        }
    auto& k = project.knob_fx;
    std::array<bool, sampler_pads> on {};
    for (unsigned n = 0; n < sampler_pads; ++n) on[n] = k.pad_on(track, n);
    for (unsigned n = 0; n < sampler_pads; ++n) {
        int to = gap_closed(n, gap);
        if (n == gap || (to >= 0 && unsigned(to) != n)) k.set_pad(track, n, false);
    }
    for (unsigned n = 0; n < sampler_pads; ++n) if (int to = gap_closed(n, gap); to >= 0 && on[n]) k.set_pad(track, unsigned(to), true);
    auto& c = project.compressor;
    if (c.key == track && c.key_pad != MasterComp::whole_kit) {
        int to = gap_closed(c.key_pad, gap);
        c.key_pad = to < 0 ? MasterComp::whole_kit : uint8_t(to);
    }
}

} // namespace rhythmer
