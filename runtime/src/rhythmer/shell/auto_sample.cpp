// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The auto sampler (shell/auto_sample.h): asked for, and what it made put on a
// new KEYGROUP track.
#include "rhythmer/shell/context.h"
#include "rhythmer/engine/engine_host.h"
#include "rhythmer/plugin/plugin_rack.h"
#include "rhythmer/shell/print_engine.h"
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/shell/track_copy.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace rhythmer {
namespace fs = std::filesystem;

namespace {
// A name for files: no character a card or Rhythmer's paths ("#": a program's pad) keep for themselves.
std::string file_word(const std::string& text) {
    std::string out;
    for (char ch : text) out += std::string("/\\:\"*?<>|#").find(ch) != std::string::npos ? '-' : ch;
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out.substr(0, 40);
}

// The input a MIDI track takes as its sound (its Audio page's INPUT), as the Record source that hears the same; false: none.
bool midi_input(const Track& tr, InputSource& out) {
    if (tr.engine != EngineKind::Midi) return false;
    switch (tr.params[midi_track::AudioIn]) {
    case midi_track::AudioRearL: out = InputSource::RearL; return true;
    case midi_track::AudioRearR: out = InputSource::RearR; return true;
    case midi_track::AudioRear: out = InputSource::Rear; return true;
    case midi_track::AudioMic: out = InputSource::Mic; return true;
    case midi_track::AudioUsb: out = InputSource::Usb; return true;
    default: return false;
    }
}

// An error's first words, before any ":" (what follows says what to do): kept on the sheet.
void keep_error(UiState::AutoSheet& sheet, const std::string& error, int key) {
    std::snprintf(sheet.error, sizeof(sheet.error), "%s", error.substr(0, error.find(':')).c_str());
    sheet.error_key = int16_t(key);
}

// The plug-in of track `t` on the live rack. Ready where there is none to wait for (a track with no plug-in, no rack:
// the check then says so itself).
PluginStatus plugin_status(const Context& c, unsigned t) {
    const Track& tr = c.project.tracks[t % track_count];
    if (!c.plugins || tr.engine != EngineKind::Plugin || tr.plugin >= max_plugins || !c.project.plugins[tr.plugin].used()) return PluginStatus::Ready;
    const PluginStatus status = c.plugins->status(tr.plugin);
    return status == PluginStatus::None ? PluginStatus::Loading : status; // not staged yet: opening
}
} // namespace

std::string Context::autosample_name(unsigned track) const {
    const auto& tr = project.tracks[track % track_count];
    std::string name;
    if (tr.engine == EngineKind::Plugin && tr.plugin < max_plugins) {
        const auto& ref = project.plugins[tr.plugin];
        std::string preset = ref.preset;
        if (auto cut = preset.find_last_of('/'); cut != std::string::npos) preset = preset.substr(cut + 1);
        if (auto dot = preset.find_last_of('.'); dot != std::string::npos && dot > 0) preset = preset.substr(0, dot);
        name = preset.empty() ? std::string(ref.name) : std::string(ref.name) + " " + preset;
    }
    if (file_word(name).empty()) name = tr.name;
    name = file_word(name);
    return name.empty() ? "Keygroup" : name;
}

void Context::autosample(unsigned track, const AutoSampleSettings& settings, bool probe_first) {
    const unsigned t = track % track_count;
    const auto& tr = project.tracks[t];
    if (!printer) return;
    // A plug-in track's plug-in, or a MIDI track's instrument outside: played over its MIDI, heard at its INPUT.
    const bool outside = tr.engine == EngineKind::Midi;
    if (tr.engine != EngineKind::Plugin && !outside) {
        notice("Auto sample: plug-in and MIDI tracks");
        printer->note("autosample: track " + std::to_string(t + 1) + " is not a plug-in or MIDI track");
        return;
    }
    if (deep(t)) { notice("Track " + std::to_string(t + 1) + " is deep frozen"); return; }
    if (printer->busy() || autosample_track>=0) { notice("A print is under way"); printer->note("busy: a print is under way"); return; }
    if (settings.low > settings.high) { notice("LOW is above HIGH"); return; }
    if (!outside && plugin_status(*this, t) != PluginStatus::Ready) { // as the sheet's check waits for (autosample_tick())
        notice("The plug-in is still opening");
        printer->note("autosample: track " + std::to_string(t + 1) + "'s plug-in is not open yet");
        return;
    }
    InputSource source = InputSource::Rear;
    if (outside) {
        if (!midi_input(tr, source)) { notice("Auto sample: pick the INPUT"); printer->note("autosample: track " + std::to_string(t + 1) + " has no input"); return; }
        if ((ui.midi_in_use >> t) & 1) { notice("The INPUT is in use"); return; }
        if (host_recording()) { notice("A take is under way"); return; }
    }
    const std::string root = library ? library->root() : std::string();
    if (root.empty()) { notice("No card"); return; }
    // Samples/Keygroups/<name>/, a number after the name when one is there already.
    const fs::path top = fs::path(root) / "Keygroups";
    const std::string base = autosample_name(t);
    std::string name = base;
    std::error_code e;
    for (unsigned n = 2; n < 1000 && fs::exists(top / name, e); ++n) name = base + " " + std::to_string(n);
    PrintJob job;
    job.project_path = project_path;
    job.track = t;
    job.autosample = true;
    job.probe_first = probe_first;
    job.sampling = settings;
    job.sample_folder = (top / name).string();
    job.sample_name = name;
    job.project = std::make_unique<Project>(project);
    job.bank = std::make_unique<PatternBank>(patterns);
    if (plugins) job.states = plugins->states();
    job.card_top = library->card();
    if (outside) { // in real time: nothing else may play the instrument, and the input's ring hears the track's input
        if (host.playing()) stop();
        job.external = true;
        job.input = &host.recall();
        job.probe_first = true; // its check and its samples in one go: it is heard only once it is asked for
        autosample_source = int8_t(source);
        host.send({Command::Type::RecordSource, uint8_t(source), 0, 0, 0});
    }
    if (!printer->start(std::move(job))) { notice("A print is under way"); return; }
    autosample_outside = outside;
    autosample_playing_told = false;
    autosample_track = int8_t(t);autosample_epoch=track_epoch[t];
    auto_last = settings;
    auto_last.found = {}; // heard again for the next sheet
    notice("Sampling " + name);
}

bool Context::autosample_check(unsigned track, const AutoSampleSettings& settings) {
    const unsigned t = track % track_count;
    if (!printer || auto_probe_track>=0 || project.tracks[t].engine != EngineKind::Plugin || deep(t) || printer->busy()) return false;
    PrintJob job;
    job.project_path = project_path;
    job.track = t;
    job.probing = true;
    job.sampling = settings;
    job.project = std::make_unique<Project>(project);
    job.bank = std::make_unique<PatternBank>(patterns);
    if (plugins) job.states = plugins->states();
    if (library && !library->root().empty()) job.card_top = library->card();
    if(!printer->start(std::move(job)))return false;
    auto_probe_track=int8_t(t);auto_probe_epoch=track_epoch[t];return true;
}

void Context::autosample_tick() {
    if (!printer) return;
    // An instrument outside: its notes as their frames come (PrintEngine::post()); called off when the sequencer
    // starts (it would play the instrument too). The input's ring and meter hear the MIDI track's input while its
    // sheet is in view or its job runs, Input Config's own source otherwise.
    Command note;
    while (printer->due(host.recall().written(), note)) host.send(note);
    const bool running = autosample_track >= 0 && autosample_outside;
    if (running && host.playing() && !autosample_playing_told) { autosample_playing_told = true; printer->cancel(); }
    {
        int want = -1;
        InputSource source = InputSource::Rear;
        const auto& sheet = ui.auto_sheet;
        if (running) want = autosample_source;
        else if (ui.page == PageId::AutoSample && midi_input(project.tracks[sheet.track % track_count], source) && !host_recording()) want = int(source);
        // While it is wanted Input Config leaves the engine's source alone (record_tick()), and takes it back after.
        if (want >= 0 && (want != autosample_source || host.record_source() != unsigned(want)))
            host.send({Command::Type::RecordSource, uint8_t(want), 0, 0, 0});
        autosample_source = int8_t(want);
    }
    AutoProbeResult heard;
    while (printer->take_probed(heard)) {
        const bool current=auto_probe_track==int(heard.track)&&auto_probe_epoch==track_epoch[heard.track%track_count];
        auto_probe_track=-1;
        if(!current)continue;
        auto& sheet = ui.auto_sheet;
        if (!sheet.checking || sheet.check_wanted || sheet.track != heard.track) continue; // called off, an earlier one's, or another's
        sheet.checking = false;
        if (heard.error.empty()) sheet.settings.found = heard.found;
        else if (heard.error != "stopped") keep_error(sheet, heard.error, heard.key); // on the sheet, which is in view
    }
    // The sheet's check, once the track's plug-in is open and the print engine is free. A plug-in such as Surge XT
    // reads thousands of files from the card as it opens: the print host's own copy opened beside the live one would
    // read them all again, slowly, at the same time; after it, they are in memory and it opens in seconds. A check
    // called off still has the print engine until its plug-in has opened: the next waits for that too.
    if (auto& sheet = ui.auto_sheet; sheet.check_wanted) {
        const unsigned t = sheet.track % track_count;
        if (ui.page != PageId::AutoSample || !sheet.checking) sheet.check_wanted = false; // the sheet was left
        else {
            const PluginStatus status = plugin_status(*this, t);
            if (status == PluginStatus::Failed || status == PluginStatus::Crashed) {
                sheet.check_wanted = sheet.checking = false;
                notice("Check: the plug-in is not open");
            } else if (status == PluginStatus::Ready && !printer->busy()) {
                sheet.check_wanted = false;
                sheet.error[0] = 0; sheet.error_key = -1;
                sheet.checking = autosample_check(t, sheet.settings);
            }
        }
    }
    AutoSampleResult r;
    while (printer->take_sampled(r)) {
        const bool current=autosample_track==int(r.track)&&autosample_epoch==track_epoch[r.track%track_count];
        autosample_track = -1;
        autosample_outside = false;
        auto& sheet = ui.auto_sheet;
        const bool shown = sheet.sampling && sheet.track == r.track;
        if (shown) sheet.sampling = false;
        if (!r.error.empty()) {
            if (r.error == "stopped") notice("Auto sample called off");
            else if (shown) keep_error(sheet, r.error, r.key); // the whole run for nothing: kept on the sheet, not a moment's notice
            else notice("Auto sample: " + r.error.substr(0, r.error.find(':')));
            continue; // the sheet stays, to try again
        }
        if (!current || r.project_path != project_path) { notice("Saved Keygroups/" + r.name); continue; } // another project's
        const bool watching = shown && ui.page == PageId::AutoSample;
        if (watching) back();
        const int before = first_empty_track();
        autosample_landed(r);
        if (watching && before >= 0 && project.tracks[unsigned(before)].engine == EngineKind::Keygroup) select_track(unsigned(before)); // the new track in hand

    }
}

int Context::first_empty_track() const {
    for (unsigned k = 0; k < track_count; ++k) if (!project.tracks[k].used()) return int(k);
    return -1;
}

// The program on the first empty track, a KEYGROUP track with the plug-in track's patterns, mix and name; one undo.
void Context::autosample_landed(const AutoSampleResult& r) {
    const unsigned from = r.track % track_count;
    const int target = first_empty_track();
    const std::string short_name = r.name; // Keygroups/<name>/
    if (target < 0) { notice("Saved Keygroups/" + short_name + ", no empty track"); return; }
    const unsigned t = unsigned(target);
    const Track source = project.tracks[from];
    Edit edit = begin_edit("Auto sample", t, -1, true);
    edit.motion_before.reset();
    edit.track_before = std::make_unique<TrackState>(track_state(*this, t));
    load_sound(t, EngineKind::Keygroup, 0);
    int slot = sample_slot(r.program, r.name.substr(0, sizeof(SampleRef::name) - 1));
    if (slot < 0) { restore_track(*this, t, *edit.track_before); notice("Samples full"); return; }
    TrackState after = track_state(*this, t);
    Track& made = after.track;
    made.set_name(r.name.c_str());
    made.mix = source.mix;
    made.mix.mute = false; made.mix.solo = false;
    made.pattern = source.pattern;
    made.speed = source.speed;
    made.chain = source.chain; made.chain_count = source.chain_count; made.chain_at = source.chain_at;
    made.midi_channel = 0; // the plug-in track keeps its MIDI channel
    // Pre FX: its chain, but for plug-in effects (one plays on one track only), the rest moved up. Post FX: the
    // samples hold the chain already, so none.
    made.fx.slots = {};
    made.fx.size = source.fx.size;
    unsigned kept = 0;
    if (!r.post_fx)
        for (const auto& fx : source.fx.slots)
            if (fx.used() && fx.kind != plugin_effect_kind && kept < made.fx.slots.size()) made.fx.slots[kept++] = fx;
    made.bend = source.bend; // the fader's pitch bend, as on the plug-in
    PadSound pad;
    pad.sample = uint16_t(slot);
    pad.play = PadPlay::Gate; pad.decay = 100; pad.sustain = 100; pad.mono = false;
    made.pads = {};
    made.pads[0] = pad;
    made.last_pad = 0; made.bank = 0; made.levels = false; made.levels_pad = 0;
    made.layout = PadLayout::Scale;
    made.last_note = 60;
    after.patterns = patterns.at[from]; // the plug-in track's notes are MIDI notes, as a keygroup's are
    restore_track(*this, t, after);
    // Its Song clips, the same on the keygroup track's lane.
    const unsigned clips = project.song.count;
    unsigned placed = 0;
    for (unsigned c = 0; c < clips; ++c) {
        Clip clip = project.song.clips[c];
        if (clip.track != from) continue;
        clip.track = uint8_t(t);
        project.song.place(clip);
        ++placed;
    }
    if (placed) { sync_song(); edit.song_after = std::make_unique<Song>(project.song); }
    else edit.song_before.reset();
    landing_pads.push_back({uint8_t(t), 0, uint16_t(slot)}); // its envelope and velocity once the program is read
    edit.track_after = std::make_unique<TrackState>(after);
    edits.push(std::move(edit));
    dirty = true;
    std::string said = "Track " + std::to_string(t + 1) + ": " + r.name;
    if (r.loops) said += ", " + std::to_string(r.loops) + " loops";
    notice(said);
}

// ---- the AUTO SAMPLE sheet

void Context::open_auto_sheet() {
    if(autosample_track>=0&&ui.auto_sheet.sampling){open(PageId::AutoSample);return;}
    const unsigned t = ui.track % track_count;
    const EngineKind engine = project.tracks[t].engine;
    if (engine != EngineKind::Plugin && engine != EngineKind::Midi) { notice("Auto sample: plug-in and MIDI tracks"); return; }
    auto& sheet = ui.auto_sheet;
    if (!(sheet.sampling && sheet.track == t)) { // one under way comes back as it is
        const uint8_t row = sheet.row;
        sheet = UiState::AutoSheet {};
        sheet.track = uint8_t(t);
        sheet.settings = auto_last;
        sheet.settings.found = {};
        sheet.row = row;
        // The check: what the plug-in is, heard from a few notes (shell/auto_sample.h AutoFindings). An instrument
        // outside is not played until SAMPLE asks for it: its check then runs first, in the same go.
        // A plug-in's check starts once its plug-in is open and the print engine is free (autosample_tick()).
        sheet.checking = sheet.check_wanted = engine == EngineKind::Plugin && printer && !deep(t);
        // A MIDI track with no INPUT yet takes the rear inputs, where an instrument is plugged in (its Audio page's
        // INPUT; K1 on the sheet's SOUND row picks another).
        if (engine == EngineKind::Midi && project.tracks[t].params[midi_track::AudioIn] == midi_track::AudioNone)
            set_param(t, midi_track::AudioIn, midi_track::AudioRear);
    }
    open(PageId::AutoSample);
}

void Context::auto_sheet_go() {
    auto& sheet = ui.auto_sheet;
    if (sheet.sampling || sheet.checking) return;
    const EngineKind engine = project.tracks[sheet.track % track_count].engine;
    if (engine != EngineKind::Plugin && engine != EngineKind::Midi) { notice("Auto sample: plug-in and MIDI tracks"); return; }
    if (first_empty_track() < 0) notice("No empty track: the program is only saved");
    sheet.error[0] = 0; sheet.error_key = -1;
    autosample(sheet.track, sheet.settings);
    sheet.sampling = autosample_track == int8_t(sheet.track);
}

void Context::auto_sheet_cancel() {
    auto& sheet = ui.auto_sheet;
    if (sheet.checking) { // the check called off: its findings, when they come, are dropped
        // Only its own check: one still waiting to start has none, and what the print engine is doing is another's.
        if (!sheet.check_wanted && printer && printer->busy() && printer->progress().checking) printer->cancel();
        sheet.checking = sheet.check_wanted = false;
        back();
        return;
    }
    if (!sheet.sampling) { back(); return; }
    if (printer && printer->busy() && printer->progress().autosample) printer->cancel(); // its files go with it
    // The sheet stays until the print engine has stopped (autosample_tick()); its result says so.
}

void Context::auto_sheet_row(int way) {
    auto& sheet = ui.auto_sheet;
    if (sheet.sampling || sheet.checking) return;
    sheet.row = uint8_t((sheet.row + 3 + (way < 0 ? 2 : 1)) % 3);
}

void Context::auto_sheet_set(Binding::Kind kind, int value) {
    auto& a = ui.auto_sheet.settings;
    if (ui.auto_sheet.sampling || ui.auto_sheet.checking) return;
    using Kind = Binding::Kind;
    switch (kind) {
    case Kind::AutoLow: a.low = uint8_t(std::clamp(value, 0, 127)); if (a.high < a.low) a.high = a.low; break; // HIGH moves along
    case Kind::AutoHigh: a.high = uint8_t(std::clamp(value, 0, 127)); if (a.low > a.high) a.low = a.high; break;
    case Kind::AutoStep: a.step = uint8_t(std::clamp(value, 1, 24)); break;
    case Kind::AutoLayers: a.layers = uint8_t(std::clamp(value, 0, 8)); break; // 0: Auto
    case Kind::AutoHold: a.hold_auto = value <= 0; if (value > 0) a.hold = std::clamp(value, 1, 200) / 10.0f; break; // 0: Auto
    case Kind::AutoTail: a.tail = std::clamp(value, 0, 200) / 10.0f; break;
    case Kind::AutoTap: a.post_fx = value != 0; break;
    case Kind::AutoLoop: a.loop_auto = value == 0; a.loop = value == 1; break; // Auto, On, Off
    case Kind::AutoLevel: a.normalize = value != 0; break;
    default: break;
    }
}

} // namespace rhythmer
