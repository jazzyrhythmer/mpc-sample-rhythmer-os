// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/engine_host.h"
#include "rhythmer/model/print.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Freeze plan F2a: one print rendered by a print host (EngineHost::Lean) on
// the calling thread, as fast as it goes. The host holds a print_project()
// (model/print.h) and has its samples and plug-ins; render_print() plays the
// pattern from its first tick and keeps the track's sound at the tap: a pass
// of the pattern (Cut), a pass and its tail (Ring), or the second of two
// passes (Loop). What is kept is moved back by the track's own latency (a
// plug-in's, a Limiter's look-ahead; at Pre FX its engine's alone), so its
// first frame is the pattern's first tick. A Ring print's sequencer stops on the frame the pass ends, so
// only the tail sounds after it. Frames are the device's; the shell converts
// them to 44.1 kHz.
namespace rhythmer {

struct PrintAudio {
    std::vector<float> left, right; // device frames
    uint64_t pass = 0;              // one pass of the pattern, device frames
    uint64_t rendered = 0;          // frames the host rendered for it
    unsigned latency = 0;           // the track's own, device frames
    // A kit's pad sends (PrintRecord::sends): for each bus in `sends`, what its pads sent into it, as many frames as
    // the print, moved back by the engine's latency alone (they leave before the chain), so they meet the print.
    uint8_t sends = 0;
    std::array<std::vector<float>, 4> send_left, send_right;
    std::string error;              // empty: made
};

// Track `track` of `host`, whose pattern is `ticks` ticks long as it plays (played_pattern()). `keep_going` is asked
// between blocks; false stops it ("stopped"). The host must not be playing yet.
// `sends`: the buses whose pad sends ride along (print_sends()), 0 none.
PrintAudio render_print(EngineHost& host, unsigned track, uint32_t ticks, PrintTap tap, PrintTail tail,
                        const std::function<bool()>& keep_going = {}, uint8_t sends = 0);

// The auto sampler (shell/auto_sample.h): one note of track `track` of `host`, played with nothing else: the key
// held `hold` device frames, then let go, and its release kept until it stays below -70 dB for 0.2 s (`most_tail`
// seconds at most). What is kept starts on the note-on, moved back by the track's latency as a print's is (at Pre
// FX its engine's alone), and its quiet end is cut off. The host must not be playing.
struct NoteAudio {
    std::vector<float> left, right; // device frames
    uint64_t held = 0;              // the frames before the note-off
    uint64_t release = 0;           // the frames after it that sounded, before the quiet
    std::string error;              // empty: made
};
NoteAudio render_note(EngineHost& host, unsigned track, uint8_t key, uint8_t velocity, uint64_t hold, PrintTap tap,
                      float most_tail, const std::function<bool()>& keep_going = {});

// A one-shot note (a drum) cut where it falls silent (under `silence`: -70 dB, or an input's own noise), with a 5 ms
// fade, even before the key was let go: no silence kept for the time the key was held.
void trim_silence(NoteAudio& note, float rate, float silence = 0.000316f);

// A note whose held part sits off zero (a plug-in's DC offset, an uneven pulse wave), through a DC blocker (5 Hz,
// one pole, so the note still starts from silence): the headroom back and no thump where a sample starts or stops.
// False, and nothing changed, when its first `held` frames average within 2 % of their peak of zero.
bool remove_dc(std::vector<float>& left, std::vector<float>& right, uint64_t held, float rate);

// The auto sampler's check: these notes (`at` frames in, on or off) over `frames`, the track's latency left out, as
// one channel (left and right's mean). The notes still held at the end are let go.
struct ProbeNote {
    uint64_t at = 0;
    bool on = true;
    uint8_t key = 60, velocity = 100;
};
std::vector<float> render_notes(EngineHost& host, unsigned track, std::vector<ProbeNote> notes, uint64_t frames, PrintTap tap,
                                const std::function<bool()>& keep_going = {});

// The frames of a pass at the model rate, as render_print() counts a pass at the device's (for PrintRecord::pass).
uint32_t model_frames_of(uint64_t device_frames, float rate);

} // namespace rhythmer
