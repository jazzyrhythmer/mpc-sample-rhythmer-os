// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/keygroup.h"
#include "rhythmer/model/project.h"
#include <array>
#include <map>
#include <cstdint>
#include <string>
#include <vector>

// The MPC Sample's own projects (.xpj: gzip around five header lines and a
// JSON document, the samples in "<name>_[ProjectData]" beside it) read into a
// Rhythmer project. The file is never written: Save makes a .mldp. Each Drum
// track becomes a kit track, each sequence that numbered pattern on all of them.
namespace rhythmer {

struct MpcImport {
    unsigned tracks = 0, sequences = 0, pads = 0, notes = 0;
    unsigned over_limit = 0; // tracks, sequences or pads Rhythmer has no room for
    unsigned missing = 0;    // pads whose file is not in the project's data folder
    // Freeze plan F3: Audio tracks' regions as patterns' takes (a sequence's region from its start), each take's WAV
    // file to copy as "Take NNN.wav" (number, file), and regions left out (a second in a sequence, or one starting
    // later than its start: a pattern plays one take, from its top).
    unsigned regions = 0, regions_left_out = 0;
    std::vector<std::pair<unsigned, std::string>> takes;
    // The song (model/song_steps.h): its steps laid out in Song, steps left out (a sequence not in the file, or past
    // Song's room), and other songs with steps (Song holds one: the first).
    unsigned song_steps = 0, steps_left_out = 0, songs_left_out = 0;
};

bool is_mpc_project(const std::string& path);

// Empty on success. A file of 10 MB of JSON takes a while: never on the MIDI or audio thread.
std::string import_mpc_project(const std::vector<uint8_t>& file, const std::string& path, Project& project, PatternBank& patterns, MpcImport& report);

// A pad of more than one layer plays them as an instrument named "<project>#<track>.<pad>"
// (shell/mpc_program.h): the project's such pads, by "<track>.<pad>", their layers as a drum program's tags.
std::map<std::string, KeygroupInstrument> mpc_project_pads(const std::vector<uint8_t>& file, const std::string& path);

// An MPC 3 program in JSON (an .xty track's, or a gzip .xpm) as the tree .xpm programs read into
// (engine/keygroup.h), drum or keygroup alike. Empty on success.
std::string read_mpc_json_program(const std::vector<uint8_t>& file, const std::string& path, KeygroupProgram& program);

// A keygroup track of an MPC 3 project (its `track`, as the file lists them) as the .xpm reader's tree, its files in the
// project's data folder: what "<project>.xpj#k<track>" plays (a KEYGROUP track's instrument). Empty on success.
std::string mpc_project_keygroup(const std::vector<uint8_t>& file, const std::string& path, unsigned track, KeygroupProgram& program);

// The sample rate of a WAV file; 0 when it cannot tell.
unsigned mpc_wav_rate(const std::string& path);

// An MPC program's pad colours, pad 1 first, as 0xRRGGBB at full brightness (model/sampler.h full_colour()); 0 where
// a pad follows its track. `json` is its ProgramPads: an .xpm keeps it as text in <ProgramPads-v2.10>, an .xty or an
// .xpj as the program's "programPads". One colour for every pad (Universal) gives every pad that colour; pads that
// follow the track's colour (PadsFollowTrackColour) give 0, Rhythmer's track colour.
std::array<uint32_t, 128> mpc_pad_colours(const std::string& json);

} // namespace rhythmer
