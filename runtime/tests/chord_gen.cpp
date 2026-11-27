// SPDX-License-Identifier: GPL-3.0-only
#include "rhythmer/model/chord_gen.h"
#include "rhythmer/model/chord_gen_access.h"
#include "rhythmer/model/project_file.h"
#include "rhythmer/model/scales.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <set>
using namespace rhythmer;
void bounded(const Pattern& p,const Gen8Voice& v){
 assert(!p.events.empty());std::set<std::pair<unsigned,unsigned>> used;unsigned last=0;
 for(const auto& e:p.events){assert(e.tick>=last&&e.tick<p.length&&e.length&&e.tick+e.length<=p.length);assert(e.key>=v.low&&e.key<=v.high&&e.velocity>0&&e.velocity<=127);assert(used.insert({e.tick,e.key}).second);last=e.tick;}
 if(v.mono){unsigned end=0;for(const auto& e:p.events){assert(e.tick>=end);end=e.tick+e.length;}}
}
int main(){
 Pattern source;source.length=4*ticks_per_quarter;MotionPoint m;m.tick=sixteenth_ticks;m.param=2;m.value=42;source.motion.push_back(m);ChordGenSettings s;Gen8Voice v;
 unsigned cases=0;
 for(unsigned genre=0;genre<61;genre++)for(unsigned scale=0;scale<13;scale++)for(unsigned play=0;play<5;play++)for(unsigned seed=1;seed<=3;seed++){
 s.genre=genre;s.play=play;s.seed=seed;s.harmony=seed;s.bars=uint8_t(1+seed*5);s.chords=1+genre%8;s.color=genre%4;s.voicing=(genre+scale)%5;s.rate=scale%4;s.rhythm=seed*30;s.energy=seed*30;
 unsigned bar=(3+scale%3)*ticks_per_quarter;v.mono=seed==3;auto p=generate_chords(source,s,v,genre%12,scale,bar);bounded(p,v);assert(p.length==s.bars*bar&&p.motion==source.motion);assert(p==generate_chords(source,s,v,genre%12,scale,bar));cases++;
 }
 s={};s.play=0;s.rhythm=0;s.voicing=1;s.color=0;v.mono=false;auto triad=generate_chords(source,s,v,0,0,4*ticks_per_quarter);s.color=3;auto rich=generate_chords(source,s,v,0,0,4*ticks_per_quarter);assert(rich.events.size()>triad.events.size());
 auto old=rich;s.energy=0;auto soft=generate_chords(source,s,v,0,0,4*ticks_per_quarter);assert(soft.events.size()==old.events.size());for(unsigned i=0;i<soft.events.size();i++){assert(soft.events[i].tick==old.events[i].tick&&soft.events[i].key==old.events[i].key);assert(soft.events[i].velocity<old.events[i].velocity);}
 s={};assert(chord_gen_value(s,13,9,2)==9&&chord_gen_value(s,14,9,2)==2);chord_gen_set(s,13,4,9,2);assert(s.local&&s.root==4&&s.scale==2);chord_gen_set(s,12,0,1,5);assert(chord_gen_value(s,13,1,5)==1);
 for(unsigned root=0;root<12;root++)for(unsigned scale=0;scale<13;scale++)for(unsigned note=24;note<100;note++){int mapped=gen_follow_note(note,2,scales()[1].mask,root,scales()[scale].mask,v);assert(scales()[scale].mask&(1u<<((mapped+12-root)%12)));}
 auto p=std::make_unique<Project>(),q=std::make_unique<Project>();auto bank=std::make_unique<PatternBank>(),loaded=std::make_unique<PatternBank>();
 p->gen8.length=16;p->tracks[7].chord_gen=s;p->tracks[7].chord_gen.seed=12345;p->tracks[7].chord_gen.harmony=98765;p->tracks[7].chord_gen.genre=40;bank->at[7][0]=rich;
 auto bytes=save_project(*p,*bank);assert(load_project(bytes,*q,*loaded).empty());assert(q->gen8.length==16&&q->tracks[7].chord_gen.seed==12345&&q->tracks[7].chord_gen.harmony==98765&&q->tracks[7].chord_gen.genre==40&&loaded->at[7][0]==rich);
 p->tracks[0].engine=EngineKind::Midi;assert(chord_gen_available(*p,0));p->tracks[0].engine=EngineKind::None;assert(!chord_gen_available(*p,0));p->tracks[0].engine=EngineKind::Plugin;p->tracks[0].plugin=0;assert(!chord_gen_available(*p,0));
 std::cout<<"CHORD_GEN model cases="<<cases<<" deterministic,bounds,mono,voicing,key,files=ok\n";
}
