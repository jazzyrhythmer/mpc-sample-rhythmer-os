// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Stems: a sample separated into its drums, its bass, its music (every other
// instrument) and its vocals, each a file of its own.
//
// The separating is done by a program of its own, rhythmer-stems
// (tools/stems/stems_engine.c): Spleeter's 4-stem separation as Deezer's own
// program does it, on SpleeterRT's network code. Each stem has its network,
// each estimates its stem from the whole mix, and every bin of the mix is
// shared between the four by their estimates, so the stems add up to the
// mix. Its model (Deezer's, tools/stems/convert_model.py) is a file beside
// it. Both are kept on the card, not in Rhythmer's binary: 80 MB that would
// otherwise sit in RAM the whole session. The card is mounted noexec, so a
// job copies the program into RAM, runs it there at the lowest priority and
// removes the copy when it ends; the model is read where it is. The program
// reads one WAV file and writes four beside it (44.1 kHz, 32-bit float,
// stereo, the input's length to the frame); those are written to the card as
// write_wav() keeps a sample. It prints how far it is, a line a step.
//
// BEST (the STEMS sheet's B2, a setting) has the networks look up to 16 kHz,
// not 11, in two passes half a pass apart: the stems' highs are their own
// and no seam is left where a pass ends, in about three times the time.
//
// Measured on the MPC with three threads: 13 s of sound in 13 s, 60 s in
// 36 s; at its fullest 478 MB for 13 s and 656 MB for 60 s.
namespace rhythmer {

constexpr unsigned stem_count = 4;
// The stems in the order they land: the word a file's name ends in, and the screen's.
inline const char* stem_name(unsigned n) {
    static const char* const names[stem_count] = {"DRUMS", "BASS", "MUSIC", "VOCALS"};
    return names[n % stem_count];
}

// What a job takes for `frames` 44.1 kHz frames, BEST or not: memory at its
// fullest, the program and its files in RAM with it, in bytes; and seconds on the MPC.
uint64_t stems_memory(uint32_t frames, bool best);
unsigned stems_seconds(uint32_t frames, bool best);
// The shortest sample the program is given: under a second there is nothing for it to tell apart.
constexpr uint32_t stems_least_frames = 44100;

struct StemsProgress {
    bool busy = false;
    float fraction = 0;        // 0..1
    unsigned seconds_left = 0;
};

struct StemsJob {
    std::string engine;        // the program, on the card
    std::string model;         // its model, on the card
    std::string work;          // a folder in RAM for its copy and its files: made here, removed after
    std::vector<float> left, right;             // the sample's 44.1 kHz frames
    bool best = false;                          // up to 16 kHz in two passes (the program's "best")
    std::array<std::string, stem_count> paths;  // the files to write, in stem_name()'s order
};

struct StemsResult {
    std::string error;         // empty: the four files are written; "stopped": it was cancelled
    std::array<std::string, stem_count> paths;
};

// One job at a time, on a thread of its own. start() and take() belong to
// one thread (the library's service thread); cancel(), busy() and progress() to any.
class StemSeparator {
public:
    StemSeparator() = default;
    ~StemSeparator(); // a job still running is stopped first
    StemSeparator(const StemSeparator&) = delete;
    StemSeparator& operator=(const StemSeparator&) = delete;

    bool start(StemsJob job); // false while a job runs or its result waits to be taken
    void cancel();
    bool busy() const { return running.load(std::memory_order_acquire); }
    StemsProgress progress() const;
    bool take(StemsResult& out);

private:
    void run(StemsJob job);
    std::string separate(StemsJob& job);
    std::thread worker;
    std::atomic<bool> running {false}, cancelled {false};
    std::atomic<float> fraction {0};
    std::atomic<unsigned> seconds_left {0};
    std::atomic<int> child {0}; // the program's process while it runs
    mutable std::mutex lock;
    StemsResult result;         // under `lock`
    bool finished = false;      // under `lock`: `result` waits to be taken
};

} // namespace rhythmer
