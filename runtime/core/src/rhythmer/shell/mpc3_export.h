// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/print.h"
#include "rhythmer/model/project.h"
#include <string>

// A Rhythmer project as an MPC 3 project (MPC One, Live, Key, X and the MPC 3
// desktop app): "<name>.xpj" (gzip around five header lines and a JSON
// document) and "<name>_[ProjectData]" beside it, holding the WAV files. The
// document is a copy of a project MPC 3 saved (mpc3_template.json, made by
// tools/mpc3_template.py) with Rhythmer's tracks, pads and patterns put in.
//
// Kit tracks are written: each becomes a Drum track, each pad a pad of WAV
// layers, each pattern number a sequence, the tempo and time signature with
// them. A pad of an instrument of key zones (an SFZ, a keygroup program), or a
// WAV pad played at other pitches, becomes a Keygroup track of its own. Warped
// pads keep their warp. A plug-in track becomes an Audio track of its prints
// (model/print.h), each printed pattern a region at its sequence's start; a
// MIDI track an MPC 3 MIDI track on its channel, with its bank and program.
// A plug-in track without prints, and sounds that are not WAV files, are
// counted in the report and left out. The song becomes one sequence, "Song", in the
// first pattern number nothing uses, and plays first.
namespace rhythmer {

struct Mpc3Export {
    unsigned tracks = 0, keygroups = 0, sequences = 0, pads = 0, notes = 0, samples = 0; // tracks: Drum tracks; keygroups: Keygroup tracks
    unsigned audio = 0, regions = 0; // Audio tracks (of prints, or audio tracks' takes), and the patterns played on them
    unsigned midi = 0;               // MIDI tracks
    unsigned chops = 0;              // copied WAV files given their chop's slices
    unsigned song_bars = 0;          // the song, as the sequence "Song" (0: no song)
    bool song_left_out = false;      // no free pattern number for it
    bool song_full = false;          // a lane had more notes than a pattern holds
    bool song_tempo_left_out = false; // its tempo changes: the sequence keeps the project's tempo
    unsigned skipped_tracks = 0; // tracks not written: plug-in tracks with no prints
    unsigned skipped_pads = 0;   // pads, layers or zones not written: not a WAV file (an SF2's, a FLAC), past eight layers
    unsigned missing = 0;        // WAV files not where the project says
    unsigned mixer = 0;          // the mixer's settings not carried (model/project.h mixer_left_out())
};

// Writes <folder>/<name>.xpj and <folder>/<name>_[ProjectData]/. Empty on success. Reads and copies files:
// never on the MIDI or audio thread.
// `prints` and `prints_dir` (model/print.h prints_folder()): the project's prints, for its plug-in tracks.
// `audio_dir` (model/audio_track.h audio_folder()): its audio tracks' takes.
std::string export_mpc3_project(const Project& project, const PatternBank& patterns, const std::string& folder,
                                const std::string& name, Mpc3Export& report, const PrintTable* prints = nullptr,
                                const std::string& prints_dir = {}, const std::string& audio_dir = {});

// A keygroup program (an .xpm, MPC 2's file, with its WAV files beside it) as MPC 3's track file `file`
// ("<name>.xty": the same gzip and header around one Keygroup track). MPC 3 looks for a track's samples in
// "<name>_[TrackData]" beside the file: that is the folder the program and its WAV files must be in, so nothing is
// copied. It carries what an .xpm has no tag for (Mono Retrig). Empty on success. Reads files: never on the MIDI or
// audio thread.
std::string write_mpc3_track(const std::string& program, const std::string& file);

} // namespace rhythmer
