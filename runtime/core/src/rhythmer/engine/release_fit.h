// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The auto sampler's release (shell/print_engine.cpp): how a plug-in's sound
// falls once its key is let go, measured against its level at that moment,
// and then played by the keygroup's own amp envelope on whatever sounds when
// a key is let go. A recorded tail started at the release would carry the
// echoes and the LFO of the one take it was recorded in, at that take's
// level, after any press however long: an envelope only ever falls from the
// level the note has, so nothing jumps or swells, whatever was held and for
// how long.
//
// The fall is one stage, or two when the sound drops fast and then rings on
// (a delay's echoes, a reverb, a slow filter): a share of the level falls
// slowly, the rest fast. Two stages play as two keygroups over the same keys
// and samples, the second quieter with the slow release, which any MPC plays.
namespace rhythmer {

// One note's fall: every 10 ms from the key's release, its mean power from there on over a span (30 ms, or as long
// again as the time since the release: echoes count by their energy, not their peaks), in dB against its level as
// the key was let go (its last tenth of a second held). A note that was fading while held (a pluck, a piano) has
// that fade taken out, leaving what letting go itself did.
struct ReleaseShape {
    std::vector<float> db;
    bool whole = true;   // it ended in silence; false: cut at the longest tail kept, nothing known after it
    bool fading = false; // it was fading while held
    static constexpr float hop = 0.01f, window = 0.03f; // seconds
    static float start_of(size_t n) { return float(n) * hop; }                 // value n's span starts here
    static float span(size_t n) { return std::max(window, start_of(n)); }      // and lasts this long
};
// `held`: frames the key was held; `cut`: the tail was stopped at its longest, not by silence. Empty when the note
// is too short or too quiet to tell.
// `quiet`: what counts as silent, dB (an input's own noise): under it a value is silence, and a note within 15 dB of
// it as its key is let go tells nothing.
ReleaseShape release_shape(const std::vector<float>& left, const std::vector<float>& right, uint64_t held, float rate, bool cut,
                           float quiet = -100.0f);

// The fall as an envelope plays it. A stage's seconds are an amp envelope's release as Rhythmer and an .xpm state it:
// the level is e^-3 (26 dB down) after that long.
struct ReleaseModel {
    float fast = 0.05f;  // the release, seconds
    float slow = 0;      // the second stage's, 0: one stage
    float share = 0;     // the slow stage's share of the level as the key is let go, 0..1
    bool two() const { return slow > 0 && share > 0; }
    float db(float seconds) const; // the level `seconds` after the release, dB under the level then
};
// The model nearest the notes' fall: their median at each time (what a note usually does: a sound that swings is let
// go loud by some notes and quiet by others, and what rings on after is the same for both), never rising (in dB, over
// time spaced evenly by ratio, down to -60 dB; a model louder than the fall counts twice). Two stages only where one
// misses the fall by more than 1.5 dB and two take off a third of that or more.
// `report`: the fall it was fitted to at a few times, and how far one stage and two missed it (dB), for a status line.
ReleaseModel fit_release(const std::vector<ReleaseShape>& notes, std::string* report = nullptr);

} // namespace rhythmer
