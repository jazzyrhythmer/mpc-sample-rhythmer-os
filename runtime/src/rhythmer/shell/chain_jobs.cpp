// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// A track's pattern chain (round 2, seq-sheet2 B): the patterns tapped in
// order while Seq is held, each played once through and the next at its end,
// round again; a song for one track without opening Song. The first tap
// launches as ever (at the next bar while playing) and starts the chain; each
// tap after it in the same hold adds a pattern. The chain is queued a
// pattern at a time: in the last bar of the one playing the next is launched
// for the bar line, where it begins as this one ends. A pattern launched by
// hand, a single tap, ends it; the song playing leaves it be. Shift + pads
// tapped are sequences (shell/sequences.h): the chain is still the selected
// track's (chain_lead), and each sequence plays as long as it is
// (sequence_length), counted from the bar it began on; in its last bar the
// next is launched on every track.
#include "rhythmer/shell/context.h"
#include "rhythmer/engine/engine_host.h"
#include "rhythmer/shell/sequences.h"
#include <algorithm>
#include <string>

namespace rhythmer {

void Context::chain_start(unsigned pattern) {
    auto& tr = track();
    tr.chain = {};
    tr.chain[0] = uint8_t(pattern % pattern_count);
    tr.chain_count = 1;
    tr.chain_at = 0;
    chain_queued[ui.track % track_count] = false;
}

void Context::chain_add(unsigned pattern) {
    auto& tr = track();
    if (tr.chain_count >= max_chain) { notice("Chain full"); return; }
    if (!tr.chain_count) { chain_start(pattern); return; }
    tr.chain[tr.chain_count++] = uint8_t(pattern % pattern_count);
    bool sequences = chain_lead == int(ui.track % track_count);
    std::string shown = sequences ? "SEQ " : "";
    for (unsigned n = 0; n < tr.chain_count && n < 4; ++n) shown += (n ? " > " : "") + std::string(sequences ? "" : "P") + std::to_string(tr.chain[n] + 1);
    if (tr.chain_count > 4) shown += " +" + std::to_string(tr.chain_count - 4);
    notice("Chain " + shown);
    dirty = true;
}

void Context::chain_sequence(unsigned pattern, bool first) {
    if (!first && chain_lead == int(ui.track % track_count)) { chain_add(pattern); return; }
    chain_start(pattern); // after launch_scene(), which ended every chain
    chain_lead = int8_t(ui.track % track_count);
    const uint32_t bar = std::max(1u, project.bar_ticks());
    chain_from[ui.track % track_count] = host.playing() ? (host.ticks() / bar + 1) * bar : UINT32_MAX; // it begins on the next bar
}

// A chain of sequences, its lead track `t`: in the last bar of the sequence playing, the next is launched for the
// bar line.
void Context::sequence_chain_tick(unsigned t, uint32_t bar) {
    auto& tr = project.tracks[t];
    const uint32_t ticks = host.ticks();
    if (chain_from[t] == UINT32_MAX) chain_from[t] = ticks / bar * bar; // after Play: from this bar
    if (ticks < chain_from[t]) {
        if (chain_from[t] - ticks > bar) chain_from[t] = ticks / bar * bar; // the playhead moved back: from this bar
        else return; // the sequence waits for its bar
    }
    const unsigned scene = tr.chain[tr.chain_at % max_chain];
    uint32_t length = sequence_length(project, patterns, t, scene, bar);
    length = std::max(bar, (length + bar - 1) / bar * bar);
    const uint32_t end = chain_from[t] + length;
    if (ticks + bar < end) return; // not in its last bar yet
    chaining = true;
    launch_scene(tr.chain_ahead(1), true); // every track, its tempo with it
    chaining = false;
    tr.chain_at = uint8_t((tr.chain_at + 1) % std::max<unsigned>(tr.chain_count, 1));
    chain_from[t] = end;
}

void Context::chain_tick() {
    bool running = host.playing() && !host.song_playing();
    uint32_t bar = std::max(1u, project.bar_ticks());
    for (unsigned t = 0; t < track_count; ++t) {
        auto& tr = project.tracks[t];
        if (!running || !tr.chained()) { chain_queued[t] = false; chain_from[t] = UINT32_MAX; continue; }
        if (chain_lead == int(t)) { sequence_chain_tick(t, bar); continue; }
        unsigned playing = host.pattern_of(t) % pattern_count;
        if (playing != tr.chain[tr.chain_at]) { // the queued one began (or the chain is still to start): find it in the chain
            for (unsigned k = 1; k < tr.chain_count; ++k)
                if (tr.chain[(tr.chain_at + k) % tr.chain_count] == playing) { tr.chain_at = uint8_t((tr.chain_at + k) % tr.chain_count); break; }
            if (playing != tr.chain[tr.chain_at]) continue; // not one of the chain's yet: the first waits for its bar
            chain_queued[t] = false;
        }
        if (chain_queued[t] || !chain_due(host.position_of(t), patterns.at[t][playing].length, bar)) continue;
        chaining = true;
        launch(t, tr.chain_ahead(1)); // for the bar line: it begins as this one ends
        chaining = false;
        chain_queued[t] = true;
    }
}

} // namespace rhythmer
