// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// PROJECT (Shift + Pad 16): SAVE, OPEN and NEW using native tiles and browser.
// SHIFT + B1 chooses SAVE AS; SHIFT + B3 opens per-project RECOVERY entries.
// Save destinations reuse the native folder list with explicit USE FOLDER and name confirmation.
// OPEN/NEW protect unsaved work; recovery never overwrites manual files.
#include "rhythmer/pages/common.h"
#include "rhythmer/pages/pages.h"
#include "rhythmer/shell/key_pick.h"
#include "rhythmer/shell/project_store.h"
#include "rhythmer/ui/widgets.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>
#include <chrono>
#include <ctime>
#include "rhythmer/shell/recovery_file.h"

namespace rhythmer {
namespace {
using namespace ui;
constexpr unsigned visible = list_rows;
const juce::Colour& amber = colour::amber; // the palette in use, set each frame (a copy keeps the first palette's)

enum Action { Load, Save, SaveAsNew, Rename, Duplicate, MoveTo, NewFolder, OpenAtStart, Delete, LoadKit, Revert };

// The sheet's rows for an entry: the open project's, or another's with Load first. REVERT (issue 18) on the open
// one while it has a file and changes since it was saved: the file opened again, the changes left behind.
unsigned actions(bool open, std::array<Action, 9>& out, bool mpc = false, bool revert = false) {
    if (mpc) { out[0] = Load; out[1] = LoadKit; return 2; } // the MPC's own file is only ever read
    if (open && revert) { out = {Save, Revert, SaveAsNew, Rename, Duplicate, MoveTo, NewFolder, OpenAtStart, Delete}; return 9; }
    if (open) { out = {Save, SaveAsNew, Rename, Duplicate, MoveTo, NewFolder, OpenAtStart, Delete}; return 8; }
    out = {Load, LoadKit, Duplicate, Rename, MoveTo, NewFolder, OpenAtStart, Delete};
    return 8;
}
// Whether the open project can go back to its file (REVERT on its sheet).
bool can_revert(bool dirty, const std::string& path) { return dirty && !path.empty(); }

// The card itself: Rhythmer/Projects' grandparent.
std::string card_dir(const ProjectStore::View& v) { return v.root.empty() ? std::string() : std::filesystem::path(v.root).parent_path().parent_path().string(); }

// The list's rows: SD card access at the top of every folder, then the folder's entries.
constexpr unsigned first_entry = 1;
const ProjectEntry* entry_at(const ProjectStore::View& v, unsigned row) {
    return row >= first_entry && row - first_entry < v.entries.size() ? &v.entries[row - first_entry] : nullptr;
}
// SD card access (parity phase 10b), at the top, as the MPC's Project menu ends with it.
bool on_card(unsigned row) { return row == 0; }

// Move to folder: where a project can go (the top, then every folder by
// name), all but the folder it is in.
std::vector<std::string> destinations(const ProjectStore::View& v, const std::string& path) {
    std::string here = std::filesystem::path(path).parent_path().string(), top = v.root;
    std::string rel = here.size() > top.size() ? here.substr(top.size() + 1) : std::string();
    std::vector<std::string> out;
    if (!rel.empty()) out.push_back("");
    for (const auto& f : v.folders) if (f != rel) out.push_back(f);
    return out;
}

juce::String free_text(uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f GB", bytes / 1e9);
    return text;
}

// Folders first, then projects newest first or by name, as K3 says.
void sort_view(ProjectStore::View& v, unsigned by_name) {
    std::stable_sort(v.entries.begin(), v.entries.end(), [&](const ProjectEntry& a, const ProjectEntry& b) {
        if (a.kind != b.kind) return a.kind == ProjectEntry::Kind::Folder;
        if (by_name || a.kind == ProjectEntry::Kind::Folder) return a.name < b.name;
        return a.modified > b.modified;
    });
}
juce::String count_text(unsigned n, const char* one, const char* many) { return juce::String(n) + " " + (n == 1 ? one : many); }

// The three jobs as tiles under the keys.
void draw_tiles(Paint& p, const Snapshot& s, const ProjectStore::View& v) {
    Header h = header_of(s);
    if (v.root.empty()) h.left.push_back(pill(Icon::Card, "NO CARD", amber));
    else if(s.saving) h.left.push_back(pill(Icon::Save, "SAVING", colour::text));
    else if (s.dirty) h.left.push_back(pill(Icon::Rec, "UNSAVED", amber)); // until the background save a moment later (shell/autosave.h)
    else if (!s.project_path.empty()) h.left.push_back(pill(Icon::Check, "SAVED", colour::play));
    h.right.push_back(label(s.project.name));
    unsigned pick = s.ui.project_pick % 3; // the wheel's tile, raised; Push presses its key
    SoftKeys keys {{s.ui.shift ? "SAVE AS" : "SAVE", "OPEN", s.ui.shift ? "RECOVERY" : "NEW"}, int(pick), colour::text};
    keys.glyphs = {Icon::Save, Icon::Folder, Icon::Plus};
    frame(p, s, keys, h);
    juce::String fresh = s.dirty ? juce::String("ASKS FIRST") : juce::String();
    key_tiles(p, {word_sub_tile(Icon::Save, s.ui.shift ? "SAVE AS" : "SAVE", s.ui.shift || s.project_path.empty() ? "PICK FOLDER" : "CURRENT FILE", pick == 0),
                  word_sub_tile(Icon::Folder, "OPEN", v.root.empty() ? juce::String("NO CARD") : juce::String("BROWSE"), pick == 1),
                  word_sub_tile(Icon::Plus, s.ui.shift ? "RECOVERY" : "NEW", s.ui.shift ? "LAST WORK" : fresh, pick == 2)});
    p.text("SHIFT: SAVE AS / RECOVERY", zone::pad, zone::knobs_y, zone::width-2*zone::pad, 12, caps_text(colour::mid));
    if(!s.recovery_status.empty())p.text(s.recovery_status,zone::pad,zone::knobs_y+16,zone::width-2*zone::pad,14,plain(colour::mid,type::small,Weight::Regular));
    if(!s.project_path.empty()) {
        const auto card=card_dir(v); const auto parent=std::filesystem::path(s.project_path).parent_path();
        juce::String location = recovery_file::under(parent,std::filesystem::path(v.root).parent_path()/"Autosave") ? juce::String("RECOVERED COPY / SAVE TO KEEP")
            : parent.string().rfind(card+"/",0)==0 ? "SD / "+juce::String(parent.string().substr(card.size()+1)).replace("/"," / ") : juce::String(parent.string());
        auto style=plain(colour::mid,type::small,Weight::Regular);
        while(location.length()>5 && p.width(location,style)>zone::width-2*zone::pad)location="..."+location.substring(4);
        p.text(location,zone::pad,zone::knobs_y+32,zone::width-2*zone::pad,14,style);
    }
}

// One row of the list: a glyph, the name, its facts at the right edge.
void draw_row(Paint& p, const Snapshot& s, const ProjectStore::View& v, unsigned n, float y, bool current) {
    float x = zone::pad, w = zone::width - 2 * zone::pad, cy = y + list_row_h / 2, right = x + w - 8;
    if (current) { p.fill(x, y, w, list_row_h, colour::raised, 3); p.fill(x, y, 2.5f, list_row_h, colour::text, 1); }
    auto glyph_c = current ? colour::text : colour::mid;
    auto facts = plain(colour::mid, type::small, Weight::SemiBold);
    float tx = x + 28;
    if (s.ui.project_save_picker && on_card(n)) {
        icon(p, Icon::Save, x + 10, cy - 6, 12, glyph_c, 1.4f);
        p.text("SAVE IN THIS FOLDER", tx, y, right - tx, list_row_h, plain(colour::text, type::list, Weight::Medium));
        return;
    }
    if (on_card(n)) { // the card, to a computer over USB-C
        icon(p, Icon::Card, x + 10, cy - 6, 12, glyph_c, 1.4f);
        float fw = p.width("USB-C", caps_text(colour::mid));
        p.text("USB-C", right - fw, y, fw + 1, list_row_h, caps_text(colour::mid));
        icon(p, Icon::Usb, right - fw - 14, cy - 5, 10, colour::mid, 1.5f);
        p.text("SD card access", tx, y, right - fw - 20 - tx, list_row_h, plain(current ? colour::text : colour::sec, type::list, Weight::Regular));
        return;
    }
    const auto* e = entry_at(v, n);
    if (!e) return;
    bool open = e->kind == ProjectEntry::Kind::Project && e->path == s.project_path;
    if (e->kind == ProjectEntry::Kind::Folder) {
        icon(p, Icon::Folder, x + 10, cy - 6, 12, glyph_c, 1.4f);
        icon(p, Icon::ChevronRight, right - 10, cy - 5, 10, colour::dim, 1.6f);
        juce::String count(e->projects);
        float cw = p.width(count, facts);
        p.text(count, right - 14 - cw, y, cw + 1, list_row_h, facts);
        right -= 20 + cw;
    } else {
        icon(p, Icon::File, x + 10, cy - 6, 12, glyph_c, 1.4f);
        if (open) { // the one open: OPEN, after a dot while it has changes the card has not
            auto chip_style = caps_text(colour::bg);
            float cw = p.width("OPEN", chip_style) + 8;
            right -= cw;
            p.fill(right, cy - 7.5f, cw, 15, colour::text, 3);
            p.text("OPEN", right, cy - 7.5f, cw, 15, chip_style, juce::Justification::centred);
            if (s.dirty) { right -= 9; p.circle(right + 2, cy, 2.5f, amber); }
            right -= 6;
        } else {
            juce::String meta = e->mpc() ? juce::String("MPC")
                              : count_text(e->tracks, "track", "tracks") + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(e->tempo / 10) + " BPM";
            if(s.project_folder==":recovery") {
                const auto stamp=std::filesystem::file_time_type(std::filesystem::file_time_type::duration(e->modified));
                const auto time=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()+std::chrono::duration_cast<std::chrono::system_clock::duration>(stamp-std::filesystem::file_time_type::clock::now()));
                std::tm local{};localtime_r(&time,&local);char date[24];std::strftime(date,sizeof date,"%m/%d %H:%M",&local);meta=date;
            }
            float mw = p.width(meta, facts);
            p.text(meta, right - mw, y, mw + 1, list_row_h, facts);
            right -= mw + 8;
        }
    }
    auto name = plain(current || open ? colour::text : colour::sec, type::list, current ? Weight::Medium : Weight::Regular);
    p.text(e->name, tx, y, right - tx, list_row_h, p.fit(e->name, name, right - tx, type::floor));
}

void draw(Paint& p, const Snapshot& s) {
    auto v = s.projects;
    sort_view(v, s.ui.project_sort);
    if (!s.ui.project_list) { draw_tiles(p, s, v); return; }
    Header h = header_of(s);
    const bool inside = on_internal(s.project_folder);
    juce::String where = s.project_folder == ":recovery" ? juce::String("RECOVERY") : inside ? "INTERNAL" + juce::String(s.project_folder.substr(std::strlen(internal_folder))).replace("/", " / ")
                       : v.root.empty() ? juce::String("NO CARD")
                       : on_card_top(s.project_folder) ? "SD" + juce::String(s.project_folder.substr(std::strlen(card_folder))).replace("/", " / ")
                       : s.project_folder.rfind("MPC Sample", 0) == 0 ? "SD / MPC PROJECTS" + juce::String(s.project_folder.substr(10)).replace("/", " / ")
                       : "SD / PROJECTS" + (s.project_folder.empty() ? juce::String() : " / " + juce::String(s.project_folder));
    h.left.push_back(pill(where));
    if (v.free_bytes) h.right.push_back(pill(Icon::Card, free_text(v.free_bytes)));
    if(s.ui.project_kit_only) h.left.push_back(chip("LOAD KIT",colour::text));
    bool sheet = s.ui.sheet_open;
    unsigned count = project_rows(v.entries.size()), cursor = std::min<unsigned>(s.ui.project_cursor, std::max(count, 1u) - 1);
    const auto* in_hand = entry_at(v, cursor);
    bool open_one = in_hand && in_hand->path == s.project_path, mpc = in_hand && (in_hand->mpc() || s.project_folder == ":recovery");
    if (s.ui.project_save_picker) frame(p, s, {{"CANCEL", "NEW FOLDER", "USE FOLDER"}, 2, colour::text}, h);
    else if (s.ui.move_open) frame(p, s, {{"BACK", "", "MOVE"}, 2, colour::text, {KeyIcon::Back, KeyIcon::None, KeyIcon::Check}}, h);
    else if (sheet) frame(p, s, {{"BACK", "", ""}, -1, colour::text, {KeyIcon::Back, KeyIcon::None, KeyIcon::None}}, h); // the cards
    else frame(p, s, {{"BACK", inside ? "CARD" : "INTERNAL", s.ui.project_kit_only ? "LOAD KIT" : "OPEN"}, 2, colour::text, {KeyIcon::Back, KeyIcon::None, KeyIcon::Check}}, h); // B2: the other drive

    unsigned first = cursor < 3 ? 0 : std::min(cursor - 3, count > visible ? count - visible : 0);
    for (unsigned row = 0; row < visible && first + row < count; ++row)
        draw_row(p, s, v, first + row, zone::picture_y + 1 + row * (list_row_h + list_row_gap), first + row == cursor);
    if (!count) { // an empty folder, or no card
        icon(p, v.root.empty() && !inside ? Icon::Card : Icon::Folder, zone::width / 2.0f - 14, zone::picture_y + 50, 28, colour::dim, 1.4f);
        return;
    }
    if (s.ui.move_open) { // the folder picker over the list
        const auto* e = in_hand;
        if (!e) return;
        auto list = destinations(v, e->path);
        p.fill(0, zone::picture_y, zone::width, zone::picture_h, colour::bg); // nothing of the list shows beside the sheet
        p.dim_zone(zone::knobs_y, zone::knobs_h);
        float px = zone::pad + 6, pw = zone::width - 2 * zone::pad - 12, py = zone::picture_y + 3, ph = zone::picture_h - 6;
        p.fill(px, py, pw, ph, colour::surf, 6);
        juce::String move = "MOVE " + juce::String(e->name) + " TO";
        p.text(move, px + 9, py + 5, pw - 18, 12, p.fit(move, caps_text(colour::mid), pw - 18, type::floor));
        if (list.empty()) {
            icon(p, Icon::Folder, px + pw / 2 - 12, py + 40, 24, colour::dim, 1.4f); // nowhere else to go (New folder here makes one)
            return;
        }
        constexpr unsigned shown = 6;
        unsigned at = std::min<unsigned>(s.ui.move_cursor, unsigned(list.size()) - 1);
        unsigned from = at < shown / 2 ? 0 : std::min<unsigned>(at - shown / 2, list.size() > shown ? unsigned(list.size()) - shown : 0);
        for (unsigned r = 0; r < shown && from + r < list.size(); ++r) {
            unsigned n = from + r;
            float ry = py + 19 + r * 20, rx = px + 5, rw = pw - 10;
            bool on = n == at;
            if (on) { p.fill(rx, ry, rw, 19, colour::raised, 3); p.fill(rx, ry, 2.5f, 19, colour::text, 1); }
            icon(p, Icon::Folder, rx + 6, ry + 4, 11, on ? colour::text : colour::mid, 1.4f);
            juce::String name = list[n].empty() ? juce::String("Projects") : juce::String(list[n]).replace("/", juce::String::fromUTF8(" \xe2\x80\xba "));
            p.text(name, rx + 24, ry, rw - 30, 19, p.fit(name, plain(on ? colour::text : colour::sec, type::small, Weight::Medium), rw - 30, type::floor));
        }
        return;
    }
    if (!v.message.empty() && !sheet)
        p.text(v.message, zone::pad, zone::picture_y + 132, zone::width - 2 * zone::pad, 12, caps_text(colour::rec), juce::Justification::centredRight);

    if (sheet) {
        const auto* e = in_hand;
        if (!e || e->kind != ProjectEntry::Kind::Project) return;
        std::array<Action, 9> list {};
        unsigned n = actions(open_one, list, mpc, can_revert(s.dirty, s.project_path));
        CardSheet m;
        m.chip = open_one ? "OPEN" : "PROJECT"; m.title = e->name;
        if (open_one && s.dirty) { m.detail = "UNSAVED"; m.detail_colour = amber; }
        else if (mpc) { m.detail = "OPENS AS A COPY"; m.detail_colour = colour::mid; }
        for (unsigned k = 0; k < n; ++k) {
            bool start = v.start == e->path;
            switch (list[k]) {
            case Load: // Autosave Off (issue 18): the open project's changes are left behind, so it asks once more
                m.items[k] = card_item(Icon::Folder, "LOAD", !s.ui.settings.autosave && s.dirty
                                       ? (s.ui.delete_armed && k == s.ui.sheet_row ? "PRESS AGAIN" : "CHANGES LOST") : "");
                break;
            case Revert: m.items[k] = card_item(Icon::Undo, "REVERT", s.ui.delete_armed && k == s.ui.sheet_row ? "PRESS AGAIN" : "TO SAVED"); break;
            case LoadKit: m.items[k] = card_item(Icon::Pads, "LOAD KIT", "NO PATTERNS"); break;
            case Save: m.items[k] = card_item(Icon::Save, "SAVE"); break;
            case SaveAsNew: m.items[k] = card_item(Icon::File, "SAVE AS NEW"); break;
            case Rename: m.items[k] = card_item(Icon::Keyboard, "RENAME"); break;
            case Duplicate: m.items[k] = card_item(Icon::Copy, "DUPLICATE"); break;
            case MoveTo: m.items[k] = card_item(Icon::ArrowRight, "MOVE TO FOLDER"); break;
            case NewFolder: m.items[k] = card_item(Icon::Plus, "NEW FOLDER HERE"); break;
            case OpenAtStart: m.items[k] = card_item(Icon::Flag, "OPEN AT START", start ? "ON" : "OFF", start); break;
            case Delete: m.items[k] = card_item(Icon::Trash, "DELETE", s.ui.delete_armed && k == s.ui.sheet_row ? "PRESS AGAIN" : "", false, true); break;
            }
        }
        m.count = n; m.current = s.ui.sheet_row % n;
        card_sheet(p, m);
    }
}

ProjectStore::View view_of(Context& c) {
    ProjectStore::View v;
    if (c.store) c.store->view(v);
    sort_view(v, c.ui.project_sort);
    c.ui.project_rows = uint16_t(project_rows(v.entries.size()));
    return v;
}

void close_sheet(Context& c) { c.ui.sheet_open = false; c.ui.delete_armed = false; }

void act(Context& c, Action action) {
    if(c.save_pending){c.notice("Wait for saving to finish");return;}
    auto v = view_of(c);
    const auto* e = entry_at(v, c.ui.project_cursor);
    if (!e || e->kind != ProjectEntry::Kind::Project || !c.store) return;
    bool open = e->path == c.project_path;
    // Delete, REVERT and (Autosave Off, with changes) Load ask once more: the second press does it
    bool twice = action == Delete || action == Revert || (action == Load && !c.ui.settings.autosave && c.dirty);
    if (!twice) c.ui.delete_armed = false;
    else if (!c.ui.delete_armed) { c.ui.delete_armed = true; return; }
    switch (action) {
    case Load:
        if (e->mpc()) c.notice("Opening " + e->name);
        c.request_project_open(e->path);
        break;
    case LoadKit: close_sheet(c); c.load_kit_file(e->path, e->name); return;
    case Save: close_sheet(c);c.save_project(); return;
    case SaveAsNew: close_sheet(c); c.open_project_save(true); return;
    case Duplicate: c.store->duplicate(e->path); break;
    case NewFolder: c.store->new_folder(); break;
    case OpenAtStart: c.store->open_at_start(e->path); break;
    case Revert: c.store->load(c.project_path); c.notice("Reverted to saved"); break; // switch_to() clears dirty
    case Delete:
        c.store->remove(e->path);
        if (open) c.project_path.clear(); // still open, but no longer on the card
        c.ui.delete_armed = false;
        break;
    case Rename: close_sheet(c); c.rename_project(e->path, e->name); return;
    case MoveTo: close_sheet(c); c.ui.move_open = true; c.ui.move_cursor = 0; return;
    }
    close_sheet(c);
    c.store->list(c.project_folder);
}

// The sheet's job: the card lit (the likely one, first, as it opens).
void act(Context& c) {
    auto v = view_of(c);
    const auto* e = entry_at(v, c.ui.project_cursor);
    if (!e) return;
    bool open = e->path == c.project_path;
    std::array<Action, 9> list {};
    unsigned n = actions(open, list, e->mpc() || c.project_folder == ":recovery", can_revert(c.dirty, c.project_path));
    act(c, list[c.ui.sheet_row % n]);
}

// The picker: Push or B3 moves the project there, B1 backs out.
void move_here(Context& c) {
    auto v = view_of(c);
    const auto* e = entry_at(v, c.ui.project_cursor);
    c.ui.move_open = false;
    if (!e || !c.store) return;
    auto list = destinations(v, e->path);
    if (list.empty()) return;
    c.store->move(e->path, list[std::min<size_t>(c.ui.move_cursor, list.size() - 1)]);
    c.store->list(c.project_folder);
}

void open_list(Context& c) {
    c.ui.project_list = true;
    if (!c.project_list_opened) { // the first time: the open project's folder, on it; with none on the card, its top
        c.project_list_opened = true;
        auto v = view_of(c);
        const std::string card = card_dir(v);
        if (!card.empty() && c.project_path.rfind(card + "/", 0) == 0) { // anywhere on the card
            c.project_folder = card_folder_of(card, v.root, std::filesystem::path(c.project_path).parent_path().string());
            c.project_row_path = c.project_path;
        } else if (!card.empty()) c.project_folder = card_folder;
    }
    if (c.store) c.store->list(c.project_folder); // else where it was left, on the same row
}

void wheel(Context& c, int delta) {
    if (!c.ui.project_list) { c.ui.project_pick = key_pick_turn(c.ui.project_pick, delta); return; } // the tiles: SAVE, OPEN, NEW
    if (c.ui.move_open) {
        auto v = view_of(c);
        const auto* e = entry_at(v, c.ui.project_cursor);
        size_t n = e ? destinations(v, e->path).size() : 0;
        c.ui.move_cursor = uint16_t(std::clamp<int>(c.ui.move_cursor + delta, 0, std::max(int(n) - 1, 0)));
        return;
    }
    if (c.ui.sheet_open) {
        auto v = view_of(c);
        const auto* e = entry_at(v, c.ui.project_cursor);
        std::array<Action, 9> list {};
        unsigned n = actions(e && e->path == c.project_path, list, e && (e->mpc() || c.project_folder == ":recovery"), can_revert(c.dirty, c.project_path));
        c.ui.sheet_row = uint8_t(std::clamp<int>(c.ui.sheet_row + delta, 0, int(n) - 1));
        c.ui.delete_armed = false;
        return;
    }
    auto v = view_of(c);
    c.ui.project_cursor = uint16_t(std::clamp<int>(c.ui.project_cursor + delta, 0, std::max(int(project_rows(v.entries.size())) - 1, 0)));
}

void open_row(Context& c) {
    auto v = view_of(c);
    if (on_card(c.ui.project_cursor)) { if(c.ui.project_save_picker)c.project_save_choose_folder();else c.card_access(); return; }
    const auto* e = entry_at(v, c.ui.project_cursor);
    if (!e) return;
    if (e->kind == ProjectEntry::Kind::Folder) {
        if (on_card_top(c.project_folder)) // Rhythmer/Projects and MPC-Sample/Projects are the list's own folders
            c.project_folder = card_folder_of(card_dir(v), v.root, card_dir(v) + c.project_folder.substr(std::strlen(card_folder)) + "/" + e->name);
        else c.project_folder = c.project_folder.empty() ? e->name : c.project_folder + "/" + e->name;
        c.ui.project_cursor = first_entry; // on its first entry, not SD card access: a second Push must not lend the card
        if (c.store) c.store->list(c.project_folder);
        return;
    }
    if(c.ui.project_save_picker){c.notice("Choose a folder, then B3 USE FOLDER");return;}
    if(c.ui.project_kit_only) {c.load_kit_file(e->path,e->name);return;}
    c.ui.sheet_open = true; c.ui.sheet_row = 0; c.ui.delete_armed = false; // the likely job (Save or Load) is the first card
}

void soft(Context& c, int key);

void push(Context& c) {
    if (!c.ui.project_list) { soft(c, c.ui.project_pick % 3); return; } // the tiles: Push presses the one the wheel lit
    if (c.ui.move_open) { move_here(c); return; }
    if (c.ui.sheet_open) { act(c); return; }
    open_row(c);
}

void soft(Context& c, int key) {
    if (!c.ui.project_list) { // SAVE, OPEN, NEW
        if (key == 0) {if(c.ui.shift)c.open_project_save(true);else c.save_project();}
        if (key == 1) open_list(c);
        if (key == 2) {if(c.ui.shift){c.project_folder=":recovery";c.ui.project_list=true;c.ui.project_cursor=1;if(c.store)c.store->list(c.project_folder);}else c.ask_new_project();} // asks first, as the MPC does; the open one is saved first if it has a file
        return;
    }
    if (c.ui.project_save_picker) {
        if(key==0)c.cancel_project_save();
        else if(key==1){ if(c.store&&!c.store->save_directory(c.project_folder).empty())c.store->new_folder_at(c.project_folder); else c.notice("Choose a writable SD folder"); }
        else if(key==2)c.project_save_choose_folder();
        return;
    }
    if (c.ui.move_open) {
        if (key == 0) c.ui.move_open = false;
        if (key == 2) move_here(c);
        return;
    }
    if (c.ui.sheet_open) { // the cards: B1 BACK; the wheel and Push do the rest
        if (key == 0) close_sheet(c);
        return;
    }
    if (key == 2) { open_row(c); return; }
    if (key == 1) { // the other drive: the card, or the internal one (as Samples' B2), where it was left
        bool inside = on_internal(c.project_folder);
        c.project_places[inside ? 1 : 0] = {c.project_folder, c.ui.project_cursor, true};
        const auto& back = c.project_places[inside ? 0 : 1];
        c.project_folder = back.set ? back.folder : std::string(inside ? card_folder : internal_folder); // the card: its top
        c.ui.project_cursor = back.set ? back.row : first_entry;
        c.project_row_path.clear();
        if (c.store) c.store->list(c.project_folder);
        return;
    }
    if (key != 0) return;
    if (project_folder_up(c)) return; // BACK: up a folder, or from the card's top back to the tiles
    if(c.ui.project_kit_only){c.back();return;}
    c.ui.project_list = false;
}
} // namespace

bool project_folder_up(Context& c) {
    auto& f = c.project_folder;
    if (f == ":recovery") {f.clear();c.ui.project_list=false;if(c.store)c.store->list(f);return true;}
    if (f == internal_folder || f == card_folder) return false;
    auto v = view_of(c);
    if (v.root.empty() && !on_internal(f)) return false;
    std::string up, from;
    if (f.empty() || f == "MPC Sample") { // the list's top and the MPC Sample folder: up into the card's own folders
        const std::string card = card_dir(v);
        auto dir = std::filesystem::path(f.empty() ? v.root : card + "/MPC-Sample/Projects");
        up = card_folder_of(card, "", dir.parent_path().string());
        from = dir.filename().string();
    } else {
        auto cut = f.find_last_of('/');
        from = cut == std::string::npos ? f : f.substr(cut + 1);
        up = cut == std::string::npos ? std::string() : f.substr(0, cut);
    }
    c.project_row_path = from; // on the folder it came out of
    f = up;
    c.ui.project_cursor = first_entry; // until project_listed() finds the folder it came out of
    if (c.store) c.store->list(f);
    return true;
}

void project_listed(Context& c) {
    if (c.project_row_path.empty() || !c.store) return;
    auto v = view_of(c);
    if (v.folder != c.project_folder) return; // the folder asked for has not listed yet
    for (size_t n = 0; n < v.entries.size(); ++n)
        if (v.entries[n].path == c.project_row_path || (v.entries[n].kind == ProjectEntry::Kind::Folder && v.entries[n].name == c.project_row_path)) {
            c.ui.project_cursor = uint16_t(first_entry + n);
            break;
        }
    c.project_row_path.clear();
}

// Erase tapped on the list (shell/erase_copy.h): the project under the cursor's sheet open on DELETE, armed; tapped
// again, deleted (as Push on DELETE twice). False on a folder, SD card access or the MPC's own file.
bool project_erase(Context& c) {
    if (!c.ui.project_list || c.ui.move_open || c.ui.project_save_picker || c.project_folder == ":recovery") return false;
    auto v = view_of(c);
    const auto* e = entry_at(v, c.ui.project_cursor);
    if (!e || e->kind != ProjectEntry::Kind::Project || e->mpc()) return false;
    std::array<Action, 9> list {};
    unsigned n = actions(e->path == c.project_path, list, e->mpc(), can_revert(c.dirty, c.project_path)), row = n;
    for (unsigned k = 0; k < n; ++k) if (list[k] == Delete) row = k;
    if (row == n) return false;
    if (!c.ui.sheet_open || c.ui.sheet_row != row) { c.ui.sheet_open = true; c.ui.sheet_row = uint8_t(row); c.ui.delete_armed = false; }
    act(c, Delete);
    return true;
}

// Shift + Erase on the list: the project under the cursor duplicated (DUPLICATE). False where there is none.
bool project_copy(Context& c) {
    if(c.save_pending){c.notice("Wait for saving to finish");return true;}
    if (!c.ui.project_list || c.ui.move_open || c.ui.project_save_picker || c.project_folder == ":recovery") return false;
    auto v = view_of(c);
    const auto* e = entry_at(v, c.ui.project_cursor);
    if (!e || e->kind != ProjectEntry::Kind::Project || e->mpc()) return false;
    act(c, Duplicate);
    c.notice("Duplicated " + e->name);
    return true;
}

const PageSpec project_page {draw, play_pad, wheel, push, soft, wheel}; // Minus / Plus move the list

} // namespace rhythmer
