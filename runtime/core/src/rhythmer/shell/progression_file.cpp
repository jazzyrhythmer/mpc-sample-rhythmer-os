// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/progression_file.h"
#include "rhythmer/model/card_folders.h"
#include "rhythmer/model/chords.h"
#include "rhythmer/model/scales.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace rhythmer {
namespace {
namespace fs = std::filesystem;

// "E", "F#", "Bb": its pitch class; -1 for anything else.
int pitch_class(const juce::String& name) {
    static const int letters[] {9, 11, 0, 2, 4, 5, 7}; // A..G
    auto text = name.trim().toUpperCase();
    if (text.isEmpty() || text[0] < 'A' || text[0] > 'G') return -1;
    int pc = letters[text[0] - 'A'];
    if (text.length() > 1 && text[1] == '#') ++pc;
    else if (name.trim().length() > 1 && name.trim()[1] == 'b') --pc;
    return (pc + 12) % 12;
}

bool has_extension(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
    return ext == ".progression";
}

void collect(const fs::path& folder, unsigned depth, std::vector<std::string>& out, size_t limit) {
    std::error_code e;
    if (!fs::is_directory(folder, e)) return;
    std::vector<fs::path> files, folders;
    for (fs::directory_iterator it(folder, e), end; !e && it != end; it.increment(e)) {
        auto name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (it->is_directory(e)) folders.push_back(it->path());
        else if (has_extension(it->path())) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    std::sort(folders.begin(), folders.end());
    for (const auto& f : files) { if (out.size() >= limit) return; out.push_back(f.string()); }
    if (depth) for (const auto& f : folders) collect(f, depth - 1, out, limit);
}
} // namespace

std::string read_progression(const std::string& path, Progression& out) {
    juce::File file(path);
    if (!file.existsAsFile()) return "No file";
    if (file.getSize() > 1024 * 1024) return "File too large";
    auto json = juce::JSON::parse(file.loadFileAsString());
    auto root = json["progression"];
    if (!root.isObject()) return "Not a progression";
    Progression prog;
    std::snprintf(prog.name, sizeof(prog.name), "%s", root["name"].toString().toRawUTF8());
    std::snprintf(prog.file, sizeof(prog.file), "%s", progression_stem(path).c_str());
    int key = pitch_class(root["rootNote"].toString());
    prog.root = uint8_t(key < 0 ? 0 : key);
    auto* chords = root["chords"].getArray();
    if (!chords) return "Not a progression";
    unsigned n = 0;
    for (const auto& c : *chords) {
        if (n >= prog_chords) break;
        auto& chord = prog.chords[n++];
        std::snprintf(chord.name, sizeof(chord.name), "%s", c["name"].toString().toRawUTF8());
        chord.root = c["role"].toString() == "Root";
        std::array<int, notes_per_step> keys {};
        unsigned count = 0;
        if (auto* notes = c["notes"].getArray())
            for (const auto& note : *notes)
                if (count < notes_per_step) {
                    int k = int(note);
                    if (k >= 0 && k <= 127) keys[count++] = k;
                }
        sort_notes(keys, count); // (std::sort here trips GCC 12's bounds check, as in model/chords.h)
        for (unsigned k = 0; k < count; ++k) chord.notes[k] = uint8_t(keys[k]);
        chord.count = uint8_t(count);
    }
    prog.count = uint8_t(n);
    out = prog;
    return {};
}

std::string progression_json(const Progression& prog) {
    juce::Array<juce::var> chords;
    for (unsigned n = 0; n < prog.count && n < prog_chords; ++n) {
        const auto& c = prog.chords[n];
        auto* chord = new juce::DynamicObject();
        chord->setProperty("name", juce::String::fromUTF8(c.name));
        chord->setProperty("role", c.root ? "Root" : "Normal");
        juce::Array<juce::var> notes;
        for (unsigned k = 0; k < c.count; ++k) notes.add(int(c.notes[k]));
        chord->setProperty("notes", notes);
        chords.add(juce::var(chord));
    }
    auto* body = new juce::DynamicObject();
    body->setProperty("name", juce::String::fromUTF8(prog.name));
    body->setProperty("rootNote", note_letter(prog.root));
    body->setProperty("scale", "Chromatic");
    body->setProperty("recordingOctave", 1);
    body->setProperty("chords", chords);
    auto* top = new juce::DynamicObject();
    top->setProperty("progression", juce::var(body));
    return juce::JSON::toString(juce::var(top)).toStdString();
}

bool write_progression(const std::string& path, const Progression& prog) {
    std::error_code e;
    fs::create_directories(fs::path(path).parent_path(), e);
    std::string text = progression_json(prog);
    std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        if (!out) return false;
    }
    fs::rename(temp, path, e);
    return !e;
}

std::string own_progression_folder(const std::string& card) { return card.empty() ? std::string() : card + "/Rhythmer/Progressions"; }

std::vector<std::string> list_progressions(const std::string& card, size_t limit) {
    std::vector<std::string> out;
    if (card.empty()) return out;
    collect(own_progression_folder(card), 0, out, limit);
    collect(fs::path(card) / "Progressions", 2, out, limit);
    collect(fs::path(samples_folder(card)) / "Expansions", 4, out, limit);
    collect(fs::path(old_samples_folder(card)) / "Expansions", 4, out, limit); // where older builds installed them
    return out;
}

std::string progression_stem(const std::string& path) { return fs::path(path).stem().string(); }

} // namespace rhythmer
