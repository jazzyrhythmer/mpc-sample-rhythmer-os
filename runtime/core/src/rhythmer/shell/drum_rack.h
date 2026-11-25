// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/project.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Ableton's drum kits: a Drum Rack preset, as the Ableton Move keeps its Track Presets (a JSON file each, the
// "devicePreset.json" schema; the Move's Core Library in /data/CoreLibrary/Track Presets/Drums/<style>/, its User
// Library's in /data/UserData/UserLibrary/Track Presets/) and Live 12.1 exports one for the Move. The preset is an
// Instrument Rack holding a Drum Rack, whose chains are its pads: each a Drum Sampler ("drumCell") with its
// sample (an "ableton:/" URI) and values, played on its receiving note (36 to 51 on the Move, its sixteen pads).
// A pad lands on the kit pad of the same note (36 is pad 1 of bank A, model/sampler.h), so a product's pad
// layout places it as any kit pad. Reading touches files: never on the audio thread.
namespace rhythmer {

// Where an "ableton:/" URI's libraries are: on the Move, core "/data/CoreLibrary" and user
// "/data/UserData/UserLibrary"; a product with a copy of them names its own.
struct AbletonLibraries {
    std::string core; // ableton:/packs/abl-core-library/...
    std::string user; // ableton:/user-library/...
};

// The file a Drum Sampler's sample URI names: the Core Library's
// ("ableton:/packs/abl-core-library/Samples/Drums/Kick/Kick%20808.wav"), the User Library's
// ("ableton:/user-library/Recordings/..."), a "file://" URI, an absolute path, or a path from the preset's own
// folder; its %XX undone. Empty for none (another pack's, which a library does not hold, or no URI).
std::string ableton_sample_path(const std::string& uri, const std::string& preset_folder, const AbletonLibraries& libraries);

// A Drum Rack pad as the preset keeps it, in Ableton's own units.
struct DrumRackPad {
    int note = -1;                    // its receiving note
    std::string uri, file;            // the sample's URI and the file it names (empty: none)
    double volume_db = 0;             // the Drum Sampler's Volume and Gain, and its chain's volume, in dB
    double pan = 0;                   // -1..1, the Drum Sampler's and its chain's
    int transpose = 0;                // semitones
    double detune = 0;                // semitones, -0.5..0.5 (shown as -50..50 cents)
    double start = 0, length = 1;     // of the sample's length, 0..1
    int choke = 0;                    // choke group, 0 none
    double velocity_to_volume = 0.35; // 0..1
    bool gate = false;                // envelope A-S-R: plays while held (A-H-D: a one shot)
    std::string filter;               // "Lowpass", "Lowpass 12dB", "Highpass", "Peak"; empty when off
    double cutoff_hz = 22000, resonance = 0;
};
struct DrumRack {
    std::string name;
    std::vector<DrumRackPad> pads; // in the rack's order
};

// A Drum Rack preset, by its name and first few kilobytes: a .json Track Preset whose rack is a Drum Rack.
bool is_drum_rack_preset(const std::string& path);

// The kit pad (0-127) a Drum Rack pad's note plays: note 36 is pad 1 of bank A; -1 off the kit.
int drum_rack_pad(int note);

// A Drum Rack pad's values onto a kit pad (its sample slot left alone): its region of a sample of `frames`
// frames at 44.1 kHz (0 unknown: the whole sample), level, pan, tune, choke group as its mute group, Vel > Vol as
// its velocity sense, A-S-R as Gate, and a low- or high-pass filter that cuts. The Drum Sampler's envelope times,
// its effect, the rack's effects, sends and macros have no pad value to go to and are left out.
void drum_rack_pad_sound(const DrumRackPad& from, uint32_t frames, PadSound& pad);

// The rack as a project of one kit track, as an MPC drum program is read (shell/mpc_program.h): each pad whose
// file is there on the pad of its note, its samples in the project's slots. `frames_of` gives a file's length at
// 44.1 kHz (0 unknown), asked only of a pad that plays part of its sample. `missing`: pads whose file is not there
// (another pack's, or gone), left empty. Empty on success.
std::string import_drum_rack(const DrumRack& rack, Project& project, unsigned& missing,
                             const std::function<uint32_t(const std::string&)>& frames_of);

// drum_rack_preset.cpp, by juce_core's JSON reader and the sample reader (shell/sample_decode.h):
// A preset's text read as a Drum Rack. Empty on success; "NOT A DRUM KIT" for any other preset.
std::string parse_drum_rack(const std::string& text, const std::string& preset_path, const AbletonLibraries& libraries,
                            DrumRack& out);
// A preset file read and imported as above, each region measured from its file's header.
std::string import_drum_rack_preset(const std::string& path, const AbletonLibraries& libraries, Project& project,
                                    unsigned& missing);

} // namespace rhythmer
