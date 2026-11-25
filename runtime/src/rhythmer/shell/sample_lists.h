// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// The sample browser's two lists (ease roadmap item 1): STARRED, the files starred with Shift + B3, and RECENT, the
// last files loaded onto a pad or track from the browser, newest first. Both are rows at the top of the card's
// Samples folder and of the card's top folder; Push opens one as a folder. They are kept on the card as text, a
// path a line (Rhythmer/Starred.txt, Rhythmer/Recent.txt): a file on the card as its path from the card's top
// folder, one on the internal drive whole. The rules here are pure, for the library, the page and the tests.
namespace rhythmer {

// The folder names the library lists the two in. They begin with a byte no folder on the card has.
inline const std::string starred_list = "\x01STARRED";
inline const std::string recent_list = "\x01RECENT";
inline bool list_folder(const std::string& folder) { return folder == starred_list || folder == recent_list; }

constexpr size_t recent_max = 16;
constexpr size_t starred_max = 500;

// A path that names a part of a file, as the library and the projects write them: a pad of an MPC program or project
// ("<file>.xpm#<pad>", .xty, .xpj) or a SoundFont's preset ("<file>.sf2#<preset>"). Its file into `file`. A file
// whose own name has a '#' in it ("KeysPh G#m90.wav", a folder "C# Loops") is a whole file.
inline bool part_of_file(const std::string& path, std::string* file = nullptr) {
    const size_t mark = path.rfind('#');
    if (mark == std::string::npos || mark < 4 || mark + 1 >= path.size() || path[mark - 4] != '.') return false;
    std::string kind = path.substr(mark - 3, 3);
    for (auto& ch : kind) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (kind != "xpm" && kind != "xty" && kind != "xpj" && kind != "sf2") return false;
    if (file) *file = path.substr(0, mark);
    return true;
}

// Shift + B3: starred if it was not, else no longer. True when it is starred now. Taken off with a range erase, not
// erase(at): GCC 12 at -O2 reads erase(at) on a list it sees empty as a write before the list (-Warray-bounds).
inline bool star_toggle(std::vector<std::string>& starred, const std::string& path) {
    auto kept = std::remove(starred.begin(), starred.end(), path);
    if (kept != starred.end()) { starred.erase(kept, starred.end()); return false; }
    if (starred.size() >= starred_max) return false;
    starred.push_back(path);
    return true;
}
// A file loaded from the browser: first on RECENT, once.
inline void recent_add(std::vector<std::string>& recent, const std::string& path) {
    recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
    recent.insert(recent.begin(), path);
    if (recent.size() > recent_max) recent.resize(recent_max);
}
// Erase on a row of a list: off that list; the file stays.
inline void list_remove(std::vector<std::string>& paths, const std::string& path) {
    paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
}

// The text kept on the card: a path a line, a card's file from the card's top folder.
inline std::string list_text(const std::vector<std::string>& paths, const std::string& card_top) {
    std::string out;
    for (const auto& p : paths) {
        bool on_card = !card_top.empty() && p.size() > card_top.size() + 1 && p.compare(0, card_top.size(), card_top) == 0 && p[card_top.size()] == '/';
        out += on_card ? p.substr(card_top.size() + 1) : p;
        out += '\n';
    }
    return out;
}
// Read back: blank lines and carriage returns left out, a path not from / is the card's, each path once.
inline std::vector<std::string> list_read(const std::string& text, const std::string& card_top, size_t most) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < text.size() && out.size() < most) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        if (line[0] != '/') {
            if (card_top.empty()) continue;
            line = card_top + "/" + line;
        }
        if (std::find(out.begin(), out.end(), line) == out.end()) out.push_back(line);
    }
    return out;
}

// A list's row: the folder its file is in, by name, at the right edge; left out when longer than fits beside a name.
inline std::string list_where(const std::string& path) {
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return {};
    size_t before = path.find_last_of('/', slash - 1);
    std::string name = path.substr(before == std::string::npos ? 0 : before + 1, slash - (before == std::string::npos ? 0 : before + 1));
    return name.size() <= 14 ? name : std::string();
}

} // namespace rhythmer
