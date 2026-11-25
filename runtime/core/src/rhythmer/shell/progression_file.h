// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/project.h"
#include <string>
#include <vector>

// MPC 3 chord progressions (.progression: JSON, {"progression": {"name", "rootNote", "scale", "recordingOctave",
// "chords": [{"name", "role", "notes"}]}}) read into a track's Progression and written back the same way.
namespace rhythmer {

// Empty on success. Chords past prog_chords and notes past notes_per_step are left out.
std::string read_progression(const std::string& path, Progression& out);
std::string progression_json(const Progression& prog);
bool write_progression(const std::string& path, const Progression& prog);

// Where Rhythmer keeps the ones made on the MPC: <card>/Rhythmer/Progressions.
std::string own_progression_folder(const std::string& card);
// Every .progression on the card, sorted by name in each folder: Rhythmer's own folder first, then <card>/Progressions,
// then the expansions' (<card>/Rhythmer/Samples/Expansions, and <card>/Samples/Expansions where older builds
// installed them, four folders deep at most). At most `limit`.
std::vector<std::string> list_progressions(const std::string& card, size_t limit = 512);
// A file's name without its folder and .progression.
std::string progression_stem(const std::string& path);

} // namespace rhythmer
