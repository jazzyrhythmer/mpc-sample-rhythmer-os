// SPDX-License-Identifier: GPL-3.0-only
#include "rhythmer/shell/context.h"
#include "rhythmer/shell/track_copy.h"
#include "rhythmer/engine/sampler.h"
#include <cmath>
#include "rhythmer/shell/ask.h"
#include "rhythmer/engine/engine_host.h"
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/plugin/plugin_rack.h"
#include "rhythmer/engine/engine_info.h"
namespace rhythmer {
void Context::gen_break_load(const std::string& path,const std::string& name){
    if(host_recording()){notice("Stop recording before BREAK");return;}
    if(frozen_edit(ui.track))return;
    int slot=sample_slot(path,name);if(slot<0){notice("No sample slot");return;}
    ui.break_pending=slot;ui.break_pending_b=ui.break_load_b;ui.break_track=ui.track;ui.break_bank=track().bank;ui.break_bars=ui.break_load_b?project.tracks[ui.break_track].break_bars:ui.break_bars;
    ui.break_job_track=int8_t(ui.break_track);ui.break_job_bank=ui.break_bank;ui.break_job_bars=ui.break_bars;ui.break_job_b=ui.break_pending_b;ui.break_job_from_picker=ui.break_from_picker;
    if(!ui.break_job_b && track().engine==EngineKind::Sampler && track().break_bars && track().break_bars!=ui.break_job_bars){
        ask(*this,UiState::Ask::BreakLength,ui.track);return;
    }
    gen_break_begin();
}
void Context::gen_break_begin(){
    ui.break_from_picker=false;ui.break_browse=false;open(PageId::Patterns);ui.sheet_open=true;ui.gen8_mode=2;ui.sheet_row=0;notice("Loading BREAK sample");
}
void Context::gen_break_tick(){
    for(unsigned t=0;t<track_count;t++)if(project.tracks[t].break_bars&&!frozen(t))project.tracks[t].break_source=uint8_t(host.break_source_of(t));
    for(unsigned t=0;t<track_count;t++)if(project.tracks[t].break_bars&&!frozen(t)){unsigned variation=0,drift=0;if(host.break_feedback(t,ui.break_feedback_seen[t],variation,drift)){project.tracks[t].break_perf[1]=uint16_t(variation);project.tracks[t].break_perf[2]=uint16_t(drift);}}
    unsigned bar=host.ticks()/project.bar_ticks();
    if(!host.playing())acid_auto_bar=~0u;
    else if(bar!=acid_auto_bar){acid_auto_bar=bar;
        if(bar&&!host_recording()&&!host.song_playing()){
            auto selected=ui.track;
            for(unsigned t=0;t<track_count;t++){const auto& tr=project.tracks[t];
                if(!frozen(t)&&tr.engine==EngineKind::Plugin&&tr.plugin<max_plugins&&std::string(project.plugins[tr.plugin].name)=="Acid Engine"&&tr.acid_gen.auto_bars&&bar%tr.acid_gen.auto_bars==0&&!tr.mix.mute&&patterns.at[t][tr.pattern].steps()<=64){ui.track=uint8_t(t);gen8_action(13);}}
            ui.track=selected;
        }
    }
    if(plugins)for(unsigned e=0;e<max_plugins;e++)if(project.plugins[e].gen_program>=0&&plugins->status(e)==PluginStatus::Ready){plugins->select_program(e,project.plugins[e].gen_program);project.plugins[e].gen_program=-1;}
    if(ui.break_pending<0||host_recording()||ui.ask==UiState::Ask::BreakLength)return;
    unsigned slot=unsigned(ui.break_pending),at=ui.break_job_track>=0?unsigned(ui.break_job_track):ui.break_track;
    unsigned job_bars=ui.break_job_track>=0?ui.break_job_bars:ui.break_bars;
    unsigned job_bank=ui.break_job_track>=0?ui.break_job_bank:ui.break_bank;
    bool job_b=ui.break_job_track>=0?ui.break_job_b:ui.break_pending_b;
    bool from_picker=ui.break_job_track>=0?ui.break_job_from_picker:ui.break_from_picker;
    auto fail_load=[&](const char* message){
        ui.break_pending=-1;ui.break_job_track=-1;
        if(ui.track==at && ui.page==PageId::Patterns && ui.sheet_open && ui.gen8_mode==2){
            ui.break_load_b=job_b;ui.break_track=uint8_t(at);ui.break_bars=uint8_t(job_bars);
            ui.break_browse=true;ui.break_from_picker=from_picker;open(PageId::Samples);
        }
        notice(message);
    };
    if(slot>=sample_slots||at>=track_count){fail_load("BREAK: invalid load target");return;}
    const auto& sample=project.samples[slot];if(!sample.frames){if(library){auto loaded=library->slot(slot);if(!loaded.error.empty()){fail_load("BREAK: sample load failed");}}return;}
    ui.break_pending=-1;ui.break_job_track=-1;
    if(frozen(at)){notice("BREAK load cancelled: frozen track");return;} // no late Unfreeze dialog on another screen
    unsigned bars=std::clamp<unsigned>(job_bars,1,4);
    if(sample.instrument()||sample.frames<16){fail_load("BREAK needs an audio loop");return;}
    double bpm=bars*4.0*60.0*44100.0/sample.frames;
    if(bpm*10<1||bpm*10>65535){fail_load("BREAK sample duration out of range");return;}
    if(job_b&&project.tracks[at].break_bars){
        Edit edit;edit.what="Load BREAK B";edit.track=uint8_t(at);edit.track_before=std::make_unique<TrackState>(track_state(*this,at));
        auto next=std::make_unique<TrackState>(*edit.track_before);auto& t=next->track;
        auto& layer=t.pads[(job_bank%sampler_banks)*16].layers[0];layer.sample=uint16_t(slot);layer.start=0;layer.end=sample.frames;
        restore_track(*this,at,*next);edit.track_after=std::move(next);edits.push(std::move(edit));dirty=true;
        if(ui.track==at && ui.page==PageId::Patterns && ui.sheet_open && ui.gen8_mode==2){open(PageId::Edit);ui.edit_page[at]=0;ui.break_perf_page=2;}notice("BREAK B ready");return;
    }
    Edit edit;edit.what="Load BREAK";edit.track=uint8_t(at);
    edit.track_before=std::make_unique<TrackState>(track_state(*this,at));
    if(plugins && project.tracks[at].engine==EngineKind::Plugin && project.tracks[at].plugin<max_plugins){
        unsigned entry=project.tracks[at].plugin;auto states=plugins->states();
        edit.gen_plugins_before.push_back({entry,project.plugins[entry],states[entry]});
        edit.gen_plugins_after=edit.gen_plugins_before;
    }
    auto next=std::make_unique<TrackState>(*edit.track_before);auto& t=next->track;
    const bool replacing=t.engine==EngineKind::Sampler && t.break_bars;
    const bool changed_bars=replacing && t.break_bars!=bars;
    if(!replacing)rhythmer::load_sound(t,EngineKind::Sampler,0);
    t.break_bars=uint8_t(bars);t.bank=job_bank%sampler_banks;if(!replacing){t.layout=PadLayout::Drums;t.set_name("BREAK");t.last_note=uint8_t(sampler_first_note+t.bank*16);}
    if(!replacing){t.params[sampler::Follow]=1;t.params[sampler::FollowHow]=0;}
    if(changed_bars)t.break_source=0;
    for(unsigned i=0;i<16;i++){
        PadSound sound=replacing?t.pads[t.bank*16+i]:PadSound{};
        // Retain sound design, but source-relative trim/loop and secondary layers
        // must not point outside the new loop. B is retained only at the same length.
        if(changed_bars || !replacing || i!=0) sound.layers={};
        sound.loop_lock=true;sound.loop_start=0;
        sound.sample=uint16_t(slot);sound.start=uint32_t(uint64_t(sample.frames)*i/16);sound.end=uint32_t(uint64_t(sample.frames)*(i+1)/16);
        sound.own_tempo=uint16_t(std::lround(bpm*10));if(!replacing){sound.play=PadPlay::OneShot;sound.group=16;sound.mono=true;}
        t.pads[t.bank*16+i]=sound;
    }
    auto& pat=next->patterns[t.pattern];
    if(!replacing && pat.events.empty() && pat.lanes.empty() && pat.motion.empty() && pat.euclids.empty() && pat.punches.empty()){
        pat.length=bars*project.bar_ticks();
        BreakMap map;map.bars=bars;for(unsigned i=0;i<16;i++)map.slices[i]=uint8_t(sampler_first_note+t.bank*16+i);
        pat=break_generate(pat,BreakAction::Straight,project.gen8,map,project.tempo/10);
    }
    restore_track(*this,at,*next);edit.track_after=std::move(next);edits.push(std::move(edit));dirty=true;
    // Keep the loading screen as a stable BREAK menu; never take focus back
    // after the player changes track, closes GEN, or opens another GEN mode.
    if(ui.track==at && ui.page==PageId::Patterns && ui.sheet_open && ui.gen8_mode==2)ui.sheet_row=6;
    notice(replacing ? "BREAK A replaced / patterns kept" : "BREAK ready / GEN edits patterns");
}
}
