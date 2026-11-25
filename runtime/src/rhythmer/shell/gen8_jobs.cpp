// SPDX-License-Identifier: GPL-3.0-only
// Native integration by jazzy rhythmer; musical sources in third_party/jiant-gen.
#include "rhythmer/shell/context.h"
#include "rhythmer/model/gen8.h"
#include "rhythmer/model/chord_gen_access.h"
#include "rhythmer/model/gen8_tables.h"
#include "rhythmer/model/gen_menu_order.h"
#include "rhythmer/model/native_gen_profiles.h"
#include "rhythmer/model/scales.h"
#include "rhythmer/plugin/plugin_rack.h"
#include <cctype>
#include "rhythmer/model/gen_engine_map.h"
#include "rhythmer/plugin/plugin_files.h"
#include "rhythmer/shell/track_copy.h"
#include "rhythmer/shell/undo_scope.h"
#include <filesystem>
#include "rhythmer/shell/fm1_gen_bridge.h"
#include <mutex>
#include "rhythmer/engine/engine_host.h"
#include <chrono>
#include "rhythmer/model/acid_gen.h"

namespace rhythmer {
namespace {
bool has(const std::string& s,const char* word) {return s.find(word)!=std::string::npos;}
std::string lower(std::string s) {for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
Gen8Voice voice_for(Context& c,unsigned at) {
    const auto& t=c.project.tracks[at]; Gen8Voice v;
    v.drums=t.engine==EngineKind::Sampler||drum_pads(t);
    if(t.engine==EngineKind::Plugin && t.plugin<max_plugins) {
        auto name=lower(std::string(c.project.plugins[t.plugin].name)+" "+c.project.plugins[t.plugin].path);
        for(size_t pos=0;(pos=name.find("groove - ",pos))!=std::string::npos;pos+=11)name.replace(pos,9,"rhythmer - ");
        v.mono=has(name,"303")||has(name,"mono")||has(name,"kwasub");
        if(has(name,"303")){v.low=24;v.high=84;}
        const auto* profile=native_gen_profile(name);
        for(const auto& e:gen_engines)if(has(name,lower(e.folder).c_str())){v.acid=e.acid;v.drums=e.drums;v.mono=e.mono;v.low=e.low;v.high=e.high;break;}
        if(profile){v.mono=profile->mono;v.low=profile->low;v.high=profile->high;v.drums|=profile->drums;}
        if(has(name,"acid engine")){v.acid=true;v.drums=false;v.mono=true;v.low=24;v.high=84;}
        if((has(name,"rhythmer - rhythmer kit")||has(name,"groove - groove kit"))) {
            v.drums=true;v.drum_keys={{36,37,38,39,40,41,42,43}};
        } else {
            // A single sound can serve any GEN role. Percussion tracks trigger
            // its C4 reference; only Rhythmer Kit receives multiple drum keys.
            if(c.plugins&&has(name,"hortator")){PluginPanel panel;c.plugins->panel(t.plugin,0,panel);if(std::string(panel.params[2].text)=="Key Pitch"){v.drums=false;v.acid=true;v.mono=true;v.low=36;v.high=84;}}
            if(c.plugins){PluginPanel panel;c.plugins->panel(t.plugin,0,panel);if(std::string(panel.params[2].text)=="Pitch + Length")v.drums=true;}
            v.drum_keys.fill(255);
            if(v.drums){std::string sound=name;if(c.plugins){PluginPanel panel;c.plugins->panel(t.plugin,0,panel);sound+=" "+lower(panel.preset);}
                unsigned lane=has(sound,"snare")?1:has(sound,"clap")?2:has(sound,"open hat")?4:(has(sound,"hat")||has(sound,"hihat"))?3:has(sound,"tom")?5:(has(sound,"rim")||has(sound,"stick"))?6:(has(sound,"cymbal")||has(sound,"ride")||has(sound,"crash"))?7:0;
                if(c.plugins){PluginPanel panel;c.plugins->panel(t.plugin,0,panel);auto preset=lower(panel.preset);if(preset.size()>2&&preset.substr(preset.size()-2)==" *")preset.resize(preset.size()-2);
                    for(const auto& r:gen_sound_roles){auto match=lower(r.sound);if(has(name,lower(r.folder).c_str())&&(preset==match||(preset.size()>match.size()&&preset.compare(preset.size()-match.size(),match.size(),match)==0)))lane=r.lane;}}
                v.drum_keys[lane]=60;
            }
        }

    } else if(t.engine==EngineKind::Sampler) {
        for(unsigned i=0;i<8;i++) {
            unsigned p=16*(t.bank%sampler_banks)+i;
            v.drum_keys[i]=t.pads[p].used()?uint8_t(sampler_first_note+p):255;
        }
    } else if(t.engine==EngineKind::Midi&&v.drums) {
        for(auto& key:v.drum_keys)key=t.midi.drum_note[(key-36)%sampler_pads];
    }
    return v;
}
}
Gen8Voice gen_track_voice(Context& c,unsigned at){return voice_for(c,at);}
void Context::gen8_action(unsigned action) {
    if(action==10&&track().engine==EngineKind::Sampler&&track().break_bars){ui.sheet_open=false;open(PageId::Edit);ui.edit_page[ui.track]=0;ui.break_perf_page=0;return;}
    if(host_recording()) {notice("Stop recording before GEN");return;}
    if(action==13&&frozen(ui.track))return; // automatic Acid never edits a frozen print
    if(action<2){
        const auto& settings=project.gen8;
        unsigned first=settings.assign?gen_song_first(settings):std::min<unsigned>(settings.first,settings.last);
        unsigned last=settings.assign?first+4:std::max<unsigned>(settings.first,settings.last)+1;
        for(unsigned t=first;t<std::min(last,track_count);++t)if(frozen_edit(t))return;
    }else if(frozen_edit(ui.track))return; // before seed, native state or MIDI changes
    if(action!=13)close_gesture();
    if((action==2||action==3||action==12||action==13)&&voice_for(*this,ui.track).acid){
        if(host.song_playing()){notice("Stop song before ACID GEN");return;}
        if(pattern().steps()>64){notice("ACID GEN: use 1-64 steps");return;}
        const bool automatic=action==13,mutate=action==3||automatic;
        Edit edit;edit.what=mutate?"Mutate ACID":"Generate ACID";
        if(!automatic)edit.tracks_before.push_back({ui.track,track_state(*this,ui.track)});
        if(action==2){auto& seed=track().acid_gen.seed;
            auto ms=uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
            seed=(ms^uint32_t(host.blocks_rendered())*2654435761u^seed)*1664525u+1013904223u;if(!seed)seed=1;}
        pattern()=acid_generate_pattern(pattern(),track().acid_gen,mutate,project.root,scales()[project.scale%scales().size()].mask);
        sync_pattern(ui.track,track().pattern);dirty=true;
        if(!automatic){edit.tracks_after.push_back({ui.track,track_state(*this,ui.track)});edits.push(std::move(edit));}
        if(!automatic)notice(action==3?"303 MUTATED":action==12?"303 SEED REPLAY":"NEW 303 LINE");
        return;
    }
    if(rhythmer_fm1_bridge_size()!=sizeof(Fm1GenResult)){notice("GEN: bridge version mismatch");return;}
    auto settings=project.gen8;
    auto musical_settings=settings;musical_settings.genre=uint8_t(settings.genre<2?(settings.seed*31u)%gen8_genre_count:gen_fm1_menu[(settings.genre-2)%gen8_genre_count]);
    static std::mutex fm1_lock;std::lock_guard<std::mutex> fm1_guard(fm1_lock);
    auto fm1=std::make_unique<Fm1GenResult>();
    // Original FM-1 generator owns the seed streams, harmony and role choices.
    if(action==9){if(track().engine!=EngineKind::Sampler||!track().break_bars){notice("Select Break Engine");return;}ui.break_load_b=false;ui.break_browse=true;ui.break_track=ui.track;ui.break_bars=track().break_bars;open(PageId::Samples);notice("K1: loop bars / Push: load");return;}
    if(action>=4) {
        if(track().engine!=EngineKind::Sampler||!track().break_bars){notice("BREAK: select a sampler track");return;}
        BreakMap map;map.bars=track().break_bars?track().break_bars:2;
        for(unsigned i=0;i<16;i++) {
            unsigned p=16*(track().bank%sampler_banks)+i;
            if(!track().pads[p].used()){notice("Load a loop: BROWSE SAMPLE");return;}
            map.slices[i]=uint8_t(sampler_first_note+p);
        }
        static constexpr BreakAction actions[] {BreakAction::Genre,BreakAction::Straight,BreakAction::Random,BreakAction::Mutate,BreakAction::Fill};
        auto made=break_generate(pattern(),actions[(action-4)%5],musical_settings,map,project.tempo/10);
        edit_pattern(ui.track,track().pattern,"Break GEN",[&]{pattern()=std::move(made);sync_pattern(ui.track,track().pattern);});
        if(action<=6)track().last_note=map.slices[0];
        notice("BREAK: "+std::string(gen_menu_name(settings.genre)));return;
    }
    rhythmer_fm1_generate(settings.genre,settings.seed,settings.amount,settings.complexity,settings.assign&&action==0,fm1.get());
    settings.seed=project.gen8.seed=uint16_t(fm1->seed);
    unsigned from=action<2&&settings.assign?gen_song_first(settings):action<2?std::min<unsigned>(settings.first,settings.last):ui.track;
    unsigned to=action<2&&settings.assign?from+4:action<2?std::max<unsigned>(settings.first,settings.last)+1:ui.track+1;
    to=std::min(to,track_count);
    // Prepare every assignment before touching any track. Old entries remain
    // intact so Undo can restore the old sound and its original state.
    std::vector<std::pair<unsigned,PluginFile>> choices;std::vector<unsigned> slots;
    if(settings.assign&&action<2&&action%2==0){
        if(!plugins){notice("GEN: plugins unavailable");return;}
        for(unsigned t=from;t<to;t++){
            const unsigned local=t-from;
            std::vector<const GenEngine*> candidates;
            unsigned native_role=fm1->role[local];unsigned slot=native_role==1?0:native_role==2?1:2;
            for(const auto& e:gen_engines)if(local==0?has(lower(e.folder),"rhythmer - rhythmer kit"):(!e.drums&&!has(lower(e.folder),"monk")&&!(slot==1&&e.mono)&&!(slot!=0&&e.acid)&&(e.programs[slot]>=0||e.acid)))candidates.push_back(&e);
            if(local>0){unsigned recipe=fm1->recipe[local]%28;std::vector<const GenEngine*> matched;
                for(auto* e:candidates)if(e->recipes[recipe]>=0&&(recipe!=1||std::string(e->name)=="Acid Engine"))matched.push_back(e);
                if(!matched.empty())candidates=std::move(matched);
            }
            if(candidates.empty()){notice("GEN: no compatible role engine");return;}
            unsigned start=(settings.seed*31u+settings.genre*17u+local)%candidates.size();
            PluginFile chosen;bool found=false;
            for(unsigned n=0;n<candidates.size();n++){
                const auto& e=*candidates[(start+n)%candidates.size()];
                auto path=(std::filesystem::path(plugins->source_folder())/e.folder).string();
                if(!std::filesystem::exists(path))continue;
                chosen=describe_plugin(path);if(chosen.loadable()){chosen.gen_program=local==0?gen_kit_programs[fm1->recipe[0]%8]:(e.recipes[fm1->recipe[local]%28]>=0?e.recipes[fm1->recipe[local]%28]:e.programs[slot]);found=true;break;}
            }
            if(!found){notice("GEN: missing engine files");return;}
            choices.push_back({t,chosen});
        }
        auto available=std::make_unique<Project>(project);
        for(const auto& choice:choices)available->tracks[choice.first].engine=EngineKind::None;
        for(unsigned e=0;e<max_plugins;e++)if(!plugin_used_by(*available,e))slots.push_back(e);
        if(slots.size()<choices.size()){notice("GEN: not enough free plugin slots");return;}
    }
    Edit edit;edit.what=action%2?"Mutate GEN":"Generate GEN";
    unsigned skipped=0;
    if(settings.assign&&action==0){
        edit.tempo_before=std::make_unique<TempoState>(tempo_of(project));
        edit.gen_key_changed=true;edit.gen_key_before={project.root,project.scale,project.tempo};unsigned mode=chromatic_scale;
        for(unsigned i=0;i<scales().size();i++)if(scales()[i].mask==fm1->scale){mode=i;break;}
        edit.gen_key_after={uint16_t(fm1->root%12),uint16_t(mode),uint16_t(fm1->tempo*10)};
        project.root=uint8_t(edit.gen_key_after[0]);project.scale=uint8_t(mode);project.tempo=edit.gen_key_after[2];
        // GEN owns one complete undo. The native set_tempo() opens a separate
        // gesture, so send its model/host changes here with the GEN snapshot.
        send({Command::Type::Tempo,0,0,0,int16_t(project.tempo)});
        if(project.seq_tempo){project.pattern_tempo[ui.lead_pattern%pattern_count]=project.tempo;
            send({Command::Type::PatternTempo,0,0,ui.lead_pattern,int16_t(project.tempo)});}
        edit.tempo_after=std::make_unique<TempoState>(tempo_of(project));
    }
    if(!choices.empty()){
        auto states=plugins->states();std::array<bool,max_plugins> kept{};
        for(const auto& choice:choices){const auto& tr=project.tracks[choice.first];if(tr.engine==EngineKind::Plugin&&tr.plugin<max_plugins)kept[tr.plugin]=true;}
        for(unsigned i=0;i<choices.size();i++)kept[slots[i]]=true;
        for(unsigned e=0;e<max_plugins;e++)if(kept[e])edit.gen_plugins_before.push_back({e,project.plugins[e],states[e]});
        for(unsigned i=0;i<choices.size();i++)plugins->set_gen_state(slots[i],{});
    }
    for(unsigned i=0;i<choices.size();i++){
        unsigned t=choices[i].first,entry=slots[i];const auto& file=choices[i].second;
        edit.tracks_before.push_back({uint8_t(t),track_state(*this,t)});
        auto next=track_state(*this,t);next.track.engine=EngineKind::Plugin;next.track.plugin=uint8_t(entry);next.track.layout=t==from?PadLayout::Drums:PadLayout::Scale;next.track.break_bars=0;if(t==from){next.track.bank=0;next.track.last_pad=0;next.track.last_note=36;}
        if(file.single_drum){next.track.octave=4;}
        next.track.set_name(file.name.c_str());
        auto& ref=project.plugins[entry];std::snprintf(ref.path,plugin_path_length,"%s",file.path.c_str());std::snprintf(ref.name,sizeof(ref.name),"%s",file.name.c_str());ref.id=file.id;ref.gen_program=file.gen_program;
        restore_track(*this,t,next);
    }
    if(!choices.empty()){
        for(const auto& before:edit.gen_plugins_before){auto state=before;for(unsigned i=0;i<choices.size();i++)if(slots[i]==state.entry){state.ref=project.plugins[state.entry];state.state.clear();}edit.gen_plugins_after.push_back(std::move(state));}
        plugins->sync(project,plugins_asleep());
    }
    for(unsigned t=from;t<to;t++) {
        const auto& tr=project.tracks[t]; auto role=Gen8Role(t%4==1?0:t%4==2?1:2);
        if(!tr.used()&&settings.assign){++skipped;continue;}
        if(tr.engine==EngineKind::Plugin&&(tr.plugin>=max_plugins||!project.plugins[tr.plugin].used())){++skipped;continue;}
        auto v=voice_for(*this,t);
        if(v.drums){unsigned lane=0;while(lane<8&&v.drum_keys[lane]==255)++lane;if(lane==8){++skipped;continue;}role=lane==0?Gen8Role::Kick:lane<3?Gen8Role::Snare:lane<5?Gen8Role::Hat:Gen8Role::Percussion;}
        auto& p=patterns.at[t][tr.pattern];
        Pattern made;
        if(tr.break_bars&&tr.engine==EngineKind::Sampler){
            BreakMap map;map.bars=tr.break_bars;bool ready=true;
            for(unsigned i=0;i<16;i++){unsigned pad=tr.bank*16+i;ready&=tr.pads[pad].used();map.slices[i]=uint8_t(sampler_first_note+pad);}
            if(!ready){++skipped;continue;}
            made=break_generate(p,action%2?BreakAction::Mutate:BreakAction::Genre,musical_settings,map,project.tempo/10);
        }else if(v.acid&&!(settings.assign&&action==0)){
            // Acid's RNG stream is track state even when GEN only replaces MIDI.
            // Keep its state in the same undo as the generated notes.
            edit.tracks_before.push_back({uint8_t(t),track_state(*this,t)});
            auto& acid=project.tracks[t].acid_gen;
            made=acid_generate_pattern(p,acid,action%2,project.root,scales()[project.scale%scales().size()].mask);
        }
        else if(action%2)made=mutate8(p,role,musical_settings,v,project.root,project.scale);
        else {
            made=p;made.events.clear();made.euclids.clear();
            unsigned source=v.drums?0:t%4?t%4:1;
            // Original song arrangements may intentionally leave a melodic role
            // empty. A requested MIDI-only / single-track generation reuses an
            // active original melodic role instead of silently creating no notes.
            if(!v.drums&&!(settings.assign&&action==0)){
                auto active=[&](unsigned track){for(unsigned n=0;n<fm1->count;n++)if(fm1->events[n].track==track)return true;return false;};
                if(!active(source))for(unsigned candidate=1;candidate<4;candidate++)if(active(candidate)){source=candidate;break;}
            }
            const unsigned original_phrase=std::max(1u,fm1->track_ticks[source]),bar_ticks=project.bar_ticks();
            const unsigned phrase=bar_ticks==384?original_phrase:((original_phrase+383)/384)*bar_ticks;
            made.length=settings.length?settings.length*bar_ticks:settings.assign&&action==0?phrase:p.length;
            if(settings.assign&&action==0)made.feel=Feel{};
            auto place=[&](unsigned raw,bool ending){unsigned bar=raw/384,pos=raw%384;if(bar_ticks==384)return raw;
                if(pos>=bar_ticks){return ending?(bar+1)*bar_ticks:UINT32_MAX;}
                return bar*bar_ticks+pos;};
            for(unsigned d=0;d<unsigned(Division::Count);d++)if(division_ticks(Division(d))==fm1->grid[source]){made.division=Division(d);break;}
            for(unsigned offset=0;offset<made.length;offset+=phrase)for(unsigned n=0;n<fm1->count;n++){const auto& original=fm1->events[n];if(original.track!=source)continue;
                unsigned raw=original.tick;
                // The FM-1 bridge has already baked its genre swing into ticks.
                // With an explicit project swing, import its straight step plus
                // human microtiming; the native shared clock supplies swing once.
                if(project.swing>50){int64_t straight=int64_t(original.step)*fm1->grid[source]+int64_t(original.micro)*fm1->grid[source]/64;raw=unsigned(std::max<int64_t>(0,straight));}
                unsigned at=place(raw,false);if(at==UINT32_MAX||offset+at>=made.length)continue;
                Event event;event.tick=offset+at;
                event.key=v.drums?v.drum_keys[original.key%8]:uint8_t(settings.assign&&action==0?std::clamp<int>(original.key,0,127):gen_follow_note(original.key,fm1->root,fm1->scale,project.root,scales()[project.scale%scales().size()].mask,v));if(event.key==255)continue;
                event.velocity=std::max<uint8_t>(1,original.velocity);unsigned finish=place(raw+original.ticks,true);unsigned duration=finish>at?finish-at:1;
                event.length=uint16_t(std::max(1u,std::min<unsigned>(duration,made.length-event.tick)));event.ratchet=original.ratchet;event.chance=original.chance;
                if(original.flags&1)event.flags|=step_accent;
                if(original.flags&2)event.flags|=step_slide;
                made.add(event);
            }made.sort();
            if(v.drums&&action==2&&made.events.empty()){
                // Some original song arrangements deliberately omit drums. A
                // dedicated kit-track request uses the same genre's native drum
                // vocabulary instead of erasing an existing rhythm with silence.
                Pattern source_pattern=made;
                for(unsigned lane=4;lane<8;lane++){
                    auto part=generate8(source_pattern,Gen8Role(lane),musical_settings,v,project.root,project.scale);
                    for(const auto& event:part.events)made.add(event);
                }
                made.sort();
                if(made.events.empty()){notice(std::string(gen_menu_name(settings.genre))+" / no drum part");return;}
            }
        }
        if(made==p)continue;
        edit.patterns_before.push_back({uint8_t(t),tr.pattern,p});
        edit.patterns_after.push_back({uint8_t(t),tr.pattern,std::move(made)});
    }
    if(edit.patterns_after.empty()&&choices.empty()&&edit.tracks_before.empty()){notice("No change: seed / track setup");return;}
    for(const auto& p:edit.patterns_after){patterns.at[p.track][p.pattern]=p.value;sync_pattern(p.track,p.pattern);}
    for(const auto& before:edit.tracks_before)edit.tracks_after.push_back({before.first,track_state(*this,before.first)});
    edits.push(std::move(edit));
    const auto target=action<2?(settings.assign?"T"+std::to_string(from+1)+"-"+std::to_string(to):"T"+std::to_string(from+1)+"-"+std::to_string(to)):"T"+std::to_string(ui.track+1);
    notice(target+(action%2?" mutated":" generated")+(skipped?" / skipped unready":" / UNDO available"));
}
} // namespace rhythmer
