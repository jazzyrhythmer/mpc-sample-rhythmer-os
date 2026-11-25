// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Rhythmer TRACK SETUP, reached through MENU > HERE or track FX B2.
// The upstream track grid and sends pages were replaced by its native mixer.
// This file retains only sound/type change and separate confirmed deletion.
#include "rhythmer/pages/common.h"
#include "rhythmer/pages/pages.h"
#include "rhythmer/shell/erase_copy.h"
#include "rhythmer/shell/ask.h"
#include "rhythmer/shell/project_store.h"
#include "rhythmer/ui/widgets.h"
#include "rhythmer/shell/new_track.h"
#include "rhythmer/shell/features.h"
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/shell/stage_map.h"
#include "rhythmer/shell/pad_bank.h"
#include <cmath>

namespace rhythmer {
namespace {
using namespace ui;

unsigned type_count(const UiState&) { return 6; } // native v0.8 track types are available in BASIC too.
unsigned type_index(const UiState&, unsigned at) { return at; }
void actions_draw(Paint& p, const Snapshot& s) {
    Header h = header_of(s);
    h.left.push_back(chip(s.ui.track_types ? "TRACK TYPE" : "TRACK SETUP",colour_of(s)));
    h.right.push_back(tag_of(s,s.ui.track));
    frame(p,s,{{"BACK","","SELECT"},-1,colour_of(s)},h);
    const char* actions[] = {s.track().sampled()?"LOAD SAMPLE":s.track().engine==EngineKind::Midi?"MIDI DEVICE":s.track().engine==EngineKind::Audio?"AUDIO INPUT":s.track().used()?"CHANGE PLUG-IN":"ADD INSTRUMENT","TRACK TYPE","LOAD MPC KIT","OPEN TRACK","DELETE TRACK"};
    const char* types[] = {"PLUG-IN","SAMPLE KIT","KEYGROUP","MIDI","AUDIO","EMPTY"};
    unsigned count=s.ui.track_types?type_count(s.ui):5;
    for (unsigned i=0;i<count;++i) {
        ListRow row; row.name = s.ui.track_types ? types[type_index(s.ui,i)] : actions[i];
        row.current = std::min<unsigned>(s.ui.track_action,count-1) == i; row.chevron = true; row.accent = colour_of(s);
        list_row(p,zone::pad,zone::picture_y+i*(list_row_h+list_row_gap),zone::width-2*zone::pad,row);
    }
    const char* hint = !s.ui.track_types && s.ui.track_action==4 ? "Removes sound AND all patterns" : "Change sound / type; patterns stay";
    const float hint_y = zone::picture_y + count * (list_row_h + list_row_gap) + 4;
    // Never let an optional hint enter the native bottom control strip.
    if (hint_y + 16 <= zone::knobs_y - 4)
        p.text(hint,zone::pad,hint_y,zone::width-2*zone::pad,16,caps_text(colour::dim),juce::Justification::centred);
}
void actions_wheel(Context& c,int delta) { c.ui.track_action=uint8_t(std::clamp<int>(int(c.ui.track_action)+delta,0,c.ui.track_types?int(type_count(c.ui))-1:4)); }
void actions_push(Context& c) {
    unsigned at=c.ui.track_action;
    if(c.ui.track_types) {
        at=type_index(c.ui,std::min<unsigned>(at,type_count(c.ui)-1));
        static constexpr EngineKind kinds[]={EngineKind::Plugin,EngineKind::Sampler,EngineKind::Keygroup,EngineKind::Midi,EngineKind::Audio,EngineKind::None};
        // A bare kit's direct chooser already has SAMPLE KIT's engine. Start
        // loading its first pad instead of returning to the same empty prompt.
        if (at%6==1 && c.track().engine==EngineKind::Sampler && back_target(c.ui)==PageId::Stage) {
            if(c.ui.recording || c.host_recording()){c.notice("Finish recording first");return;}
            if(c.frozen_edit(c.ui.track))return;
            c.ui.track_types=false;c.ui.layer_load=-1;c.ui.break_browse=false;
            c.open(PageId::Stage);c.open(PageId::Samples);
            if(c.library)c.library->rescan();
            return;
        }
        change_track_type(c,kinds[at%6]); return;
    }
    if(at==1 || (at==0 && !c.track().used())) {c.ui.track_types=true;c.ui.track_action=0;return;}
    if(at==3) {c.open(PageId::Stage);return;}
    if(c.ui.recording || c.host_recording()) {c.notice("Finish recording first");return;}
    if(c.frozen_edit(c.ui.track))return;
    if(at==4) {ask(c,UiState::Ask::ClearTrack,c.ui.track);return;}
    if(at==2) {c.open(PageId::Project);c.ui.project_kit_only=true;c.ui.project_list=true;
        if(c.store)c.store->list(c.project_folder);
        return;
    }
    if(c.track().engine==EngineKind::Plugin)c.open_plugin_picker(UiState::PluginTarget::Instrument);
    else if(c.track().sampled())c.open(PageId::Samples);
    else if(c.track().engine==EngineKind::Audio){c.open(PageId::Stage);stage_go(c.ui,1,0);}
    else {c.open(PageId::Stage);stage_go(c.ui,0,0);c.notice("MIDI: DEVICE sets port/channel");}
}
void actions_soft(Context& c,int key) {
    if(key==0) c.back();
    if(key==2)actions_push(c);
}
} // namespace

const PageSpec track_actions_page {actions_draw,play_pad,actions_wheel,actions_push,actions_soft,actions_wheel};

} // namespace rhythmer
