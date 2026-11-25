// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/multisample.h"
#include "rhythmer/engine/sample_data.h"
#include <cstdint>
#include <functional>
#include <string>

// A multisampled instrument's frames laid out once on the card, as
// rhythmer-ios's mapped audio: 32-bit float at the source's own rate (decoded,
// never converted), the left side, then the right starting on a chunk boundary,
// so chunks are whole pages
// to pin. A 64 KB header says what the file holds, so opening it again reads
// nothing else. Named by the source's path on the card, size and date and the
// cache version; a change misses it by itself. Service thread only.
//
// A kit pad's long sample of a kind that cannot be read in place (MP3, FLAC,
// Ogg, AIFF) is laid out here too, as a PCM WAV at the source's own rate
// (wav_copy()): the load then reads that WAV from the card as it plays, as
// it reads any long WAV (shell/sample_library.cpp). Named the same way,
// ".wav"; the project keeps the source's path.
namespace rhythmer {

struct LaidOut {
    uint32_t frames = 0, channels = 0;
    uint32_t source_rate = 0;            // the file's own, for its region's frames
    int64_t loop_start = -1, loop_end = -1; // its smpl chunk's, at the source's rate
};

constexpr uint64_t cache_most_bytes = 4000000000ull; // least lately played go first past this
constexpr int cache_unplayed_days = 30;               // not played for this long: removed
// Raise when what a source reads as changes (a decoder or loop fix, the layout):
// every older file then misses and ages out. 3: at the source's own rate (2 was 44.1 kHz).
constexpr unsigned cache_version = 3;

// What a source's WAV is to hold: its frames at `rate` Hz, at most `frames` of them (an MP3's length may be a guess:
// the file holds what was read), one side for a mono source, as integer PCM of 16 or 24 bits or as 32-bit float.
// `dither`: integer PCM from a source that kept no such steps (an MP3, an Ogg); a FLAC's or an AIFF's own values
// are written as they are.
struct WavCopy {
    unsigned rate = 0, channels = 2, bits = 16;
    bool dither = false;
    uint32_t frames = 0;
};
// The bits such a WAV takes for a source that kept `bits` a value (`floating`: as floats; 0: it does not say, an
// MP3 or an Ogg): its own where a PCM WAV has them (16, 24, 32-bit float), else the next up; 16 where it does not say.
inline unsigned wav_copy_bits(unsigned bits, bool floating) {
    return floating || bits > 24 ? 32 : bits > 16 ? 24 : 16;
}
// The next frames of a source, left and right apart (a mono source's on both); fewer than asked for: its end.
using FrameReader = std::function<uint32_t(float* left, float* right, uint32_t frames)>;

class SampleCache {
public:
    void set_folder(const std::string& folder) { folder_ = folder; }
    const std::string& folder() const { return folder_; }
    // Where a source's frames go; empty when it cannot be read or there is no folder.
    std::string file_of(const std::string& source) const;
    // One sound inside a file holding many (an .sf2's, roadmap P3): named after the file and `part`.
    std::string file_of(const std::string& source, const std::string& part) const;
    // True when `file` is whole and says what it holds. Marks it played.
    bool ready(const std::string& file, LaidOut& out) const;
    // Written to a .part, then renamed. Empty on success.
    std::string lay_out(const std::string& file, const SampleData& sample, const LaidOut& info) const;
    // Mapped read only, frames left 0 for the caller to publish.
    static bool map(const std::string& file, const LaidOut& info, ZonePart& part);
    static uint64_t bytes_of(uint32_t frames, uint32_t channels);
    bool has_room(uint64_t bytes) const; // on the card, beyond a margin
    // Removes .part files, files not played for cache_unplayed_days, then the least lately played past `most`.
    // Before any lay_out of this run: a .part found is one a stop left behind.
    void tidy(uint64_t most = cache_most_bytes) const;

    // A source's PCM WAV (see the top): where it goes, named as file_of() names; empty as file_of() is.
    std::string wav_of(const std::string& source) const;
    // True when `file` is such a WAV, whole (it is written to a .part, then renamed). Marks it played.
    bool wav_ready(const std::string& file) const;
    // The source's WAV, found, or made now from what `read` hands on, a piece at a time (nothing of the source is
    // held but a piece). Its path; empty when there is none: no folder, no room on the card, it could not be
    // written, or `read` gave nothing. `made`: true when it was written now.
    std::string wav_copy(const std::string& source, const WavCopy& what, const FrameReader& read, bool* made = nullptr) const;
    // What such a WAV takes on the card, and the most frames one holds (a WAV's lengths are 32 bits).
    static uint64_t wav_bytes(const WavCopy& what);
    static uint32_t wav_most_frames(const WavCopy& what);
private:
    std::string name_of(const std::string& source, const std::string& part, const char* kind) const;
    std::string folder_;
};

} // namespace rhythmer
