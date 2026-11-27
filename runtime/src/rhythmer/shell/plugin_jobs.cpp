// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Plug-ins on the MIDI thread (docs/RHYTHMER.md, Plug-ins): the PLUG-INS picker
// opened for a PLUGIN track's instrument or for a chain's slot, Push on a
// plug-in there, PRESETS (its programs) and a pick there, the cards of the
// plug-in in view, and the rack kept in step with the project
// (plugin/plugin_rack.h does the loading on its own thread).
//
// A plug-in is an entry of Project::plugins: its path on the card. A PLUGIN
// track names its entry; a Plugin effect names it in its first parameter. A
// track or slot that already has one keeps its entry for the new plug-in; a
// new one takes an entry nothing uses. The rack loads what is used and lets go
// of the rest, so undo, a project switch or a removed effect need nothing more.
#include "rhythmer/shell/mixer_channels.h"
#include "rhythmer/shell/context.h"
#include "rhythmer/shell/ask.h"
#include "rhythmer/shell/delete_jobs.h"
#include "rhythmer/engine/effect_info.h"
#include "rhythmer/engine/engine_host.h"
#include "rhythmer/model/scales.h"
#include "rhythmer/plugin/plugin_files.h"
#include "rhythmer/plugin/plugin_rack.h"
#include "rhythmer/plugin/preset_rows.h"
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/shell/screen_view.h"
#include "rhythmer/shell/stage_map.h"
#include "rhythmer/shell/track_copy.h"
#include "rhythmer/ui/tokens.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace rhythmer {
using Type = Command::Type;

namespace {
// An entry for a new plug-in: one never set first, else one nothing uses now.
int free_entry(const Project& project) {
    for (unsigned e = 0; e < max_plugins; ++e) if (!project.plugins[e].used()) return int(e);
    for (unsigned e = 0; e < max_plugins; ++e) if (!plugin_used_by(project, e)) return int(e);
    return -1;
}

} // namespace

// One instance plays one place: a track copy (or a copied chain) leaves two users on an entry, and the second and
// after each get an entry of their own with the same plug-in and its state as it is now.
void Context::split_shared_plugins() {
    std::array<bool, max_plugins> seen {};
    auto claim = [&](unsigned entry) -> int { // -1: keep it; else the entry to move to
        if (entry >= max_plugins) return -1;
        if (!seen[entry]) { seen[entry] = true; return -1; }
        int copy = free_entry(project);
        if (copy < 0 || seen[size_t(copy)]) return -1; // none free: it stays shared (and is heard twice)
        project.plugins[size_t(copy)] = project.plugins[entry];
        seen[size_t(copy)] = true;
        if (plugins) plugins->clone(entry, unsigned(copy));
        return copy;
    };
    for (unsigned t = 0; t < track_count; ++t) {
        auto& tr = project.tracks[t];
        if (tr.engine != EngineKind::Plugin) continue;
        int copy = claim(tr.plugin);
        if (copy < 0) continue;
        tr.plugin = uint8_t(copy);
        send({Type::TrackPlugin, uint8_t(t), 0, 0, int16_t(copy)});
    }
    for (unsigned n = 0; n < mix_chain_count; ++n) // every chain but the kit pads' (no plug-ins there)
        for (unsigned k = 0; k < project.chain(n).size; ++k) {
            const auto& s = project.chain(n).slots[k];
            if (s.kind != plugin_effect_kind) continue;
            int copy = claim(unsigned(s.params[0]));
            if (copy >= 0) set_effect_param(n, k, 0, copy);
        }
}

void Context::open_plugin_picker(UiState::PluginTarget target, unsigned chain, unsigned slot) {
    if (!plugins) { notice("Plug-ins are not available"); return; }
    ui.plugin_target = target;
    ui.plugin_chain = uint8_t(chain);
    ui.plugin_slot = uint8_t(slot);
    ui.plugin_back = ui.page;
    plugins->browse(plugins->view().folder); // where the picker was left, listed afresh, on the row it was on
    if (ui.page != PageId::Plugins) ui.plugin_from = ui.page;
    open(PageId::Plugins);
}

void Context::choose_plugin(const PluginFile& file) {
    if (!plugins) return;
    if(!file.folder && !file.several() && ui.plugin_target==UiState::PluginTarget::Instrument) {
        if(ui.recording || host_recording()){notice("Finish recording first");return;}
        if(frozen_edit(ui.track))return;
    }
    if(file.name=="Break Engine"){
        if(ui.plugin_target!=UiState::PluginTarget::Instrument){notice("Break Engine is an instrument");return;}
        close_gesture();ui.break_from_picker=true;ui.break_load_b=false;
        ui.break_browse=true;ui.break_track=ui.track;ui.break_bars=track().break_bars?track().break_bars:2;
        open(PageId::Samples);notice("BREAK: choose a loop; Back cancels");return;
    }
    if (file.folder || file.several()) { plugins->browse(file.path); ui.plugins_cursor = 0; return; } // a folder, or a bundle's plug-ins
    if (!file.loadable()) { notice(file.why.empty() ? "Not a plug-in for this MPC" : file.why); return; }
    bool instrument = ui.plugin_target == UiState::PluginTarget::Instrument;
    if (instrument && file.instrument == 0) { notice(file.name + " is an effect"); return; }
    if (!instrument && file.instrument == 1) { notice(file.name + " is an instrument"); return; }
    if (file.path.size() + 1 > plugin_path_length) { notice("Its path is too long"); return; }

    int entry = -1;
    unsigned n = ui.plugin_chain, k = ui.plugin_slot;
    if (instrument) {
        const auto& t = track();
        if (t.engine == EngineKind::Plugin && t.plugin < max_plugins) entry = t.plugin;
    } else {
        if (n >= mix_chain_count || k >= project.chain(n).size) return;
        const auto& s = project.chain(n).slots[k];
        if (s.kind == plugin_effect_kind && unsigned(s.params[0]) < max_plugins) entry = s.params[0];
    }
    bool replace_asked = ui.plugin_replace;
    ui.plugin_replace = false;
    if (entry >= 0 && project.plugins[size_t(entry)].used() && !replace_asked
        && !(file.path == project.plugins[size_t(entry)].path && file.id == project.plugins[size_t(entry)].id)) {
        // Another plug-in over one in use: its sound and settings go (undo brings them back); ask first
        std::snprintf(ui.ask_name.data(), ui.ask_name.size(), "%s", project.plugins[size_t(entry)].name);
        ask(*this, UiState::Ask::ReplacePlugin, int(ui.plugins_cursor));
        return;
    }
    if (entry < 0) entry = free_entry(project);
    if (entry < 0) { notice("Every plug-in entry is in use"); return; }

    close_gesture();
    // An empty track turned PLUGIN: one undo step, as the carousel's KIT and MIDI ("New track"; shell/new_track.h).
    std::unique_ptr<Edit> made;
    if (instrument) {
        made = std::make_unique<Edit>(begin_edit(track().used()?"Change instrument":"New track", ui.track, -1, false));
        made->motion_before.reset();
        made->track_before = std::make_unique<TrackState>(track_state(*this, ui.track));
        auto states=plugins->states();
        made->gen_plugins_before.push_back({unsigned(entry),project.plugins[size_t(entry)],states[size_t(entry)]});
        if(kit_target==ui.track)kit_target=-2;
        if(bank_target==ui.track)bank_target=-2;
        if(ui.break_pending>=0 && unsigned(ui.break_job_track>=0?ui.break_job_track:ui.break_track)==ui.track)ui.break_pending=-1;
    }
    auto& ref = project.plugins[size_t(entry)];
    bool same = file.path == ref.path && file.id == ref.id && (!instrument || track().engine==EngineKind::Plugin);
    // Another plug-in over one in use: one undo step, the old plug-in back with its state (swap_plugin); on a
    // track, its name and pads too.
    if (!made && !same && ref.used()) {
        made = std::make_unique<Edit>(begin_edit((std::string("Replace ") + ref.name).c_str(), ui.track, -1, false));
        made->motion_before.reset();
        made->plugin = std::make_unique<PluginSwap>();
        made->plugin->entry = uint8_t(entry);
        made->plugin->before = ref;
        made->plugin->state_before = plugins->state(unsigned(entry));
        if (instrument) made->track_before = std::make_unique<TrackState>(track_state(*this, ui.track));
    }
    std::snprintf(ref.path, plugin_path_length, "%s", file.path.c_str());
    std::snprintf(ref.name, sizeof(ref.name), "%s", file.name.c_str());
    ref.id = file.id;
    if (!same) { ref.preset[0] = 0; plugins->set_gen_state(unsigned(entry),{}); }
    if (same) plugins->reload(unsigned(entry)); // picked again: opened afresh (after it failed or stopped working)
    if (instrument) {
        unsigned t = ui.track % track_count;
        auto& tr = project.tracks[t];
        if(tr.engine != EngineKind::Plugin) {
            Track fresh;fresh.mix=tr.mix;fresh.fx=tr.fx;fresh.pattern=tr.pattern;fresh.speed=tr.speed;
            tr=fresh;
        }
        tr.plugin = uint8_t(entry);tr.break_bars=0;
        send({Type::AllNotesOff,uint8_t(t)});
        send({Type::TrackPlugin, uint8_t(t), 0, 0, int16_t(entry)});
        if (tr.engine != EngineKind::Plugin) load_sound(t, EngineKind::Plugin, 0);
        if (!file.drums.empty()) use_drum_pads(tr); // a drum machine (8W8, 9W9...): its pads as a kit's, 36 up
        else if (tr.layout == PadLayout::Drums) tr.layout = PadLayout::Scale; // a drum machine's pads, gone with it
        if(file.single_drum){tr.octave=4;tr.last_note=uint8_t(std::clamp(pad_note(project,tr,0).key,0,127));}
        std::string name = file.name;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
        tr.name_from(name.c_str());
        if (made) {
            if (!made->plugin) stage_go(ui, 0, 0); // a new track: B1, its PRESETS (or its face), as a new kit's TRIM
            made->track_after = std::make_unique<TrackState>(track_state(*this, t));
            restore_track(*this,t,*made->track_after);
            made->gen_plugins_after.push_back({unsigned(entry),ref,same?made->gen_plugins_before.front().state:std::vector<uint8_t>{}});
            // Commit once below, after both the track and plug-in snapshots are complete.
        }
    } else {
        unsigned count = project.chain(n).count();
        if (k > count) k = count;
        if (project.chain(n).slots[k].kind != plugin_effect_kind) set_effect(n, k, EffectKind::Plugin);
        set_effect_param(n, k, 0, entry);
        ui.fx_cursor[UiState::cursor_of(n)] = uint8_t(k);
        ui.fx_row[UiState::cursor_of(n)] = 0;
    }
    if(instrument){ui.plugin_panel={};plugin_panel_at=0;ui.preset_due=0;}
    if (made) {
        if (made->plugin) made->plugin->after = ref;
        edits.push(std::move(*made));
    }
    ui.plugin_row[size_t(entry)] = 0;
    dirty = true;
    plugins->sync(project, plugins_asleep());
    notice("Loading " + file.name);
    if(instrument) {ui.new_for=-1;ui.new_back=PageId::Stage;open(PageId::Stage);ui.back_depth=0;}
    else { open(ui.plugin_back); picker_done(); }
}

void Context::swap_plugin(Edit& e, bool after) {
    if (!e.plugin) return;
    auto& swap = *e.plugin;
    const unsigned entry = swap.entry % max_plugins;
    if (plugins) (after ? swap.state_before : swap.state_after) = plugins->state(entry); // the side left, as heard now
    project.plugins[entry] = after ? swap.after : swap.before;
    ui.plugin_row[entry] = 0;
    dirty = true;
    if (!plugins) return;
    plugins->restore(entry, after ? swap.state_after : swap.state_before);
    plugins->sync(project, plugins_asleep());
}

void Context::refresh_plugin_panel(unsigned entry){
    if(!plugins||entry>=max_plugins)return;
    const bool editing=((ui.page==PageId::Stage&&stage_page_now(ui,track(),false).kind==StageKind::PluginEdit)||ui.page==PageId::Edit||(ui.page==PageId::PluginPresets&&ui.presets_back==PageId::Edit))&&track().engine==EngineKind::Plugin&&track().plugin==entry&&(ui.page==PageId::Stage||ui.edit_page[ui.track]);
    if(editing){
        auto row=std::max<unsigned>(2,ui.plugin_row[entry]);
        if(ui.page==PageId::Edit&&ui.edit_row[ui.track])row=ui.edit_row[ui.track]==1?0:1;
        plugins->panel(entry,row,ui.plugin_panel);ui.plugin_panel.design=false;
        ui.plugin_row[entry]=ui.plugin_panel.row;
        if(ui.plugin_panel.layout_revision&&row>=2)for(unsigned k=0;k<ui.plugin_panel.window.size();++k)if(ui.plugin_panel.window_first+k<6)ui.plugin_panel.window[k]={};
    }else {plugins->panel(entry,ui.plugin_row[entry],ui.plugin_panel);ui.plugin_row[entry]=ui.plugin_panel.row;}
    plugin_panel_at=0;
}
void Context::set_plugin_param(int32_t id, int value) {
    if (!plugins) return;
    unsigned entry = plugin_binding_entry(id), shown = plugin_binding_shown(id);
    auto& panel = ui.plugin_panel;
    if (plugin_binding_indexed(id)) { // a companion screen's own edit (plugin/companion_screen.h), by its index
        plugins->set_param_index(entry, plugins->live_generation(entry), int(shown - plugin_index_slot), value);
        dirty = true; // K1-K3's cards read it back at the panel's next tick
        return;
    }
    if (shown >= plugin_screen_slot) { // one of the three its own screen names
        unsigned k = shown - plugin_screen_slot;
        plugins->set_screen_param(entry, k, value);
        if (panel.entry == entry && panel.screen && k < 3) panel.params[k].value = int16_t(std::clamp(value, 0, plugin_param_max));
        dirty = true;
        return;
    }
    plugins->set_param(entry,shown,value,ui.plugin_panel.entry==entry?ui.plugin_panel.layout_revision:0);
    if(ui.plugin_panel.entry==entry)refresh_plugin_panel(entry);
    if(shown==0&&ui.plugin_panel.entry==entry&&ui.plugin_panel.focused_pad>=0){for(auto& t:project.tracks)if(t.engine==EngineKind::Plugin&&t.plugin==entry){t.last_pad=uint8_t(ui.plugin_panel.focused_pad);t.bank=uint8_t(ui.plugin_panel.focused_pad/16);t.last_note=uint8_t(36+ui.plugin_panel.focused_pad);}}
    plugin_panel_at=0;
    dirty = true;
}

namespace {
std::string presets_folder(const UiState& ui) { return std::string(ui.presets_folder.data()); }
void set_presets_folder(UiState& ui, const std::string& folder) {
    ui.presets_folder.fill(0);
    folder.copy(ui.presets_folder.data(), ui.presets_folder.size() - 1);
}
} // namespace

// PRESETS opens where the preset picked last is: its folder, its row (plugin/preset_rows.h).
bool Context::plugin_frozen_edit(unsigned entry) {
    auto uses=[&](const Chain& chain){for(unsigned k=0;k<chain.count();++k){const auto& slot=chain.slots[k];
        if(slot.kind==plugin_effect_kind&&slot.params[0]>=0&&unsigned(slot.params[0])==entry)return true;}return false;};
    for(unsigned t=0;t<track_count;++t){
        if(!frozen(t))continue;
        const auto& tr=project.tracks[t];bool held=tr.engine==EngineKind::Plugin&&tr.plugin==entry;
        if(prints.freeze[t].tap==PrintTap::PostFx)held|=uses(tr.fx);
        if(tr.engine==EngineKind::Sampler)for(unsigned pad=0;pad<sampler_pads&&!held;++pad)held|=uses(project.chain(pad_chain_base+t*sampler_pads+pad));
        if(held){ask_unfreeze(t);return true;}
    }return false;
}

void Context::toggle_plugin_transport(unsigned entry) {
    if (entry >= max_plugins || !project.plugins[entry].used()) return;
    if(plugin_frozen_edit(entry))return;
    auto& ref = project.plugins[entry];
    ref.transport = !ref.transport;
    dirty = true;
    if (plugins) plugins->sync(project, plugins_asleep()); // heard from the next block
}

void Context::open_plugin_presets(unsigned entry) {
    if (!plugins || entry >= max_plugins) return;
    if (ui.preset_due && ui.presets_entry == entry) { // Stage's turn still to load: now, as the cursor means a row here
        ui.preset_due = 0;
        choose_plugin_program(ui.presets_cursor);
    }
    auto programs = plugins->programs(entry);
    if (programs.list.empty()) { notice(programs.finding ? "Looking for presets" : "No presets in this plug-in"); return; }
    auto place = preset_place(programs.list, programs.current);
    ui.presets_entry = uint8_t(entry);
    ui.presets_back = ui.page;
    set_presets_folder(ui, place.folder);
    ui.presets_cursor = uint16_t(place.row);
    ui.presets_rows_count=uint16_t(std::min<size_t>(65535,preset_rows(programs.list,place.folder,programs.current).size()));
    open(PageId::PluginPresets);
}

void Context::choose_plugin_program(int index) {
    if (!plugins) return;
    if(plugin_frozen_edit(ui.presets_entry))return;
    auto programs = plugins->programs(ui.presets_entry);
    if (index < 0 || size_t(index) >= programs.list.size()) return;
    const auto& picked=programs.list[size_t(index)];
    plugins->select_program(ui.presets_entry,picked.index);
    if(ui.presets_entry<max_plugins)std::snprintf(project.plugins[ui.presets_entry].preset,sizeof(project.plugins[0].preset),"%s",program_key(picked).c_str());
    ui.plugin_design_page[ui.presets_entry]=0;ui.plugin_design_half[ui.presets_entry]=0;ui.plugin_panel.design=false;
    if(std::string(project.plugins[ui.presets_entry].name)=="Rhythmer Kit"){
        ui.kit_common=false;
        const bool editing=ui.presets_back==PageId::Edit&&track().engine==EngineKind::Plugin&&track().plugin==ui.presets_entry;
        ui.plugin_row[ui.presets_entry]=editing?(ui.edit_page[ui.track]?2:ui.edit_row[ui.track]==3?1:0):0;
        plugin_panel_at=0;
        for(auto& t:project.tracks)if(t.engine==EngineKind::Plugin&&t.plugin==ui.presets_entry){t.bank=0;t.last_pad=0;t.last_note=36;}
    }
    dirty = true; // the plug-in's state, saved with the project, now holds it
}

void Context::push_plugin_preset() {
    if (!plugins) return;
    auto programs = plugins->programs(ui.presets_entry);
    auto inside = presets_folder(ui);
    auto rows = preset_rows(programs.list, inside, programs.current);
    if (rows.empty()) return;
    const auto& row = rows[std::min<size_t>(ui.presets_cursor, rows.size() - 1)];
    if (!row.folder) { choose_plugin_program(int(row.at)); return; }
    std::string into = inside.empty() ? row.name : inside + "/" + row.name;
    if (into.size() >= ui.presets_folder.size()) { notice("Folder name too long"); return; }
    set_presets_folder(ui, into);
    ui.presets_cursor = 0;
    ui.presets_rows_count=uint16_t(std::min<size_t>(65535,preset_rows(programs.list,into,programs.current).size()));
    if (!row.current) return;
    auto in = preset_rows(programs.list, into, programs.current); // the preset picked last is in here: open on it
    for (size_t n = 0; n < in.size(); ++n)
        if (in[n].current) { ui.presets_cursor = uint16_t(n); break; }
}

void Context::back_plugin_presets() {
    auto inside = presets_folder(ui);
    if (inside.empty() || !plugins) { open(ui.presets_back); return; }
    auto programs = plugins->programs(ui.presets_entry);
    ui.presets_cursor = uint16_t(preset_row_of_folder(programs.list, inside, programs.current));
    set_presets_folder(ui, preset_folder_up(inside));
    ui.presets_rows_count=uint16_t(std::min<size_t>(65535,preset_rows(programs.list,presets_folder(ui),programs.current).size()));
}

void Context::pick_plugin_program(unsigned entry, int program) {
    if (!plugins || entry >= max_plugins) return;
    const auto programs = plugins->programs(entry);
    for (const auto& p : programs.list) {
        if (p.index != program) continue;
        plugins->select_program(entry, p.index);
        std::snprintf(project.plugins[entry].preset, sizeof(project.plugins[0].preset), "%s", program_key(p).c_str());
        dirty = true;
        return;
    }
}

// Companion screens (plugin/companion_screen.h) ask; the shell makes the change as a pot turn on the plug-in's card
// makes it (apply(): a frozen track's question, one undo step per control) or as PRESETS picks a program. One asked of
// a plug-in replaced since is dropped. And why a screen stopped, said once.
void Context::companion_requests() {
    static std::vector<ScreenRequest> asked; // the MIDI thread's, its room kept
    asked.clear();
    plugins->take_screen_requests(asked);
    for (const auto& r : asked) {
        if (r.entry >= max_plugins || r.generation != plugins->live_generation(r.entry)) continue;
        if (r.program) { pick_plugin_program(r.entry, r.index); continue; }
        if (r.index < 0 || unsigned(r.index) >= plugin_screen_slot - plugin_index_slot) continue;
        Binding b {Binding::Kind::PluginParam, plugin_binding(r.entry, plugin_index_slot + unsigned(r.index)), 0, plugin_param_max, 0};
        apply(b, int(std::lround(std::clamp(r.value, 0.0f, 1.0f) * plugin_param_max)));
    }
    if (const auto why = plugins->screen_notice(); !why.empty()) notice(why);
}

// An input on a plug-in's own screen: handed to it. False when the page in view is not one (the page's own job then).
bool Context::plugin_screen_input(ScreenKey key, int value) {
    const auto at = plugin_screen_place(project, ui);
    if (at.entry < 0 || !plugins || !ui.plugin_panel.screen) return false;
    plugins->screen_input(unsigned(at.entry), {key, int32_t(value), ui.shift});
    return true;
}

// MUTE or ERASE tapped on a plug-in's own page (not an effect's strip, where the chain's own jobs act): handed to it
// when its page takes that button (rhythmer_screen.h keys()), asked now rather than from the panel's last refresh.
bool Context::plugin_screen_key(ScreenKey key) {
    const auto at = plugin_screen_place(project, ui);
    if (at.entry < 0 || at.area != 0 || !plugins || !ui.plugin_panel.screen) return false;
    const uint8_t bit = key == ScreenKey::Mute ? screen_key_mute : key == ScreenKey::Erase ? screen_key_erase : 0;
    if (!bit || !(plugins->screen_keys(unsigned(at.entry)) & bit)) return false;
    plugins->screen_input(unsigned(at.entry), {key, 1, ui.shift});
    return true;
}

int Context::plugin_in_focus() const {
    const auto& t = project.tracks[ui.track % track_count];
    if (ui.page == PageId::PluginPresets && ui.presets_entry < max_plugins) return ui.presets_entry;
    if (ui.page == PageId::Stage && t.engine == EngineKind::Plugin && t.plugin < max_plugins) return t.plugin; // B1 PRESETS, EDIT
    const bool mixer_fx = mixer_fx_lit(ui) && channel_chain(mixer_strip(ui)) >= 0; // the mixer's FX block: its lit effect
    if ((fx_chain_page(ui.page) && !ui.picker_open) || mixer_fx) {
        unsigned n = mixer_fx ? unsigned(channel_chain(mixer_strip(ui))) : page_chain(project, ui), k = ui.fx_cursor[UiState::cursor_of(n)];
        const auto& chain = project.chain(n);
        if (k < chain.count() && chain.slots[k].kind == plugin_effect_kind && unsigned(chain.slots[k].params[0]) < max_plugins)
            return chain.slots[k].params[0];
    }
    return -1;
}

void Context::plugins_tick(uint32_t at) {
    if (!plugins) return;
    if (library) plugins->set_card(library->card()); // the card's top folder, as the sample library found it
    for(auto& t:project.tracks) if(t.engine==EngineKind::Plugin&&t.plugin<max_plugins) {
        const std::string path=project.plugins[t.plugin].path;
        if(path.find("RHYTHMER - Rhythmer Kit")!=std::string::npos||path.find("GROOVE - Groove Kit")!=std::string::npos){use_drum_pads(t);auto& ref=project.plugins[t.plugin];if(std::string(ref.name)=="Groove Kit")std::snprintf(ref.name,sizeof(ref.name),"Rhythmer Kit");if(std::string(t.name)=="GROOVE KIT")t.set_name("RHYTHMER KIT");}
        else if(t.layout==PadLayout::Drums){t.layout=PadLayout::Scale;if(t.octave==2)t.octave=4;}
    }
    split_shared_plugins();
    plugins->sync(project, plugins_asleep());
    delete_tick(*this); // the picker listed again once a plug-in is deleted (shell/delete_jobs.h)
    // The web page installed or deleted a plug-in: an open picker lists again, its cursor on the plug-in it was on.
    if (plugins->card_changes() != plugin_card_seen) {
        plugin_card_seen = plugins->card_changes();
        if (ui.page == PageId::Plugins) {
            auto view = plugins->view();
            const PluginFile on = ui.plugins_cursor < view.files.size() ? view.files[ui.plugins_cursor] : PluginFile {};
            plugin_came_back = false;
            plugins->browse(view.folder, on.path, on.id);
        }
    }
    if (ui.page == PageId::Plugins && plugins->changes() != plugin_view_seen) { // the picker's rows, and where going up lands
        plugin_view_seen = plugins->changes();
        auto view = plugins->view();
        ui.plugins_count = uint16_t(view.files.size());
        if (view.came_from >= 0 && !plugin_came_back) { ui.plugins_cursor = uint16_t(view.came_from); plugin_came_back = true; }
        if (ui.plugins_cursor >= ui.plugins_count) ui.plugins_cursor = uint16_t(ui.plugins_count ? ui.plugins_count - 1 : 0);
    }
    { // A plug-in's own screen in view: what it is told, each tick, so a pot reaches its picture at once.
        ScreenView view;
        if (const auto at = plugin_screen_place(project, ui); at.entry >= 0) {
            view.entry = uint8_t(at.entry);
            view.area = at.area;
            view.shift = ui.shift;
            view.playing = host.playing();
            view.colour = ui::track_colour(ui.track).getARGB() & 0xffffffu;
            for (unsigned k = 0; k < 3; ++k) { view.pots[k] = ui.pots[k].position; view.live[k] = ui.pots[k].engaged ? 1 : 0; }
        }
        plugins->screen_view(view);
    }
    companion_requests();
    if (at - plugin_panel_at < 50) return; // the cards' texts twenty times a second
    plugin_panel_at = at;
    { // the selected track's drum voices, for Keys and the pad lights (loaded after the pick, so asked each time)
        const auto& t = track();
        if (t.engine == EngineKind::Plugin && drum_pads(t)) plugins->drum_voices(t.plugin, ui.drum_voices);
        else if (ui.drum_voices.entry != no_plugin) ui.drum_voices = DrumVoices {};
    }
    int entry = plugin_in_focus();
    if (entry < 0) { ui.plugin_panel = PluginPanel {}; return; }
    refresh_plugin_panel(unsigned(entry)); // clamped to its rows
    if(ui.page==PageId::PluginPresets){auto programs=plugins->programs(ui.presets_entry);
        ui.presets_rows_count=uint16_t(std::min<size_t>(65535,preset_rows(programs.list,presets_folder(ui),programs.current).size()));
        if(ui.presets_cursor>=ui.presets_rows_count)ui.presets_cursor=ui.presets_rows_count?ui.presets_rows_count-1:0;
    }
}

} // namespace rhythmer
