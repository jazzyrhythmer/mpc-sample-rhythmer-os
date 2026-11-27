// SPDX-License-Identifier: GPL-3.0-only
#include "rhythmer/model/gen8.h"
#include "rhythmer/model/gen_engine_map.h"
#include "rhythmer/model/gen8_tables.h"
#include "rhythmer/model/gen_menu_order.h"
#include "rhythmer/model/native_gen_profiles.h"
#include "rhythmer/model/project_file.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <set>
#include <string>
using namespace rhythmer;
unsigned genre(const char* name) {
    for(unsigned i=0;i<gen8_genre_count;i++)if(std::string(name)==gen8_genres[i].name)return i;
    assert(false);return 0;
}
void bounded(const Pattern& p,const BreakMap& m) {
    uint32_t prev=0;
    for(const auto& e:p.events) {
        assert(e.tick>=prev&&e.tick<p.length&&e.length>0&&e.tick+e.length<=p.length);
        assert(e.ratchet>=1&&e.ratchet<=4&&e.velocity>=1&&e.velocity<=127);
        assert(std::find(m.slices.begin(),m.slices.end(),e.key)!=m.slices.end());prev=e.tick;
        if(e.ratchet>1)assert(e.length<=2*sixteenth_ticks);
    }
}
int main() {
    assert(gen8_genre_count==59);
    std::set<std::string> names;for(const auto& g:gen8_genres)names.insert(g.name);
    assert(names.size()==59);
    Pattern p;p.length=16*ticks_per_quarter;MotionPoint point;point.tick=ticks_per_quarter;point.param=2;point.value=42;p.motion.push_back(point);
    BreakMap map;for(unsigned i=0;i<16;i++)map.slices[i]=uint8_t(148+i); // highest sampler bank
    Gen8Settings s;s.seed=42;s.amount=80;s.genre=uint8_t(genre("JUNGLE"));
    auto straight=break_generate(p,BreakAction::Straight,s,map,164);
    assert(straight.events.size()==32);
    for(unsigned i=0;i<32;i++){assert(straight.events[i].tick==2*sixteenth_ticks*i);assert(straight.events[i].key==map.slices[i%16]);}
    assert(straight.motion==p.motion&&p.events.empty());
    auto random=break_generate(p,BreakAction::Random,s,map,164);
    assert(random.events==break_generate(p,BreakAction::Random,s,map,164).events);
    s.seed++;assert(random.events!=break_generate(p,BreakAction::Random,s,map,164).events);s.seed--;
    auto mut=break_generate(straight,BreakAction::Mutate,s,map,164);
    assert(mut.events!=straight.events&&mut.events.size()==straight.events.size()&&mut.motion==straight.motion);
    for(unsigned i=0;i<mut.events.size();i++)if(straight.events[i].tick%ticks_per_quarter==0)assert(mut.events[i]==straight.events[i]);
    assert(break_generate(p,BreakAction::Mutate,s,map,164).events.empty());
    auto fill=break_generate(straight,BreakAction::Fill,s,map,164);
    for(const auto& e:straight.events)if(e.tick<16*ticks_per_quarter-8*sixteenth_ticks)assert(std::find(fill.events.begin(),fill.events.end(),e)!=fill.events.end());
    assert(fill.motion==straight.motion);bounded(fill,map);
    s.amount=0;assert(break_generate(straight,BreakAction::Mutate,s,map,164).events==straight.events);s.amount=80;
    s.genre=uint8_t(genre("BOOM BAP"));auto hip=break_generate(p,BreakAction::Genre,s,map,140);
    assert(!hip.events.empty());for(const auto& e:hip.events)assert(e.tick%(2*sixteenth_ticks)==0&&e.ratchet==1);
    s.genre=uint8_t(genre("JUNGLE"));auto jungle=break_generate(p,BreakAction::Genre,s,map,164);
    assert(std::any_of(jungle.events.begin(),jungle.events.end(),[](const Event& e){return e.ratchet>1;}));
    // Every genre, cold start, multiple seeds, short/partial bars and long patterns.
    for(unsigned g=0;g<59;g++)for(unsigned seed=0;seed<8;seed++) {
        s.genre=uint8_t(g);s.seed=uint16_t(seed);
        for(unsigned len:{1u,sixteenth_ticks+1,4*ticks_per_quarter,16*ticks_per_quarter,512*ticks_per_quarter}) {
            Pattern src=p;src.length=len;
            for(auto action:{BreakAction::Genre,BreakAction::Straight,BreakAction::Random}) {
                auto made=break_generate(src,action,s,map,160);bounded(made,map);assert(made.motion==src.motion);
            }
        }
        for(unsigned r=0;r<8;r++) {
            Gen8Voice v;v.drums=r>=4;v.mono=r==0;
            auto made=generate8(p,Gen8Role(r),s,v,2,1);
            assert(made.events==generate8(p,Gen8Role(r),s,v,2,1).events&&made.motion==p.motion);
            for(const auto& e:made.events)assert(e.tick<p.length&&e.length&&e.tick+e.length<=p.length&&e.key<=127);
        }
    }
    // Exercise the actual accepted instruments' ranges and note maps across all
    // genres. A missing lane must stay absent; synth ranges must survive GEN.
    for(const auto& profile:native_gen_profiles)for(unsigned g=0;g<59;++g){
        Gen8Voice v;v.drums=profile.drums;v.mono=profile.mono;v.low=profile.low;v.high=profile.high;v.drum_keys=profile.keys;
        s.genre=uint8_t(g);s.seed=73;
        for(unsigned r=v.drums?4:0;r<(v.drums?8:4);++r){
            auto made=generate8(p,Gen8Role(r),s,v,2,1);
            for(const auto& e:made.events){
                if(v.drums)assert(std::find(v.drum_keys.begin(),v.drum_keys.end(),e.key)!=v.drum_keys.end()&&e.key!=255);
                else assert(e.key>=v.low&&e.key<=v.high);
            }
            if(v.mono){uint32_t end=0;for(const auto& e:made.events){assert(e.tick>=end);end=e.tick+e.length;}}
        }
    }
    for(unsigned bars=1;bars<=4;bars++){
        Pattern src;src.length=bars*4*ticks_per_quarter;BreakMap map;map.bars=bars;for(unsigned i=0;i<16;i++)map.slices[i]=uint8_t(36+i);
        auto straight=break_generate(src,BreakAction::Straight,Gen8Settings{},map,120);
        assert(straight.events.size()==16);
        for(unsigned i=0;i<16;i++){assert(straight.events[i].tick==i*bars*sixteenth_ticks);assert(straight.events[i].key==36+i);}
    }
    for(const auto& engine:gen_engines){
        Pattern src;src.length=16*ticks_per_quarter;Gen8Voice voice;voice.drums=engine.drums;voice.mono=engine.mono;voice.acid=engine.acid;voice.low=engine.low;voice.high=engine.high;
        auto made=generate8(src,engine.drums?Gen8Role::Percussion:Gen8Role::Bass,Gen8Settings{},voice,0,0);
        assert(!made.events.empty());for(const auto& e:made.events){assert(e.tick<src.length&&e.key<=127);if(!voice.drums)assert(e.key>=voice.low&&e.key<=voice.high);}
    }
    // Settings and music survive files; older files get defaults. Truncation does not corrupt the target.
    auto project=std::make_unique<Project>(),loaded=std::make_unique<Project>();
    auto bank=std::make_unique<PatternBank>(),restored=std::make_unique<PatternBank>();
    project->gen8={9999,60,99,83};bank->at[0][0]=jungle;
    project->plugins[0].gen_program=2301;project->gen8.first=3;project->gen8.last=15;project->gen8.assign=0;project->gen8.bars=3;project->tracks[7].break_bars=4;
    auto data=save_project(*project,*bank);
    assert(load_project(data,*loaded,*restored).empty());
    assert(loaded->gen8.seed==9999&&loaded->gen8.genre==60&&loaded->gen8.amount==99&&loaded->gen8.complexity==83);
    assert(restored->at[0][0].events==jungle.events);
    assert(loaded->plugins[0].gen_program==2301);
    assert(loaded->gen8.first==3&&loaded->gen8.last==15&&!loaded->gen8.assign&&loaded->gen8.bars==3&&loaded->tracks[7].break_bars==4);
    for(unsigned group=0;group<4;++group){
        project->gen8.group=uint8_t(group);auto grouped=save_project(*project,*bank);
        assert(load_project(grouped,*loaded,*restored).empty()&&loaded->gen8.group==group);
        assert(gen_song_first(loaded->gen8)==group*4);
    }
    project->gen8.group=3;data=save_project(*project,*bank);
    const std::vector<uint8_t> group_tag={'G','N','G','P'};
    auto group_at=size_t(std::search(data.begin(),data.end(),group_tag.begin(),group_tag.end())-data.begin());
    assert(group_at+9<=data.size());auto old_groups=data;old_groups.resize(group_at);
    assert(load_project(old_groups,*loaded,*restored).empty()&&loaded->gen8.group==0);
    auto invalid_group=data;invalid_group[group_at+8]=255;
    assert(load_project(invalid_group,*loaded,*restored).empty()&&loaded->gen8.group==3);
    auto legacy_genre=data;const std::vector<uint8_t> legacy_tag={'G','N','8','S'};
    auto legacy_at=size_t(std::search(legacy_genre.begin(),legacy_genre.end(),legacy_tag.begin(),legacy_tag.end())-legacy_genre.begin());
    legacy_genre[legacy_at+8]=1;
    for(unsigned genre=0;genre<59;genre++){
        legacy_genre[legacy_at+11]=uint8_t(genre);
        assert(load_project(legacy_genre,*loaded,*restored).empty());
        assert(loaded->gen8.seed==9999&&std::string(gen_menu_name(loaded->gen8.genre))==gen8_genres[genre].name);
    }
    assert(std::string(gen_menu_name(7))=="FUNK"&&std::string(gen_menu_name(54))=="NEO CLASSIC");
    auto old=data;const std::vector<uint8_t> tag={'G','N','8','S'};old.resize(size_t(std::search(old.begin(),old.end(),tag.begin(),tag.end())-old.begin())); // 8-byte tag/length + 6-byte GN8S payload
    assert(load_project(old,*loaded,*restored).empty()&&loaded->gen8.seed==1&&loaded->gen8.genre==0);
    auto bad=data;bad.pop_back();loaded->gen8.seed=123;
    assert(!load_project(bad,*loaded,*restored).empty()&&loaded->gen8.seed==123);
    std::cout<<"gen8 + break: 59 genres, deterministic seeds, anchors, rolls, boundaries and files ok\n";
}
