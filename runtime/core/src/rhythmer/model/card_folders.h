// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <filesystem>
#include <string>
#include <system_error>

namespace rhythmer {

// Rhythmer's own folders on the card that hold nothing to open on the MPC: Rhythmer/Cache (samples decoded at the
// device's rate) and Rhythmer/Crashes (crash reports). The MPC's browsers (plug-ins, samples, projects) leave them out;
// the web page's file list still shows them, so crash reports can be downloaded.
inline bool rhythmer_own_folder(const std::filesystem::path& dir) {
    const auto name = dir.filename().string();
    return dir.parent_path().filename() == "Rhythmer" && (name == "Cache" || name == "Crashes" || name == "Autosave");
}

// Everything Rhythmer makes on the card is in Rhythmer/ at its top; the release's autorun/ is the only other folder
// there. Samples it records, edits and saves (with its banks, expansions, auto sampled programs and a sample's stems)
// go in Rhythmer/Samples, and a MIDI track's .syx files are read from Rhythmer/SysEx. Older builds kept both at the
// card's top: those folders are still read. Empty: no card.
inline std::string samples_folder(const std::string& card_top) {
    return card_top.empty() ? std::string() : (std::filesystem::path(card_top) / "Rhythmer" / "Samples").string();
}
inline std::string sysex_folder(const std::string& card_top) {
    return card_top.empty() ? std::string() : (std::filesystem::path(card_top) / "Rhythmer" / "SysEx").string();
}
// The folders at the card's top that older builds used.
inline std::string old_samples_folder(const std::string& card_top) {
    return card_top.empty() ? std::string() : (std::filesystem::path(card_top) / "Samples").string();
}
inline std::string old_sysex_folder(const std::string& card_top) {
    return card_top.empty() ? std::string() : (std::filesystem::path(card_top) / "SysEx").string();
}

// Where a project's sample opens on the card now. A path in Samples/ at the card's top (where older builds wrote
// samples) opens from the same place in Rhythmer/Samples when it is there, so that folder can be moved into Rhythmer/
// by hand; else, and any other path, as saved. "<file>#<pad>" (a pad of a program) is looked for by its file. Plain
// C++, no JUCE: the host tests read it too.
inline std::string card_sample_path(const std::string& card_top, const std::string& saved) {
    namespace fs = std::filesystem;
    if (card_top.empty() || saved.empty() || saved[0] != '/') return saved;
    const fs::path inside = fs::path(saved).lexically_relative(old_samples_folder(card_top));
    if (inside.empty() || inside == "." || *inside.begin() == "..") return saved;
    const std::string moved = (fs::path(samples_folder(card_top)) / inside).string();
    std::error_code e;
    if (fs::exists(moved, e)) return moved;
    const auto pad = moved.rfind('#');
    return pad != std::string::npos && pad > 0 && fs::exists(moved.substr(0, pad), e) ? moved : saved;
}

} // namespace rhythmer
