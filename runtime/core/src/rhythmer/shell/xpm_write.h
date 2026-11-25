// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// An MPC keygroup program written as an .xpm (the MPC 2 XML that MPC 2 and 3,
// Force and MPC Beats open, and Rhythmer reads with shell/mpc_program.h): one
// keygroup a key range, each with up to four velocity layers of WAV files
// beside the program. The auto sampler (shell/auto_sample.h) makes them. The
// layout follows the MPC's own: 128 instruments (those past the count unused),
// four layers each, the program's pads JSON, pad note and group maps; a loop
// is a slice loop, the slice ending where the loop does.
namespace rhythmer {

struct XpmLayer {
    std::string sample;    // the WAV's name beside the program, without ".wav"
    uint8_t root = 60;     // the MIDI note it sounds at its own pitch
    uint8_t low_velocity = 0, high_velocity = 127;
    uint32_t frames = 0;   // the WAV's
    // A loop while the key is held: from loop_end back to loop_start (0, 0: none).
    uint32_t loop_start = 0, loop_end = 0;
    bool looped() const { return loop_end > loop_start; }
    // Its level over its keygroup's: a velocity layer is recorded at its loudest velocity, and the keygroup's
    // Velocity would make it quieter still there, so it is raised by as much.
    double gain = 1;
};
struct XpmKeygroup {
    uint8_t low = 0, high = 127;
    std::vector<XpmLayer> layers; // eight at most, lowest velocity first (an .xpm for MPC 2 holds four: xpm_layers())
    bool cycle = false;           // the layers are takes of one hit, played in turn (ZonePlay Cycle), not by velocity
    bool one_shot = false;        // plays out whole however short the press (a drum), else held while the key is
    // Its own release and level, for a fall of two stages (engine/release_fit.h): two keygroups over the same keys
    // and layers, this one's share of the level and how long it takes to fall once the key is let go.
    double release = -1;          // seconds; below 0 the program's
    double gain = 1;              // each layer's Volume
    // How far its level follows velocity, 0..1 (VelocitySensitivity), as the check heard the plug-in's. Below 0, not
    // heard: fully with one layer, not at all with velocity layers.
    double velocity = -1;
};
struct XpmProgram {
    std::string name;
    std::vector<XpmKeygroup> keygroups; // 128 at most, in key order
    double release = 0.05;              // seconds after the key is let go (the samples hold their own release)
    // The program's Volume, dB. A sample in the centre plays 3 dB down a side (constant power pan, Rhythmer's sampler
    // and the MPC's): an auto sampled program takes those 3 dB back, so it is as loud as what was sampled.
    double volume_db = 0;
    // Mono and Portamento (engine/keygroup.h mpc_portamento_value()): one note at a time, the pitch sliding to a new
    // key over `glide` seconds (0: none); with `legato` only a key pressed while another is held slides. `tied`: such
    // a key carries the held note on, no new attack; else it starts again. That is MPC 3's Mono Retrig
    // (monoRetrigger in its projects and track files, <MonoRetrigger> here, after PortamentoQuantized as MPC 3 has
    // them); an MPC 2 has no such setting, passes the tag over and always starts the note again.
    bool mono = false, legato = false, tied = false;
    double glide = 0;
    // The keys Mono holds for. All of them: the program's Mono. One side of a split: the program is not Mono (an MPC
    // program is mono or poly as a whole); the keygroups on that side are each Mono and share mute group 1, so on an
    // MPC a note there cuts the last (no glide: its portamento is the program's Mono's, and would slide the poly
    // side too). Rhythmer reads the side back from them and plays it as a mono program on those keys, with the glide
    // kept in tags of its own (MonoSidePortamentoTime, MonoSidePortamentoLegato).
    uint8_t mono_low = 0, mono_high = 127;
    bool mono_all() const { return mono && mono_low == 0 && mono_high == 127; }
    bool mono_side(const XpmKeygroup& k) const { return mono && !mono_all() && k.low >= mono_low && k.high <= mono_high; }
};

// The layers a keygroup's instrument is written with, `most` at most (an MPC 2 .xpm holds four). Over that, its
// layers in `most` runs, the softest runs a layer longer where they do not share out evenly; each run plays its top
// layer over the run's velocities (eight in four: layers 2, 4, 6 and 8).
std::vector<XpmLayer> xpm_layers(const XpmKeygroup& keygroup, unsigned most = 4);
// The program as .xpm text, each instrument with `most_layers` layers (4, as MPC 2 and MPC Beats read them; 8 for
// MPC 3's track file to be made from, shell/mpc3_export.h).
std::string xpm_text(const XpmProgram& program, unsigned most_layers = 4);
// The program written to `path` (beside its WAVs). Empty on success.
std::string write_xpm(const std::string& path, const XpmProgram& program, unsigned most_layers = 4);

// keygroup_seconds()'s inverse (engine/keygroup.h): seconds as the MPC's 0..1 envelope value.
double xpm_envelope_value(double seconds);

} // namespace rhythmer
