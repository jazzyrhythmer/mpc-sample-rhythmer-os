// SPDX-License-Identifier: AGPL-3.0-only
// Manual saves and one recovery checkpoint per project, using the native project serializer/browser.
#include "rhythmer/shell/context.h"
#include "rhythmer/shell/project_store.h"
#include "rhythmer/shell/recovery_file.h"
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/shell/looper_jobs.h"
#include "rhythmer/shell/ask.h"
#include "rhythmer/plugin/plugin_rack.h"
#include "rhythmer/engine/engine_host.h"
#include <filesystem>
#include <ctime>
#include "rhythmer/shell/print_engine.h"

namespace rhythmer {
namespace fs=std::filesystem;
uint64_t Context::project_save_hash() {
    PluginStates states; if(plugins)states=plugins->states();
    auto bytes=rhythmer::save_project(project,patterns,&states,&prints);
    write_jams(bytes,jams);
    return recovery_file::hash(bytes);
}
bool Context::queue_project_save(const std::string& path, bool recovery, bool new_identity) {
    if(!store||path.empty())return false;
    if(save_pending){notice("Wait for saving to finish");return false;}
    ProjectStore::View view;store->view(view);
    if(view.root.empty()){notice("No SD card for saving");return false;}
    if(ui.recording||host_recording()||(printer&&(printer->busy()||freeze_waiting))){notice("Finish recording or printing first");return false;}
    save_media=path;
    const auto slot=recovery?recovery_file::next_slot(path):std::string();
    if(recovery){std::vector<uint8_t> meta(8,0);recovery_file::append_text(meta,"RCMP",slot);save_media=recovery_file::media_path(path,meta);}
    save_loop_errors=library?library->loop_save_errors():0;
    save_loop_files(*this,save_media);keep_chop();free_unused_samples();
    save_before_hash=project_save_hash();
    auto copy=std::make_unique<Project>(project);
    std::string id=copy->recovery_id;
    if(new_identity||!recovery_file::valid_id(id))id=new_identity||project_path.empty()?recovery_file::new_id():recovery_file::legacy_id(project_path);
    std::snprintf(copy->recovery_id,sizeof(copy->recovery_id),"%s",id.c_str());
    if(!recovery)std::snprintf(copy->name,sizeof(copy->name),"%s",fs::path(path).stem().c_str());
    PluginStates states;if(plugins)states=plugins->states();
    save_bytes=rhythmer::save_project(*copy,patterns,&states,&prints);
    write_jams(save_bytes,jams);
    if(recovery){recovery_file::append_origin(save_bytes,recovered_project?recovery_origin:project_path);recovery_file::append_text(save_bytes,"RCMP",slot);}
    save_from=project_path.empty()?audio_staging_from:project_path;
    save_target=path;save_id=id;save_is_recovery=recovery;save_pending=true;save_queued=false;last_save_failed=false;++save_ticket;
    return true;
}
void Context::open_project_save(bool as_new) {
    if(!store)return;
    if(save_pending){notice("Wait for saving to finish");return;}
    ProjectStore::View v;store->view(v);if(v.root.empty()){notice("No SD card for saving");return;}
    save_old_folder=project_folder;
    save_picker_from=project_path;
    ui.project_save_as=as_new;ui.project_save_picker=true;ui.project_list=true;
    ui.sheet_open=ui.move_open=false;ui.project_kit_only=false;ui.project_cursor=0;
    const auto initial=recovered_project?recovery_origin:project_path;
    if(!initial.empty())project_folder=card_folder_of(fs::path(v.root).parent_path().parent_path().string(),v.root,fs::path(initial).parent_path().string());
    else project_folder=last_save_folder;
    if(store->save_directory(project_folder).empty())project_folder.clear();
    open(PageId::Project);store->list(project_folder);
    notice(as_new?"SAVE AS: choose a folder":"SAVE: choose a folder");
}
void Context::cancel_project_save() {
    ui.project_save_picker=false;ui.name.target=NameTarget::None;
    project_folder=save_old_folder;
    if(store)store->list(project_folder);
    save_target.clear();after_project_save=0;after_project_path.clear();
    notice("Save cancelled");
}
void Context::cancel_name() {
    const auto target=ui.name.target;ui.name.target=NameTarget::None;
    if(target==NameTarget::ProjectSave||target==NameTarget::ProjectSaveAs) {
        // Back from naming returns to the destination browser; B1 CANCEL there cancels the entire save.
        ui.project_save_picker=true;
    }
}
void Context::project_save_choose_folder() {
    if(!store)return;
    auto dir=store->save_directory(project_folder);
    if(dir.empty()){notice("Choose a writable SD folder");return;}
    save_target=dir;
    open_name(ui.project_save_as?NameTarget::ProjectSaveAs:NameTarget::ProjectSave,project.name,sizeof(project.name)-1,false);
}
void Context::project_save_named(const std::string& name) {
    if(!store)return;
    if(name=="."||name==".."||name.find_first_of("/\\\r\n")!=std::string::npos){notice("Use a file name without slashes");project_save_choose_folder();return;}
    const auto dir=store->save_directory(project_folder);
    if(dir.empty()){notice("Folder is not writable");return;}
    save_target=(fs::path(dir)/(name+".mldp")).string();
    std::error_code error;
    if(fs::exists(save_target,error)) {std::snprintf(ui.ask_name.data(),ui.ask_name.size(),"%s",name.c_str());ask(*this,UiState::Ask::OverwriteProject,0);return;}
    commit_project_save();
}
void Context::commit_project_save() {
    if(save_target.empty())return;
    const auto target=save_target;
    // First SAVE keeps the session's ID; SAVE AS deliberately creates a distinct project.
    const auto& prior_manual=recovered_project?recovery_origin:project_path;
    std::error_code same_error;
    const bool same_file=!prior_manual.empty()&&(fs::path(target).lexically_normal()==fs::path(prior_manual).lexically_normal()||fs::equivalent(target,prior_manual,same_error));
    const bool independent=ui.project_save_as&&!prior_manual.empty()&&!same_file;
    if(!queue_project_save(target,false,independent))return;
    ui.project_save_picker=false;ui.name.target=NameTarget::None;
    last_save_folder=project_folder;notice("Saving project...");
}
void Context::request_project_open(const std::string& path) {
    if(!store||path.empty())return;
    if(save_pending){notice("Wait for saving to finish");return;}
    if(dirty){after_project_save=1;after_project_path=path;ask(*this,UiState::Ask::ProjectChanges,0);return;}
    store->load(path);
}
void Context::project_changes_answer(bool save) {
    if(save){save_project();return;}
    // The current project stays recoverable until the replacement actually opens.
    project_continue();
}
void Context::project_continue() {
    const int next=after_project_save;const auto path=after_project_path;
    after_project_save=0;after_project_path.clear();
    if(next==1&&store) store->load(path);
    else if(next==2)ask(*this,UiState::Ask::NewProject,0);
    else if(next==3)start_new_project(false);
}
void Context::project_save_tick() {
    if(!store)return;
    if(save_pending&&!save_queued&&save_bytes.empty()) {
        if(host.playing()||host.song_playing()||ui.recording||host_recording()||(printer&&printer->busy()))return;
        save_pending=false;
        if(!autosave()){last_save_failed=true;notice("Could not save recovery");return;}
    }
    if(save_pending&&!save_queued&&!(library&&library->loops_busy())) {
        if(library&&library->loop_save_errors()!=save_loop_errors){save_pending=false;last_save_failed=true;after_project_save=0;after_project_path.clear();notice("Loop save failed: project was not replaced");return;}
        save_queued=store->save(save_target,std::move(save_bytes),save_from,save_is_recovery,save_ticket,save_media);
        if(!save_queued){save_pending=false;last_save_failed=true;notice("Save queue full: try SAVE again");after_project_save=0;after_project_path.clear();}
    }
    while(auto result=store->take_save_result()) {
        if(!result->moved_from.empty()) {
            bool current=project_path==result->moved_from;
            if(current)project_path=result->path;
            if(recovered_project&&recovery_origin==result->moved_from){recovery_origin=result->path;current=true;}
            if(current)std::snprintf(project.name,sizeof(project.name),"%s",fs::path(result->path).stem().c_str());
            continue;
        }
        if(!save_pending||result->ticket!=save_ticket)continue;
        save_pending=save_queued=false;
        if(!result->error.empty()){last_save_failed=true;notice(result->error);after_project_save=0;after_project_path.clear();continue;}
        const bool unchanged=project_save_hash()==save_before_hash;
        if(!save_is_recovery) {
            if(std::string(project.recovery_id)!=save_id)recovery_status.clear();
            std::snprintf(project.recovery_id,sizeof(project.recovery_id),"%s",save_id.c_str());
            std::snprintf(project.name,sizeof(project.name),"%s",fs::path(result->path).stem().c_str());
            project_path=result->path;recovered_project=false;recovery_origin.clear();import_unsaved=false;
            dirty=!unchanged;
            if(unchanged)recovery_hash=project_save_hash();
            notice(unchanged?"Project saved":"Saved; newer changes are unsaved");
            if(after_project_save&&unchanged)project_continue();
            else if(after_project_save){after_project_save=0;after_project_path.clear();notice("New changes: SAVE again before leaving");}
        } else {
            if(unchanged)recovery_hash=project_save_hash();
            auto time=std::time(nullptr);std::tm local{};localtime_r(&time,&local);
            char text[40];std::snprintf(text,sizeof text,"Recovery saved %02d:%02d",local.tm_hour,local.tm_min);recovery_status=text;
        }
    }
    if(!recovery_offer_shown&&!save_pending&&!ui.loading&&samples_to_load>=sample_slots&&ui.ask==UiState::Ask::None&&!ui.name.open()&&!ui.exit_open) {
        ProjectStore::View v;store->view(v);
        if(!v.recovery_offer.empty()){recovery_offer=v.recovery_offer;recovery_offer_shown=true;ask(*this,UiState::Ask::RecoverProject,0);}
    }
}
} // namespace rhythmer
