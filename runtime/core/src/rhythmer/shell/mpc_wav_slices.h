// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/chop_slices.h"
#include <cstdint>
#include <string>

// A sample's slices kept in its WAV file, as the MPC 3 (and the stock MPC Sample) keep them: the "atem" chunk, JSON,
// "value0" holding "defaultSlice", "Num slices" and "Slice 0", "Slice 1" ... each with its own Start and End (frames
// of the file; End past the slice's last frame), and the "slic" chunk beside it, the same slices in binary
// (u16 1, u16 count, u16 0, then each slice's u32 Start, u32 End, u32 LoopStart and u16 LoopMode).
namespace rhythmer {

// The file's slices as a chop (Manual) in the model's frames: its region the file's default slice (to 0: the end),
// each slice its start and end (0: the end). Unused when the file has none or is not a WAV.
SampleChop read_mpc_slices(const std::string& path);

// The chop's slices written into the WAV file at `path` (its frames at the file's rate): the "atem" chunk's slices
// replaced (its other values kept; one made when there is none) and the "slic" chunk written again; a chop with
// no slices takes them out. False when it is not a WAV or cannot be written.
// `in_place` (the sample's own file, which may be playing from the card at this moment): its audio never moves and
// is never copied. The two chunks go where the old ones were before the audio when they fit there (what is left a
// JUNK chunk), else after the audio, the old ones made JUNK. Otherwise (a copy for an export) the file is written
// beside, the chunks before the audio as the MPC writes them, and renamed over.
bool write_mpc_slices(const std::string& path, const SampleChop& chop, bool in_place = true);

} // namespace rhythmer
