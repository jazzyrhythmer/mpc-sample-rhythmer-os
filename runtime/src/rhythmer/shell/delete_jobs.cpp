// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/delete_jobs.h"
#include "rhythmer/model/card_folders.h"
#include "rhythmer/plugin/plugin_rack.h"
#include "rhythmer/shell/ask.h"
#include "rhythmer/shell/context.h"
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/shell/sample_lists.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace rhythmer {
namespace fs = std::filesystem;

namespace {
// `path` is `folder` or lies in it.
bool within(const std::string& path, const std::string& folder) {
    if (folder.empty() || path.size() < folder.size() || path.compare(0, folder.size(), folder)) return false;
    return path.size() == folder.size() || path[folder.size()] == '/' || folder.back() == '/';
}

// The data folder that is an MPC 3 track's, program's or project's own; empty for any other file.
std::string owned_folder(const std::string& path) {
    const fs::path file(path);
    std::string kind = file.extension().string();
    for (auto& c : kind) c = char(std::tolower(static_cast<unsigned char>(c)));
    const char* tail = kind == ".xty" ? "_[TrackData]" : kind == ".xpm" ? "_[ProgramData]" : kind == ".xpj" ? "_[ProjectData]" : nullptr;
    return tail ? (file.parent_path() / (file.stem().string() + tail)).string() : std::string();
}

// A sample slot of the open project plays `path`, something in it, or something in its data folder.
bool sample_in_use(const Context& c, const std::string& path) {
    const std::string data = owned_folder(path);
    for (const auto& ref : c.project.samples) {
        if (!ref.used()) continue;
        std::string file = ref.path;
        part_of_file(ref.path, &file); // "<file>#<pad>": a pad of a program (shell/sample_lists.h)
        if (within(file, path) || (!data.empty() && within(file, data))) return true;
    }
    return false;
}

bool plugin_in_use(const Context& c, const std::string& path) {
    for (const auto& ref : c.project.plugins)
        if (ref.used() && within(ref.path, path)) return true;
    return false;
}

void ask_delete(Context& c, const std::string& path, const std::string& name, DeleteWhat what) {
    c.delete_path = path;
    std::snprintf(c.ui.ask_name.data(), c.ui.ask_name.size(), "%s", name.c_str());
    ask(c, UiState::Ask::DeleteFile, int(what));
}
} // namespace

bool samples_erase(Context& c) {
    if (!c.library) return false;
    const auto files = c.library->files();
    if (files.empty()) return false;
    const auto& file = files[std::min<size_t>(c.ui.files_cursor, files.size() - 1)]; // as the page draws the cursor
    if (file.list) return false; // STARRED and RECENT themselves stay
    if (list_folder(c.library->folder())) { c.library->unlist(file.path); return true; } // in a list: off it, the file kept
    const std::string card = c.library->card(), samples = samples_folder(card); // <card>/Rhythmer/Samples
    const std::string name = fs::path(file.path).filename().string();
    if (part_of_file(file.path)) { c.notice("Delete: its file, not a preset of it"); return true; }
    if (card.empty() || !within(file.path, card) || file.path == card) { c.notice("Delete: files on the card only"); return true; }
    if (fs::path(file.path).parent_path().string() == card) { c.notice("Delete: not the card's own folders"); return true; }
    // In Rhythmer's folder only what is in its Samples folder: the samples are the user's, the rest Rhythmer's.
    if (within(file.path, (fs::path(card) / "Rhythmer").string()) && (!within(file.path, samples) || file.path == samples)) {
        c.notice("Delete: not in Rhythmer's folder");
        return true;
    }
    if (sample_in_use(c, file.path)) { c.notice("In use by this project"); return true; }
    const bool program = !file.folder && !owned_folder(file.path).empty();
    ask_delete(c, file.path, name, file.folder && !file.presets ? DeleteWhat::Folder : program ? DeleteWhat::Program : DeleteWhat::File);
    return true;
}

bool plugins_erase(Context& c) {
    if (!c.plugins || !c.library) return false;
    const auto view = c.plugins->view();
    if (view.files.empty()) return false;
    const auto& file = view.files[std::min<size_t>(c.ui.plugins_cursor, view.files.size() - 1)];
    if (view.root.empty() || !within(file.path, view.root) || file.path == view.root) { c.notice("Delete: plug-ins on the card only"); return true; }
    if (file.path == view.folder) { c.notice("Delete: its bundle, not one plug-in of it"); return true; } // a bundle's plug-ins, listed
    if (plugin_in_use(c, file.path)) { c.notice("In use by this project"); return true; }
    ask_delete(c, file.path, file.name.empty() ? fs::path(file.path).filename().string() : file.name,
               file.folder ? DeleteWhat::PluginFolder : DeleteWhat::Plugin);
    return true;
}

void delete_asked(Context& c) {
    if (!c.library || c.delete_path.empty()) return;
    const auto what = DeleteWhat(c.ui.ask_target);
    const bool plugin = what == DeleteWhat::Plugin || what == DeleteWhat::PluginFolder;
    // Asked again here: the project may have taken it up since the question.
    if (plugin ? plugin_in_use(c, c.delete_path) : sample_in_use(c, c.delete_path)) { c.notice("In use by this project"); c.delete_path.clear(); return; }
    if (!plugin) { c.stop_preview(); c.library->preview({}); } // what was heard of it stops
    c.library->remove(c.delete_path);
    c.delete_path.clear();
}

void delete_tick(Context& c) {
    if (!c.library || !c.plugins) return;
    const unsigned done = c.library->removals();
    if (done == c.removals_seen) return;
    c.removals_seen = done;
    if (c.ui.page == PageId::Plugins) c.plugins->browse(c.plugins->view().folder); // its rows, without what went
}

} // namespace rhythmer
