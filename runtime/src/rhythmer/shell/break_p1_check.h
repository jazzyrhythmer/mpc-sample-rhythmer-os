// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "rhythmer/shell/gen8_check.h"
#include "rhythmer/pages/break_status.h"
#include "rhythmer/shell/sample_library.h"
#include <filesystem>
#include <fstream>
namespace rhythmer {
inline int break_p1_check(){
    std::setvbuf(stdout,nullptr,_IOLBF,0);
    const char* where=std::getenv("RHYTHMER_BREAK_QA");
    if(!where){std::fprintf(stderr,"Set RHYTHMER_BREAK_QA to the registered SSD QA directory\n");return 1;}
    const std::string root=where;
    auto fail=[](int n){std::fprintf(stderr,"BREAK_P1 failure=%d\n",n);return n;};
    SampleData tone;tone.frames=176400;tone.left.resize(tone.frames);
    for(unsigned n=0;n<tone.frames;++n)tone.left[n]=.22f*std::sin(2*dsp::pi*220*n/44100.f);
    tone.right=tone.left; // decoded mono files have two populated sides in SampleData
    // Preview works before assignment, on MIDI/plug-in tracks, and on an existing
    // Break; no track, pattern, insert, send or stem is used for monitoring.
    for(auto kind:{EngineKind::None,EngineKind::Midi,EngineKind::Plugin,EngineKind::Sampler}){
        auto host_owner=std::make_unique<EngineHost>();auto& h=*host_owner;Shell shell(h);auto& c=shell.context();
        for(auto& t:c.project.tracks)t.engine=EngineKind::None;
        c.track().engine=kind;c.track().mix.level=0;
        if(kind==EngineKind::Sampler){c.track().break_bars=2;c.track().pads[0].sample=0;}
        auto saved=save_project(c.project,c.patterns);h.load(c.project,c.patterns);
        h.offer_sample(0,&tone);h.offer_sample(preview_slot,&tone);h.watch_taps(0);
        Command a{Command::Type::Audition,0,0,1,100};a.pad.sample=preview_slot;h.send(a);
        std::array<int32_t,256> audio{};std::array<float,track_count*128*2> stems{};double energy=0;
        for(unsigned b=0;b<40;++b){h.render(audio.data(),128,1,nullptr,stems.data());for(auto v:audio)energy+=double(v)*v;}
        if(energy<1||save_project(c.project,c.patterns)!=saved)return fail(11);
        for(float v:stems)if(v!=0)return fail(12);
        TrackTaps taps;if(!h.taps(taps)||!taps.count||taps.voice[0].slot!=preview_slot)return fail(13);
        a.pad.sample=no_sample;h.send(a);h.render(audio.data(),128,1);h.render(audio.data(),128,1);
        for(auto v:audio)if(v)return fail(14);
        a.pad.sample=preview_slot;h.send(a);h.render(audio.data(),128,1);
        Command panic{Command::Type::AllNotesOff,255,0,1};h.send(panic);h.render(audio.data(),128,1);
        for(auto v:audio)if(v)return fail(15);
    }
    std::puts("BREAK_P1_MONITOR targets=EMPTY,MIDI,PLUGIN,BREAK isolated_from_tracks_and_stems=ok stop_panic=ok taps=ok");
    // Existing sampler Warp/Beats is reused, and only after the file is complete.
    // Tone frequency remains 220 Hz while duration follows source/project tempo.
    for(unsigned tempo:{600u,1200u,2400u}){
        SamplerEngine sampler;SamplerEngine::Table table{};table[preview_slot]=&tone;sampler.set_samples(&table);sampler.set_tempo(tempo);
        sampler.audition(preview_slot,0,0,100,-1,1200);
        unsigned last=0,crossings=0;float previous=0;double square=0;unsigned measured=0;
        std::vector<float> result;const unsigned expected=unsigned(uint64_t(tone.frames)*1200/tempo);
        std::array<float,128> l{},r{};
        for(unsigned at=0;at<expected+44100;at+=128){
            l.fill(0);r.fill(0);sampler.render_stereo(l.data(),r.data(),128);
            for(unsigned n=0;n<128;++n){unsigned frame=at+n;float v=l[n];if(std::fabs(v)>1e-5)last=frame;
                if(frame>4410&&frame<std::min(expected-4410,44100u)){crossings+=previous<=0&&v>0;square+=v*v;++measured;}previous=v;result.push_back(v);}
        }
        double hz=double(crossings)*44100/std::max(1u,measured);
        if(last<expected-9000 || last>expected+9000 || hz<200||hz>240||square<.01)return fail(16);
        SampleData audio;audio.frames=unsigned(result.size());audio.left=std::move(result);audio.right=audio.left;
        if(!write_wav(root+"/Rendered/preview-"+std::to_string(tempo/10)+"bpm.wav",audio).empty())return fail(17);
        std::printf("BREAK_P1_SYNC tempo=%u duration_expected=%.3f last=%.3f pitch=%.2f_Hz\n",tempo,expected/44100.,last/44100.,hz);
    }
    // Offering an unrelated sample (especially preview_slot) must not reset a
    // playing Break. Compare identical native DSP state, sample for sample.
    {
        SamplerEngine a,b;SamplerEngine::Table table{};table[0]=&tone;a.set_samples(&table);b.set_samples(&table);
        for(unsigned n=0;n<16;++n){PadSound pad;pad.sample=0;pad.start=tone.frames*n/16;pad.end=tone.frames*(n+1)/16;a.set_pad(n,pad);b.set_pad(n,pad);}
        a.configure_break(2,0);b.configure_break(2,0);
        std::array<float,128> al{},ar{},bl{},br{};
        a.render_stereo(al.data(),ar.data(),128);b.render_stereo(bl.data(),br.data(),128);
        a.note_on(36,110);b.note_on(36,110);double energy=0;
        for(unsigned block=0;block<80;++block){
            if(block==8){b.sample_gone(preview_slot);b.sample_gone(1);}
            al.fill(0);ar.fill(0);bl.fill(0);br.fill(0);a.render_stereo(al.data(),ar.data(),128);b.render_stereo(bl.data(),br.data(),128);
            if(al!=bl||ar!=br)return fail(18);
            for(float v:al)energy+=v*v;
        }
        if(energy<.01)return fail(58);
    }
    // Real decoder/streaming metadata, all native formats, cancellation and rapid changes.
    {
        auto host_owner=std::make_unique<EngineHost>();auto& h=*host_owner;Shell shell(h);SampleLibrary library(h);shell.set_library(&library);auto saved=save_project(shell.project(),shell.patterns());
        for(const auto& ext:{"wav","flac","ogg","mp3","aiff"}){
            std::string path=root+"/Fixtures/2bar-aligned."+ext;library.preview(path);
            for(unsigned n=0;n<100;++n){library.preview_service();std::array<int32_t,256> audio{};h.render(audio.data(),128,1);auto info=library.preview_info();if(info.path==path&&info.complete)break;}
            auto info=library.preview_info();if(info.path!=path||!info.complete||std::fabs(info.seconds-4.)>.06)return fail(19);
        }
        library.preview(root+"/Fixtures/4bar-aligned.wav");library.preview_service();
        library.preview(root+"/Fixtures/1bar-aligned.wav");library.preview_service();
        library.preview(root+"/Fixtures/not-a-file.wav");library.preview_service();
        if(!library.preview_info().complete||library.preview_info().seconds>0)return fail(57);
        library.preview("");library.preview_service();if(!library.preview_info().path.empty())return fail(20);
        if(save_project(shell.project(),shell.patterns())!=saved)return fail(21);
    }
    std::puts("BREAK_P1_STREAM formats=WAV,FLAC,OGG,MP3,AIFF complete_before_sync=ok metadata=measured rapid_change_cancel=ok project_unchanged=ok");
    // Mode feedback covers stopped transport, CHOP, hold, zero amount and active
    // gates. Leaving the editor ends live commands; paging within it does not.
    {
        if(std::string(break_variation_status(true,false,true,3,50,50)).find("INACTIVE")!=0)return fail(22);
        if(std::string(break_variation_status(false,true,true,3,50,50)).find("PATTERN HOLD")!=0)return fail(23);
        if(std::string(break_variation_status(false,false,false,0,50,50)).find("READY")!=0)return fail(24);
        if(std::string(break_variation_status(false,false,true,3,0,0)).find("READY")!=0)return fail(25);
        if(std::string(break_variation_status(false,false,true,1,50,50)).find("READY")!=0)return fail(59);
        if(std::string(break_variation_status(false,false,true,3,50,50)).find("ACTIVE")!=0)return fail(26);
        if(std::string(break_stretch_status(true,4)).find("STRETCH ACTIVE")!=0)return fail(27);
        auto host_owner=std::make_unique<EngineHost>();auto& h=*host_owner;Shell shell(h);auto& c=shell.context();c.track().engine=EngineKind::Sampler;c.track().break_bars=2;
        for(unsigned n=0;n<16;++n){c.track().pads[n].sample=0;c.track().pads[n].end=tone.frames;}
        h.load(c.project,c.patterns);h.offer_sample(0,&tone);std::array<int32_t,256> audio{};h.render(audio.data(),128,1);
        for(unsigned route=0;route<3;++route){c.ui.track=0;c.track().repeat=route==1;c.track().arp=route==2;c.ui.page=PageId::Stage;c.ui.back_depth=0;c.open(PageId::Edit);c.ui.break_perf_page=0;
            edit_page.pad(c,2,100,true);for(unsigned n=0;n<100;++n)h.render(audio.data(),128,1);if(!(h.break_activity_of(0)&4))return fail(28);
            // Other chops and a recorded copy of the command cannot release a live hold.
            for(unsigned n=0;n<160;++n){
                Command chop{Command::Type::NoteOn,0,0,uint16_t(36+n%16),60};h.send(chop);
                h.render(audio.data(),128,1);if(!(h.break_activity_of(0)&4))return fail(60);
            }
            edit_page.wheel(c,3);if(c.pad_keys[2][0]<0)return fail(29);
            if(route==0)c.open(PageId::Patterns);else if(route==1)c.back();else c.select_track(1);
            h.render(audio.data(),128,1);if(c.pad_keys[2][0]>=0||h.break_activity_of(0)&4){std::fprintf(stderr,"release route=%u key=%d activity=%u page=%u\n",route,c.pad_keys[2][0],h.break_activity_of(0),unsigned(c.ui.page));return fail(30);}
        }
        c.ui.page=PageId::Samples;c.ui.break_browse=true;auto before=save_project(c.project,c.patterns);auto edits=c.edits.size();
        c.apply(Binding{Binding::Kind::BreakPreviewMode,0,0,1,0},1);
        if(!c.ui.break_preview_sync||save_project(c.project,c.patterns)!=before||c.edits.size()!=edits)return fail(31);
    }
    std::puts("BREAK_P1_UX conditions=CHOP,HOLD,STOPPED,ZERO,ACTIVE,STRETCH live_release=GEN,BACK,TRACK paging_hold=preserved mode=UI_only");

    // Replay the written guide's state-machine route. Decoder completion is fed
    // from the real permitted WAV fixture; this is scripted QA, not a human novice study.
    {
        auto host_owner=std::make_unique<EngineHost>();auto& h=*host_owner;Shell shell(h);SampleLibrary library(h);shell.set_library(&library);auto& c=shell.context();c.track().engine=EngineKind::None;c.ui.page=PageId::Samples;c.ui.break_browse=true;c.ui.break_from_picker=true;c.ui.break_bars=2;
        SampleData decoded;std::string path=root+"/Fixtures/2bar-aligned.wav";if(!read_wav(path,decoded).empty())return fail(39);
        auto load=[&](const std::string& file,unsigned bars){c.ui.break_bars=uint8_t(bars);c.gen_break_load(file,"Guide loop");
            if(c.ui.break_pending>=0){SampleData data;if(!read_wav(file,data).empty())return false;c.project.samples.edit(unsigned(c.ui.break_pending)).frames=data.frames;}return true;};
        std::ofstream trace(root+"/guide-trace.csv");trace<<"step,expected,result\n";
        if(!load(path,2))return fail(40);
        c.gen_break_tick();
        if(c.track().break_bars!=2||c.ui.page!=PageId::Patterns||!c.ui.sheet_open||c.ui.gen8_mode!=2||c.ui.sheet_row!=6)return fail(41);
        trace<<"load,BREAK GEN with BRK PERF selected,pass\n";
        auto straight=c.pattern();patterns_page.push(c);if(c.ui.page!=PageId::Edit)return fail(42);
        edit_page.soft(c,0);
        if(c.ui.page!=PageId::Patterns||c.ui.gen8_mode!=2)return fail(43);
        c.project.gen8.genre=0;c.gen8_action(4);auto made=c.pattern();c.undo();if(!(c.pattern()==straight))return fail(44);
        c.redo();if(!(c.pattern()==made))return fail(45);
        trace<<"generate,GENRE CHOPS then Undo and Redo,pass\n";
        c.gen8_action(10);c.ui.break_perf_page=2;edit_page.soft(c,1);if(!c.ui.break_load_b||c.ui.page!=PageId::Samples)return fail(46);
        if(!load(path,2))return fail(47);
        c.gen_break_tick();if(c.ui.page!=PageId::Edit||c.ui.break_perf_page!=2||!c.track().pads[0].layers[0].used()||!(c.pattern()==made))return fail(48);
        trace<<"load_B,SOURCE A/B and pattern retained,pass\n";
        c.ui.break_perf_page=0;edit_page.soft(c,1);std::string three=root+"/Fixtures/3bar-aligned.wav";
        if(!load(three,3)||c.ui.ask!=UiState::Ask::BreakLength)return fail(49);
        ask_button(c,SurfaceButton::TopLeft);c.gen_break_tick();if(c.track().break_bars!=2||c.ui.page!=PageId::Samples)return fail(50);
        if(!load(three,3))return fail(51);
        ask_button(c,SurfaceButton::TopRight);c.gen_break_tick();
        if(c.track().break_bars!=3||c.track().pads[0].layers[0].used()||!(c.pattern()==made))return fail(52);
        trace<<"replace_A,Cancel then Load A keeps pattern and clears B,pass\n";
        c.undo();if(c.track().break_bars!=2||!c.track().pads[0].layers[0].used())return fail(53);
        c.redo();
        auto saved=save_project(c.project,c.patterns);{std::ofstream file(root+"/guide-session.mldp",std::ios::binary);file.write(reinterpret_cast<const char*>(saved.data()),std::streamsize(saved.size()));}
        std::ifstream input(root+"/guide-session.mldp",std::ios::binary);std::vector<uint8_t> from_file((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        auto project=std::make_unique<Project>();auto bank=std::make_unique<PatternBank>();
        if(!load_project(from_file,*project,*bank).empty()||project->tracks[0].break_bars!=3||!(bank->at[0][0]==made))return fail(54);
        trace<<"save_reopen,real project bytes and source references restored,pass\n";
        c.ui.page=PageId::Patterns;c.ui.sheet_open=true;c.ui.gen8_mode=2;c.back();if(!c.ui.sheet_open||c.ui.gen8_mode)return fail(55);
        c.back();if(c.ui.sheet_open)return fail(56);
        trace<<"back,BREAK GEN to GEN to PATTERNS,pass\n";
    }
    std::puts("BREAK_P1_GUIDE scripted_routes=load,perform,generate,undo,redo,A/B,length_confirm,cancel,save,reopen,back human_novice_validation=not_claimed");
    // Repeatable musical matrix, genuine native DSP rendering, permitted fixtures.
    // No auto-trimming or generation-algorithm edits: source prep issues are kept
    // observable rather than disguised by changing the user's source or seed.
    std::ofstream csv(root+"/matrix.csv");csv<<"bars,source,tempo,action,mode,bank,notes,peak,rms\n";
    unsigned cases=0;
    for(unsigned bars=1;bars<=4;++bars)for(const auto& variant:{"aligned","offset","padding","trim"}){
        SampleData data;std::string path=root+"/Fixtures/"+std::to_string(bars)+"bar-"+variant+".wav";
        if(!read_wav(path,data).empty())return fail(32);
        BreakMap map;map.bars=bars;for(unsigned i=0;i<16;++i)map.slices[i]=uint8_t(36+i);
        Pattern source;source.length=bars*384;Gen8Settings settings{};settings.seed=137;settings.amount=80;settings.complexity=55;settings.genre=0;
        source=break_generate(source,BreakAction::Straight,settings,map,120);
        for(unsigned bpm:{90u,140u,175u})for(auto action:{BreakAction::Straight,BreakAction::Genre,BreakAction::Random,BreakAction::Mutate}){
            auto pattern=break_generate(source,action,settings,map,bpm);
            if(!(pattern==break_generate(source,action,settings,map,bpm))||pattern.events.empty())return fail(33);
            for(const auto& e:pattern.events)if(e.key<36||e.key>51||e.tick>=pattern.length||!e.length||e.tick+e.length>pattern.length)return fail(34);
            if(action==BreakAction::Mutate)for(const auto& e:source.events)if(e.tick%96==0){
                auto it=std::find_if(pattern.events.begin(),pattern.events.end(),[&](const Event& v){return e.tick==v.tick&&e.key==v.key;});if(it==pattern.events.end())return fail(35);}
            for(unsigned mode=0;mode<2;++mode){
                struct Edge{unsigned frame;int key;bool on;};std::vector<Edge> edges;
                for(const auto& e:pattern.events){unsigned count=e.hits();for(unsigned n=0;n<count;++n){
                    double tick=e.tick+double(e.length)*n/count,to=e.tick+double(e.length)*(n+1)/count;
                    edges.push_back({unsigned(tick*60.*44100/(bpm*96)),int(e.key),true});edges.push_back({unsigned(to*60.*44100/(bpm*96)),int(e.key),false});}}
                std::sort(edges.begin(),edges.end(),[](const Edge& a,const Edge& b){return a.frame<b.frame||(a.frame==b.frame&&a.on<b.on);});
                BreakManipulator dsp;dsp.configure(bars,0);dsp.sources(&data,&data);dsp.tempo(bpm*10);dsp.param(9,mode);dsp.transport(true,0);
                unsigned bank=(unsigned(action)+bars)%2;if(bank)dsp.note(17,100);
                unsigned total=unsigned(pattern.length*60.*44100/(bpm*96));size_t edge=0;double square=0,peak=0;unsigned position=0;
                SampleData render;bool save=bars==2&&std::string(variant)=="aligned"&&bpm==140&&mode==1;
                if(save){render.frames=total;render.left.resize(total);}
                while(position<total){while(edge<edges.size()&&edges[edge].frame<=position){const auto& e=edges[edge++];if(e.on)dsp.note(e.key,100);else dsp.off(e.key);}
                    unsigned n=std::min(128u,total-position);if(edge<edges.size()&&edges[edge].frame>position)n=std::min(n,edges[edge].frame-position);
                    std::array<float,128> l{},r{};dsp.render(l.data(),r.data(),n,44100);
                    for(unsigned i=0;i<n;++i){if(!std::isfinite(l[i]))return fail(36);
        peak=std::max(peak,double(std::fabs(l[i])));square+=l[i]*l[i];if(save)render.left[position+i]=l[i];}position+=n;
                }
                if(peak<1e-5)return fail(37);
                csv<<bars<<','<<variant<<','<<bpm<<','<<unsigned(action)<<','<<(mode?"CHOP":"LOOP")<<','<<bank<<','<<pattern.events.size()<<','<<peak<<','<<std::sqrt(square/std::max(1u,total))<<'\n';
                if(save){render.right=render.left;}
                if(save&&!write_wav(root+"/Rendered/break-action-"+std::to_string(unsigned(action))+".wav",render).empty())return fail(38);
                ++cases;
            }
        }
    }
    std::printf("BREAK_P1_MATRIX cases=%u source_bars=1-4 variants=aligned,offset,padding,trim tempos=90,140,175 actions=Straight,Genre,Random,Mutate modes=CHOP,LOOP sources=A,B deterministic=ok anchors=ok native_audio=finite_nonzero\n",cases);
    return 0;
}
}
