// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/engine_host.h"
#include "rhythmer/engine/print_render.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// An instrument outside Rhythmer, played and heard in real time (the auto
// sampler on a MIDI track, shell/print_engine.cpp): its notes go out over
// MIDI on the track's port and channel, and its sound comes back at an input.
// What a plug-in's print host gives faster than real time (a note held and
// let go with its release, a run of notes for the check), this gives as the
// instrument plays them.
//
// Before the first note it listens to the input with nothing playing, for
// what counts as silence there (an input has noise, a plug-in has none), and
// plays one note to find how long a note takes to come back (MIDI out, the
// instrument, the input). Each note after is heard from the frame it is sent
// and cut to where its own sound starts (that time is never twice the same),
// so a sample starts on its sound.
namespace rhythmer {

// How the instrument is reached: the input's own clock (frames heard so far), commands sent once that clock reaches
// a frame (by the MIDI thread, so a note's length does not hang on this thread's turn), the input's frames, and time
// passing. The auto sampler's is the live engine's input ring; a test's is an instrument made up.
struct ExternalIo {
    virtual ~ExternalIo() = default;
    virtual uint64_t now() = 0;
    virtual void post(const Command& command, uint64_t at) = 0;
    virtual void forget() = 0; // what was posted and not sent yet is dropped (a run of notes called off)
    virtual bool copy(uint64_t from, uint32_t frames, float* left, float* right) = 0; // [from, from + frames), heard already
    virtual void wait(unsigned milliseconds) = 0;
};

class ExternalNotes {
public:
    // Each stretch of a note as it comes in (for the screen); false stops the note.
    using Heard = std::function<bool(const float* left, const float* right, unsigned frames)>;

    ExternalNotes(ExternalIo& io, unsigned track, float rate) : io(io), track(uint8_t(track)), rate(rate) {}
    // The input's silence and the instrument's delay, from the input once nothing sounds and one short note of `key`. Empty, or
    // why not: "the input is not quiet" (over -26 dB with nothing playing), "no sound at the input", "stopped".
    std::string open(uint8_t key, const std::function<bool()>& keep_going);
    float silence() const { return quiet; }       // what counts as silent at this input (linear, -70 dB at least)
    uint32_t latency() const { return delay; }    // frames from a note sent to its sound
    // The input clipped since open(): three frames or more in a row at full scale on one side (a flat top, as a
    // clip meter counts it); one or two frames that touch it are a peak, not a clip.
    bool clipped() const { return clipping; }
    int clipped_key() const { return clip_key; }  // the key sounding when it first clipped (its tail too); -1: none or unknown

    // Nothing playing, until the input has been silent for 0.1 s (4 s at most).
    void settle(const std::function<bool()>& keep_going);
    // As render_note(): `key` held `hold` frames and let go, its release kept until it has been silent for 0.2 s
    // (`most_tail` seconds at most), from where its sound starts, its quiet end cut off.
    NoteAudio note(uint8_t key, uint8_t velocity, uint64_t hold, float most_tail, const Heard& heard);
    // As render_notes(): these notes over `frames`, as one channel. Empty when stopped.
    std::vector<float> notes(std::vector<ProbeNote> notes, uint64_t frames, const std::function<bool()>& keep_going);

private:
    bool take(uint64_t from, uint32_t frames, std::vector<float>& left, std::vector<float>& right); // appended
    ExternalIo& io;
    uint8_t track;
    float rate;
    float quiet = 0.000316f;
    float clear = 0.003f; // a sound has started: well over the silence
    uint32_t delay = 0;
    bool clipping = false;
    int clip_key = -1;
    int sounding = -1;          // the key whose note (or tail) the input hears now; -1: several or none
    uint64_t run_end = 0;       // the frame after the last one taken: frames taken from there carry the runs on
    unsigned run_l = 0, run_r = 0; // frames in a row at full scale, each side
};

} // namespace rhythmer
