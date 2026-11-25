// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "rhythmer/shell/new_track.h"
#include "rhythmer/shell/stage_map.h"
#include "rhythmer/shell/ask.h"
#include "rhythmer/plugin/plugin_files.h"
#include "rhythmer/plugin/preset_rows.h"
#include "rhythmer/shell/preset_index.h"
#include "rhythmer/shell/track_copy.h"
#include "rhythmer/shell/menu.h"

namespace rhythmer {
inline int track_workflow_check() {
    unsigned checks=0;
    auto require=[&](bool ok,const char* what){++checks;if(!ok)std::fprintf(stderr,"TRACK_WORKFLOW FAIL %u: %s\n",checks,what);return ok;};
#define TRACK_CHECK(x,what) do {if(!require((x),what))return 1;}while(false)
    auto host_owner=std::make_unique<EngineHost>();auto& host=*host_owner;Shell shell(host);auto& c=shell.context();
    // Empty-pad shortcuts use real surface dispatch; native page maps and loaded-pad focus survive.
    {
        auto reset_empty=[&](bool elsewhere=false){
            c.ui=UiState{};c.project.tracks[0]=Track{};c.load_sound(0,EngineKind::Sampler,0);
            if(elsewhere)c.track().pads[127].sample=0;
            c.ui.page=PageId::Stage;stage_go(c.ui,0,0);
        };
        auto tap=[&](SurfaceButton b){for(bool down:{true,false}){SurfaceEvent e;e.input.kind=SurfaceInput::Kind::Button;e.input.index=uint8_t(b);e.input.pressed=down;shell.surface(e,3000+checks);}};
        for(auto kind:{StageKind::Params,StageKind::Trim}) {
            reset_empty();stage_go(c.ui,EngineKind::Sampler,kind);tap(SurfaceButton::WheelPush);
            TRACK_CHECK(c.ui.page==PageId::Samples&&!c.ui.track_types&&c.ui.layer_load==-1,"empty kit source/trim opens selected pad sample browser");
            TRACK_CHECK(c.track().engine==EngineKind::Sampler,"opening sample picker leaves kit intact");
            c.back();TRACK_CHECK(c.ui.page==PageId::Stage&&!c.ui.track_types,"empty kit browser Back returns to source");
            tap(SurfaceButton::WheelPush); // Shift must be held for Back
            c.ui.shift=true;tap(SurfaceButton::WheelPush);c.ui.shift=false;
            TRACK_CHECK(c.ui.page==PageId::Stage,"empty kit browser Shift Push returns to source");
            reset_empty(true);stage_go(c.ui,EngineKind::Sampler,kind);c.ui.layer_load=2;tap(SurfaceButton::WheelPush);
            TRACK_CHECK(c.ui.page==PageId::Samples&&c.track().last_pad==0&&c.ui.layer_load==-1,"other-bank sample opens selected empty pad browser, clears layer target");
            c.back();TRACK_CHECK(c.ui.page==PageId::Stage&&c.track().pads[127].used()&&!c.track().pads[0].used(),"browser cancel preserves both pads");
        }
        // Printed SAMPLE SELECT / SAMPLE RECORD remain direct upstream paths.
        reset_empty();c.track().last_pad=11;tap(SurfaceButton::SampleSelect);
        TRACK_CHECK(c.ui.page==PageId::Samples&&c.track().last_pad==11,"SAMPLE SELECT opens selected empty pad browser");
        c.back();tap(SurfaceButton::SampleRecord);
        TRACK_CHECK(c.ui.page==PageId::Record&&c.track().last_pad==11,"SAMPLE RECORD opens input recording for selected pad");
        c.back();
        // The upstream long hold owns the press; releasing it must not also
        // invoke Rhythmer's short-click type/load/save shortcut.
        reset_empty();
        SurfaceEvent hold;hold.input.kind=SurfaceInput::Kind::Button;hold.input.index=uint8_t(SurfaceButton::WheelPush);hold.input.pressed=true;
        shell.surface(hold,5000);SurfaceLights menu_lights;shell.lights(menu_lights,5400);
        TRACK_CHECK(c.ui.menu!=0,"encoder hold opens native MENU on empty sample kit");
        hold.input.pressed=false;shell.surface(hold,5401);
        TRACK_CHECK(c.ui.page==PageId::Stage&&!c.ui.track_types,"MENU release does not trigger empty-kit short click");
        menu_button(c,SurfaceButton::TopLeft);
        TRACK_CHECK(!c.ui.menu&&c.ui.page==PageId::Stage,"MENU Back returns to original track");
        PluginRack picker_rack(host);c.plugins=&picker_rack;
        for(bool full:{false,true})for(unsigned pick=0;pick<6;++pick) {
            reset_empty();c.ui.settings.full_features=full;open_track_actions(c);c.ui.track_types=true;c.ui.track_action=uint8_t(pick);tap(SurfaceButton::WheelPush);
            if(pick==0) TRACK_CHECK(c.ui.page==PageId::Plugins,"direct type PLUG-IN opens native picker");
            if(pick==1) {
                TRACK_CHECK(c.ui.page==PageId::Samples&&c.track().engine==EngineKind::Sampler,"direct SAMPLE KIT starts first sample load");
                c.back();TRACK_CHECK(c.ui.page==PageId::Stage,"first-sample browser Back returns to source");
            }
            if(pick==2) TRACK_CHECK(c.ui.page==PageId::Stage&&c.track().engine==EngineKind::Keygroup,"direct KEYGROUP choice lands on track");
            if(pick==4) TRACK_CHECK(c.ui.page==PageId::Stage&&c.track().engine==EngineKind::Audio&&c.ui.stage_group[c.ui.track]==1,"direct AUDIO choice opens INPUT");
            if(pick==3) TRACK_CHECK(c.ui.page==PageId::Stage&&c.track().engine==EngineKind::Midi,"direct MIDI choice lands on track");
            if(pick==5) TRACK_CHECK(c.ui.page==PageId::Stage&&!c.track().used(),"direct EMPTY choice returns native carousel");
        }
        c.plugins=nullptr;
        reset_empty(true);c.track().last_pad=11;tap(SurfaceButton::WheelPush);
        std::strcpy(c.project.samples.edit(0).path,"Samples/empty-pad-target.wav");
        c.load_file("Samples/empty-pad-target.wav","target",false);
        TRACK_CHECK(c.track().pads[11].sample==0&&!c.track().pads[0].used()&&c.track().pads[127].sample==0,"sample load targets selected pad, preserves other bank");
        c.undo();TRACK_CHECK(!c.track().pads[11].used()&&c.track().pads[127].sample==0,"sample load Undo restores empty selected pad only");
        c.project.samples.edit(0)=SampleRef{};
        reset_empty();c.track().pads[127].layers[0].sample=0;tap(SurfaceButton::WheelPush);
        TRACK_CHECK(c.ui.page==PageId::Samples,"secondary layer in another bank preserves empty-pad sample load");
        reset_empty();c.track().pads[0].layers[0].sample=0;tap(SurfaceButton::WheelPush);
        TRACK_CHECK(c.ui.page==PageId::Stage&&!c.ui.track_types,"selected layered pad retains native click");
        reset_empty();c.track().pads[0].sample=0;tap(SurfaceButton::WheelPush);
        TRACK_CHECK(c.ui.page==PageId::Stage&&c.ui.last_pot==1,"loaded pad click still advances knob focus");
        for(auto page:{PageId::PadPage,PageId::LayersPage,PageId::EnvelopePage}) {
            reset_empty(true);c.ui.page=page;tap(SurfaceButton::WheelPush);
            TRACK_CHECK(c.ui.page==page&&c.ui.map_open,"legacy sampler page click still opens page map");
        }
        reset_empty(true);stage_go(c.ui,2,0);tap(SurfaceButton::WheelPush);
        TRACK_CHECK(c.ui.page==PageId::Stage,"empty pad MIX click retains native behavior");
        reset_empty();c.track().break_bars=2;tap(SurfaceButton::WheelPush);
        TRACK_CHECK(c.ui.page==PageId::Samples&&c.ui.break_browse&&!c.ui.track_types,"Break retains loop A browser");
        reset_empty();c.ui.recording=true;tap(SurfaceButton::WheelPush);
        TRACK_CHECK(c.ui.page==PageId::Stage,"empty kit recording blocks replacement shortcut");
        reset_empty();c.prints.freeze[0].state=FreezeState::Frozen;tap(SurfaceButton::WheelPush);
        TRACK_CHECK(c.ui.page==PageId::Stage&&c.ui.ask==UiState::Ask::Unfreeze,"frozen kit keeps unfreeze guard");c.prints.freeze[0]={};
        reset_empty();c.ui.page=PageId::Tracks;open_track_actions(c);c.ui.track_types=true;c.back();
        TRACK_CHECK(c.ui.page==PageId::TrackActions&&!c.ui.track_types,"TRACK SETUP type submenu still backs to parent");
        reset_empty(true);tap(SurfaceButton::WheelPush);c.project.tracks[1].engine=EngineKind::Midi;c.select_track(1);
        TRACK_CHECK(c.ui.page==PageId::Stage&&c.ui.track==1,"browser track switch follows new selected track");
        c.ui=UiState{};c.project.tracks[0]=Track{};c.project.tracks[1]=Track{};
    }
    // Native v0.8 moved Tracks to Mixer: creating an empty track returns to its caller.
    {
        c.ui=UiState{};c.project.tracks[0]=Track{};c.ui.page=PageId::Mixer;
        offer_new_track(c);
        TRACK_CHECK(c.ui.page==PageId::Stage&&c.ui.new_back==PageId::Mixer,"empty Mixer track remembers native caller");
        make_new_track(c,NewChoice::MidiUsb);
        TRACK_CHECK(c.track().engine==EngineKind::Midi&&c.ui.page==PageId::Mixer,"new MIDI track returns to Mixer");
        c.ui=UiState{};c.project.tracks[0]=Track{};c.edits.clear();
    }
    // Track setup is modal; its BACK/SELECT must not adjust a running repeat rate.
    {
        c.ui=UiState{};c.project.tracks[0]=Track{};c.load_sound(0,EngineKind::Midi,0);
        c.track().repeat=true;const auto rate=c.track().repeat_rate;c.ui.page=PageId::Mixer;
        auto tap_setup=[&](SurfaceButton b){for(bool down:{true,false}){SurfaceEvent e;e.input.kind=SurfaceInput::Kind::Button;e.input.index=uint8_t(b);e.input.pressed=down;shell.surface(e,12000+checks);}};
        open_track_actions(c);tap_setup(SurfaceButton::TopLeft);
        TRACK_CHECK(c.ui.page==PageId::Mixer&&c.track().repeat&&c.track().repeat_rate==rate,"setup BACK retains live repeat state");
        open_track_actions(c);c.ui.track_action=1;tap_setup(SurfaceButton::TopRight);
        TRACK_CHECK(c.ui.track_types&&c.track().repeat_rate==rate,"setup SELECT opens types without changing repeat rate");
        c.back();c.back();c.ui=UiState{};c.project.tracks[0]=Track{};c.edits.clear();
    }
    TRACK_CHECK(preset_index(4,4)==0&&preset_index(-1,4)==3,"preset boundaries wrap both ways");
    TRACK_CHECK(preset_index(13,4)==1&&preset_index(-9,4)==3,"accelerated preset turns wrap modulo count");
    TRACK_CHECK(preset_index(1,0)==0&&preset_index(-1,1)==0,"empty and single-item preset lists stay safe");
    // The owned plug-in EDIT face exposes source/save via Push, while PRESETS keeps its native job.
    {
        auto tap_tools=[&](SurfaceButton button){for(bool down:{true,false}){SurfaceEvent e;e.input.kind=SurfaceInput::Kind::Button;e.input.index=uint8_t(button);e.input.pressed=down;shell.surface(e,2000+checks);}};
        c.ui.track=0;auto& t=c.project.tracks[0];t.engine=EngineKind::Plugin;t.plugin=0;
        c.ui.page=PageId::Stage;stage_go(c.ui,EngineKind::Plugin,StageKind::PluginEdit);c.ui.plugin_row[0]=7;
        c.ui.plugin_panel.entry=0;c.ui.plugin_panel.status=PluginStatus::Ready;c.ui.plugin_panel.layout_revision=1;
        std::snprintf(c.ui.plugin_panel.name,sizeof(c.ui.plugin_panel.name),"Rhythmer Kit");
        tap_tools(SurfaceButton::WheelPush);
        TRACK_CHECK(c.ui.page==PageId::Edit&&c.ui.edit_row[0]==1,"kit EDIT Push reaches PAD SOURCE");
        TRACK_CHECK(c.ui.sound_tools_track==0&&c.ui.sound_tools_row==7,"tool entry remembers sound row");
        tap_tools(SurfaceButton::TopRight);TRACK_CHECK(c.ui.edit_row[0]==2,"kit source goes to SAVE");
        tap_tools(SurfaceButton::TopRight);TRACK_CHECK(c.ui.edit_row[0]==0,"kit save returns to EDIT");
        c.back();TRACK_CHECK(c.ui.page==PageId::Stage&&c.ui.plugin_row[0]==7&&c.ui.sound_tools_track==-1,"Back restores Stage edit position");
        c.ui.plugin_panel.layout_revision=0;tap_tools(SurfaceButton::WheelPush);
        TRACK_CHECK(c.ui.page==PageId::Stage&&c.ui.sound_tools_track==-1,"external plug-in Push keeps native behavior");
        c.ui.plugin_panel.layout_revision=1;stage_go(c.ui,0,0);tap_tools(SurfaceButton::WheelPush);
        TRACK_CHECK(c.ui.page==PageId::Stage&&c.ui.sound_tools_track==-1,"PRESETS Push is not intercepted by tools");
        stage_go(c.ui,EngineKind::Plugin,StageKind::PluginEdit);
        c.ui.plugin_panel.entry=0;c.ui.plugin_panel.status=PluginStatus::Ready;c.ui.plugin_panel.layout_revision=1;
        std::snprintf(c.ui.plugin_panel.name,sizeof(c.ui.plugin_panel.name),"Sophie");
        tap_tools(SurfaceButton::WheelPush);TRACK_CHECK(c.ui.page==PageId::Edit&&c.ui.edit_row[0]==2,"single sound EDIT Push reaches SAVE");
        tap_tools(SurfaceButton::TopRight);TRACK_CHECK(c.ui.edit_row[0]==0,"single save returns to EDIT");
        c.select_track(1);TRACK_CHECK(c.ui.sound_tools_track==-1,"track change invalidates tool bookmark");
        c.ui=UiState{};c.project.tracks[0]=Track{};
    }

    auto press=[&](SurfaceButton b,bool down){SurfaceEvent e;e.input.kind=SurfaceInput::Kind::Button;e.input.index=uint8_t(b);e.input.pressed=down;shell.surface(e,2000+checks);};
    c.ui.track=0;c.ui.page=PageId::Stage;
    c.load_sound(0,EngineKind::Midi,0);c.track().mix.level=77;c.track().mix.pan=-12;c.track().pattern=73;
    for(unsigned n=0;n<pattern_count;++n){Event e;e.tick=n;e.key=uint8_t(36+n%60);c.patterns.at[0][n].events={e};}
    auto before=c.patterns.at[0];
    auto patterns_kept=[&]{return c.patterns.at[0]==before;};
    c.open(PageId::Stage);offer_new_track(c);
    TRACK_CHECK(c.ui.page==PageId::Stage && patterns_kept(),"used track encoder still opens its sound");
    c.open(PageId::Mixer);menu_open(c);
    std::array<MenuItem,menu_here_max> setup_items{};auto setup_count=menu_here(c.project,c.ui,setup_items);
    unsigned setup_at=setup_count;for(unsigned i=0;i<setup_count;++i)if(setup_items[i].job==MenuJob::TrackSetup)setup_at=i;
    TRACK_CHECK(setup_at<setup_count,"new Mixer HERE exposes TRACK SETUP");
    c.ui.menu=menu_here_view;c.ui.menu_row=uint8_t(setup_at);menu_button(c,SurfaceButton::WheelPush);
    TRACK_CHECK(c.ui.page==PageId::TrackActions,"TRACK SETUP remains reachable from its new menu entry");
    c.ui.track_action=1;track_actions_page.push(c);
    TRACK_CHECK(c.ui.track_types,"type submenu");
    c.back();TRACK_CHECK(!c.ui.track_types && c.ui.page==PageId::TrackActions,"Back leaves submenu first");
    c.ui.track_types=true;c.ui.track_action=1;track_actions_page.push(c);
    TRACK_CHECK(c.track().engine==EngineKind::Sampler && patterns_kept(),"MIDI to kit retains all 128 patterns");
    TRACK_CHECK(c.track().pattern==73 && c.track().mix.level==77 && c.track().mix.pan==-12,"selected pattern and mix survive");
    TRACK_CHECK(c.ui.page==PageId::Stage && c.ui.back_depth==0,"successful conversion lands on track, no stale menu");
    c.undo();TRACK_CHECK(c.track().engine==EngineKind::Midi && patterns_kept(),"type undo");
    c.redo();TRACK_CHECK(c.track().engine==EngineKind::Sampler && patterns_kept(),"type redo");
    change_track_type(c,EngineKind::None);TRACK_CHECK(!c.track().used() && patterns_kept(),"empty retains patterns");
    TRACK_CHECK(c.ui.page==PageId::Stage && c.ui.new_for==0 && c.ui.new_pick==uint8_t(NewType::Plugin),"intentional EMPTY opens native type carousel");
    c.open(PageId::Stage);offer_new_track(c);
    TRACK_CHECK(c.ui.page==PageId::Stage && c.ui.new_for==0 && patterns_kept(),"intentional EMPTY encoder opens type selection, not actions");
    c.ui.page=PageId::Edit;edit_page.push(c);
    TRACK_CHECK(c.ui.page==PageId::Stage && c.ui.new_for==0 && patterns_kept(),"EMPTY stranded on Edit opens type selection");
    c.open(PageId::Tracks);open_track_actions(c);
    TRACK_CHECK(c.ui.page==PageId::TrackActions,"EMPTY has a separate TRACK SETUP entry");
    c.open(PageId::Stage);
    TRACK_CHECK(!new_track_lock(c,SurfaceButton::Minus,true),"empty track permits Undo");
    TRACK_CHECK(!new_track_lock(c,SurfaceButton::TopMiddle,true) && !new_track_lock(c,SurfaceButton::TopRight,true),"visible KIT/MIDI empty-track buttons are enabled");
    TRACK_CHECK(!new_track_lock(c,SurfaceButton::WheelPush,true),"empty browser permits Back");
    press(SurfaceButton::Shift,true);press(SurfaceButton::Minus,true);press(SurfaceButton::Minus,false);press(SurfaceButton::Shift,false);
    TRACK_CHECK(c.track().engine==EngineKind::Sampler,"physical Shift-Minus restores empty track");
    open_track_actions(c);c.ui.track_action=4;track_actions_page.push(c);
    TRACK_CHECK(c.ui.ask==UiState::Ask::ClearTrack && patterns_kept(),"delete requires confirmation");
    ask_button(c,SurfaceButton::TopLeft);TRACK_CHECK(c.track().used() && patterns_kept(),"cancel delete is non-destructive");
    c.ui.track_action=4;track_actions_page.push(c);ask_button(c,SurfaceButton::TopRight);
    bool clear=true;for(const auto& p:c.patterns.at[0])clear &= p.events.empty();
    TRACK_CHECK(!c.track().used() && clear && c.ui.page==PageId::Stage,"confirmed delete clears all patterns and lands empty");
    c.undo();TRACK_CHECK(c.track().engine==EngineKind::Sampler && patterns_kept(),"delete undo restores sound and patterns");
    c.ui.recording=true;change_track_type(c,EngineKind::Midi);TRACK_CHECK(c.track().engine==EngineKind::Sampler,"recording guard");c.ui.recording=false;
    c.prints.freeze[0].state=FreezeState::Frozen;change_track_type(c,EngineKind::Midi);
    TRACK_CHECK(c.track().engine==EngineKind::Sampler && c.ui.ask==UiState::Ask::Unfreeze,"freeze guard");c.ui.ask=UiState::Ask::None;c.prints.freeze[0]={};
    const PageId owned[]={PageId::AutoSample,PageId::Stems,PageId::PluginPresets,PageId::Plugins,PageId::Samples,PageId::Sounds,PageId::Edit,PageId::Chop,PageId::PadPage,PageId::LayersPage,PageId::Print,PageId::TrackActions};
    for(auto page:owned){c.ui.track=0;c.ui.page=page;c.ui.presets_back=PageId::Stage;c.ui.plugin_target=UiState::PluginTarget::Instrument;c.ui.sheet_open=true;
        c.select_track(1);TRACK_CHECK(c.ui.track==1 && c.ui.page==PageId::Stage && !c.ui.sheet_open,"track-owned screen follows selected track");
        c.back();TRACK_CHECK(c.ui.page==PageId::Stage,"Back cannot resurrect previous track's browser");}
    c.ui.track=0;c.ui.page=PageId::PluginPresets;c.ui.presets_back=PageId::Bus;c.select_track(1);
    TRACK_CHECK(c.ui.page==PageId::PluginPresets,"bus preset list remains global");
    c.ui.back_depth=1;c.ui.back_stack[0]=PageId::Bus;c.select_track(2);c.back();
    TRACK_CHECK(c.ui.page==PageId::Bus,"global Back retains bus owner");
    c.ui.page=PageId::Plugins;c.ui.plugin_target=UiState::PluginTarget::Effect;c.ui.plugin_chain=track_count;c.select_track(2);
    TRACK_CHECK(c.ui.page==PageId::Plugins,"bus effect picker remains global");
    c.ui.track=0;c.ui.page=PageId::PluginPresets;c.ui.presets_back=PageId::Edit;c.select_track(1);offer_new_track(c);make_new_track(c,NewChoice::Kit);
    TRACK_CHECK(c.ui.track==1 && c.ui.page==PageId::Stage && c.track().engine==EngineKind::Sampler,"preset A to empty B to kit B");
    c.ui.track=0;c.ui.page=PageId::PluginPresets;c.ui.presets_back=PageId::Stage;
    SurfaceEvent button;button.input.kind=SurfaceInput::Kind::Button;button.input.index=uint8_t(SurfaceButton::PadBank);button.input.pressed=true;shell.surface(button,1000);
    SurfaceEvent hit;hit.pad=8;hit.midi.status=0x90;hit.midi.key=8;hit.midi.value=100;shell.surface(hit,1001);
    hit.midi.status=0x80;hit.midi.value=0;shell.surface(hit,1002);button.input.pressed=false;shell.surface(button,1003);
    TRACK_CHECK(c.ui.track==8 && c.ui.page==PageId::Stage,"physical PAD BANK plus pad follows new track");
    c.ui.track=3;c.ui.page=PageId::Plugins;c.ui.plugin_target=UiState::PluginTarget::Instrument;c.track().engine=EngineKind::None;
    TRACK_CHECK(!new_track_lock(c,SurfaceButton::PadBank,true),"empty plug-in picker permits track switching");
    // Async whole-kit load belongs to the original target, never whichever track is selected later.
    auto source=std::make_unique<Project>();source->tracks[0].engine=EngineKind::Sampler;source->tracks[0].set_name("TEST KIT");
    source->tracks[0].pads[0].sample=0;std::strcpy(source->samples.edit(0).path,"Samples/workflow.wav");
    c.project.samples.edit(0)=source->samples[0];
    c.ui.track=4;c.project.tracks[4].engine=EngineKind::Midi;c.kit_target=5;c.load_kit(*source,"TEST KIT");
    TRACK_CHECK(c.ui.track==4 && c.project.tracks[4].engine==EngineKind::Midi && c.project.tracks[5].engine==EngineKind::Sampler,"async kit target captured");
    BankFile bank;bank.pads[0].sample=0;std::strcpy(bank.samples[0].path,"Samples/workflow.wav");
    c.project.tracks[7]=Track{};c.project.tracks[7].engine=EngineKind::Sampler;c.project.tracks[7].bank=1;
    c.bank_target=7;c.bank_target_bank=3;c.load_bank(bank,"ASYNC BANK");
    TRACK_CHECK(c.ui.track==4 && c.project.tracks[7].pads[48].used() && !c.project.tracks[7].pads[16].used(),"async bank retains original track AND bank");
    c.kit_target=4;c.bank_target=4;auto restored=std::make_unique<TrackState>(track_state(c,4));restore_track(c,4,*restored);
    TRACK_CHECK(c.kit_target==-2 && c.bank_target==-2,"whole-track undo/replacement invalidates late loads");
    c.kit_target=-2;c.load_kit(*source,"TEST KIT");TRACK_CHECK(c.project.tracks[4].engine==EngineKind::Midi,"invalidated request discarded");
    c.ui.track=4;c.ui.break_pending=0;c.ui.break_pending_b=true;c.ui.break_track=5;c.ui.break_bars=2;c.project.tracks[5].break_bars=2;c.project.samples.edit(0).frames=176400;
    c.gen_break_tick();TRACK_CHECK(c.ui.track==4,"Break B completion does not steal selected track");
    if(const char* card=std::getenv("RHYTHMER_GEN_CARD")){
        PluginRack rack(host);rack.set_card(card);c.plugins=&rack;
        auto files=list_plugins(plugin_folder(card));
        auto file=[&](const char* name){return std::find_if(files.begin(),files.end(),[&](const PluginFile& f){return f.name==name;});};
        for(const auto& f:files) {
            if(f.name=="Break Engine")continue; // native loop sampler, not a plug-in preset bank
            c.ui.track=0;c.ui.page=PageId::Stage;c.ui.plugin_target=UiState::PluginTarget::Instrument;c.ui.plugin_replace=true;c.choose_plugin(f);
            bool ready=false;for(unsigned n=0;n<2000;++n){if(rack.status(c.track().plugin)==PluginStatus::Ready){ready=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            TRACK_CHECK(ready && std::string(c.project.plugins[c.track().plugin].path)==f.path,"preset-loop correct instrument loads");auto programs=rack.programs(c.track().plugin);
            if(programs.list.empty())continue;
            c.open_plugin_presets(c.track().plugin);auto rows=preset_rows(programs.list,std::string(c.ui.presets_folder.data()),programs.current);
            if(!rows.empty()){
                c.ui.presets_cursor=uint16_t(rows.size()-1);plugin_presets_page.wheel(c,1);TRACK_CHECK(c.ui.presets_cursor==0,"preset list last to first");
                plugin_presets_page.wheel(c,-1);TRACK_CHECK(c.ui.presets_cursor==rows.size()-1,"preset list first to last");
            }
            c.open(PageId::Stage);c.ui.preset_due=1;c.ui.presets_entry=c.track().plugin;c.ui.presets_cursor=uint16_t(programs.list.size()-1);
            c.plugin_preset_turn(1);TRACK_CHECK(c.ui.presets_cursor==0,"main preset turn last to first");
            c.plugin_preset_turn(-1);TRACK_CHECK(c.ui.presets_cursor==programs.list.size()-1,"main preset turn first to last");c.ui.preset_due=0;
        }
        auto first=file("FM-106"),second=file("Sophie"),br=file("Break Engine");
        TRACK_CHECK(first!=files.end() && second!=files.end() && br!=files.end(),"real plugin fixture available");
        auto wait=[&]{for(unsigned n=0;n<2000;++n){if(rack.status(c.track().plugin)==PluginStatus::Ready)return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}return false;};
        c.ui.track=0;c.ui.page=PageId::Stage;c.ui.plugin_target=UiState::PluginTarget::Instrument;c.ui.plugin_replace=true;c.choose_plugin(*first);
        TRACK_CHECK(c.track().engine==EngineKind::Plugin && wait() && patterns_kept(),"load real plugin over kit, keep patterns");
        auto entry=c.track().plugin;auto old_ref=c.project.plugins[entry];auto old_state=rack.states()[entry];
        c.open_plugin_presets(entry);TRACK_CHECK(c.ui.page==PageId::PluginPresets,"native preset list opens from Stage");
        plugin_presets_page.soft(c,1);TRACK_CHECK(c.ui.page==PageId::Plugins,"preset list exposes replacement");
        auto replacement_history=c.edits.size();c.ui.plugin_replace=true;c.choose_plugin(*second);
        TRACK_CHECK(c.edits.size()==replacement_history+1,"instrument replacement commits exactly one undo step");TRACK_CHECK(wait() && c.ui.page==PageId::Stage && c.ui.back_depth==0 && patterns_kept(),"replacement lands at selected track");
        c.undo();bool undo_ready=wait();auto restored_state=rack.states()[entry];
        if(!undo_ready || std::strcmp(c.project.plugins[entry].path,old_ref.path) || restored_state!=old_state){
            std::fprintf(stderr,"BEFORE %.180s\nAFTER %.180s\n",std::string(old_state.begin(),old_state.end()).c_str(),std::string(restored_state.begin(),restored_state.end()).c_str());
            size_t diff=0;while(diff<restored_state.size()&&diff<old_state.size()&&restored_state[diff]==old_state[diff])++diff;
            std::fprintf(stderr,"UNDO detail ready=%d old=%s actual=%s old_bytes=%zu new_bytes=%zu first_diff=%zu notice=%s\n",undo_ready,old_ref.path,c.project.plugins[entry].path,old_state.size(),restored_state.size(),diff,c.ui.notice);
        }
        TRACK_CHECK(undo_ready && std::strcmp(c.project.plugins[entry].path,old_ref.path)==0 && restored_state==old_state,"replacement undo restores original plugin and sound state");
        c.redo();TRACK_CHECK(wait() && std::strcmp(c.project.plugins[entry].name,"Sophie")==0,"replacement redo");
        auto plugin_state=rack.states()[c.track().plugin];
        change_track_type(c,EngineKind::Midi);TRACK_CHECK(c.track().engine==EngineKind::Midi && patterns_kept(),"plugin to MIDI keeps sequence");
        c.undo();TRACK_CHECK(c.track().engine==EngineKind::Plugin && wait() && rack.states()[c.track().plugin]==plugin_state,"type undo restores real plugin state");
        auto engine=c.track().engine;auto history=c.edits.size();c.choose_plugin(*br);
        TRACK_CHECK(c.track().engine==engine && c.edits.size()==history && c.ui.break_browse,"choosing Break defers destructive conversion");
        samples_page.soft(c,0);TRACK_CHECK(c.track().engine==engine && c.ui.page==PageId::Plugins,"Break browser cancel preserves instrument");
        c.ui.track=0;c.ui.page=PageId::PluginPresets;c.ui.presets_back=PageId::Stage;c.select_track(6);offer_new_track(c);make_new_track(c,NewChoice::Plugin);c.ui.plugin_replace=true;c.choose_plugin(*first);
        TRACK_CHECK(c.ui.track==6 && c.ui.page==PageId::Stage && wait(),"preset A to empty B to plugin B");
        c.back();TRACK_CHECK(c.ui.page==PageId::Stage,"loaded B Back cannot return to A");
        c.dirty=false;c.start_new_project(true);
        TRACK_CHECK(c.ui.track==0&&c.track().engine==EngineKind::Plugin&&std::string(c.project.plugins[c.track().plugin].name)=="Rhythmer Kit","normal NEW assigns Rhythmer Kit to track 1");
        TRACK_CHECK(wait(),"normal NEW default kit reaches Ready");auto defaults=rack.programs(c.track().plugin);
        TRACK_CHECK(!defaults.list.empty()&&defaults.current==0&&defaults.list[0].name=="K-WASUB / 909","normal NEW loads K-WASUB / 909");
        c.dirty=false;c.start_new_project(false);bool empty_refs=true;for(const auto&ref:c.project.plugins)empty_refs&=!ref.used();
        TRACK_CHECK(empty_refs&&c.track().engine!=EngineKind::Plugin&&!c.track().pads[0].used(),"EMPTY project keeps empty sampler, no default plug-in");
        c.plugins=nullptr;
    }else {std::fprintf(stderr,"TRACK_WORKFLOW: missing real plugin card fixture\n");return 2;}
    // Empty tracks retain their carousel but can reach MIX, in either key order.
    {auto owned_host=std::make_unique<EngineHost>();auto& h=*owned_host;Shell s(h);auto& x=s.context();for(auto& t:x.project.tracks)t=Track{};
     x.ui.track=0;x.ui.page=PageId::Stage;offer_new_track(x);
     auto button=[&](SurfaceButton b,bool down){SurfaceEvent e;e.input.kind=SurfaceInput::Kind::Button;e.input.index=uint8_t(b);e.input.pressed=down;s.surface(e,3000);};
     for(bool reversed:{false,true}){
       x.ui.page=PageId::Stage;auto first=reversed?SurfaceButton::KnobFx:SurfaceButton::PadFx;auto second=reversed?SurfaceButton::PadFx:SurfaceButton::KnobFx;
       button(first,true);button(second,true);button(first,false);button(second,false);
       TRACK_CHECK(x.ui.page==PageId::Mixer,"all-empty MIX chord works in either order");
       TRACK_CHECK(!x.track().used()&&!x.ui.pad_fx_down&&!x.ui.knob_fx_down&&!x.ui.fx_chord_used,"MIX creates no track and leaves no held keys");
     }
     for(auto key:{SurfaceButton::PadFx,SurfaceButton::KnobFx}){
       x.ui.page=PageId::Stage;button(key,true);button(key,false);
       TRACK_CHECK(x.ui.page==PageId::Stage&&!x.track().used(),"empty solo FX key keeps native lock");
     }
    }
    // Shift + Push always cancels Ask dialogs; ordinary confirmations remain.
    {auto owned_host=std::make_unique<EngineHost>();auto& h=*owned_host;Shell s(h);auto& x=s.context();x.ui.track=0;x.load_sound(0,EngineKind::Midi,0);
     Event note;note.key=60;x.patterns.at[0][0].events={note};auto kept=x.patterns.at[0];auto history=x.edits.size();
     auto button=[&](SurfaceButton b,bool down){SurfaceEvent e;e.input.kind=SurfaceInput::Kind::Button;e.input.index=uint8_t(b);e.input.pressed=down;s.surface(e,4000);};
     for(unsigned k=unsigned(UiState::Ask::EraseKeyEvents);k<=unsigned(UiState::Ask::Unfreeze);++k)for(int pick:{0,2}){
       ask(x,UiState::Ask(k),0);x.ui.ask_pick=uint8_t(pick);
       button(SurfaceButton::Shift,true);button(SurfaceButton::WheelPush,true);button(SurfaceButton::WheelPush,false);button(SurfaceButton::Shift,false);
       TRACK_CHECK(x.ui.ask==UiState::Ask::None,"Shift-Push cancels every Ask answer, including selected destructive answers");
       TRACK_CHECK(x.ui.shift_used,"confirmation Back consumes Shift, avoiding a second Shift-tap action");
       TRACK_CHECK(x.track().engine==EngineKind::Midi&&x.patterns.at[0]==kept&&x.edits.size()==history,"dialog Back leaves sound, patterns and Undo untouched");
     }
     x.ui.page=PageId::Roll;x.ui.roll_marked=1;x.ui.roll_mark_track=0;x.ui.roll_mark_pattern=x.track().pattern;x.ui.roll_marking=false;
     ask(x,UiState::Ask::ClearTrack,0);x.ui.ask_pick=2;button(SurfaceButton::Shift,true);button(SurfaceButton::WheelPush,true);button(SurfaceButton::WheelPush,false);button(SurfaceButton::Shift,false);
     TRACK_CHECK(x.ui.roll_marked==1&&x.patterns.at[0]==kept,"Back on Roll preserves selected notes when Shift is released");
     x.ui.page=PageId::Stage;
     ask(x,UiState::Ask::ClearTrack,0);x.ui.ask_pick=2;button(SurfaceButton::WheelPush,true);button(SurfaceButton::WheelPush,false);
     TRACK_CHECK(!x.track().used()&&x.patterns.at[0][0].events.empty(),"plain Push still confirms selected deletion");
     x.undo();TRACK_CHECK(x.track().engine==EngineKind::Midi&&x.patterns.at[0]==kept,"confirmed deletion remains undoable");
     ask(x,UiState::Ask::ClearTrack,0);button(SurfaceButton::TopRight,true);button(SurfaceButton::TopRight,false);
     TRACK_CHECK(!x.track().used(),"B3 confirmation remains available");x.undo();
     ask(x,UiState::Ask::ClearTrack,0);x.ui.ask_pick=2;button(SurfaceButton::TopLeft,true);button(SurfaceButton::TopLeft,false);
     TRACK_CHECK(x.track().engine==EngineKind::Midi&&x.patterns.at[0]==kept,"B1 cancellation remains available");
    }
    std::printf("TRACK_WORKFLOW passed %u assertions; 128-pattern retention, real plugins, undo/redo, scoped navigation, async ownership, empty MIX chord, safe dialog Back\n",checks);
#undef TRACK_CHECK
    return 0;
}
} // namespace rhythmer
