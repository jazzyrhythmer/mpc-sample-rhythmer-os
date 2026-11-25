// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "rhythmer/model/project_file.h"
#include "rhythmer/shell/ask.h"
#include "rhythmer/shell/new_track.h"
#include "rhythmer/shell/shell.h"
#include "rhythmer/model/scales.h"
#include "rhythmer/engine/engine_host.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <set>
#include <functional>
#include "rhythmer/model/chord_gen_access.h"
#include "rhythmer/plugin/plugin_rack.h"
#include "rhythmer/pages/pages.h"
#include "rhythmer/shell/step_edit.h"
#include "rhythmer/shell/step_lane.h"
#include "rhythmer/shell/stage_map.h"
#include "rhythmer/pages/common.h"

namespace rhythmer {
#include "rhythmer/shell/upstream_check.inc"
inline int gen_failure(int code,int line){std::fprintf(stderr,"GEN CHECK failure=%d line=%d\n",code,line);return code;}
inline int gen8_shell_check(const std::function<void(Shell&,const std::string&)>& capture = {}) {
    if(int result=upstream_shell_check()){std::fprintf(stderr,"UPSTREAM shell failure %d\n",result);return 120+result;}
    auto host_owner=std::make_unique<EngineHost>();auto& host=*host_owner;
    auto shell_owner=std::make_unique<Shell>(host);auto& shell=*shell_owner;auto& c=shell.context();
    {
        auto picture=std::make_unique<Snapshot>();shell.snapshot(*picture,2000);const auto whole_at=picture->whole_at;
        c.project.gen8.seed=1234;std::strcpy(c.project.recovery_id,"0123456789abcdef0123456789abcdef");
        shell.snapshot(*picture,2010);
        if(picture->whole_at!=whole_at||picture->project.gen8.seed!=1234||std::strcmp(picture->project.recovery_id,c.project.recovery_id))return gen_failure(101,__LINE__);
        std::fill(std::begin(c.project.recovery_id),std::end(c.project.recovery_id),0);
    }
    c.ui.track=0;c.project.gen8={42,2,100,50};c.project.gen8.assign=0;
    for(unsigned t=0;t<8;t++) {
        auto& tr=c.project.tracks[t];tr.engine=EngineKind::Midi;tr.pattern=0;
        tr.layout=t<4?PadLayout::Scale:PadLayout::Drums;
        for(unsigned k=0;k<sampler_pads;k++)tr.midi.drum_note[k]=uint8_t(36+k);
        auto& p=c.patterns.at[t][0];p.length=1536;p.events.clear();
    }
    // Range targets 9-16 must leave 1-8 unchanged in MIDI-only mode.
    for(unsigned t=8;t<16;t++){c.project.tracks[t]=c.project.tracks[t-8];c.patterns.at[t][0].length=1536;c.patterns.at[t][0].events.clear();}
    c.project.gen8.first=8;c.project.gen8.last=15;c.gen8_action(0);
    for(unsigned t=0;t<8;t++)if(!c.patterns.at[t][0].events.empty())return gen_failure(30,__LINE__);
    for(unsigned t=8;t<16;t++)if(c.patterns.at[t][0].events.empty())return gen_failure(31,__LINE__);
    c.project.gen8.first=0;c.project.gen8.last=7;
    host.load(c.project,c.patterns);c.edits.clear();c.gen8_action(0);
    if(c.edits.size()!=1)return gen_failure(1,__LINE__);
    std::array<Pattern,8> made;
    for(unsigned t=0;t<8;t++) {
        made[t]=c.patterns.at[t][0];if(made[t].events.empty())return gen_failure(2,__LINE__);
    }
    c.undo();for(unsigned t=0;t<8;t++)if(!c.patterns.at[t][0].events.empty())return gen_failure(3,__LINE__);
    c.redo();for(unsigned t=0;t<8;t++)if(c.patterns.at[t][0].events!=made[t].events)return gen_failure(4,__LINE__);
    // Native sampler bank H: keys may exceed MIDI 127 inside this host.
    c.ui.track=15;auto& tr=c.project.tracks[15];tr.engine=EngineKind::Sampler;tr.break_bars=2;tr.bank=7;tr.pattern=0;
    for(unsigned k=112;k<128;k++)tr.pads[k].sample=0;
    auto& p=c.patterns.at[15][0];p.length=1536;p.events.clear();
    c.edits.clear();c.gen8_action(5);
    if(c.edits.size()!=1||p.events.size()!=32||p.events.front().key!=148)return gen_failure(5,__LINE__);
    auto straight=p.events;c.undo();if(!p.events.empty())return gen_failure(6,__LINE__);c.redo();if(p.events!=straight)return gen_failure(7,__LINE__);
    c.gen8_action(7);if(p.events==straight)return gen_failure(8,__LINE__);
    for(size_t i=0;i<straight.size();i++)if(straight[i].tick%96==0&&p.events[i]!=straight[i])return gen_failure(9,__LINE__);
    c.undo();if(p.events!=straight)return gen_failure(10,__LINE__);
    // Missing kit semantics must not erase a working pattern.
    c.ui.track=7;auto& un=c.project.tracks[7];un.engine=EngineKind::Plugin;un.plugin=0;un.layout=PadLayout::Drums;
    std::strcpy(c.project.plugins[0].name,"Unmapped Drum");
    auto before=c.patterns.at[7][0].events;auto edits=c.edits.size();c.gen8_action(2);
    if(c.patterns.at[7][0].events!=before||c.edits.size()!=edits)return gen_failure(11,__LINE__);
    // Individual percussion sounds use one C4 key, even on a drum role.
    std::strcpy(c.project.plugins[0].path,"Plugins/Matt Estela - VST - Sophie");
    std::strcpy(c.project.plugins[0].name,"Sophie");un.layout=PadLayout::Scale;
    c.gen8_action(2);
    if(c.patterns.at[7][0].events.empty())return gen_failure(12,__LINE__);
    for(const auto& e:c.patterns.at[7][0].events)if(e.key!=60)return gen_failure(13,__LINE__);
    // Rhythmer Kit uses its canonical pad roles rather than one sound key.
    c.ui.track=4;auto& kit=c.project.tracks[4];kit.engine=EngineKind::Plugin;kit.plugin=1;kit.layout=PadLayout::Drums;
    std::strcpy(c.project.plugins[1].path,"Plugins/RHYTHMER - Rhythmer Kit");
    std::strcpy(c.project.plugins[1].name,"Rhythmer Kit");c.gen8_action(2);
    if(c.patterns.at[4][0].events.empty())return gen_failure(14,__LINE__);
    for(const auto& e:c.patterns.at[4][0].events)if(e.key<36||e.key>43)return gen_failure(15,__LINE__);
    for(unsigned bars=1;bars<=4;bars++){
        // Duration/chop fixture: start empty; occupied-pattern preservation is tested separately.
        c.project.tracks[10]={};c.patterns.at[10]={};
        c.ui.track=10;c.ui.break_track=10;c.ui.break_bank=0;c.ui.break_bars=uint8_t(bars);c.ui.break_pending=0;
        c.project.samples.edit(0).frames=bars*88200;
        c.gen_break_tick();
        const auto& tr=c.project.tracks[10];if(tr.break_bars!=bars||tr.engine!=EngineKind::Sampler)return gen_failure(32,__LINE__);
        for(unsigned i=0;i<16;i++)if(!tr.pads[i].used()||tr.pads[i].start!=uint64_t(bars*88200)*i/16||tr.pads[i].own_tempo!=1200)return gen_failure(33,__LINE__);
        if(c.patterns.at[10][tr.pattern].events.size()!=16)return gen_failure(34,__LINE__);
    }
    if(const char* card=std::getenv("RHYTHMER_GEN_CARD")){
        PluginRack rack(host);rack.set_card(card);c.plugins=&rack;
        auto choose=[&](const PluginFile& file){
            c.choose_plugin(file);
            if(c.ui.ask==UiState::Ask::ReplacePlugin){
                // This fixture selects files directly, not through the asynchronous browser rows.
                ask_button(c,SurfaceButton::TopLeft);
                c.ui.plugin_replace=true;c.choose_plugin(file);
            }
        };
        c.ui.track=0;c.project.tracks[0]=c.project.tracks[4];c.project.gen8.first=0;c.project.gen8.last=3;c.project.gen8.assign=1;
        auto old_track=c.project.tracks[0];
        std::strcpy(c.project.plugins[old_track.plugin].path,"Plugins/GROOVE - Groove Kit");
        rack.sync(c.project);
        for(unsigned tries=0;tries<9000&&rack.status(old_track.plugin)!=PluginStatus::Ready;tries++)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(rack.status(old_track.plugin)!=PluginStatus::Ready)return gen_failure(44,__LINE__);
        auto old_ref=c.project.plugins[old_track.plugin];auto previous=rack.states()[old_track.plugin];
        auto undo_size=c.edits.size();c.gen8_action(0);
        if(c.edits.size()!=undo_size+1||c.project.tracks[0].engine!=EngineKind::Plugin){std::fprintf(stderr,"GEN assignment: %s\n",c.ui.notice);return gen_failure(40,__LINE__);}
        auto selected=c.project.tracks[0].plugin;
        for(unsigned tries=0;tries<9000&&rack.status(selected)!=PluginStatus::Ready;tries++)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(rack.status(selected)!=PluginStatus::Ready)return gen_failure(41,__LINE__);
        c.gen_break_tick();
        c.undo();if(c.project.tracks[0].plugin!=old_track.plugin||std::strcmp(c.project.plugins[old_track.plugin].path,old_ref.path)){
            std::fprintf(stderr,"GEN UNDO slot before=%u after=%u path_equal=%d notice=%s history=%zu\n",old_track.plugin,c.project.tracks[0].plugin,!std::strcmp(c.project.plugins[old_track.plugin].path,old_ref.path),c.ui.notice,size_t(c.edits.size()));
            return gen_failure(42,__LINE__);
        }
        for(unsigned tries=0;tries<9000&&rack.status(old_track.plugin)!=PluginStatus::Ready;tries++)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(rack.status(old_track.plugin)!=PluginStatus::Ready||rack.states()[old_track.plugin]!=previous)return gen_failure(45,__LINE__);
        c.redo();if(c.project.tracks[0].plugin!=selected)return gen_failure(43,__LINE__);
        auto available=list_plugins(plugin_folder(card));auto drum=std::find_if(available.begin(),available.end(),[](const PluginFile& f){return f.single_drum;});
        if(drum==available.end())return gen_failure(50,__LINE__);
        c.ui.track=5;c.project.root=0;c.project.tracks[5].transpose=0;c.project.tracks[5].octave=2;c.ui.plugin_target=UiState::PluginTarget::Instrument;c.ui.plugin_back=PageId::Edit;choose(*drum);
        if(c.track().octave!=4||pad_note(c.project,c.track(),0).key!=60||c.track().layout==PadLayout::Drums){std::fprintf(stderr,"Drum load: name=%s flag=%d octave=%d key=%d layout=%d engine=%d notice=%s\n",drum->name.c_str(),drum->single_drum,c.track().octave,pad_note(c.project,c.track(),0).key,int(c.track().layout),int(c.track().engine),c.ui.notice);return gen_failure(51,__LINE__);}

        auto br=std::find_if(available.begin(),available.end(),[](const PluginFile& f){return f.name=="Break Engine";});
        if(br==available.end()){return gen_failure(52,__LINE__);}
        choose(*br);
        if(c.track().engine!=EngineKind::Plugin||!c.ui.break_from_picker||!c.ui.break_browse||c.ui.page!=PageId::Samples)return gen_failure(53,__LINE__);
        auto browse_engine=std::find_if(available.begin(),available.end(),[](const PluginFile& f){return f.name=="FM-106";});if(browse_engine==available.end())return gen_failure(54,__LINE__);
        c.ui.track=6;c.ui.plugin_target=UiState::PluginTarget::Instrument;c.ui.plugin_back=PageId::Edit;choose(*browse_engine);auto entry=c.track().plugin;
        for(unsigned tries=0;tries<9000&&rack.status(entry)!=PluginStatus::Ready;tries++){std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        if(rack.status(entry)!=PluginStatus::Ready){PluginPanel failed;rack.panel(entry,0,failed);std::fprintf(stderr,"GEN load failed name=%s status=%d error=%s\n",browse_engine->name.c_str(),int(failed.status),failed.error);return gen_failure(55,__LINE__);}
        auto programs=rack.programs(entry);if(programs.list.size()<3)return gen_failure(56,__LINE__);
        rack.set_param(entry,0,int(std::lround(2000.0/(programs.list.size()-1))));rack.states();if(rack.programs(entry).current!=2)return gen_failure(57,__LINE__);
        auto hortator=std::find_if(available.begin(),available.end(),[](const PluginFile& f){return f.name=="Hortator";});if(hortator==available.end()){return gen_failure(59,__LINE__);}
        c.ui.track=7;c.ui.plugin_target=UiState::PluginTarget::Instrument;choose(*hortator);entry=c.track().plugin;
        for(unsigned tries=0;tries<9000&&rack.status(entry)!=PluginStatus::Ready;tries++){std::this_thread::sleep_for(std::chrono::milliseconds(10));}if(rack.status(entry)!=PluginStatus::Ready){return gen_failure(60,__LINE__);}
        programs=rack.programs(entry);int acid=-1;for(const auto& p:programs.list)if(p.name=="SQUELCH")acid=p.index;if(std::string(c.project.plugins[entry].path)!=hortator->path||acid<0){return gen_failure(61,__LINE__);}rack.select_program(entry,acid);
        for(unsigned tries=0;tries<9000&&rack.programs(entry).loading;tries++){std::this_thread::sleep_for(std::chrono::milliseconds(10));}rack.states();
        c.project.gen8.assign=0;c.project.gen8.first=c.project.gen8.last=7;c.gen8_action(0);int first=-1;bool varied=false;for(const auto& event:c.pattern().events){if(first<0)first=event.key;varied|=event.key!=first;}if(!varied){std::fprintf(stderr,"Hortator ACID GEN was a fixed drum note\n");return gen_failure(62,__LINE__);}
        auto kit_file=std::find_if(available.begin(),available.end(),[](const PluginFile& f){return f.name=="Rhythmer Kit";});if(kit_file==available.end())return gen_failure(220,__LINE__);
        c.ui.track=8;c.ui.plugin_target=UiState::PluginTarget::Instrument;c.ui.plugin_back=PageId::Edit;choose(*kit_file);entry=c.track().plugin;
        for(unsigned tries=0;tries<9000&&rack.status(entry)!=PluginStatus::Ready;tries++)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(rack.status(entry)!=PluginStatus::Ready)return gen_failure(221,__LINE__);
        c.ui.plugin_panel.layout_revision=1;
        for(unsigned context=0;context<3;++context){
            c.ui.page=PageId::Edit;c.ui.sound_tools_track=int(c.ui.track);c.ui.edit_page[c.ui.track]=1;c.ui.edit_row[c.ui.track]=context==1?3:0;
            c.open_plugin_presets(entry);c.ui.presets_cursor=uint16_t(context+1);plugin_presets_page.soft(c,2);
            for(unsigned tries=0;tries<9000&&rack.programs(entry).loading;tries++)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(rack.programs(entry).current!=int(context+1)||c.ui.plugin_row[entry]!=2)return gen_failure(222,__LINE__);
            plugin_presets_page.soft(c,0);if(c.ui.page!=PageId::Edit||c.ui.edit_page[c.ui.track]!=1||c.ui.edit_row[c.ui.track]!=(context==1?3:0)){std::fprintf(stderr,"Preset return context=%u page=%u back=%u edit_page=%u row=%u tools=%d\n",context,unsigned(c.ui.page),unsigned(c.ui.presets_back),unsigned(c.ui.edit_page[c.ui.track]),unsigned(c.ui.edit_row[c.ui.track]),c.ui.sound_tools_track);return gen_failure(223,__LINE__);}
        }
        c.ui.track=15;c.project.tracks[15]={};c.ui.page=PageId::Stage;c.ui.new_sources=false;c.ui.new_pick=1;
        stage_page.wheel(c,-1);if(c.ui.new_pick!=0)return gen_failure(245,__LINE__);
        stage_page.wheel(c,2);if(c.ui.new_pick!=unsigned(NewType::Plugin))return gen_failure(246,__LINE__);
        stage_page.push(c);if(c.ui.page!=PageId::Plugins||c.track().used())return gen_failure(243,__LINE__);
        c.back();if(c.track().used())return gen_failure(244,__LINE__);
        unsigned design_sources=0,design_controls=0;
        for(const auto& file:available){if(file.name=="Break Engine")continue;
            if(const char* only=std::getenv("RHYTHMER_GEN_DESIGN_ONLY"))if(file.name!=only)continue;
            std::fprintf(stderr,"GEN DESIGN load=%s\n",file.name.c_str());
            c.ui.track=9;c.ui.plugin_target=UiState::PluginTarget::Instrument;c.ui.plugin_back=PageId::Edit;choose(file);entry=c.track().plugin;
            for(unsigned tries=0;tries<9000&&rack.status(entry)!=PluginStatus::Ready;tries++){std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            if(rack.status(entry)!=PluginStatus::Ready){PluginPanel failed;rack.panel(entry,0,failed);std::fprintf(stderr,"GEN DESIGN failed=%s entry=%u status=%d error=%s notice=%s\n",file.name.c_str(),unsigned(entry),int(failed.status),failed.error,c.ui.notice);return gen_failure(230,__LINE__);}
            c.ui.page=PageId::Edit;c.ui.edit_page[c.ui.track]=1;c.refresh_plugin_panel(entry);
            if(c.ui.plugin_panel.layout_revision&&c.ui.plugin_panel.row<2)return gen_failure(231,__LINE__);
            const auto before=rack.states()[entry];const auto rows=c.ui.plugin_panel.rows;
            for(unsigned row=2;row<rows;++row){
                c.ui.plugin_row[entry]=uint16_t(row);c.refresh_plugin_panel(entry);
                auto bindings=page_bindings(c.project,c.ui,c.pattern());
                for(unsigned k=0;k<3;++k)if(c.ui.plugin_panel.params[k].used){
                    if(bindings[k].kind!=Binding::Kind::PluginParam||plugin_binding_shown(bindings[k].id)!=row*3+k)return gen_failure(234,__LINE__);
                    ++design_controls;
                }
                if(capture){
                    c.ui.page=PageId::Stage;stage_go(c.ui,EngineKind::Plugin,StageKind::PluginEdit);
                    capture(shell,file.name+"-controls-"+std::to_string(row));
                    c.ui.page=PageId::Edit;
                }
            }
            c.ui.plugin_row[entry]=2;c.refresh_plugin_panel(entry);
            edit_page.wheel(c,-1000);if(c.ui.plugin_row[entry]!=2)return gen_failure(239,__LINE__);
            c.ui.page=PageId::Stage;stage_go(c.ui,EngineKind::Plugin,StageKind::PluginEdit);c.refresh_plugin_panel(entry);
            if(c.ui.plugin_panel.design||c.ui.plugin_panel.row<2)return gen_failure(237,__LINE__);
            c.ui.page=PageId::Edit;
            const bool kit_mode=file.name=="Rhythmer Kit";
            c.ui.edit_row[c.ui.track]=0;c.refresh_plugin_panel(entry);
            for(unsigned step=0;step<(kit_mode?3u:2u);++step){
                edit_page.soft(c,2);
                unsigned mode=kit_mode?(step+1)%3:step?0:2;
                if(c.ui.edit_row[c.ui.track]!=mode)return gen_failure(242,__LINE__);
                unsigned row=mode==1?0:mode==2?1:2;
                if(c.ui.plugin_panel.row!=row)return gen_failure(243,__LINE__);
                auto mapped=page_bindings(c.project,c.ui,c.pattern());
                for(unsigned k=0;k<3;++k)if(c.ui.plugin_panel.params[k].used&&
                   (mapped[k].kind!=Binding::Kind::PluginParam||plugin_binding_shown(mapped[k].id)!=row*3+k))return gen_failure(244,__LINE__);
            }
            if(rack.states()[entry]!=before)return gen_failure(240,__LINE__);
            c.open_plugin_presets(entry);auto bindings=page_bindings(c.project,c.ui,c.pattern());if(bindings[0].kind!=Binding::Kind::PluginJump||bindings[1].kind!=Binding::Kind::None||bindings[2].kind!=Binding::Kind::None)return gen_failure(241,__LINE__);
            c.apply(bindings[0],1);if(c.ui.presets_cursor!=std::min<int>(1,bindings[0].max)||rack.states()[entry]!=before)return gen_failure(242,__LINE__);plugin_presets_page.soft(c,0);
            // The first page and EDIT must operate the same source values,
            // including the Kit engine's own global filter (never an insert).
            for(unsigned quick=0;quick<(kit_mode?2u:3u);++quick){
                c.ui.page=PageId::Stage;stage_go(c.ui,EngineKind::Plugin,StageKind::PluginSound);c.refresh_plugin_panel(entry);
                auto front=page_bindings(c.project,c.ui,c.pattern());auto binding=front[quick];
                if(binding.kind!=Binding::Kind::PluginParam)return gen_failure(250,__LINE__);
                const int old=binding.value;const auto shown=plugin_binding_shown(binding.id);
                c.apply(binding,old>800?old-100:old+100);c.close_gesture();rack.states();c.refresh_plugin_panel(entry);
                auto updated=page_bindings(c.project,c.ui,c.pattern())[quick];
                c.ui.page=PageId::Edit;c.ui.edit_page[c.ui.track]=1;c.ui.edit_row[c.ui.track]=0;c.ui.plugin_row[entry]=uint16_t(shown/3);c.refresh_plugin_panel(entry);
                auto backend=page_bindings(c.project,c.ui,c.pattern());const auto slot=shown%3;
                if(backend[slot].id!=binding.id||backend[slot].value!=updated.value){std::fprintf(stderr,"QUICK link engine=%s knob=%u front_id=%d back_id=%d front=%d back=%d\n",file.name.c_str(),quick,int(binding.id),int(backend[slot].id),updated.value,backend[slot].value);return gen_failure(251,__LINE__);}
            }
            // Plug-in native pots are not included in upstream's model-only
            // Undo scopes. Restore the captured plug-in state explicitly;
            // do not assert or advertise an Undo capability it does not have.
            rack.set_gen_state(entry,before);rack.sync(c.project,c.plugins_asleep());
            for(unsigned tries=0;tries<9000&&rack.status(entry)!=PluginStatus::Ready;++tries)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(rack.status(entry)!=PluginStatus::Ready||rack.states()[entry]!=before){
                PluginPanel failed;rack.panel(entry,0,failed);auto after=rack.states()[entry];
                std::fprintf(stderr,"GEN RESTORE engine=%s status=%d error=%s before=%zu after=%zu\n",file.name.c_str(),int(failed.status),failed.error,before.size(),after.size());
                if(const char* dir=std::getenv("RHYTHMER_GEN_DIAGNOSTICS")){std::ofstream a(std::string(dir)+"/restore-before.json"),b(std::string(dir)+"/restore-after.json");a.write(reinterpret_cast<const char*>(before.data()),before.size());b.write(reinterpret_cast<const char*>(after.data()),after.size());}
                return gen_failure(253,__LINE__);
            }
            if(capture){
                auto saved_ui=c.ui;
                c.ui.page=PageId::Stage;stage_go(c.ui,EngineKind::Plugin,StageKind::PluginSound);c.refresh_plugin_panel(entry);
                capture(shell,file.name+"-home");
                c.open_plugin_presets(entry);capture(shell,file.name+"-presets");
                c.ui.page=PageId::Stage;
                stage_go(c.ui,EngineKind::Plugin,StageKind::PluginEdit);c.ui.plugin_row[entry]=2;c.refresh_plugin_panel(entry);
                capture(shell,file.name+"-edit");
                stage_page.push(c);
                if(c.ui.page!=PageId::Edit||c.ui.sound_tools_track!=int(c.ui.track))return gen_failure(254,__LINE__);
                capture(shell,file.name+(kit_mode?"-source":"-save"));
                if(kit_mode){edit_page.soft(c,2);capture(shell,file.name+"-save");}
                if(std::getenv("RHYTHMER_REVIEW_CAPTURE_ALL")){
                    auto programs=rack.programs(entry);size_t longest=0;
                    for(size_t i=1;i<programs.list.size();++i)if(programs.list[i].name.size()>programs.list[longest].name.size())longest=i;
                    if(!programs.list.empty()){
                        c.ui.presets_entry=uint8_t(entry);c.choose_plugin_program(int(longest));rack.states();
                        c.ui.page=PageId::Stage;stage_go(c.ui,EngineKind::Plugin,StageKind::PluginSound);c.refresh_plugin_panel(entry);
                        capture(shell,file.name+"-longest-preset-home");c.open_plugin_presets(entry);capture(shell,file.name+"-longest-preset-list");
                    }
                    if(kit_mode){
                        unsigned pads=0;
                        for(int program=0;program<69;++program){
                            c.ui.presets_entry=uint8_t(entry);c.choose_plugin_program(program);rack.states();
                            c.ui.page=PageId::Stage;stage_go(c.ui,EngineKind::Plugin,StageKind::PluginSound);c.refresh_plugin_panel(entry);
                            capture(shell,"Kit-"+std::to_string(program)+"-home");
                            for(int pad=0;pad<16;++pad){
                                c.ui.page=PageId::Edit;c.ui.sound_tools_track=int8_t(c.ui.track);c.ui.edit_page[c.ui.track]=1;c.ui.edit_row[c.ui.track]=1;c.refresh_plugin_panel(entry);
                                auto source=page_bindings(c.project,c.ui,c.pattern());
                                if(source[0].kind!=Binding::Kind::PluginParam||plugin_binding_shown(source[0].id)!=0)return gen_failure(255,__LINE__);
                                c.set_plugin_param(source[0].id,int(std::lround(pad*plugin_param_max/63.0)));rack.states();c.refresh_plugin_panel(entry);
                                if(c.ui.plugin_panel.focused_pad!=pad||c.track().last_pad!=pad)return gen_failure(256,__LINE__);
                                capture(shell,"Kit-"+std::to_string(program)+"-pad-"+std::to_string(pad+1)+"-source");
                                c.ui.page=PageId::Stage;stage_go(c.ui,EngineKind::Plugin,StageKind::PluginEdit);c.ui.plugin_row[entry]=2;c.refresh_plugin_panel(entry);
                                capture(shell,"Kit-"+std::to_string(program)+"-pad-"+std::to_string(pad+1)+"-mix");++pads;
                            }
                        }
                        std::printf("RHYTHMER_KIT_SCREENS kits=69 pads=%u source_and_edit_owner=matched\n",pads);
                    }
                    rack.set_gen_state(entry,before);rack.sync(c.project,c.plugins_asleep());
                    for(unsigned tries=0;tries<9000&&rack.status(entry)!=PluginStatus::Ready;++tries)std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    if(rack.status(entry)!=PluginStatus::Ready)return gen_failure(257,__LINE__);
                }
                c.ui=saved_ui;
            }
            ++design_sources;
        }
        std::printf("RHYTHMER_DESIGN sources=%u mapped_controls=%u navigation_state=unchanged native_stage_edit=ok preset_browse=safe quick_front_edit=linked state_restore=exact\n",design_sources,design_controls);
        c.plugins=nullptr;
    }
    {auto snap=std::make_unique<Snapshot>();auto& t=snap->project.tracks[0];t.engine=EngineKind::Plugin;t.plugin=0;
     for(const char* name:{"Rhythmer Kit","Acid Engine","Sophie","Aphex","CS-1","FM-106","FMini-1","S1Vave","K-WASUB","MONO2K","OberVave","Hortator","Elixir","Guru-600","Prophet","Dexed","Hera","Braids","Plaits"}){
       std::snprintf(snap->project.plugins[0].name,sizeof snap->project.plugins[0].name,"%s",name);t.set_name(name);if(track_display_name(*snap,0)!=name)return gen_failure(201,__LINE__);}
     t.set_name("MY BASS");if(track_display_name(*snap,0)!="My Bass")return gen_failure(202,__LINE__);t.engine=EngineKind::None;if(track_display_name(*snap,0)!="Empty")return gen_failure(203,__LINE__);
    }
    // Real page handlers keep engine-specific screens scoped and Back inside GEN.
    {EngineHost h;Shell s(h);auto& x=s.context();x.ui.page=PageId::Patterns;x.ui.sheet_open=true;x.ui.gen8_mode=0;x.ui.sheet_row=4;x.track().engine=EngineKind::Sampler;x.track().break_bars=2;
     patterns_page.push(x);if(x.ui.gen8_mode!=2)return gen_failure(63,__LINE__);patterns_page.soft(x,0);if(x.ui.gen8_mode!=0||!x.ui.sheet_open)return gen_failure(64,__LINE__);
     x.track().engine=EngineKind::Plugin;x.track().plugin=0;std::strcpy(x.project.plugins[0].name,"Acid Engine");x.ui.sheet_row=4;patterns_page.push(x);if(x.ui.gen8_mode!=4)return gen_failure(65,__LINE__);patterns_page.soft(x,0);if(x.ui.gen8_mode!=0||!x.ui.sheet_open)return gen_failure(66,__LINE__);
     x.track().engine=EngineKind::Sampler;x.track().break_bars=2;x.load_sound(0,EngineKind::Sampler,0);if(x.track().break_bars)return gen_failure(68,__LINE__);
     x.track().engine=EngineKind::Midi;x.ui.gen8_mode=2;auto snap=std::make_unique<Snapshot>();s.snapshot(*snap,1);if(x.ui.gen8_mode!=0)return gen_failure(67,__LINE__);
    }
    // Physical Back follows the same hierarchy as the on-screen GEN BACK.
    {EngineHost h;Shell s(h);auto& x=s.context();x.load_sound(0,EngineKind::Sampler,0);
     auto press=[&](SurfaceButton b,bool down){SurfaceEvent e;e.input.kind=SurfaceInput::Kind::Button;e.input.index=uint8_t(b);e.input.pressed=down;s.surface(e,2000);};
     auto unchanged=x.patterns.at[0];auto seed=x.project.gen8.seed;
     for(unsigned mode:{1u,2u,3u,4u,5u}){
       x.ui.page=PageId::Patterns;x.ui.sheet_open=true;x.ui.gen8_mode=uint8_t(mode);x.ui.sheet_row=1;
       press(SurfaceButton::Shift,true);press(SurfaceButton::WheelPush,true);press(SurfaceButton::WheelPush,false);press(SurfaceButton::Shift,false);
       if(x.ui.page!=PageId::Patterns||!x.ui.sheet_open||x.ui.gen8_mode||x.ui.sheet_row)return gen_failure(230,__LINE__);
       if(x.patterns.at[0]!=unchanged||x.project.gen8.seed!=seed)return gen_failure(231,__LINE__);
       press(SurfaceButton::Shift,true);press(SurfaceButton::WheelPush,true);press(SurfaceButton::WheelPush,false);press(SurfaceButton::Shift,false);
       if(x.ui.sheet_open||x.ui.page!=PageId::Patterns)return gen_failure(232,__LINE__);
     }
    }
    // Native Edit starts at synthesis row two; Back returns to its caller.
    {EngineHost h;Shell s(h);auto& x=s.context();x.track().engine=EngineKind::Plugin;x.track().plugin=0;x.ui.page=PageId::Stage;
     x.open(PageId::Edit);if(x.ui.edit_page[x.ui.track]!=1)return gen_failure(215,__LINE__);
     x.back();if(x.ui.page!=PageId::Stage)return gen_failure(219,__LINE__);
    }
    // Entry always starts at GEN; browser cancel and load bars are contextual.
    {EngineHost h;Shell s(h);auto& x=s.context();x.ui.page=PageId::Patterns;x.ui.sheet_open=false;x.ui.gen8_mode=4;x.ui.sheet_row=1;
     x.ui.shift=true;patterns_page.soft(x,2);x.ui.shift=false;if(!x.ui.sheet_open||x.ui.gen8_mode||x.ui.sheet_row)return gen_failure(69,__LINE__);
     x.ui.sheet_row=5;patterns_page.push(x);if(x.ui.gen8_mode!=3)return gen_failure(70,__LINE__);
     x.apply(Binding{Binding::Kind::GenFirst,0,0,15},15);if(x.project.gen8.first!=15||x.project.gen8.last!=15)return gen_failure(71,__LINE__);
     x.apply(Binding{Binding::Kind::GenLast,0,0,15},2);if(x.project.gen8.first!=2||x.project.gen8.last!=2)return gen_failure(72,__LINE__);
     patterns_page.push(x);if(x.ui.gen8_mode||!x.ui.sheet_open)return gen_failure(73,__LINE__);
     x.ui.page=PageId::Samples;x.ui.break_browse=true;
     auto knobs=page_bindings(x.project,x.ui,x.pattern());if(knobs[0].kind!=Binding::Kind::GenBars)return gen_failure(74,__LINE__);
     samples_page.soft(x,0);if(x.ui.page!=PageId::Patterns||!x.ui.sheet_open||x.ui.gen8_mode!=2||x.ui.break_browse)return gen_failure(75,__LINE__);
     // Same start/end really changes one track, irrespective of the selection.
     for(auto& t:x.project.tracks){t.engine=EngineKind::Midi;t.layout=PadLayout::Scale;t.pattern=0;}
     x.project.gen8.assign=0;x.project.gen8.amount=100;x.project.gen8.first=x.project.gen8.last=11;x.ui.track=0;x.gen8_action(0);
     if(x.patterns.at[11][x.project.tracks[11].pattern].events.empty()){std::fprintf(stderr,"Single MIDI GEN: %s pattern=%u\n",x.ui.notice,x.project.tracks[11].pattern);return gen_failure(76,__LINE__);}
     for(unsigned t=0;t<track_count;t++)if(t!=11&&!x.patterns.at[t][0].events.empty())return gen_failure(77,__LINE__);
    }
    {EngineHost h;Shell s(h);auto& x=s.context();x.ui.page=PageId::Edit;x.track().engine=EngineKind::Plugin;x.track().plugin=0;
     std::strcpy(x.project.plugins[0].name,"Rhythmer Kit");std::strcpy(x.ui.plugin_panel.name,"Rhythmer Kit");x.ui.plugin_panel.entry=0;x.ui.plugin_panel.layout_revision=1;
     x.ui.edit_page[x.ui.track]=1;x.ui.edit_row[x.ui.track]=0;
     edit_page.soft(x,2);if(x.ui.edit_row[x.ui.track]!=1)return gen_failure(98,__LINE__);
     edit_page.soft(x,2);if(x.ui.edit_row[x.ui.track]!=2)return gen_failure(99,__LINE__);
     edit_page.soft(x,2);if(x.ui.edit_row[x.ui.track])return gen_failure(100,__LINE__);
     std::strcpy(x.ui.plugin_panel.name,"Prophet");edit_page.soft(x,2);if(x.ui.edit_row[x.ui.track]!=2)return gen_failure(101,__LINE__);
     edit_page.soft(x,2);if(x.ui.edit_row[x.ui.track])return gen_failure(102,__LINE__);
    }
    if(const char* card=std::getenv("RHYTHMER_GEN_CARD"))for(unsigned group=0;group<4;group++){
        EngineHost h;Shell s(h);auto& x=s.context();PluginRack rack(h);rack.set_card(card);x.plugins=&rack;
        for(unsigned t=0;t<track_count;t++){
            auto& track=x.project.tracks[t];track.engine=EngineKind::Midi;track.layout=PadLayout::Scale;track.pattern=0;
            auto& pattern=x.patterns.at[t][0];pattern.length=384;pattern.events.clear();Event marker;marker.tick=0;marker.key=uint8_t(48+t);marker.velocity=100;marker.length=24;pattern.add(marker);
        }
        x.project.gen8={42,2,85,50};x.project.gen8.assign=1;x.ui.page=PageId::Patterns;x.ui.sheet_open=true;x.ui.gen8_mode=3;
        x.apply(Binding{Binding::Kind::GenGroup,0,0,3},int(group));
        auto bindings=page_bindings(x.project,x.ui,x.pattern());if(bindings[0].kind!=Binding::Kind::GenGroup||bindings[0].value!=int(group))return gen_failure(78,__LINE__);
        patterns_page.push(x);if(x.ui.gen8_mode||x.project.gen8.group!=group)return gen_failure(90,__LINE__);
        patterns_page.soft(x,1);patterns_page.soft(x,1);if(x.ui.gen8_mode!=3||x.ui.sheet_row!=group)return gen_failure(91,__LINE__);
        patterns_page.wheel(x,1);patterns_page.push(x);if(x.project.gen8.group!=std::min(group+1,3u))return gen_failure(92,__LINE__);
        x.project.gen8.group=uint8_t(group);
        const auto before=std::make_unique<Project>(x.project);std::array<Pattern,track_count> original;
        for(unsigned t=0;t<track_count;t++)original[t]=x.patterns.at[t][0];
        const unsigned first=4*group;x.gen8_action(0);if(x.edits.size()!=1){std::fprintf(stderr,"Group GEN %u: %s\n",group,x.ui.notice);return gen_failure(79,__LINE__);}
        for(unsigned t=0;t<track_count;t++){
            if(t<first||t>=first+4){if(std::memcmp(&x.project.tracks[t],&before->tracks[t],sizeof(Track))||x.patterns.at[t][0].events!=original[t].events)return gen_failure(80,__LINE__);continue;}
            const auto& track=x.project.tracks[t];if(track.engine!=EngineKind::Plugin||track.plugin>=max_plugins)return gen_failure(81,__LINE__);
            const auto& plugin=x.project.plugins[track.plugin];
            if(t==first){if(std::strcmp(plugin.name,"Rhythmer Kit")||track.layout!=PadLayout::Drums)return gen_failure(82,__LINE__);}
            else if(!std::strcmp(plugin.name,"Rhythmer Kit")||std::string(plugin.name).find("Monk")!=std::string::npos)return gen_failure(83,__LINE__);
            for(unsigned tries=0;tries<9000&&rack.status(track.plugin)!=PluginStatus::Ready;tries++)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(rack.status(track.plugin)!=PluginStatus::Ready)return gen_failure(84,__LINE__);
        }
        x.gen_break_tick();auto after=std::make_unique<Project>(x.project);std::array<Pattern,4> generated;
        for(unsigned n=0;n<4;n++)generated[n]=x.patterns.at[first+n][0];
        x.undo();for(unsigned t=0;t<track_count;t++)if(std::memcmp(&x.project.tracks[t],&before->tracks[t],sizeof(Track))||x.patterns.at[t][0].events!=original[t].events)return gen_failure(85,__LINE__);
        x.redo();for(unsigned n=0;n<4;n++)if(std::memcmp(&x.project.tracks[first+n],&after->tracks[first+n],sizeof(Track))||x.patterns.at[first+n][0].events!=generated[n].events)return gen_failure(86,__LINE__);
        x.gen8_action(1);for(unsigned t=0;t<track_count;t++){
            auto expected=after->tracks[t];
            // MUTATE advances Acid's RNG state; the instrument and all sound controls stay unchanged.
            const auto& tr=x.project.tracks[t];
            if(t>=first&&t<first+4&&tr.engine==EngineKind::Plugin&&tr.plugin<max_plugins&&std::string(x.project.plugins[tr.plugin].name)=="Acid Engine")expected.acid_gen.seed=tr.acid_gen.seed;
            if(std::memcmp(&tr,&expected,sizeof(Track)))return gen_failure(87,__LINE__);

            if((t<first||t>=first+4)&&x.patterns.at[t][0].events!=original[t].events)return gen_failure(88,__LINE__);
        }
        std::printf("RHYTHMER_GEN_GROUP T%u-%u kit=T%u assignment=ok untouched=ok undo_redo=ok mutate=ok\n",first+1,first+4,first+1);x.plugins=nullptr;
    }
    // A stuck third-party voice ignoring MIDI must still be cut by Stop twice.
    {
        struct Stuck final:PluginRunner{void midi(uint8_t,uint8_t,uint8_t)override{}void silence()override{}void render(float* l,float* r,unsigned n)override{for(unsigned i=0;i<n;++i)l[i]=r[i]=0.2f;}} stuck;
        EngineHost h;Shell s(h);auto& x=s.context();x.project.tracks[0].engine=EngineKind::Plugin;x.project.tracks[0].plugin=0;h.load(x.project,x.patterns);h.offer_plugin(0,&stuck);
        std::array<int32_t,256> out{};h.render(out.data(),128,1);bool heard=false;for(auto v:out)heard|=v!=0;if(!heard)return gen_failure(46,__LINE__);
        SurfaceEvent e;e.input.kind=SurfaceInput::Kind::Button;e.input.index=unsigned(SurfaceButton::Stop);e.input.pressed=true;s.surface(e,100);e.input.pressed=false;s.surface(e,110);e.input.pressed=true;s.surface(e,300);
        if(std::strcmp(x.ui.notice,"ALL SOUND OFF"))return gen_failure(47,__LINE__);
        for(int i=0;i<8;++i){h.render(out.data(),128,1);for(auto v:out)if(v)return gen_failure(48,__LINE__);}
        h.send({Command::Type::NoteOn,0,0,60,100});h.render(out.data(),128,1);heard=false;for(auto v:out)heard|=v!=0;if(!heard)return gen_failure(49,__LINE__);
    }
    #include "rhythmer/shell/chord_gen_check.inc"
    #include "rhythmer/shell/break_acid_check.inc"
    std::printf("RHYTHMER_GEN8_SHELL original_fm1_four_tracks range=1-16 engine_gen_back=ok undo_redo=ok break_bars=1-4 unmapped=preserved single=C4 kit=canonical\n");return 0;
}
}
