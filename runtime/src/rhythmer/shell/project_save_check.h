// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "rhythmer/shell/project_store.h"
#include "rhythmer/shell/recovery_file.h"
#include "rhythmer/shell/shell.h"
#include "rhythmer/shell/ask.h"
#include "rhythmer/pages/pages.h"
#include <fstream>
#include <iostream>

namespace rhythmer {
struct ProjectSavingChecks {
    static void setup(ProjectStore& store,const std::string& root) {
        store.root=root;store.started=true;store.known.root=root;store.scan();
    }
    static void service(ProjectStore& store) {
        ProjectStore::Request* raw=nullptr;
        while(store.requests.pop(raw)){std::unique_ptr<ProjectStore::Request> r(raw);store.run(*r);}
    }
    static std::vector<uint8_t> read(const std::string& path) {
        std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};
    }
    static int run(const std::string& out, const std::function<void(Shell&,const std::string&)>& capture = {}) {
        namespace fs=std::filesystem;
        fs::create_directories(out);
        const auto card=fs::absolute(out)/("card-"+recovery_file::new_id()),root=card/"Rhythmer/Projects";
        fs::create_directories(root);
        auto host=std::make_unique<EngineHost>();auto shell=std::make_unique<Shell>(*host);
        ProjectStore store;setup(store,root.string());shell->set_store(&store);auto& c=shell->context();
        unsigned checks=0;
        auto check=[&](bool ok,const char* text){++checks;if(!ok)std::cerr<<"PROJECT SAVE FAIL "<<checks<<": "<<text<<'\n';return ok;};
#define SAVE_CHECK(x,text) do{if(!check((x),text))return 1;}while(false)
        auto pump=[&]{for(unsigned i=0;i<5;++i){c.project_save_tick();service(store);}};
        auto recovery_count=[&]{unsigned n=0;std::error_code e;for(const auto& x:fs::directory_iterator(card/"Rhythmer/Autosave",e))n+=x.path().extension()==".mldp";return n;};
        auto load=[&](const std::string& path){auto p=std::make_unique<Project>();auto b=std::make_unique<PatternBank>();
            const auto error=load_project(read(path),*p,*b);if(!error.empty())throw std::runtime_error(error);return p;};
        auto tap=[&](SurfaceButton b){for(bool down:{true,false}){SurfaceEvent e;e.input.kind=SurfaceInput::Kind::Button;e.input.index=uint8_t(b);e.input.pressed=down;shell->surface(e,10000+checks*30);}};
        c.project.tempo=1260;c.dirty=true;
        SAVE_CHECK(c.autosave(),"unsaved session checkpoint accepted");pump();
        const std::string first_id=c.project.recovery_id;
        const auto recovery=(card/"Rhythmer/Autosave"/(first_id+".mldp")).string();
        SAVE_CHECK(recovery_file::valid_id(first_id),"stable 128-bit identity");
        SAVE_CHECK(c.project_path.empty()&&c.dirty,"autosave does not assign a manual path or clear UNSAVED");
        SAVE_CHECK(fs::is_empty(root)&&recovery_count()==1,"unsaved session creates one recovery and no Project N files");
        for(unsigned i=0;i<12;++i){c.project.tempo=1270+i;c.dirty=true;SAVE_CHECK(c.autosave(),"repeated checkpoint accepted");pump();}
        SAVE_CHECK(recovery_count()==1&&load(recovery)->tempo==1281,"updates replace one recovery manifest");
        auto writes=store.saves();c.autosave();pump();SAVE_CHECK(store.saves()==writes,"unchanged state does not rewrite recovery");
        // New upstream JAMS must survive Rhythmer's queued recovery and manual save paths.
        JamLog run;run.bars.resize(2);run.bars[0].tempo=1281;run.bars[1].tempo=1290;
        run.bars[0].pattern[0]=3;run.bars[1].pattern[0]=7;
        SAVE_CHECK(c.jams.keep(std::move(run)),"two-bar JAM fixture kept");c.dirty=true;
        SAVE_CHECK(c.autosave(),"JAMS-only change queues a new checkpoint");pump();
        JamList recovered_jams;
        SAVE_CHECK(read_jams(read(recovery),recovered_jams)&&recovered_jams==c.jams,"JAMS survives recovery serialization");
        // First SAVE uses the real Project-page click and folder picker.
        c.open(PageId::Project);c.ui.project_list=false;c.ui.project_pick=0;if(capture)capture(*shell,"01-project");tap(SurfaceButton::TopLeft);
        SAVE_CHECK(c.ui.project_save_picker&&c.ui.page==PageId::Project,"first SAVE opens folder browser");
        fs::create_directories(card/"Music/Set A");c.project_folder=":card/Music/Set A";store.list(c.project_folder);service(store);
        if(capture)capture(*shell,"02-save-folder");
        tap(SurfaceButton::TopRight);
        if(capture)capture(*shell,"03-name");
        SAVE_CHECK(c.ui.name.open(),"USE FOLDER opens native name entry");
        c.project_save_named("Session");pump();
        const auto manual=(card/"Music/Set A/Session.mldp").string();
        SAVE_CHECK(c.project_path==manual&&!c.dirty&&fs::exists(manual),"SAVE honors selected SD folder and commits only on success");
        SAVE_CHECK(first_id==c.project.recovery_id,"first manual SAVE keeps session identity");
        if(capture){c.ui.project_list=false;capture(*shell,"04-saved");c.ui.project_list=true;c.ui.project_cursor=1;capture(*shell,"04-project-list");}
        JamList manual_jams;
        SAVE_CHECK(read_jams(read(manual),manual_jams)&&manual_jams==c.jams,"JAMS survives manual folder SAVE");
        const auto original=read(manual);
        c.project.tempo=1450;c.dirty=true;c.autosave();pump();
        SAVE_CHECK(read(manual)==original&&load(recovery)->tempo==1450,"autosave preserves manual file byte for byte");
        SAVE_CHECK(recovery_count()==1,"manual naming did not create a second recovery");
        c.save_project();SAVE_CHECK(c.dirty&&c.project_path==manual,"queued SAVE is not prematurely reported as saved");pump();
        SAVE_CHECK(!c.dirty&&load(manual)->tempo==1450,"normal SAVE updates the existing selected file");
        // Save As makes a new project, but does not overwrite the original or inherit its recovery ID.
        c.ui.page=PageId::Project;c.ui.project_list=false;c.ui.shift=true;tap(SurfaceButton::TopLeft);c.ui.shift=false;
        SAVE_CHECK(c.ui.project_save_picker&&c.ui.project_save_as,"SHIFT B1 opens SAVE AS through native input dispatch");
        c.project_folder=":card/Music/Set A";c.project_save_choose_folder();c.project_save_named("Session B");
        const auto before_record_engine=c.track().engine;c.track().engine=EngineKind::Audio;
        SAVE_CHECK(!c.record_audio(0)&&std::strstr(c.ui.notice,"Wait for project save"),"audio recording cannot start against a project path being changed by SAVE AS");
        c.freeze(Context::PrintAsk{});
        SAVE_CHECK(std::strstr(c.ui.notice,"Wait for project save"),"Freeze waits for a changing SAVE AS path instead of printing into the previous folder");
        c.track().engine=before_record_engine;pump();
        const std::string second_id=c.project.recovery_id;
        SAVE_CHECK(second_id!=first_id&&load(manual)->tempo==1450,"SAVE AS forks identity and preserves original");
        SAVE_CHECK(c.recovery_status.empty(),"a new project copy cannot display its source project recovery timestamp");
        c.open_project_save(true);c.project_folder=":card/Music/Set A";c.project_save_choose_folder();c.project_save_named("Session B");
        SAVE_CHECK(c.ui.ask==UiState::Ask::OverwriteProject,"SAVE AS to current file asks before replacement");
        ask_button(c,SurfaceButton::TopRight);pump();
        SAVE_CHECK(std::string(c.project.recovery_id)==second_id,"SAVE AS to the same current file keeps its existing recovery identity");
        c.project.tempo=1510;c.dirty=true;c.autosave();pump();
        SAVE_CHECK(recovery_count()==2,"two distinct projects each have one recovery");
        // A save finishing after another edit must leave the newer edit unsaved.
        c.save_project();c.project.tempo=1520;c.dirty=true;pump();
        SAVE_CHECK(c.dirty&&load(c.project_path)->tempo==1510,"edits during write are not incorrectly marked SAVED");
        // Folder browsing cannot silently redirect writes from internal/system/recovery folders.
        SAVE_CHECK(store.save_directory(":internal").empty()&&store.save_directory(":recovery").empty()&&store.save_directory("MPC Sample").empty(),"read-only browser locations cannot be save destinations");
        SAVE_CHECK(store.save_directory(":card/../outside").empty()&&store.save_directory(":card/autorun").empty(),"destination stays on card outside system folders");
        // New folder belongs to the actual chosen directory, even outside Rhythmer/Projects.
        store.new_folder_at(":card/Music/Set A");service(store);
        SAVE_CHECK(fs::is_directory(card/"Music/Set A/Folder 1")&&!fs::exists(root/"Folder 1"),"NEW FOLDER uses selected save folder");
        const auto path_before=c.project_path;const auto id_before=std::string(c.project.recovery_id);
        c.open_project_save(true);c.back();SAVE_CHECK(c.ui.project_save_picker,"Shift-Back moves up a folder, retaining save mode");
        c.cancel_project_save();SAVE_CHECK(c.project_path==path_before&&id_before==c.project.recovery_id,"cancel does not change project identity or path");
        c.open_project_save(true);c.project_folder=":card/Music/Set A";c.project_save_choose_folder();c.project_save_named("Session");
        if(capture)capture(*shell,"05-overwrite");
        SAVE_CHECK(c.ui.ask==UiState::Ask::OverwriteProject,"existing file requires explicit REPLACE");
        SAVE_CHECK(std::string(c.ui.ask_name.data())=="Session","replacement confirmation identifies the destination project");
        ask_button(c,SurfaceButton::TopLeft);SAVE_CHECK(read(manual)==original||load(manual)->tempo==1450,"overwrite cancel preserves destination");
        // Failure leaves the original name/path and dirty status intact.
        const auto invalid=card/"Music/Set A/Blocked.mldp";fs::create_directories(invalid);
        SAVE_CHECK(c.queue_project_save(invalid.string(),false,true),"failure test queues a replacement");pump();
        SAVE_CHECK(c.dirty&&c.project_path==path_before&&id_before==c.project.recovery_id,"failed save cannot adopt a new file or clear unsaved changes");
        // OPEN and NEW share the same non-destructive prompt, including physical encoder/Back.
        c.request_project_open(manual);if(capture)capture(*shell,"06-unsaved");
        SAVE_CHECK(c.ui.ask==UiState::Ask::ProjectChanges&&c.after_project_save==1,"OPEN with edits asks SAVE DISCARD CANCEL");
        c.ui.shift=true;tap(SurfaceButton::WheelPush);c.ui.shift=false;
        SAVE_CHECK(c.ui.ask==UiState::Ask::None&&c.after_project_save==0&&c.project_path==path_before,"Shift encoder cancels project switch");
        c.ask_new_project();SAVE_CHECK(c.ui.ask==UiState::Ask::ProjectChanges,"NEW does not silently overwrite");
        ask_button(c,SurfaceButton::TopMiddle);service(store);
        SAVE_CHECK(c.ui.ask==UiState::Ask::NewProject&&store.active_recovery.empty(),"DISCARD retains manual file and dismisses startup recovery");
        ask_button(c,SurfaceButton::TopLeft);
        // Media copies and origin are stored with the recovery, not inferred from its display name.
        c.project_path=manual;c.project.tempo=1610;c.dirty=true;
        fs::create_directories(card/"Music/Set A/Session Audio");
        {std::ofstream file(card/"Music/Set A/Session Audio/take.wav");file<<"test take";}
        SAVE_CHECK(c.autosave(),"checkpoint after cancelled project switch accepted");pump();
        const auto latest=(card/"Rhythmer/Autosave"/(std::string(c.project.recovery_id)+".mldp")).string();
        SAVE_CHECK(recovery_file::origin(read(latest))==manual,"recovery embeds the original path");
        const auto media=fs::path(recovery_file::media_path(latest,read(latest)));
        const auto audio=media.parent_path()/(media.stem().string()+" Audio/take.wav");
        SAVE_CHECK(fs::is_regular_file(audio),"audio takes accompany recovery manifest");
        store.load(latest);service(store);auto recovered=store.take_loaded();
        SAVE_CHECK(recovered&&recovered->recovery&&recovered->origin==manual,"loader identifies recoveries without making them manual projects");
        const auto settings=ProjectStore::read_settings_file(root.string());
        SAVE_CHECK(settings.recovery==latest&&settings.last!=latest,"startup checkpoint is separate from last manually opened file");
        ProjectStore restarted;restarted.mount_projects(root.string());
        SAVE_CHECK(restarted.known.recovery_offer==latest,"restart offers the active recovery checkpoint");
        auto resumed=restarted.take_loaded();
        SAVE_CHECK(!resumed||resumed->path!=latest,"restart does not silently replace the manually saved project");
        restarted.dismiss_recovery();service(restarted);
        ProjectStore after_later;after_later.mount_projects(root.string());
        SAVE_CHECK(after_later.known.recovery_offer.empty()&&fs::exists(latest),"LATER suppresses next startup offer without deleting recovery");
        auto legacy=std::make_unique<Project>();auto legacy_bank=std::make_unique<PatternBank>();
        const auto legacy_path=(root/"Legacy.mldp").string();
        SAVE_CHECK(recovery_file::atomic_write(legacy_path,save_project(*legacy,*legacy_bank)).empty(),"legacy fixture writes");
        const auto legacy_id=recovery_file::legacy_id(legacy_path);
        store.rename(legacy_path,"Legacy Renamed");service(store);
        auto renamed=load((root/"Legacy Renamed.mldp").string());
        SAVE_CHECK(renamed->recovery_id==legacy_id&&std::string(renamed->name)=="Legacy Renamed","legacy Rename pins identity and updates display name only");
        fs::create_directories(root/"Moved");store.move((root/"Legacy Renamed.mldp").string(),"Moved");service(store);
        SAVE_CHECK(load((root/"Moved/Legacy Renamed.mldp").string())->recovery_id==legacy_id,"Move preserves project identity on reopening");
        store.folder.clear();store.scan();store.duplicate(manual);service(store);
        auto duplicate=load((root/"Session copy.mldp").string());
        SAVE_CHECK(std::string(duplicate->recovery_id)!=load(manual)->recovery_id,"Duplicate creates an independent project identity");
        SAVE_CHECK(fs::is_regular_file(root/"Session copy Audio/take.wav"),"Duplicate preserves copied audio takes");
        const std::string long_name(31,'x');const auto long_path=(root/(long_name+".mldp")).string();
        SAVE_CHECK(recovery_file::atomic_write(long_path,save_project(*legacy,*legacy_bank)).empty()&&recovery_file::atomic_write((root/"Moved"/(long_name+".mldp")).string(),save_project(*legacy,*legacy_bank)).empty(),"long-name collision fixtures write");
        store.move(long_path,"Moved");service(store);
        SAVE_CHECK(load((root/"Moved"/(long_name+" 2.mldp")).string())->recovery_id==recovery_file::legacy_id(long_path),"Move with a long-name collision retains identity and a valid display name");
        // A stray temporary file never replaces the last valid checkpoint.
        {std::ofstream file(latest+".new");file<<"interrupted write";}
        SAVE_CHECK(load(latest)->tempo==1610,"interrupted temporary write preserves last valid checkpoint");
        fs::remove(latest+".new");
        const auto keep_path=c.project_path;
        const auto corrupt=(root/"Corrupt.mldp").string();{std::ofstream file(corrupt);file<<"broken project";}
        store.load(corrupt);service(store);c.ui.ask=UiState::Ask::None;SurfaceLights lamps;shell->lights(lamps,200000);
        SAVE_CHECK(c.project_path==keep_path&&std::strstr(c.ui.notice,"project"),"corrupt LOAD reports its error and preserves the current project");
        // A real failed exit checkpoint returns to actionable choices rather than displaying SAVING forever.
        const auto autos=card/"Rhythmer/Autosave",preserved=card/"Rhythmer/Autosave-preserved";
        const auto before_failed_exit=read(latest);
        fs::rename(autos,preserved);{std::ofstream blocked(autos);blocked<<"not a directory";}
        c.project.tempo=1620;c.dirty=true;c.ui.ask=UiState::Ask::None;c.ui.exit_open=true;c.ui.exit_off=false;c.ui.exit_saved=false;
        tap(SurfaceButton::TopMiddle);pump();shell->lights(lamps,200100);
        SAVE_CHECK(c.ui.exit_open&&!c.ui.exit_off&&!c.ui.exit_saved&&!c.save_pending,"failed POWER OFF checkpoint returns to STAY / POWER OFF / MPC APP choices");
        tap(SurfaceButton::TopLeft);SAVE_CHECK(!c.ui.exit_open,"STAY cancels a failed exit and keeps the project open");
        fs::remove(autos);fs::rename(preserved,autos);
        SAVE_CHECK(read(latest)==before_failed_exit,"failed exit checkpoint leaves the previous recovery intact");
        // A cached Rename/Move notification must not redirect a later SAVE AS back to the old destination.
        c.project=*load(manual);c.project_path=manual;c.dirty=false;
        store.rename(manual,"Renamed Session");service(store);c.project_save_tick();
        const auto renamed_current=(card/"Music/Set A/Renamed Session.mldp").string();
        SAVE_CHECK(c.project_path==renamed_current,"current project follows its successful rename acknowledgement");
        c.open_project_save(true);c.project_folder=":card/Music/Set A";c.project_save_choose_folder();c.project_save_named("Session");pump();
        shell->lights(lamps,200200);
        SAVE_CHECK(c.project_path==manual&&fs::exists(renamed_current),"reusing an old name cannot replay a stale path change or redirect SAVE");
        // Restoring to the original file, including SAVE AS, is still the same project.
        c.project=*load(latest);c.project_path=recovery_file::media_path(latest,read(latest));c.recovered_project=true;c.recovery_origin=manual;c.dirty=true;
        const auto recovered_id=std::string(c.project.recovery_id);
        c.open_project_save(true);c.project_folder=":card/Music/Set A";c.project_save_choose_folder();c.project_save_named("Session");
        SAVE_CHECK(c.ui.ask==UiState::Ask::OverwriteProject,"restoring over the original project still requires confirmation");
        ask_button(c,SurfaceButton::TopRight);pump();
        SAVE_CHECK(std::string(c.project.recovery_id)==recovered_id&&c.project_path==manual,"restoring to the original file does not fork another recovery entry");
        const auto unsaved_id=recovery_file::new_id();std::snprintf(c.project.recovery_id,sizeof(c.project.recovery_id),"%s",unsaved_id.c_str());
        c.project_path=recovery_file::media_path(latest,read(latest));c.recovered_project=true;c.recovery_origin.clear();c.dirty=true;
        c.open_project_save(true);c.project_folder=":card/Music/Set A";c.project_save_choose_folder();c.project_save_named("Recovered Unsaved");pump();
        SAVE_CHECK(std::string(c.project.recovery_id)==unsaved_id,"the first manual save of a recovered unsaved session retains its recovery identity");
        const auto before_pending_offer=read(latest);const auto old_offer=store.known.recovery_offer;const auto old_shown=c.recovery_offer_shown;
        store.known.recovery_offer=latest;c.recovery_offer_shown=false;c.dirty=true;
        SAVE_CHECK(!c.autosave()&&read(latest)==before_pending_offer,"unresolved startup recovery is protected from autosave until the user chooses");
        store.known.recovery_offer=old_offer;c.recovery_offer_shown=old_shown;
        c.ui.ask=UiState::Ask::None;c.ui.page=PageId::Project;c.ui.project_list=true;c.project_folder=":recovery";store.list(c.project_folder);service(store);c.ui.project_cursor=1;
        if(capture)capture(*shell,"07-recovery");
        ask(c,UiState::Ask::RecoverProject,0);if(capture)capture(*shell,"08-recover");
        std::cout<<"PROJECT_SAVING_OK checks="<<checks<<" recovery_files="<<recovery_count()<<'\n';
        return 0;
#undef SAVE_CHECK
    }
};
} // namespace rhythmer
