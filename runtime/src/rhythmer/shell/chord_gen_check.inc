// CHORD GEN integration: native pages, ownership, persistence/undo and main GEN dispatch.
{
 EngineHost h;Shell s(h);auto& x=s.context();x.ui.track=2;x.track().engine=EngineKind::Midi;x.track().layout=PadLayout::Scale;x.project.root=9;x.project.scale=2;x.project.tempo=1240;x.project.swing=61;x.pattern().length=4*ticks_per_quarter;
 x.ui.page=PageId::Patterns;x.ui.sheet_open=true;x.ui.gen8_mode=0;x.ui.sheet_row=4;patterns_page.push(x);if(x.ui.gen8_mode!=5)return gen_failure(300,__LINE__);
 for(unsigned page=0;page<5;page++){if(x.ui.chord_gen_page!=page)return gen_failure(301,__LINE__);patterns_page.soft(x,1);}if(x.ui.chord_gen_page)return gen_failure(302,__LINE__);
 x.track().chord_gen.rhythm=0;x.track().chord_gen.bars=2;x.track().chord_gen.play=4;
 auto before=x.pattern();auto seed=x.track().chord_gen.seed;x.edits.clear();x.chord_gen_action(false);auto made=x.pattern();auto harmony=x.track().chord_gen.harmony;
 if(made.events.empty()||made.length!=8*ticks_per_quarter||x.project.root!=9||x.project.scale!=2||x.project.tempo!=1240||x.project.swing!=61||x.edits.size()!=1)return gen_failure(303,__LINE__);
 x.undo();if(!(x.pattern()==before)||x.track().chord_gen.seed!=seed)return gen_failure(304,__LINE__);x.redo();if(!(x.pattern()==made)||x.track().chord_gen.harmony!=harmony)return gen_failure(305,__LINE__);
 x.chord_gen_action(true);if(x.track().chord_gen.harmony!=harmony)return gen_failure(306,__LINE__);x.undo();if(!(x.pattern()==made))return gen_failure(307,__LINE__);
 x.back();if(x.ui.gen8_mode||!x.ui.sheet_open)return gen_failure(308,__LINE__);
 x.track().chord_gen.local=1;x.track().chord_gen.root=3;x.track().chord_gen.scale=0;x.chord_gen_action(false);if(x.project.root!=9||x.project.scale!=2)return gen_failure(309,__LINE__);
 x.track().chord_gen.type=1;x.project.gen8.assign=0;x.project.gen8.length=3;x.project.gen8.genre=40;x.gen8_action(2);if(x.pattern().length!=12*ticks_per_quarter||x.pattern().events.empty())return gen_failure(310,__LINE__);
 const auto mask=scales()[x.project.scale].mask;
 for(const auto& e:x.pattern().events)if(!(mask&(1u<<((e.key+12-x.project.root)%12))))return gen_failure(311,__LINE__);
 x.ui.gen8_mode=0;x.ui.sheet_row=2;auto controls=page_bindings(x.project,x.ui,x.pattern());if(controls[0].kind!=Binding::Kind::Gen8Amount)return gen_failure(312,__LINE__);
 // Kit track generation always uses the original genre's drum lane, never chords.
 x.track().engine=EngineKind::Plugin;x.track().plugin=0;x.track().layout=PadLayout::Drums;x.track().break_bars=0;
 std::strcpy(x.project.plugins[0].name,"Rhythmer Kit");std::strcpy(x.project.plugins[0].path,"Plugins/RHYTHMER - Rhythmer Kit");
 if(chord_gen_available(x.project,x.ui.track))return gen_failure(313,__LINE__);
 std::set<std::vector<unsigned>> rhythms;unsigned silent_genres=0;
 for(unsigned genre=2;genre<=60;genre++){x.pattern().events.clear();x.project.gen8.genre=genre;x.project.gen8.seed=42;x.gen8_action(2);if(x.pattern().events.empty()){
   if(std::string(x.ui.notice).find("no drum part")==std::string::npos)return gen_failure(314,__LINE__);
   Event marker;marker.key=36;marker.tick=0;marker.length=12;marker.velocity=100;x.pattern().add(marker);auto kept=x.pattern();x.project.gen8.seed=42;x.gen8_action(2);
   if(!(x.pattern()==kept))return gen_failure(317,__LINE__);
   std::printf("RHYTHMER_KIT_NO_DRUM genre=%u pattern=preserved\n",genre);++silent_genres;continue;
  }
  std::vector<unsigned> fingerprint;for(const auto&e:x.pattern().events){if(e.key<36||e.key>43)return gen_failure(315,__LINE__);fingerprint.push_back(e.tick*256+e.key);}rhythms.insert(std::move(fingerprint));}
 if(rhythms.size()<20)return gen_failure(316,__LINE__);
 std::printf("RHYTHMER_KIT_GEN genres=59 distinct_patterns=%zu no_drum_genres=%u drum_keys=36-43 chord_gen=excluded\n",rhythms.size(),silent_genres);
 std::printf("RHYTHMER_CHORD_SHELL five_pages=ok back=ok project_follow=ok globals=preserved undo_redo=ok local=ok track_gen=independent length=ok\n");
}
