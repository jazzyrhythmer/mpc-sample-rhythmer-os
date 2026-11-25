// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/engine.h"
#include "rhythmer/engine/multisample.h"
#include "rhythmer/engine/stream_pager.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// Freeze plan F3: an audio track's regions as the audio thread plays them
// (model/audio_track.h). Each take is a WAV file read in place by the stream
// pager (engine/stream_pager.h), its frames at its own rate: a region's first
// chunk stays pinned while it is in the song, so it plays at once there; the
// pager pins about a second ahead of a voice. Past what is pinned a voice plays
// silence (an underrun; just after a jump, a wait) and keeps its place.
//
// The pattern leads: on each tick of the track's pattern the engine host names
// its take and how far into the pattern it is. The take starts again each time
// the pattern comes round; between, a voice plays on at its own pace (a tempo
// change does not move it) and only a jump (a locate) puts it where the
// pattern is, fading across.
//
// Warp fits a take to the tempo playing, as the MPC's Warp does: it plays
// faster or slower by the tempo against the one it was recorded at, and keeps
// its place in the pattern. Repitch plays it faster or slower, its pitch with
// it; Stretch keeps its pitch (or moves it by its semitones and cents) by
// reading it in overlapping 60 ms windows, each starting where it best lines
// up with the one before (WSOLA).
namespace rhythmer {

// A pattern's take as played: frames [offset, offset + frames) of `take`, from the pattern's top.
struct RegionClip {
    ZonePart* take = nullptr;
    uint32_t offset = 0, frames = 0;    // the take's frames, at its own rate
    uint32_t fade_in = 0, fade_out = 0; // the take's frames
    float gain = 1;
    bool reverse = false;
    uint16_t tempo = 1200; // tenths of a BPM it was recorded at
    WarpMode mode = WarpMode::Stretch;
    bool warp = false;     // fitted to the tempo playing (else as recorded)
    float pitch = 1;       // Stretch: its semitones and cents, as a ratio of frequencies
    bool resident = false; // tests: every frame in memory, read without the pager
    uint32_t held = 0;     // frames from where it starts playing pinned for as long as the pack lives
};

// An audio track's patterns' takes (nullptr: none) and the files they play. The MIDI thread makes it
// (shell/audio_tracks.cpp), the audio thread plays it (Command::AudioRegions) and hands it back to be freed once no
// voice reads it.
struct AudioPack {
    uint8_t track = 0;
    std::array<const RegionClip*, pattern_count> clips {};
    std::vector<std::unique_ptr<RegionClip>> owned_clips;
    std::vector<std::unique_ptr<ZonePart>> takes;
    std::vector<std::vector<float>> owned; // tests: frames held whole (left, then right)
    // Tests: a take of `frames` frames at `rate`, `left` and `right` held whole; its index in `takes`.
    unsigned hold_take(std::vector<float> left, std::vector<float> right, float rate);
    RegionClip& add(unsigned pattern) { // a clip for pattern `pattern`, owned here
        owned_clips.push_back(std::make_unique<RegionClip>());
        clips[pattern % pattern_count] = owned_clips.back().get();
        return *owned_clips.back();
    }
};

class RegionPlayer {
public:
    static constexpr unsigned voice_count = 4;
    static constexpr float fade_seconds = 0.005f;
    static constexpr double slack = 256; // device frames a voice may be off the song after a jump before it moves
    static constexpr float window_seconds = 0.06f; // Stretch's windows, half of each overlapping the next
    static constexpr float search_seconds = 0.008f, match_seconds = 0.008f; // where a window may start, what it matches
    void set_pager(StreamPager* p) { pager = p; }
    void set_tempo(unsigned tenths) { tempo_now = std::max(tenths, 1u); } // the tempo playing, for Warp
    // On a tick of the track's pattern: `clip` its take (nullptr: none), `into` the device frames from the pattern's top
    // to this tick (unrounded), `top` when the pattern starts (again) here, `jumped` when it did not come here from the
    // tick before.
    void follow(const RegionClip* clip, double into, bool top, bool jumped, float rate);
    void release(float rate);  // the pattern stopped or has no take: its voice fades out
    void fade_all(float rate); // its pack replaced: every voice fades out, marked as leaving
    void cut_all();            // every voice closed at once
    bool leaving() const;      // a voice from before fade_all() still sounds
    bool sounding() const;
    const RegionClip* playing() const { return voices[0].clip; }
    void render(float* left, float* right, unsigned frames);
    uint32_t underruns = 0, waits = 0;
    bool quiet = false; // its regions not heard (Monitor In; Auto while it records): the voices go on where they are

private:
    struct Voice {
        const RegionClip* clip = nullptr;
        double pos = 0, step = 1; // frames into the region, and a device frame's worth of them (Warp: at the tempo playing)
        double read = 1;          // Stretch: a window's own step through the take (its pitch)
        bool stretch = false;
        int stream = -1;
        bool seeking = false, leaving = false;
        float gain = 1, fade = 0; // a fade: `fade` a frame
        std::array<double, 2> tap {};      // Stretch: each window's place in the region
        std::array<uint32_t, 2> age {};    // and how far through it is
        std::array<bool, 2> on {};
        uint32_t since = 0, window = 1, hop = 1;
    };
    std::array<Voice, voice_count> voices {}; // [0] the main voice
    StreamPager* pager = nullptr;
    unsigned tempo_now = 1200;
    // Stretch's sin² weight a frame of a window, worked out once for its length (60 ms at up to 96 kHz).
    static constexpr unsigned max_window = 5760;
    std::array<float, max_window> weights {};
    uint32_t weights_for = 0;
    // A device frame's worth of the take's frames: its rate against the device's, and with Warp the tempo against its.
    double step_of(const RegionClip& c, float rate) const;
    void steer(Voice& v, float rate); // the voice's steps for the tempo playing
    double align(const Voice& v, const float* l, const float* r, uint32_t limit, double from, double near) const;
    void begin(const RegionClip* clip, double pos, bool fade_in, float rate);
    void retire_main(float rate);
    void close(Voice& v);
};

// The engine of an AUDIO track: its regions' voices, in stereo; notes do nothing (its pads play nothing, as on the MPC).
// The engine host renders the player itself, run by run between the song's ticks (as a frozen track's prints), so a
// region starts on its frame whether the tracks render once a block or step by step; the engine's own render adds
// nothing to the track's buffers.
class AudioTrackEngine : public Engine {
public:
    RegionPlayer player;
    void note_on(int, int) override {}
    void note_off(int) override {}
    void all_notes_off() override {}
    void silence() override { player.cut_all(); }
    void set_param(int, int) override {}
    void render(float*, unsigned) override {}
    bool stereo() const override { return true; }
    void render_stereo(float*, float*, unsigned) override {}
    bool sounding() const override { return player.sounding(); }
};

} // namespace rhythmer
