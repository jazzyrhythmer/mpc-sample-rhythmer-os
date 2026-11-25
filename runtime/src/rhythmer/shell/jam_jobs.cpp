// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Jam capture, after Rhythmer FM-1's jam to song (model/jam_log.h). From PLAY outside Song to STOP, each bar is
// logged as it plays: the pattern each used track plays (none while it is silent or off), which tracks are not heard
// as the bar begins (Mute, or another track's Solo), the tempo, and a sequence that took over on it. At STOP the run
// is kept as a jam (two bars or more; the newest eight). JAMS (Song's Shift + B1) lists them: the wheel picks, K1 and
// K2 set FROM and TO, B2 plays the part as a song, B3 or Push lays it after the song's end, ERASE deletes the jam.
#include "rhythmer/shell/context.h"
#include "rhythmer/engine/engine_host.h"
#include <algorithm>
#include <string>

namespace rhythmer {
namespace {

// The patterns the used tracks play now; read through the bar, so the bar keeps what played to its end (a launch
// lands on a bar line).
void read_patterns(const Project& project, const EngineHost& host, JamBar& bar) {
    for (unsigned t = 0; t < track_count; ++t)
        bar.pattern[t] = project.tracks[t].used() && host.position_of(t) >= 0 ? uint8_t(host.pattern_of(t) % pattern_count) : jam_silent;
}

// The tracks not heard and the tempo, as a bar begins.
void read_mix(const Project& project, unsigned tempo, JamBar& bar) {
    bool solo = false;
    for (const auto& t : project.tracks) solo = solo || (t.used() && t.mix.solo);
    bar.muted = 0;
    for (unsigned t = 0; t < track_count; ++t) {
        const auto& mix = project.tracks[t].mix;
        if (mix.mute || (solo && !mix.solo)) bar.muted = uint16_t(bar.muted | 1u << t);
    }
    bar.tempo = uint16_t(tempo);
}

} // namespace

void Context::jam_tick() {
    if (jam_auditioning) { // the jam played to its end, or STOP (once the engine has started it)
        if (host.playing()) jam_audition_heard = true;
        else if (jam_audition_heard) end_audition();
    }
    const bool running = host.playing() && !host.count_in() && !host.song_playing();
    if (!running) {
        if (jam_running) { // the run stops: the bar it stopped in, then the run kept as a jam
            if (!jam.full()) jam.bars.push_back(jam_now);
            const bool full = jams.jams.size() == jams_kept;
            const bool kept = jams.keep(std::move(jam));
            jam = JamLog {};
            if (kept && full && ui.jam_pick) --ui.jam_pick; // the oldest went: the picked one moved up
            if (kept) { dirty = true; publish_jams(); }
        }
        jam_running = false;
        if (host.song_playing()) jam_scene = -1; // launched while the song plays: no jam's
        ui.jam_logging = false;
        ui.jam_bars = 0;
        return;
    }
    const uint32_t bar = host.ticks() / std::max(project.bar_ticks(), 1u);
    auto begin_bar = [&] {
        jam_now.scene = int16_t(jam_scene);
        jam_scene = -1;
        read_mix(project, tempo_now(), jam_now);
    };
    if (!jam_running) { // PLAY outside Song: a new run
        jam = JamLog {};
        jam_running = true;
        jam_bar_seen = bar;
        jam_now = JamBar {};
        begin_bar();
    } else if (bar != jam_bar_seen) {
        // The bar before is logged; bars passed between two looks played as it did.
        const uint32_t passed = bar > jam_bar_seen ? std::min<uint32_t>(bar - jam_bar_seen, 64) : 1;
        for (uint32_t n = 0; n < passed && !jam.full(); ++n) {
            jam.bars.push_back(jam_now);
            jam_now.scene = -1;
        }
        jam_bar_seen = bar;
        begin_bar();
    }
    read_patterns(project, host, jam_now);
    ui.jam_logging = !jam.full();
    ui.jam_bars = uint16_t(jam.bars.size());
}

void Context::publish_jams() {
    jam_view = std::make_shared<const JamList>(jams);
    ui.jam_count = uint8_t(jams.jams.size());
    if (!ui.jam_count) { ui.jam_sheet = false; ui.jam_pick = 0; ui.jam_pick_bars = ui.jam_from = ui.jam_to = 0; return; }
    if (ui.jam_pick >= ui.jam_count) ui.jam_pick = uint8_t(ui.jam_count - 1);
    const auto bars = uint16_t(jams.jams[ui.jam_pick].bars.size());
    if (bars != ui.jam_pick_bars) { ui.jam_pick_bars = bars; ui.jam_from = 0; ui.jam_to = bars; } // another jam: all of it
    ui.jam_to = std::clamp<uint16_t>(ui.jam_to, 1, bars);
    ui.jam_from = std::min<uint16_t>(ui.jam_from, uint16_t(ui.jam_to - 1));
}

void Context::open_jams(bool open) {
    if (jam_auditioning) { stop(); end_audition(); }
    if (!open) { ui.jam_sheet = false; return; }
    if (jams.jams.empty()) { notice(ui.jam_logging ? "The jam is kept at STOP" : "No jams yet"); return; }
    ui.jam_sheet = true;
    ui.jam_pick = uint8_t(jams.jams.size() - 1); // the newest
    ui.jam_pick_bars = 0;                        // all of it
    publish_jams();
}

void Context::pick_jam(int delta) {
    if (jams.jams.empty()) return;
    const int pick = std::clamp(int(ui.jam_pick) - delta, 0, int(jams.jams.size()) - 1); // the list runs newest first
    if (pick == ui.jam_pick) return;
    if (jam_auditioning) { stop(); end_audition(); }
    ui.jam_pick = uint8_t(pick);
    ui.jam_pick_bars = 0;
    publish_jams();
}

void Context::take_jam() {
    if (jams.jams.empty()) { notice("No jams yet"); return; }
    if (jam_auditioning) { stop(); end_audition(); }
    const auto& picked = jams.jams[ui.jam_pick % jams.jams.size()];
    const JamTake took = jam_to_song(project, patterns, picked, ui.jam_from, ui.jam_to);
    if (!took.bars) { notice("Song is full"); return; }
    ui.song_cursor = uint16_t(took.at);
    ui.song_view = uint16_t(took.at);
    ui.jam_sheet = false; // back on Song, at the jam
    sync_song();
    dirty = true;
    notice("Jam " + std::to_string(picked.number) + " at bar " + std::to_string(took.at + 1) + (took.left_out ? ", some left out" : ""));
}

void Context::erase_jam() {
    if (jams.jams.empty()) return;
    if (jam_auditioning) { stop(); end_audition(); }
    const unsigned pick = ui.jam_pick % jams.jams.size();
    const std::string name = "Jam " + std::to_string(jams.jams[pick].number);
    Edit edit;
    edit.what = "Delete " + name;
    edit.track = ui.track;
    edit.jams_before = std::make_unique<JamList>(jams);
    jams.erase(pick);
    edit.jams_after = std::make_unique<JamList>(jams);
    edits.push(std::move(edit));
    if (ui.jam_pick && ui.jam_pick >= jams.jams.size()) --ui.jam_pick; // the next older, else the newest left
    ui.jam_pick_bars = 0;
    dirty = true;
    publish_jams();
    notice(name + " deleted");
}

// The picked jam's FROM-TO as a song of its own on the audio thread (the project's song stays as it is), played from
// its first bar; the engine's song comes back when it stops (end_audition).
void Context::audition_jam() {
    if (jam_auditioning || host.playing()) { stop(); return; } // again: it stops (jam_tick gives the song back)
    if (jams.jams.empty()) return;
    const auto& picked = jams.jams[ui.jam_pick % jams.jams.size()];
    Song kept = project.song;
    project.song = Song {};
    jam_to_song(project, patterns, picked, ui.jam_from, ui.jam_to);
    Song heard = project.song;
    project.song = std::move(kept);
    if (!heard.count) { notice("Nothing plays there"); return; }
    send_song(heard);
    played_song = true;
    send({Command::Type::Play, 0, 0, 0, 1}); // the song from its first bar
    jam_auditioning = true;
    jam_audition_heard = false;
    ui.jam_audition = true;
}

void Context::end_audition() {
    jam_auditioning = false;
    ui.jam_audition = false;
    sync_song();
}

} // namespace rhythmer
