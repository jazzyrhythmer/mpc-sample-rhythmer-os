// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/stream_pager.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>

// What limits a project's samples: memory. A kit pad's sample is read whole into memory, its frames at its own rate
// and nothing more, unless it is long: then it is read from the card as it plays; an instrument's files are streamed from
// the card, only each zone's start kept. The MPC has 2 GB and no swap, so a load that would leave less than memory_headroom
// free is refused (shell/sample_library.cpp, shell/sample_library_instruments.cpp). Pure but for memory_free().
namespace rhythmer {

// A PCM WAV longer than stream_seconds is not read into memory: it is read from the card as it plays (its pads'
// starts held). Any sample, and a capture (a take, a render), up to stream_most_seconds. A file of another kind
// (MP3, FLAC, Ogg, AIFF) that long is decoded once, a piece at a time, into a PCM WAV in the card's Rhythmer/Cache
// (shell/sample_cache.h's wav_copy()), and that WAV is read from the card as it plays; with no card, or no room on
// it, the file is read whole, as memory allows.
constexpr unsigned stream_seconds = 10;
constexpr unsigned stream_most_seconds = 6 * 3600;
// A shorter sample is read into memory while memory has room for it: it then plays and is edited with no card under
// it. With no room, one longer than stream_least_seconds is read from the card as it plays instead of being refused
// (the MPC's own app streams every sample longer than that); a shorter one is refused.
constexpr unsigned stream_least_seconds = 2;
constexpr uint64_t memory_headroom = uint64_t(256) << 20;  // left free after a load: the app's own work and the system's

// What is held in memory where a pad on a streamed sample starts, loops and (reversed) ends, in the file's own
// frames at `rate` Hz: half a second, and a chunk at the least.
inline uint32_t stream_head_frames(float rate) { return std::max<uint32_t>(stream_chunk, uint32_t(rate / 2)); }

// What a kit pad's sample of `frames` frames at `rate` Hz holds once read: its own frames, left and right as floats.
inline uint64_t kit_sample_bytes(uint64_t frames, unsigned rate) {
    if (!rate) return 0;
    return std::min<uint64_t>(frames, uint64_t(stream_most_seconds) * rate) * 2 * sizeof(float);
}

// Whether a sample of `frames` frames at `rate` Hz is read from the card as it plays. `room`: memory would take it.
inline bool streams_from_card(uint64_t frames, unsigned rate, bool room) {
    return frames > uint64_t(stream_seconds) * rate || (!room && frames > uint64_t(stream_least_seconds) * rate);
}

// Whether a load that takes `wanted` bytes leaves the headroom, with `free` bytes free now (0: the system does not
// say, a host: allowed).
inline bool memory_allows(uint64_t free, uint64_t wanted) {
    return !free || (free >= memory_headroom && wanted <= free - memory_headroom);
}

// The memory free to take now, as the system counts it (MemAvailable); 0 where it does not say (a host).
inline uint64_t memory_free() {
    std::ifstream info("/proc/meminfo");
    std::string name;
    uint64_t kb = 0;
    while (info >> name >> kb) {
        if (name == "MemAvailable:") return kb * 1024;
        std::string rest;
        std::getline(info, rest);
    }
    return 0;
}

// Whether a load that takes `wanted` bytes may go ahead now. Where memory is short, what the pager keeps of the
// files it has read (engine/stream_pager.h: up to stream_cache_bytes, nothing counting on it) is given back first.
inline bool memory_for(uint64_t wanted) {
    if (memory_allows(memory_free(), wanted)) return true;
    return stream_cache_give_back() && memory_allows(memory_free(), wanted);
}

} // namespace rhythmer
