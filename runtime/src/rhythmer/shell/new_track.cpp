// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/new_track.h"
#include "rhythmer/engine/midi_engine.h"
#include "rhythmer/shell/context.h"
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/shell/stage_map.h"
#include "rhythmer/shell/track_copy.h"
#include "rhythmer/plugin/plugin_rack.h"
#include <algorithm>
#include <cstring>
#include <memory>

namespace rhythmer {

bool new_track_locked(const Context& c) {
    const auto& t = c.project.tracks[c.ui.track % track_count];
    return !t.used() && (c.ui.page == PageId::Stage || (c.ui.page == PageId::Plugins && c.ui.plugin_target == UiState::PluginTarget::Instrument));
}

bool new_track_lock(Context& c, SurfaceButton button, bool pressed) {
    using B = SurfaceButton;
    auto& ui = c.ui;
    uint32_t bit = 1u << unsigned(button);
    if (!pressed) { // a release goes where its press went
        if (!(ui.new_taken & bit)) return false;
        ui.new_taken &= ~bit;
        return true;
    }
    if (!new_track_locked(c) || ui.menu) return false;
    if (ui.ask != UiState::Ask::None) return false; // a question on screen (Erase on the picker): its keys answer it
    if (ui.menu && (button == B::TopMiddle || button == B::TopRight)) return false; // the MENU's HERE and GO: its places with every track empty
    bool carousel = ui.page == PageId::Stage;
    switch (button) {
    case B::Play: case B::Stop: case B::TapTempo: case B::TopLeft: return false;
    case B::Erase: if (!carousel) return false; break;     // the picker: the plug-in under the cursor deleted (shell/delete_jobs.h)
    case B::WheelPush: return false; // Push makes it (Shift + Push, Back, waits)
    case B::PadBank: return false; // Rhythmer follows the newly selected track, including from the empty plug-in picker
    case B::Minus: case B::Plus: return false;
    case B::TopMiddle: case B::TopRight: if(carousel)return false; break;
    case B::PadFx: case B::KnobFx: if(!ui.shift)return false; break;    // + pad: another track
    default: break;
    }
    ui.new_taken |= bit;
    return true;
}

int nearest_used_track(const Context& c) {
    int at = int(c.ui.track % track_count);
    for (int d = 1; d < int(track_count); ++d)
        for (int t : {at - d, at + d})
            if (t >= 0 && t < int(track_count) && c.project.tracks[size_t(t)].used()) return t;
    return -1;
}

void cancel_new_track(Context& c) {
    auto& ui = c.ui;
    int to = nearest_used_track(c);
    const PageId back=ui.new_back; // select_track deliberately clears old track ownership
    bool picked = ui.new_for == int8_t(ui.track % track_count);
    if (to < 0) { ui.new_for=-1;ui.new_sources=false;c.open(back==PageId::Stage?PageId::Tracks:back);return; }
    c.select_track(unsigned(to));
    ui.new_for = -1;
    if (picked && back != PageId::Stage) c.open(back);
}

void offer_new_track(Context& c) {
    auto& ui = c.ui;
    if (c.track().used()) { // another track picked on the carousel: back where the empty one was picked
        if (ui.new_for >= 0 && ui.page == PageId::Stage) { ui.new_for = -1; c.open(ui.new_back); }
        return;
    }
    if (ui.recording) return;
    bool doors = ui.new_for >= 0 && ui.page == PageId::Stage; // another empty track picked on the doors: the same page to go back to
    if (!doors) ui.new_back = ui.page; // native v0.8: Mixer/Song callers return to their actual page.
    ui.new_for = int8_t(ui.track % track_count);
    c.open(PageId::Stage);
    if (!doors) { // the types, the last one made in the middle; the cards slide in
        if (ui.new_sources) { ui.new_sources = false; ui.new_pick = uint8_t(ui.new_type % new_type_count); }
        ui.new_pick_from = float(ui.new_pick % new_type_count) + 0.9f; ui.new_pick_at = c.now ? c.now : 1;
    }
}

void new_track_push(Context& c) {
    auto& ui = c.ui;
    if (ui.new_sources) { make_new_track(c, new_pick_choice(ui)); return; }
    ui.new_type = uint8_t(ui.new_pick % new_type_count);
    auto list = type_sources(NewType(ui.new_type));
    if (list.count == 1) { make_new_track(c, list.items[0]); return; } // Keygroup, Plugin: nothing to pick
    ui.new_sources = true;
    ui.new_pick = list.first;
    ui.new_pick_from = float(list.first) + 0.9f; ui.new_pick_at = c.now ? c.now : 1; // the sources slide in
}

void new_track_types(Context& c) {
    auto& ui = c.ui;
    ui.new_sources = false;
    ui.new_pick = uint8_t(ui.new_type % new_type_count);
    ui.new_pick_from = float(ui.new_pick) - 0.9f; ui.new_pick_at = c.now ? c.now : 1; // the types slide back in
}

void make_new_track(Context& c, NewChoice choice) {
    auto& ui = c.ui;
    unsigned t = ui.track % track_count;
    if (c.track().used()) return;
    bool picked = ui.new_for == int8_t(t);
    PageId back = ui.new_back;
    if (choice == NewChoice::Plugin) { // the card's instruments: the track turns PLUGIN once one is picked (choose_plugin)
        c.open_plugin_picker(UiState::PluginTarget::Instrument);
        ui.plugin_back = PageId::Stage;
        return;
    }
    // One undo for the track made (Shift + Minus leaves it empty again), as Load Kit.
    Edit edit = c.begin_edit("New track", t, -1, false);
    edit.motion_before.reset();
    edit.track_before = std::make_unique<TrackState>(track_state(c, t));
    auto kit = [&] {
        c.load_sound(t, EngineKind::Sampler, 0);
        c.track().set_name("KIT");
        stage_go(ui, 0, 0); // TRIM
    };
    auto keygroup = [&] { // one sound across the keys: its instrument (or WAV) from Samples
        c.load_sound(t, EngineKind::Keygroup, 0);
        c.track().set_name("KEYS");
        c.track().last_pad = 0;
        stage_go(ui, 0, 0); // TRIM (ZONES once an instrument is on it)
    };
    auto midi = [&](unsigned port) {
        c.load_sound(t, EngineKind::Midi, 0);
        c.set_param(t, midi_track::Port, int(port));
        c.set_param(t, midi_track::Channel, std::clamp<int>(ui.new_channel, 1, 16));
        stage_go(ui, 0, 0); // DEVICE
    };
    auto audio = [&] { // freeze plan F3: records into its patterns; Stage on INPUT, what it records from
        c.load_sound(t, EngineKind::Audio, 0);
        c.track().set_name("AUDIO");
        stage_go(ui, 1, 0);
    };
    switch (choice) {
    case NewChoice::Audio: audio(); break;
    case NewChoice::Kit: kit(); break;
    case NewChoice::Samples: kit(); break;
    case NewChoice::Keygroup: keygroup(); break;
    case NewChoice::Record: kit(); break;
    case NewChoice::MidiOut: midi(0); break;
    case NewChoice::MidiUsb: midi(1); break;
    case NewChoice::MidiAllUsb: midi(2); break;
    default: return;
    }
    edit.track_after = std::make_unique<TrackState>(track_state(c, t));
    c.edits.push(std::move(edit));
    c.dirty = true;
    if (choice == NewChoice::Samples || choice == NewChoice::Keygroup) { // Sample Select: the first sample loaded goes back (new_track_landed)
        c.open(PageId::Samples);
        ui.new_kit = int8_t(t); // CANCEL there leaves it empty
        if (c.library) c.library->rescan();
        return;
    }
    if (choice == NewChoice::Record) { ui.new_for = -1; c.open(PageId::Record); return; } // Sample Record: a take of its own
    if (choice == NewChoice::Audio) { ui.new_for = -1; c.open(PageId::Stage); return; }   // its INPUT
    if (picked) { ui.new_for = -1; c.open(back); }
}

void new_track_landed(Context& c) {
    auto& ui = c.ui;
    if (ui.new_for != int8_t(ui.track % track_count) || ui.page != PageId::Samples) return;
    ui.new_for = -1;
    c.open(ui.new_back);
}

void samples_cancel(Context& c) {
    auto& ui = c.ui;
    unsigned t = ui.track % track_count;
    const auto& tr = c.project.tracks[t];
    bool bare = tr.sampled() && std::none_of(tr.pads.begin(), tr.pads.end(), [](const auto& p) { return p.used(); });
    const Edit* last = c.edits.latest();
    if (ui.new_kit == int8_t(t) && bare && last && last->track == t && !std::strcmp(last->what.c_str(), "New track"))
        c.undo(); // the kit made for its first sample: empty again
    ui.new_kit = -1;
    c.back();
}

void open_track_actions(Context& c) {
    c.ui.track_action = 0;
    c.ui.track_types = false;
    c.open(PageId::TrackActions);
}

void change_track_type(Context& c, EngineKind kind) {
    if (c.ui.recording || c.host_recording()) { c.notice("Finish recording first"); return; }
    if (c.frozen_edit(c.ui.track)) return;
    if (kind == EngineKind::Plugin) { c.open_plugin_picker(UiState::PluginTarget::Instrument); return; }
    if (kind == c.track().engine) {
        if(kind==EngineKind::None) offer_new_track(c);
        else c.open(PageId::Stage);
        return;
    }
    c.close_gesture();
    unsigned t = c.ui.track;
    Edit e = c.begin_edit("Change track type", t, -1, false);
    e.motion_before.reset();
    e.track_before = std::make_unique<TrackState>(track_state(c,t));
    if(c.plugins && c.track().engine==EngineKind::Plugin && c.track().plugin<max_plugins){
        unsigned entry=c.track().plugin;auto states=c.plugins->states();
        e.gen_plugins_before.push_back({entry,c.project.plugins[entry],states[entry]});
        e.gen_plugins_after=e.gen_plugins_before;
    }
    auto next = std::make_unique<TrackState>(*e.track_before);
    Track fresh;
    fresh.mix = next->track.mix;
    fresh.fx = next->track.fx;
    fresh.pattern = next->track.pattern;
    fresh.speed = next->track.speed;
    rhythmer::load_sound(fresh,kind,0);
    if (kind == EngineKind::Sampler) { fresh.layout = PadLayout::Drums; fresh.set_name("KIT"); }
    next->track = fresh;
    if(c.kit_target==int(t))c.kit_target=-2;
    if(c.bank_target==int(t))c.bank_target=-2;
    if (c.ui.break_pending >= 0 && unsigned(c.ui.break_job_track>=0?c.ui.break_job_track:c.ui.break_track) == t) c.ui.break_pending = -1;
    c.send({Command::Type::AllNotesOff,uint8_t(t)});
    restore_track(c,t,*next);
    e.track_after = std::move(next);
    c.edits.push(std::move(e));
    c.ui.new_for = -1;
    c.ui.new_back = PageId::Stage;
    c.ui.back_depth = 0;
    stage_go(c.ui,kind==EngineKind::Audio?1:0,0);
    if(kind==EngineKind::None) {
        c.ui.new_sources=false; c.ui.new_pick=uint8_t(NewType::Plugin);
        offer_new_track(c);
    } else c.open(PageId::Stage);
    c.ui.back_depth=0;
    if (c.plugins) c.plugins->sync(c.project,c.plugins_asleep());
    c.notice("Type changed; patterns kept");
}

} // namespace rhythmer
