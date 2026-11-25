// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <cstdint>

// Idle checkpoint timing for a separate per-project recovery file. Manual files and their dirty status stay
// unchanged. Plug-in state is captured only while transport/record/print/load work is stopped.
// Pure timing policy, shared by the shell and regression tests.
namespace rhythmer {

constexpr uint32_t autosave_idle_ms = 3000;   // this long after the last control or note
constexpr uint32_t autosave_retry_ms = 10000; // after a save that found no card: not before this

struct AutosaveFacts {
    bool enabled = true; // Settings: disable recovery checkpoints too
    bool dirty = false;     // changed since manual save or load
    bool playing = false;   // the sequencer, the song or a loop take runs
    bool recording = false; // a pattern take, a sample take, a print, an auto sample or an export runs
    bool sheet = false;     // a question, the Name sheet, the Exit sheet, SD card access or an update has the controls
    bool loading = false;   // the project's samples are still being read
    bool editing = false;   // Chop/Record, a save destination browser, or a lesson owns unfinished work
    bool failed = false;    // the last save found no card
    uint32_t now = 0, last_input = 0, last_try = 0;
};

inline bool autosave_due(const AutosaveFacts& f) {
    if (!f.enabled || !f.dirty || f.playing || f.recording || f.sheet || f.loading || f.editing) return false;
    if (f.now - f.last_input < autosave_idle_ms) return false;
    return f.now - f.last_try >= (f.failed ? autosave_retry_ms : autosave_idle_ms);
}

} // namespace rhythmer
