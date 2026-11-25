// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The Song's export (parity phase 6), as Akai's song_mode page has it: B1
// on Song opens it; B1 Cancel, B2 Audio mixdown (the Name sheet, then
// shell/render.cpp plays the song into a WAV file), B3 New pattern.
//
// Song -> new pattern: the first pattern number empty on every track and
// used by no clip takes, track by track, the song's clips laid end to end
// (each clip its pattern repeated from the clip's start, muted clips left
// out) for the song's length, at most 128 bars. One undo. Motion and the
// song's punches stay in the song (a pattern holds neither).
//
// MPC 3 project: the project as MPC 3 opens it (MPC One, Live, Key, X, the
// MPC 3 app), Rhythmer/MPC 3/<name>.xpj on the card (shell/mpc3_export.h): the
// kits, their patterns, the song, the prints. Ableton Live Set (the wheel's
// last): the project as Live 12.1 or later opens it, Rhythmer/Ableton/<name>
// Project/ (shell/live_export.h): each pattern a Session View clip, the song
// in Arrangement View, kits as Drum Racks, plug-in tracks as their notes and
// their prints.
#include "rhythmer/model/slice_hits.h"
#include "rhythmer/model/song_flatten.h"
#include "rhythmer/shell/context.h"
#include "rhythmer/plugin/plugin_rack.h"
#include "rhythmer/shell/project_store.h"
#include "rhythmer/shell/print_engine.h"
#include "rhythmer/shell/sample_library.h"
#include <filesystem>
#include <algorithm>

namespace rhythmer {

// The project's bytes for the MPC 3 and Live Set exports. Neither plays a slice of a pad's chop from a note: slice hits
// (issue 24) go onto free pads of their kit first, in copies (model/slice_hits.h), the project as it is. How many found
// no free pad.
std::vector<uint8_t> Context::export_bytes(unsigned& left) {
    PluginStates states;
    if (plugins) states = plugins->states();
    left = 0;
    if (!any_slice_hits(patterns)) return rhythmer::save_project(project, patterns, &states, &prints);
    auto laid = std::make_unique<Project>(project);
    auto bank = std::make_unique<PatternBank>(patterns);
    left = lay_slice_hits(*laid, *bank, [this](unsigned sample) -> const Slices& { return live_chop(sample); });
    return rhythmer::save_project(*laid, *bank, &states, &prints);
}

static std::string export_said(bool live) { return live ? "Live Set: " : "MPC 3: "; }

static std::string exporting_note(bool live, unsigned left) {
    return left ? export_said(live) + "exporting; " + std::to_string(left) + (left == 1 ? " slice hit plays" : " slice hits play") + " its whole pad, no pad free"
                : export_said(live) + "exporting";
}

// The export, handed to the store's thread.
static void hand_over(ProjectStore& store, bool live, const std::string& path, std::vector<uint8_t> bytes, const std::string& name) {
    if (live) store.export_live(path, std::move(bytes), name);
    else store.export_mpc3(path, std::move(bytes), name);
}

void Context::song_export_soft(int key) {
    ui.song_export = false;
    if (key == 1) { // roadmap P4: the wheel picked what (a MIDI file needs no song: the patterns playing then)
        if (!project.song.length() && ui.export_kind < export_kinds::midi) { notice("Mixdown: the song is empty"); return; }
        open_name(NameTarget::Mixdown, project.name[0] ? project.name : "Song", sizeof(project.name) - 1, false);
    }
    if (key == 2) song_to_pattern();
}

// Plug-in tracks go to MPC 3 and to Live as their prints: each one with notes is printed first (the print engine skips
// a pattern whose print is up to date), a track at a time, then the export starts (export_tick()).
void Context::export_project(const std::string& name, bool live) {
    if (!store) return;
    if (store->exporting() || export_waiting) { notice(export_said(live) + "an export runs"); return; }
    uint32_t tracks = 0;
    for (unsigned t = 0; t < track_count; ++t) {
        if (project.tracks[t].engine != EngineKind::Plugin) continue;
        for (const auto& pattern : patterns.at[t]) if (!pattern.empty()) { tracks |= 1u << t; break; }
    }
    if (tracks && printer) {
        if (project_path.empty()) { notice(export_said(live) + "save first, prints go beside the project"); return; }
        export_name = name;
        export_live = live;
        export_pending = tracks;
        export_printing = -1;
        export_waiting = true;
        notice(export_said(live) + "printing plug-in tracks");
        return;
    }
    unsigned left = 0;
    hand_over(*store, live, project_path, export_bytes(left), name);
    notice(exporting_note(live, left));
}

void Context::export_tick() {
    if (!export_waiting || !printer || !store) return;
    if (printer->busy()) return; // ours, or a freeze's: its turn first
    if (export_printing >= 0) { export_printing = -1; return; } // ours finished: its results are taken on the next pass
    if (export_pending) {
        unsigned t = 0;
        while (!((export_pending >> t) & 1)) ++t;
        PrintJob job;
        job.project_path = project_path;
        job.track = t;
        for (unsigned p = 0; p < pattern_count; ++p)
            if (!patterns.at[t][p].empty()) job.patterns.push_back(uint8_t(p));
        job.project = std::make_unique<Project>(project);
        job.bank = std::make_unique<PatternBank>(patterns);
        if (plugins) job.states = plugins->states();
        if (library) job.card_top = library->card();
        job.known = prints;
        if (!printer->start(std::move(job))) return; // busy after all: the next pass
        export_pending &= ~(1u << t);
        export_printing = int(t);
        notice(export_said(export_live) + "printing track " + std::to_string(t + 1));
        return;
    }
    export_waiting = false;
    unsigned left = 0;
    hand_over(*store, export_live, project_path, export_bytes(left), export_name);
    notice(exporting_note(export_live, left));
}

void Context::song_to_pattern() {
    const auto& song = project.song;
    unsigned bars = song.length();
    if (!bars) { notice("Song: empty"); return; }
    bool cut = bars > max_bars;
    bars = std::min(bars, max_bars);
    uint32_t bar = project.bar_ticks();
    int column = -1;
    for (unsigned p = 0; p < pattern_count && column < 0; ++p) {
        bool free = true;
        for (unsigned t = 0; t < track_count && free; ++t) free = patterns.at[t][p].empty();
        for (unsigned n = 0; n < song.count && free; ++n) free = song.clips[n].pattern != p;
        if (free) column = int(p);
    }
    if (column < 0) { notice("Song: no empty pattern"); return; }
    Edit edit;
    edit.what = "Song to P" + std::to_string(column + 1);
    edit.track = ui.track;
    edit.column = int16_t(column);
    bool full = false;
    for (unsigned t = 0; t < track_count; ++t) {
        Pattern out;
        bool any = flatten_lane(song, t, patterns.at[t], bar, bars, out, full);
        if (!any) continue;
        edit.column_before.emplace_back(uint8_t(t), patterns.at[t][size_t(column)]);
        edit.column_after.emplace_back(uint8_t(t), std::move(out));
    }
    if (edit.column_after.empty()) { notice("Song: no notes"); return; }
    for (const auto& [t, value] : edit.column_after) restore_pattern(t, unsigned(column), value);
    std::string what = edit.what;
    edits.push(std::move(edit));
    dirty = true;
    notice(what + (cut ? ", 128 bars" : full ? ", notes full" : ""));
}

} // namespace rhythmer
