// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/project.h"
#include "rhythmer/model/scene_links.h"
#include "rhythmer/shell/ui_state.h"
#include <algorithm>

// Sequences first. A sequence (the model's Scene) keeps a pattern, or off, for each track, and a pattern picked by
// hand is kept in the sequence in hand (model/scene_links.h). Seq opens the Sequences page (shell/page_stack.h),
// whose pads launch a sequence at the next bar: each track on its kept pattern, a track kept off stops, a track it
// keeps nothing for plays on. An empty kept pattern takes the sequence's length, so a take recorded on it fits. Seq
// held makes the pads the selected track's patterns on any page, as the Patterns page's pads are (B1 again): a pad
// launches that track only, and is kept. The track list on Patterns marks a track off its kept pattern (a follow
// action, a chain, Song). Shift + pad, Seq held or on Patterns, launches the sequence; Shift + pads tapped in order
// while Seq is held chain the sequences: the selected track's chain leads (shell/chain_jobs.cpp). Pure, for the
// shell, the screen and the tests.
namespace rhythmer {

// Whether Seq held's pads launch sequences now: with Shift held, as Shift + pad launches the sequence. Not while
// patterns freeze (Seq + Sample Record) or a pattern copy waits for its pads.
inline bool sequences_on_pads(const UiState& ui) {
    return ui.seq_held && ui.shift && !ui.seq_print && ui.copying != UiState::Copying::Patterns;
}
// The same for the lights and the pads chip: with Erase held the pads clear the selected track's patterns.
inline bool sequences_lit(const UiState& ui) { return sequences_on_pads(ui) && !ui.erase; }

// The sequence in view: the sequence in hand.
inline unsigned sequence_in_view(const Project& project, const UiState&) { return project.scene % pattern_count; }
// A used track off what the sequence in hand keeps for it: on another pattern, or off where it keeps a pattern, or
// playing where it keeps off (moved by a follow action, a chain or Song). Launching the sequence puts it back.
inline bool own_pattern(const Project& project, const UiState& ui, unsigned t) {
    const auto& track = project.tracks[t % track_count];
    if (!track.used()) return false;
    const int target = scene_target(track, sequence_in_view(project, ui));
    if (target == scene_plays_on) return false;
    if (target == scene_stops) return !track.off;
    return track.off || track.pattern % pattern_count != unsigned(target);
}
// How many tracks are off their kept pattern, and the first of them (-1: none).
inline unsigned own_patterns(const Project& project, const UiState& ui, int& first) {
    unsigned count = 0;
    first = -1;
    for (unsigned t = 0; t < track_count; ++t)
        if (own_pattern(project, ui, t)) { if (first < 0) first = int(t); ++count; }
    return count;
}

// Sequence n launched: which tracks it can move (a used track it keeps a pattern or off for).
inline bool sequence_takes(const Track& track) { return track.used(); }
// The length an empty kept pattern of sequence n takes when the sequence is launched: the selected track's kept
// pattern when it has notes, else the longest kept pattern with notes, else `fallback` (the pattern the selected
// track plays now).
inline uint32_t sequence_length(const Project& project, const PatternBank& patterns, unsigned selected, unsigned n, uint32_t fallback) {
    n %= pattern_count;
    const int own = scene_target(project.tracks[selected % track_count], n);
    if (own >= 0 && !patterns.at[selected % track_count][unsigned(own)].empty() && patterns.at[selected % track_count][unsigned(own)].length)
        return patterns.at[selected % track_count][unsigned(own)].length;
    uint32_t longest = 0;
    for (unsigned t = 0; t < track_count; ++t) {
        const int target = scene_target(project.tracks[t], n);
        if (project.tracks[t].used() && target >= 0 && !patterns.at[t][unsigned(target)].empty())
            longest = std::max(longest, patterns.at[t][unsigned(target)].length);
    }
    return longest ? longest : fallback;
}

} // namespace rhythmer
