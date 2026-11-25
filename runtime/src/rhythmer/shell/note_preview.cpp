// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Note preview (shell/note_preview.h): the notes go to the engine as a pad's would, straight (no arp, Note Repeat or
// Seq Record), and end on the UI tick once their time is up.
#include "rhythmer/shell/context.h"
#include "rhythmer/model/sampler.h"
#include <algorithm>

namespace rhythmer {

void Context::preview_notes(const std::vector<Event>& events, bool settle) {
    if (!ui.settings.note_preview) return;
    auto& np = note_preview;
    np.count = uint8_t(preview_chord(events, np.waiting));
    if (!np.count) return;
    np.track = ui.track;
    np.q = uint16_t(std::clamp<unsigned>(pattern().q(), 1, 0xffff));
    np.due = now + (settle ? preview_settle_ms : 0);
    note_preview_end(); // the last one stops as the next is asked for
    if (!settle) note_preview_tick();
}

void Context::note_preview_end() {
    auto& np = note_preview;
    const unsigned t = np.sounding_track % track_count;
    for (auto& k : np.sounding) {
        if (k < 0) continue;
        bool held = false; // a pad or key playing the same note now keeps it
        for (unsigned p = 0; p < pad_keys.size(); ++p)
            if (pad_track[p] % track_count == t && std::find(pad_keys[p].begin(), pad_keys[p].end(), k) != pad_keys[p].end()) held = true;
        if (k < 128 && midi_held[t][unsigned(k)]) held = true;
        if (!held) note(t, k, 0);
        k = -1;
    }
}

void Context::note_preview_tick() {
    auto& np = note_preview;
    if (np.sounding[0] >= 0 && int32_t(now - np.until) >= 0) note_preview_end();
    if (!np.count || int32_t(now - np.due) < 0) return;
    note_preview_end();
    const unsigned count = np.count;
    np.count = 0;
    if (!ui.settings.note_preview) return;
    const unsigned t = np.track % track_count;
    const bool kit = project.tracks[t].engine == EngineKind::Sampler;
    uint32_t longest = 0;
    unsigned n = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto& e = np.waiting[i];
        if (e.key > 127 && !kit) continue; // past 127: kit pads only, as the sequencer has it
        const int velocity = e.flags & step_accent ? std::min(127, e.velocity + 24) : int(e.velocity);
        const int key = pad_note(e.key, e.pitch); // the key, and over it what a kit pad plays at
        if (kit && (e.cutoff || e.slice) && e.key >= sampler_first_note) { // 16 Levels Filter's cutoff, a slice hit's slice
            Command c {Command::Type::PadNote, uint8_t(t), uint8_t(std::clamp(e.pitch + 64, 0, 255)), uint16_t(e.key - sampler_first_note), int16_t(velocity)};
            c.position = e.cutoff;
            c.micros = e.slice;
            send(c);
        } else {
            note(t, key, velocity);
        }
        np.sounding[n++] = key;
        longest = std::max(longest, preview_ms(e, np.q, tempo_now()));
    }
    np.sounding_track = uint8_t(t);
    np.until = now + longest;
}

} // namespace rhythmer
