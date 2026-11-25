// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/sample_data.h"
#include <memory>
#include <string>

namespace rhythmer {

// A sound file, whatever its kind, at its own rate; empty on success.
std::string read_audio(const std::string& path, SampleData& out, unsigned max_seconds, WavInfo* info);

// A sound file a piece at a time, as read_audio() reads it whole (it is read_audio()'s reader): WAV by WavReader,
// FLAC, Ogg, AIFF and MP3 by JUCE's (an MP3's encoder delay and padding left out). open() finds its rate and its
// frames, at most `max_seconds` of them (empty on success, else read_audio()'s words); read() hands on the next ones at
// the file's own rate (a mono file's on both sides), fewer when the file turns out shorter than it said, which
// ends it (failed(): it could not be read there, not an MP3 whose length was a guess). Service thread only.
// What a copy of it as a PCM WAV needs (shell/sample_cache.h's wav_copy()): decoded() for a kind that is not a WAV,
// its sides, and the bits it keeps a value in where the file says (a WAV's, a FLAC's, an AIFF's; floating(): as
// floats), 0 for an MP3 or an Ogg, which keep none.
class AudioReader {
public:
    AudioReader();
    ~AudioReader();
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;
    std::string open(const std::string& path, unsigned max_seconds, WavInfo* info = nullptr);
    unsigned rate() const { return hz; }
    uint32_t frames() const { return count; }
    uint32_t read(float* left, float* right, uint32_t frames);
    bool ended() const { return done >= count; }
    bool failed() const { return broken; }
    bool decoded() const { return juce != nullptr; }
    unsigned channels() const { return sides; }
    unsigned bits() const { return depth; }
    bool floating() const { return floats; }
private:
    struct Juce;
    std::unique_ptr<Juce> juce; // none: a WAV
    WavReader wav;
    unsigned hz = 0, sides = 0, depth = 0;
    uint32_t count = 0, done = 0;
    bool broken = false, floats = false;
};

} // namespace rhythmer
