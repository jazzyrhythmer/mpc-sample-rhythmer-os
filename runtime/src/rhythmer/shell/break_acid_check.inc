// SPDX-License-Identifier: GPL-3.0-only
    {
        EngineHost h;Shell s(h);auto& x=s.context();x.ui.track=0;x.ui.page=PageId::Edit;
        auto& t=x.track();t.engine=EngineKind::Sampler;t.break_bars=2;t.bank=0;
        SampleData a,b;a.frames=b.frames=44100;a.left.resize(a.frames);b.left.resize(b.frames);
        for(unsigned n=0;n<a.frames;n++){a.left[n]=.25f*std::sin(n*.08f);b.left[n]=.25f*std::sin(n*.13f);}
        for(unsigned i=0;i<16;i++){t.pads[i].sample=0;t.pads[i].start=a.frames*i/16;t.pads[i].end=a.frames*(i+1)/16;}
        t.pads[0].layers[0].sample=1;t.pads[0].layers[0].end=b.frames;
        auto& pattern=x.pattern();pattern.length=8*ticks_per_quarter;pattern.events.clear();Event gate;gate.key=36;gate.length=8*ticks_per_quarter;pattern.add(gate);
        h.load(x.project,x.patterns);h.offer_sample(0,&a);h.offer_sample(1,&b);h.send({Command::Type::Play});
        std::array<int32_t,256> audio{};h.render(audio.data(),128,1);
        x.ui.recording=true;x.ui.settings.rec_quantize=false;x.ui.break_perf_page=0;
        edit_page.pad(x,8,110,true);for(unsigned n=0;n<40;n++)h.render(audio.data(),128,1);edit_page.pad(x,8,0,false);
        if(steps_lane(t)!=8||key_label(t,8)!="1/16 REPEAT")return gen_failure(422,__LINE__);
        auto roll=std::find_if(pattern.events.begin(),pattern.events.end(),[](const Event& e){return e.key==8;});
        if(roll==pattern.events.end()||roll->length<2)return gen_failure(401,__LINE__);
        x.ui.page=PageId::StepEdit;x.ui.step_pads=true;x.ui.held_step=13;Event edited;edited.key=8;edited.tick=13*x.pattern().q();x.pattern().add(edited);
        step_edit_page.wheel(x,1);if(steps_lane(t)!=9||key_label(t,9)!="1/16 DOWN")return gen_failure(423,__LINE__);
        x.ui.held_step=-1;x.ui.page=PageId::Edit;
        x.ui.break_perf_page=2;edit_page.pad(x,1,110,true);h.render(audio.data(),128,1);edit_page.pad(x,1,0,false);
        if(h.break_source_of(0)!=1)return gen_failure(402,__LINE__);
        Binding variation;variation.kind=Binding::Kind::BreakPerfParam;variation.id=1;variation.min=0;variation.max=127;x.apply(variation,73);h.render(audio.data(),128,1);
        edit_page.soft(x,2);h.render(audio.data(),128,1);x.gen_break_tick();if(t.break_perf[1]||t.break_perf[2])return gen_failure(424,__LINE__);
        for(unsigned key:{8u,17u,18u})if(std::none_of(pattern.events.begin(),pattern.events.end(),[&](const Event& e){return e.key==key;}))return gen_failure(403,__LINE__);
        x.ui.recording=false;auto recorded=pattern.events;edit_page.soft(x,0);
        if(x.ui.page!=PageId::Patterns||!x.ui.sheet_open||x.ui.gen8_mode!=2)return gen_failure(404,__LINE__);
        patterns_page.soft(x,0);if(!x.ui.sheet_open||x.ui.gen8_mode)return gen_failure(405,__LINE__);
        x.gen8_action(10);if(x.ui.page!=PageId::Edit||x.ui.break_perf_page)return gen_failure(406,__LINE__);
        for(unsigned page=0;page<6;page++){auto cards=page_bindings(x.project,x.ui,x.pattern());
            for(const auto& card:cards)if(card.kind==Binding::Kind::None)return gen_failure(407,__LINE__);
            edit_page.wheel(x,1);}
        if(x.ui.break_perf_page)return gen_failure(408,__LINE__);
        h.send({Command::Type::Stop});h.render(audio.data(),128,1);t.break_source=0;
        h.load(x.project,x.patterns);h.send({Command::Type::Play});bool selected_b=false,heard=false;
        for(unsigned n=0;n<160;n++){h.render(audio.data(),128,1);selected_b|=h.break_source_of(0)==1;for(auto sample:audio)heard|=sample!=0;}
        if(!selected_b||!heard||pattern.events!=recorded)return gen_failure(409,__LINE__);
        auto saved=save_project(x.project,x.patterns);auto project=std::make_unique<Project>();auto bank=std::make_unique<PatternBank>();
        if(!load_project(saved,*project,*bank).empty()||project->tracks[0].pads[0].layers[0].sample!=1||project->tracks[0].break_perf!=t.break_perf||bank->at[0][0].events!=recorded)return gen_failure(410,__LINE__);
        std::printf("RHYTHMER_BREAK_PERF pads=16 commands=19 pages=6 recording=ok replay=ok sources=AB save_restore=ok gen_back=ok\n");
    }
    {
        EngineHost h;Shell s(h);auto& x=s.context();x.ui.page=PageId::Patterns;x.ui.sheet_open=true;x.ui.gen8_mode=4;
        x.track().engine=EngineKind::Plugin;x.track().plugin=0;std::strcpy(x.project.plugins[0].name,"Acid Engine");
        x.pattern().length=8*ticks_per_quarter;x.pattern().events.clear();x.track().acid_gen.seed=0xdeadbeef;
        auto song_seed=x.project.gen8.seed;x.gen8_action(12);auto first=x.pattern().events;
        if(first.empty()||x.project.gen8.seed!=song_seed||x.track().acid_gen.seed!=0xdeadbeef)return gen_failure(411,__LINE__);
        x.gen8_action(12);if(x.pattern().events!=first)return gen_failure(412,__LINE__);
        x.gen8_action(3);auto next_seed=x.track().acid_gen.seed;if(next_seed==0xdeadbeef)return gen_failure(413,__LINE__);
        x.undo();if(x.pattern().events!=first||x.track().acid_gen.seed!=0xdeadbeef)return gen_failure(414,__LINE__);
        x.redo();if(x.track().acid_gen.seed!=next_seed)return gen_failure(415,__LINE__);
        for(unsigned page=0;page<2;page++){auto cards=page_bindings(x.project,x.ui,x.pattern());
            for(unsigned k=0;k<3;++k)if(cards[k].kind!=Binding::Kind::AcidGenParam || cards[k].id!=int(page*3+k))return gen_failure(416,__LINE__);
            patterns_page.soft(x,1);}
        if(x.ui.acid_gen_page)return gen_failure(417,__LINE__);
        patterns_page.wheel(x,50);if(x.ui.sheet_row!=1)return gen_failure(422,__LINE__);
        auto keep_seed=x.track().acid_gen.seed;auto keep_edits=x.edits.size();
        x.ui.sheet_row=2;patterns_page.push(x);
        if(x.ui.sheet_row || x.track().acid_gen.seed!=keep_seed || x.edits.size()!=keep_edits)return gen_failure(423,__LINE__);
        x.ui.acid_gen_page=1;patterns_page.soft(x,0);
        auto normal=page_bindings(x.project,x.ui,x.pattern());
        if(normal[2].kind!=Binding::Kind::Gen8Seed)return gen_failure(424,__LINE__);
        x.ui.sheet_row=4;patterns_page.push(x);
        if(x.ui.gen8_mode!=4 || x.ui.acid_gen_page)return gen_failure(425,__LINE__);
        auto root_cards=page_bindings(x.project,x.ui,x.pattern());
        if(root_cards[0].kind!=Binding::Kind::AcidGenParam || root_cards[0].id!=0)return gen_failure(426,__LINE__);
        auto saved=save_project(x.project,x.patterns);auto project=std::make_unique<Project>();auto bank=std::make_unique<PatternBank>();
        if(!load_project(saved,*project,*bank).empty()||project->tracks[0].acid_gen.seed!=next_seed)return gen_failure(418,__LINE__);
        patterns_page.soft(x,0);if(x.ui.gen8_mode||!x.ui.sheet_open)return gen_failure(419,__LINE__);
        auto previous_seed=x.track().acid_gen.seed;x.gen8_action(2);
        if(x.track().acid_gen.seed==previous_seed||x.project.gen8.seed!=song_seed)return gen_failure(420,__LINE__);
        x.track().acid_gen.auto_bars=1;previous_seed=x.track().acid_gen.seed;x.project.tempo=3000;
        h.load(x.project,x.patterns);h.send({Command::Type::Play});x.ui.track=3;auto edits=x.edits.size();
        std::array<int32_t,256> audio{};
        for(unsigned n=0;n<400;n++){h.render(audio.data(),128,1);x.gen_break_tick();}
        if(x.project.tracks[0].acid_gen.seed==previous_seed||x.ui.track!=3||x.project.gen8.seed!=song_seed||x.edits.size()!=edits)return gen_failure(421,__LINE__);
        std::printf("RHYTHMER_ACID settings_pages=2 seed_ui=hidden replay_menu=removed general_gen_seed=retained seed=uint32 deterministic=ok mutate_stream=ok undo_redo=ok save_restore=ok gen_back=ok\n");
    }

    // Initial loop load remains in BREAK; completing an asynchronous load
    // cannot reopen a dismissed menu or pull the player from another track.
    for(unsigned route=0;route<5;++route){
        EngineHost h;Shell s(h);auto& x=s.context();
        x.ui.track=0;x.ui.page=PageId::Patterns;x.ui.sheet_open=true;x.ui.gen8_mode=2;
        x.ui.break_pending=0;x.ui.break_job_track=0;x.ui.break_job_bank=0;x.ui.break_job_bars=2;
        x.ui.break_job_b=false;x.ui.break_job_from_picker=route!=4;
        x.project.samples.edit(0).frames=176400;
        x.ui.sheet_row=1;patterns_page.push(x);if(x.ui.sheet_row!=1||!x.pattern().events.empty())return gen_failure(430,__LINE__);
        if(route==1)x.ui.track=1;
        if(route==2)x.ui.sheet_open=false;
        if(route==3)x.ui.gen8_mode=0;
        x.gen_break_tick();
        if(x.project.tracks[0].break_bars!=2||x.ui.page!=PageId::Patterns)return gen_failure(431,__LINE__);
        if(route==0||route==4){if(!x.ui.sheet_open||x.ui.gen8_mode!=2||x.ui.sheet_row!=6)return gen_failure(432,__LINE__);
            patterns_page.push(x);if(x.ui.page!=PageId::Edit||x.ui.break_perf_page)return gen_failure(433,__LINE__);
            edit_page.soft(x,0);if(x.ui.page!=PageId::Patterns||x.ui.gen8_mode!=2)return gen_failure(434,__LINE__);
        }else if(x.ui.sheet_row!=1||x.ui.track!=(route==1?1:0)||x.ui.sheet_open!=(route!=2)||x.ui.gen8_mode!=(route==3?0:2))return gen_failure(435,__LINE__);
    }
    std::printf("RHYTHMER_BREAK_FEEDBACK load_destination=stable pending_apply=blocked async_focus=preserved perf_gen_roundtrip=ok\n");

    // Visible CANCEL and Shift + Push both cancel assignment, not just navigation.
    for(unsigned back_key=0;back_key<2;++back_key){
        EngineHost h;Shell s(h);auto& x=s.context();x.ui.track=0;
        x.ui.page=PageId::Patterns;x.ui.sheet_open=true;x.ui.gen8_mode=2;
        x.ui.break_pending=0;x.ui.break_job_track=0;x.ui.break_job_bars=2;
        x.project.samples.edit(0).frames=176400;auto old_engine=x.track().engine;auto history=x.edits.size();
        auto cards=page_bindings(x.project,x.ui,x.pattern());for(const auto& b:cards)if(b.kind!=Binding::Kind::None)return gen_failure(436,__LINE__);
        patterns_page.soft(x,1);if(x.ui.gen8_mode!=2)return gen_failure(437,__LINE__);
        if(back_key) x.back();else patterns_page.soft(x,0);
        x.gen_break_tick();if(x.ui.break_pending>=0||x.ui.gen8_mode||!x.ui.sheet_open||x.track().engine!=old_engine||x.track().break_bars||x.edits.size()!=history)return gen_failure(438,__LINE__);
    }
    // B completion may return to SOURCE, but never reopen a dismissed menu.
    for(unsigned route=0;route<4;++route){
        EngineHost h;Shell s(h);auto& x=s.context();x.ui.track=0;
        x.track().engine=EngineKind::Sampler;x.track().break_bars=2;
        x.ui.page=PageId::Patterns;x.ui.sheet_open=true;x.ui.gen8_mode=2;
        x.ui.break_pending=0;x.ui.break_job_track=0;x.ui.break_job_bank=0;x.ui.break_job_bars=2;x.ui.break_job_b=true;
        x.project.samples.edit(0).frames=176400;
        if(route==1)x.select_track(1);
        if(route==2)x.ui.sheet_open=false;
        if(route==3)x.ui.gen8_mode=0;
        x.gen_break_tick();if(x.project.tracks[0].pads[0].layers[0].sample!=0)return gen_failure(439,__LINE__);
        if(route==0){if(x.ui.page!=PageId::Edit||x.ui.break_perf_page!=2)return gen_failure(440,__LINE__);}
        else if(x.ui.page!=PageId::Patterns||x.ui.track!=(route==1?1:0)||x.ui.sheet_open!=(route!=1&&route!=2)||x.ui.gen8_mode!=(route==3?0:2))return gen_failure(441,__LINE__);
    }
    // Undo/Redo and project serialization retain A/B, hold, mode and all patterns.
    {
        EngineHost h;Shell s(h);auto& x=s.context();x.ui.track=0;x.track().engine=EngineKind::Midi;
        x.pattern().length=8*ticks_per_quarter;Event e;e.key=48;e.length=ticks_per_quarter;x.pattern().add(e);auto before=x.pattern().events;
        x.ui.break_pending=0;x.ui.break_job_track=0;x.ui.break_job_bank=2;x.ui.break_job_bars=3;x.project.samples.edit(0).frames=264600;
        x.gen_break_tick();auto made=x.pattern().events;if(made!=before)return gen_failure(466,__LINE__);x.track().break_perf[4]=1;x.track().break_perf[9]=0;
        auto saved=save_project(x.project,x.patterns);auto project=std::make_unique<Project>();auto bank=std::make_unique<PatternBank>();
        if(!load_project(saved,*project,*bank).empty()||project->tracks[0].break_perf[4]!=1||project->tracks[0].break_perf[9]||bank->at[0][0].events!=made)return gen_failure(442,__LINE__);
        x.undo();if(x.track().engine!=EngineKind::Midi||x.pattern().events!=before)return gen_failure(443,__LINE__);
        x.redo();if(x.track().break_bars!=3||x.track().bank!=2||x.pattern().events!=made)return gen_failure(444,__LINE__);
        // A held STRETCH pad must release even after paging to GRAIN.
        x.ui.page=PageId::Edit;x.ui.break_perf_page=0;edit_page.pad(x,2,100,true);if(x.pad_keys[2][0]!=2)return gen_failure(445,__LINE__);
        edit_page.wheel(x,3);edit_page.pad(x,2,0,false);if(x.pad_keys[2][0]>=0)return gen_failure(446,__LINE__);
    }
    std::printf("RHYTHMER_BREAK_RELEASE cancel_paths=2 B_focus_routes=4 undo_redo=ok saved_hold_mode=compatible held_stretch_page_release=ok\n");

    // General GEN on an Acid track must undo the RNG stream together with MIDI.
    {
        EngineHost h;Shell s(h);auto& x=s.context();x.track().engine=EngineKind::Plugin;x.track().plugin=0;
        std::strcpy(x.project.plugins[0].name,"Acid Engine");std::strcpy(x.project.plugins[0].path,"Plugins/RHYTHMER - Acid Engine");x.project.gen8.assign=0;x.project.gen8.first=x.project.gen8.last=0;
        x.track().acid_gen.seed=0x1a2b3c4d;auto seed=x.track().acid_gen.seed;auto original=x.pattern().events;
        x.gen8_action(1);auto changed=x.track().acid_gen.seed;auto made=x.pattern().events;
        if(changed==seed)return gen_failure(447,__LINE__);
        x.undo();if(x.track().acid_gen.seed!=seed||x.pattern().events!=original)return gen_failure(448,__LINE__);
        x.redo();if(x.track().acid_gen.seed!=changed||x.pattern().events!=made)return gen_failure(449,__LINE__);
    }
    std::puts("RHYTHMER_GEN_ACID general_mutate_seed_undo_redo=ok algorithm=unchanged");

    // A rejected loop returns to its owned browser for retry, not an unusable
    // BREAK menu on the previous instrument. Errors never steal another track.
    for(unsigned route=0;route<3;++route){
        EngineHost h;Shell s(h);auto& x=s.context();x.ui.track=0;x.track().engine=route==2?EngineKind::Sampler:EngineKind::Midi;
        if(route==2)x.track().break_bars=2;
        auto engine=x.track().engine;auto history=x.edits.size();
        x.ui.page=PageId::Patterns;x.ui.sheet_open=true;x.ui.gen8_mode=2;x.ui.break_pending=0;
        x.ui.break_job_track=0;x.ui.break_job_bars=2;x.ui.break_job_from_picker=route!=2;x.ui.break_job_b=route==2;
        x.project.samples.edit(0).frames=8;
        if(route==1)x.select_track(1);
        x.gen_break_tick();if(x.ui.break_pending>=0||x.project.tracks[0].engine!=engine||x.edits.size()!=history)return gen_failure(450,__LINE__);
        if(route!=1){if(x.ui.page!=PageId::Samples||!x.ui.break_browse||x.ui.break_load_b!=(route==2)||x.ui.break_from_picker!=(route!=2))return gen_failure(451,__LINE__);}
        else if(x.ui.page!=PageId::Patterns||x.ui.track!=1||x.ui.sheet_open)return gen_failure(452,__LINE__);
    }
    std::puts("RHYTHMER_BREAK_ERRORS invalid_loop_retry=owned_browser background_error=no_focus_theft previous_track=preserved");

    {
        EngineHost h;Shell s(h);auto& x=s.context();x.project.tracks[0].engine=EngineKind::Midi;
        x.ui.track=1;x.ui.page=PageId::Stage;x.ui.break_pending=0;x.ui.break_job_track=0;x.ui.break_job_bars=2;
        x.project.samples.edit(0).frames=176400;x.prints.freeze[0].state=FreezeState::Deep;
        auto history=x.edits.size();x.gen_break_tick();
        if(x.ui.ask!=UiState::Ask::None||x.ui.page!=PageId::Stage||x.ui.track!=1||x.ui.break_pending>=0||x.project.tracks[0].engine!=EngineKind::Midi||x.edits.size()!=history)return gen_failure(453,__LINE__);
    }
    std::puts("RHYTHMER_BREAK_FROZEN late_completion=cancelled no_surprise_unfreeze_dialog=ok");

    // Replacing a source is not pattern generation. The complete track edit
    // restores source B and the old declared length together on Undo.
    for(unsigned bars : {2u,3u}){
        EngineHost h;Shell s(h);auto& x=s.context();x.track().engine=EngineKind::Sampler;x.track().break_bars=2;
        x.track().pads[0].sample=1;x.track().pads[0].layers[0].sample=2;
        x.track().pads[0].pitch=7;x.track().pads[0].cutoff=42;x.track().break_perf[4]=1;
        x.pattern().length=12*ticks_per_quarter;Event note;note.key=42;note.tick=ticks_per_quarter;note.length=ticks_per_quarter/2;x.pattern().add(note);
        auto original=x.pattern();
        x.ui.break_pending=0;x.ui.break_job_track=0;x.ui.break_job_bars=uint8_t(bars);x.project.samples.edit(0).frames=264600;
        x.gen_break_tick();
        if(!(x.pattern()==original)||x.track().pads[0].sample!=0||x.track().pads[0].pitch!=7||x.track().pads[0].cutoff!=42||x.track().break_perf[4]!=1)return gen_failure(454,__LINE__);
        if(x.track().pads[0].layers[0].sample!=(bars==2?2:no_sample))return gen_failure(455,__LINE__);
        x.undo();
        if(!(x.pattern()==original)||x.track().pads[0].sample!=1||x.track().pads[0].layers[0].sample!=2||x.track().break_bars!=2)return gen_failure(456,__LINE__);
        x.redo();if(!(x.pattern()==original)||x.track().pads[0].sample!=0||x.track().break_bars!=bars)return gen_failure(457,__LINE__);
        auto saved=save_project(x.project,x.patterns);auto restored=std::make_unique<Project>();auto bank=std::make_unique<PatternBank>();
        if(!load_project(saved,*restored,*bank).empty()||!(bank->at[0][0]==original)||restored->tracks[0].pads[0].layers[0].sample!=(bars==2?2:no_sample))return gen_failure(458,__LINE__);
    }
    for(unsigned cancel=0;cancel<3;++cancel){
        EngineHost h;Shell s(h);auto& x=s.context();x.ui.page=PageId::Samples;x.ui.break_browse=true;
        x.track().engine=EngineKind::Sampler;x.track().break_bars=2;x.ui.break_pending=0;x.ui.break_job_track=0;x.ui.break_job_bars=3;
        x.project.samples.edit(0).frames=264600;auto history=x.edits.size();ask(x,UiState::Ask::BreakLength,0);x.gen_break_tick();
        if(x.edits.size()!=history||x.track().break_bars!=2)return gen_failure(459,__LINE__);
        if(cancel==2)x.ui.shift=true;
        ask_button(x,cancel==2?SurfaceButton::WheelPush:cancel?SurfaceButton::TopLeft:SurfaceButton::TopRight);x.gen_break_tick();
        if(cancel){if(x.ui.page!=PageId::Samples||x.edits.size()!=history||x.ui.break_pending>=0)return gen_failure(460,__LINE__);}
        else if(x.ui.page!=PageId::Patterns||x.track().break_bars!=3||x.edits.size()!=history+1)return gen_failure(461,__LINE__);
    }
    {
        std::vector<SampleFile> files(10);files[0].folder=true;files[0].path="folder";files[1].path="loop.wav";
        files[2].bank=true;files[3].instrument=true;files[4].kit=true;files[5].expansion=true;
        files[6].presets=true;files[6].folder=true;files[7].midi=true;files[8].path="loop.flac";files[9].path="loop.mp3";
        auto loops=sample_browser_files(files,true),all=sample_browser_files(files,false);
        if(loops.size()!=4||loops[0].path!="folder"||loops[1].path!="loop.wav"||loops[2].path!="loop.flac"||all.size()!=10)return gen_failure(462,__LINE__);
    }
    // Existing source actions remain visible; generic Shift+pad never assigns a
    // previewed loop to an arbitrary kit pad while the Break browser owns it.
    {
        EngineHost h;Shell s(h);auto& x=s.context();x.track().engine=EngineKind::Sampler;x.track().break_bars=2;
        for(unsigned page=0;page<6;++page){x.ui.page=PageId::Edit;x.ui.break_perf_page=page;edit_page.soft(x,1);
            if(x.ui.page!=PageId::Samples||!x.ui.break_browse||x.ui.break_load_b!=(page==2))return gen_failure(463,__LINE__);}
    }
    std::puts("RHYTHMER_BREAK_P0 replace_A=patterns_and_design_kept length_confirmation=cancel_and_load B_length_policy=undo_redo_atomic save_restore=ok browser_projection=consistent source_routes=6");

    {
        EngineHost h;Shell s(h);auto& x=s.context();x.ui.page=PageId::Samples;x.ui.break_browse=true;x.ui.break_bars=2;
        auto settings=x.project.gen8.bars;auto cards=page_bindings(x.project,x.ui,x.pattern());x.apply(cards[0],4);
        if(x.ui.break_bars!=4||x.project.gen8.bars!=settings)return gen_failure(464,__LINE__);
        x.ui.page=PageId::Patterns;x.ui.sheet_open=true;x.ui.gen8_mode=2;
        if(page_bindings(x.project,x.ui,x.pattern())[0].kind!=Binding::Kind::PatternLength)return gen_failure(465,__LINE__);
    }
