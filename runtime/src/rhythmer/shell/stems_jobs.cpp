// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Stems for a kit pad (shell/stems.h): Shift + B3 on the pad's TRIM, or the
// MENU's STEMS, opens the STEMS sheet for the pad in hand; B3 SEPARATE there
// starts the job in the sample library (shell/sample_library_stems.cpp) and
// B1 calls it off. The job goes on with the sheet closed. Its four files,
// Samples/Stems/<name> DRUMS.wav, BASS, MUSIC and VOCALS, are loaded as any
// file is and land together on the next four empty pads after the pad they
// came from, as one undo. Each stem is as long as the sample to the frame, so
// a stem's pad plays as the pad did: its start and end, loop, tune, warp,
// envelope and filter, and the sample's slices in Chop. It leaves the pad's
// layers, mute group and Pad Link behind, so the stems can play together.
#include "rhythmer/shell/context.h"
#include "rhythmer/shell/project_store.h"
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/shell/stems_room.h"
#include <cstdio>
#include <filesystem>

namespace rhythmer {
namespace {
constexpr uint8_t stems_land = 3; // pending_files' land for DRUMS; BASS, MUSIC and VOCALS follow

// The sample's name with room after it for the longest stem's word, as the files' and the slots' name.
std::string stems_base(const SampleRef& ref) {
    std::string base = ref.name[0] ? ref.name : "SAMPLE";
    base = base.substr(0, sizeof(ref.name) - 1 - 7); // " VOCALS"
    while (!base.empty() && base.back() == ' ') base.pop_back();
    return base.empty() ? std::string("SAMPLE") : base;
}
} // namespace

void Context::open_stems_sheet() {
    auto& sheet = ui.stems_sheet;
    if (sheet.running) { // the job under way: its sheet comes back as it is
        if (ui.page != PageId::Stems) open(PageId::Stems);
        return;
    }
    const auto& t = track();
    const unsigned kit = t.last_pad % sampler_pads;
    const auto& sound = t.pads[kit];
    if (t.engine != EngineKind::Sampler || !sound.used()) { notice("Stems: no sample"); return; }
    if (refuse_instrument("Stems")) return;
    if (!library) return;
    const auto& ref = project.samples[sound.sample % sample_slots];
    if (!ref.frames) { notice("Stems: still loading"); return; }
    if (ref.frames < stems_least_frames) { notice("Stems: under a second"); return; }
    sheet = UiState::StemsSheet {};
    sheet.track = ui.track;
    sheet.pad = uint8_t(kit);
    sheet.sample = sound.sample;
    sheet.frames = ref.frames;
    if (ui.page != PageId::Stems) open(PageId::Stems);
}

void Context::stems_sheet_go() {
    auto& sheet = ui.stems_sheet;
    if (sheet.running || !library) return;
    for(const auto& p:pending_files)if(p.slot>=0&&p.land>=stems_land&&p.land<stems_land+stem_count){notice("Loading previous stems");return;}
    const auto& t = project.tracks[sheet.track % track_count];
    const auto& sound = t.pads[sheet.pad % sampler_pads];
    if (!sound.used() || sound.sample != sheet.sample) { notice("Stems: the pad changed"); back(); return; }
    if (!stems_room(project, sheet.track, sheet.pad).enough()) { notice("Stems: 4 empty pads needed"); return; }
    SampleLibrary::StemsRequest request;
    request.source = uint16_t(sheet.sample % sample_slots);
    std::snprintf(request.name, sizeof(request.name), "%s", stems_base(project.samples[sheet.sample % sample_slots]).c_str());
    request.best = ui.settings.stems_best;
    if (!library->separate(request)) { notice("Stems: busy"); return; }
    stems_from = {sheet.track, sheet.pad, sound,track_epoch[sheet.track%track_count]};
    sheet.running = true;
}

void Context::stems_sheet_best() {
    if (ui.stems_sheet.running) return; // the job under way keeps what it started with
    ui.settings.stems_best = !ui.settings.stems_best;
    if (store) store->save_settings(ui.settings.text());
    notice(ui.settings.stems_best ? "Stems: best, up to 16 kHz" : "Stems: fast, up to 11 kHz");
}

void Context::stems_sheet_cancel() {
    if (!ui.stems_sheet.running) { back(); return; }
    if (library) library->stop_stems(); // what comes of it ("stopped") ends the sheet's run
}

// Auto's CLEAN (B2 on Chop's Auto type): the chopped sample's stems separated once (BEST off: about 15 s for 13 s
// of sound), its DRUMS stem read into a slot of its own, and Auto's cuts found on that (Context::type_chop): hits
// under chords and voices show there. The pads still play the sample; KIT lays the stem. Off, or another type: the
// cuts from the sample again. Each turn is one undo step. The four files stay in Samples/Stems, as STEMS leaves them.
unsigned Context::chop_cut_slot() const {
    const unsigned stem = unsigned(ui.chop_clean_slot);
    if (ui.chop_mode == ChopMode::Auto && ui.chop_clean && ui.chop_clean_slot >= 0 && stem < sample_slots && ui.chop_clean_for == ui.chop_sample
        && ui.chop_sample < sample_slots && project.samples[stem].frames && project.samples[stem].frames == project.samples[ui.chop_sample].frames)
        return stem;
    return ui.chop_sample;
}

void Context::chop_clean_key() {
    if (ui.chop_mode != ChopMode::Auto || !library || ui.chop_sample >= sample_slots || ui.chop_cleaning) return; // B1 calls a job off
    auto recut = [&](bool on) {
        auto edit = scope_edit(UndoScope {UndoScope::Kind::Chop, uint8_t(ui.track % track_count), 0}, on ? "Clean" : "Clean off");
        ui.chop_clean = on;
        rechop();
        if (edit && finish_scope(*edit)) { edits.push(std::move(*edit)); dirty = true; }
        notice(on ? "Clean: cuts from the drums stem" : "Clean off: cuts from the sample");
    };
    if (chop_cut_slot() != ui.chop_sample) { recut(false); return; }
    const auto& ref = project.samples[ui.chop_sample];
    const unsigned stem = unsigned(ui.chop_clean_slot);
    if (ui.chop_clean_slot >= 0 && stem < sample_slots && ui.chop_clean_for == ui.chop_sample && ref.frames && project.samples[stem].frames == ref.frames) {
        recut(true); // its stem is in already
        return;
    }
    if (ui.stems_sheet.running) { notice("Stems: busy"); return; }
    if (ref.frames < stems_least_frames) { notice("Clean: under a second"); return; }
    SampleLibrary::StemsRequest request;
    request.source = uint16_t(ui.chop_sample);
    std::snprintf(request.name, sizeof(request.name), "%s", stems_base(ref).c_str());
    request.best = false;
    if (!library->separate(request)) { notice("Stems: busy"); return; }
    ui.chop_cleaning = true;
    ui.chop_clean_for = uint16_t(ui.chop_sample);
    ui.chop_clean_slot = -1;
}

void Context::stems_tick() {
    if (!library) return;
    StemsResult made;
    while (library->take_stems(made)) {
        auto& sheet = ui.stems_sheet;
        if (ui.chop_cleaning && !sheet.running) { // CLEAN's: its DRUMS stem into a slot
            if (!made.error.empty()) { ui.chop_cleaning = false; notice(made.error == "stopped" ? std::string("Clean called off") : "Clean: " + made.error); continue; }
            const int slot = sample_slot(made.paths[0], std::filesystem::path(made.paths[0]).stem().string());
            if (slot < 0) { ui.chop_cleaning = false; notice("Clean: samples full"); continue; }
            ui.chop_clean_slot = int16_t(slot);
            continue;
        }
        const bool ours = sheet.running;
        sheet.running = false;
        if (!made.error.empty()) { // the sheet stays, its keys back
            notice(made.error == "stopped" ? std::string("Stems called off") : "Stems: " + made.error);
            continue;
        }
        if (!ours) continue; // asked for in a project since closed: the files stay on the card
        if(track_epoch[stems_from.track%track_count]!=stems_from.epoch){notice("Stems saved; source changed");continue;}
        for (unsigned n = 0; n < stem_count; ++n) {
            unsigned held = 0;
            while (held < pending_files.size() && pending_files[held].slot >= 0) ++held;
            const int slot = held < pending_files.size() ? sample_slot(made.paths[n], std::filesystem::path(made.paths[n]).stem().string()) : -1;
            if (slot < 0) { notice(held < pending_files.size() ? "Stems: samples full" : "Stems: busy"); break; }
            pending_files[held] = {int16_t(slot), stems_from.track, int16_t(stems_from.pad), false, stems_from.sound, uint8_t(stems_land + n),stems_from.epoch};
        }
        if (ui.page == PageId::Stems) back();
    }
    // CLEAN's DRUMS stem read in: on, where Chop is still on Auto with that sample.
    if (ui.chop_cleaning && ui.chop_clean_slot >= 0) {
        const auto loaded = library->slot(unsigned(ui.chop_clean_slot));
        if (!loaded.error.empty()) { ui.chop_cleaning = false; ui.chop_clean_slot = -1; notice("Clean: stem not read"); }
        else if (loaded.ready && (!loaded.streamed || loaded.shaped)) {
            auto& ref = project.samples.edit(size_t(ui.chop_clean_slot));
            ref.frames = loaded.frames;
            ref.root = loaded.root;
            ui.chop_cleaning = false;
            if (ui.page == PageId::Chop && ui.chop_mode == ChopMode::Auto && ui.chop_clean_for == ui.chop_sample) chop_clean_key();
        }
    }
}

// The stems' files as they come in: all four together, once each is read or could not be.
void Context::stems_landed() {
    if (!library) return;
    std::array<PendingFile*, stem_count> held {};
    bool any = false;
    for (auto& p : pending_files)
        if (p.slot >= 0 && p.land >= stems_land && p.land < stems_land + stem_count) { held[p.land - stems_land] = &p; any = true; }
    if (!any) return;
    for (const auto* p : held) {
        if (!p) continue;
        const auto loaded = library->slot(unsigned(p->slot));
        if (!loaded.ready && loaded.error.empty()) return; // still being read
    }
    Edit edit;
    for(const auto* p:held)if(p){unsigned t=p->track%track_count;if(track_epoch[t]!=p->epoch||project.tracks[t].engine!=EngineKind::Sampler||project.tracks[t].pads[unsigned(p->pad)%sampler_pads].sample!=p->sound.sample||frozen(t)){for(auto* old:held)if(old)*old=PendingFile{};notice("Stems saved; source changed");return;}}
    unsigned landed = 0, first = 0, last = 0, after = 0;
    bool placed_any = false;
    for (unsigned n = 0; n < stem_count; ++n) {
        if (!held[n]) continue;
        const PendingFile was = *held[n];
        *held[n] = PendingFile {};
        const unsigned t = was.track % track_count;
        auto& track = project.tracks[t];
        auto& ref = project.samples.edit(size_t(was.slot));
        const auto loaded = library->slot(unsigned(was.slot));
        if (!loaded.ready) { ref = SampleRef {}; continue; } // its file could not be read
        std::snprintf(ref.path, sizeof(ref.path), "%s", loaded.path.c_str());
        std::snprintf(ref.name, sizeof(ref.name), "%s", loaded.name.c_str());
        ref.frames = loaded.frames;
        ref.root = loaded.root;
        if (!placed_any) after = unsigned(was.pad) % sampler_pads;
        int kit = -1;
        for (unsigned k = 1; k < sampler_pads && kit < 0; ++k)
            if (!track.pads[(after + k) % sampler_pads].used()) kit = int((after + k) % sampler_pads);
        if (kit < 0) { ref = SampleRef {}; notice("Stems: no empty pad"); continue; }
        edit.track = uint8_t(t);
        edit.pads_before.emplace_back(uint8_t(kit), pad_state(track, unsigned(kit)));
        PadState now = edit.pads_before.back().second;
        now.sound = was.sound; // the pad it came from, as it was at SEPARATE: the stem is as long, to the frame
        now.sound.sample = uint16_t(was.slot);
        now.sound.layers = {};
        now.sound.low_velocity = 1;
        now.sound.high_velocity = 127;
        now.sound.layer_play = LayerPlay::Velocity;
        now.sound.group = 0; // the stems play together
        now.sound.link = 0;
        if (was.sound.sample < sample_slots) project.chops.edit(size_t(was.slot)) = project.chops[was.sound.sample]; // its slices, in Chop
        restore_pad(t, unsigned(kit), now);
        edit.pads_after.emplace_back(uint8_t(kit), now);
        if (!placed_any) first = unsigned(kit);
        last = after = unsigned(kit);
        placed_any = true;
        ++landed;
    }
    if (!landed) { notice("Stems: files not read"); return; }
    char from[4], to[4];
    pad_label(first, from);
    pad_label(last, to);
    edit.what = landed > 1 ? std::string("Stems on ") + from + "-" + to : std::string("Stem on ") + from;
    notice(edit.what);
    edits.push(std::move(edit));
    dirty = true;
}

} // namespace rhythmer
