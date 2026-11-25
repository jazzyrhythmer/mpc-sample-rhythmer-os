// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// The auto sampler: a plug-in track's instrument played a key at a time by
// the print engine (shell/print_engine.h, a print host of its own, at SCHED_IDLE),
// each note held, let go and its release kept, written as 24-bit WAVs (mono where
// both sides are the same) and an MPC keygroup program (shell/xpm_write.h) in
// Samples/Keygroups/<name>/, which a new KEYGROUP track then plays with the
// plug-in track's patterns and mix (shell/auto_sample.cpp). Taken before the
// track's chain (the keygroup track gets the chain) or after it (the keygroup
// track's chain empty).
namespace rhythmer {

// What the check heard (engine/auto_probe.h), before anything is sampled: nothing to pick, nothing read from the
// plug-in. Keys: a pitched instrument, as before. Mono: one note at a time; `glide` the seconds its pitch slides to a
// new key, `legato` only when the keys overlap (the program is written Mono with that Portamento). Drums: each key a
// sound of its own, sampled once each, not stretched over its neighbours; `keys` those of the range that sound.
// `accent`: the velocity the sound jumps at (the layers split there), 0 none. `takes`: hits of one key differ, so each
// key is taken this many times and the program plays them in turn. `sustains`: a held key keeps its level, so LOOP
// (at Auto) loops each note's held part and it plays on for as long as the key is held. `moves`: held, its level or
// brightness keeps changing (detuned oscillators, an LFO), so its loop must be long. `hold`: the seconds HOLD (at
// Auto) holds each key: 2 for a steady sound, 8 for one that moves, and for one that fades as long as it is heard
// (1 to 8).
enum class AutoKind : uint8_t { Keys, Mono, Drums };
// How one side of a split plays (AutoFindings::upper; the lower side's, or the whole instrument's, are AutoFindings'
// own fields of the same names).
struct AutoZone {
    AutoKind kind = AutoKind::Keys; // Keys or Mono
    float glide = 0;
    bool legato = true, tied = true;
    bool sustains = false, moves = false;
    float hold = 0;
    float release = 0, release_slow = 0, release_share = 0;
    float velocity = 1;
};
struct AutoFindings {
    bool heard = false;
    AutoKind kind = AutoKind::Keys;
    float glide = 0;
    bool legato = true;
    // Mono: a key pressed while another is held carries that note on to it, no new attack (a 303's slide, a synth's
    // single trigger); false: it starts the sound again, as the plug-in was heard to (probe_retriggers()).
    bool tied = true;
    uint8_t accent = 0;
    uint8_t takes = 1;
    bool sustains = false;
    bool moves = false;
    float hold = 0; // seconds; 0: not heard
    // How it falls let go after half a second, while it is loud (engine/release_fit.h): the release for a sound whose
    // sampled notes are held until they have died away, and so tell nothing of it. Seconds; 0: not heard.
    float release = 0, release_slow = 0, release_share = 0;
    // How far its level follows velocity, 0..1 as a pad's Velocity has it (1: half the velocity, half the level;
    // 0: every note as loud), fitted to one key played at sixteen velocities. The program's keygroups get it, so a
    // soft press is as soft as the plug-in's and no softer.
    float velocity = 1;
    // A split: two sounds side by side on the keyboard (a bass under a lead), each with its own way of playing.
    // `split`: the first key of the upper one, 0 none. The fields above are then the lower side's, `upper` the other's.
    uint8_t split = 0;
    AutoZone upper;
    // How many times real time its notes played during the check (an instrument outside: about 1); 0: not heard.
    float speed = 0;
    // Velocity layers, from the accent's sixteen hits of one key: their velocities and brightness (Hz), how many
    // layers the tone needs (LAYERS at Auto, 1-8; 1 where the hits differ in level only, or from hit to hit) and,
    // for 1 to 8 layers, each one's top velocity (engine/auto_probe.h probe_layers(); 0 past the last).
    uint8_t sweep_count = 0;
    std::array<uint8_t, 16> sweep_velocity {};
    std::array<float, 16> sweep_bright {};
    uint8_t layers = 1;
    std::array<std::array<uint8_t, 8>, 8> layer_tops {};
    AutoZone zone(unsigned key) const {
        if (split && key >= split) return upper;
        return {kind, glide, legato, tied, sustains, moves, hold, release, release_slow, release_share, velocity};
    }
    std::array<uint64_t, 2> keys {};
    uint8_t scan_low = 0, scan_high = 127; // drums: the keys the check tried (outside them nothing is known)
    bool sounds(unsigned key) const {
        if (key > 127) return false;
        return key < scan_low || key > scan_high || ((keys[key / 64] >> (key % 64)) & 1);
    }
    unsigned sounding() const { unsigned n = 0; for (auto k : keys) n += unsigned(__builtin_popcountll(k)); return n; }
};

// What a plug-in track is played over, a note at a time.
struct AutoSampleSettings {
    uint8_t low = 36, high = 96; // the lowest and highest root, MIDI notes (C2, C7)
    uint8_t step = 3;            // semitones between roots
    uint8_t layers = 0;          // velocity layers: 0 Auto (as many as the check heard the tone needs), 1-8
    float hold = 2.0f;           // seconds each key is held, when not at Auto
    bool hold_auto = true;       // HOLD at Auto: as long as the check found the sound needs (AutoFindings::hold)
    float tail = 8.0f;           // seconds of release kept at most
    bool post_fx = false;        // taken after the track's chain (its effects in the samples), else before it
    bool loop = false;           // each sample a loop where the key was held (engine/loop_find.h), when not at Auto
    bool loop_auto = true;       // LOOP at Auto: a loop when the check heard the sound sustain (AutoFindings::sustains)
    bool normalize = false;      // one gain for the whole program, its loudest sample at -1 dB (layers keep their balance);
                                 // else as played: the program as loud as what was sampled
    AutoFindings found;          // what the check heard
    // For the key `key` (its side of a split), and without one for the lower side or the whole instrument.
    bool looping(unsigned key = 0) const {
        if (!loop_auto) return loop;
        return found.kind != AutoKind::Drums && found.zone(key).sustains;
    }
    float hold_seconds(unsigned key = 0) const {
        if (!hold_auto) return hold;
        const AutoZone z = found.zone(key);
        if (!found.heard || z.hold <= 0) return 2.0f;
        if (!loop_auto && loop && !z.sustains) return 2.0f; // LOOP put on a sound that fades: looped early, while it sounds
        return z.hold;
    }
    // The longest any key is held, and whether any key loops (the card's room, the sheet).
    float hold_most() const { return found.split ? std::max(hold_seconds(0), hold_seconds(found.split)) : hold_seconds(0); }
    bool loops_any() const { return looping(0) || (found.split && looping(found.split)); }
    // The velocity layers taken: as asked, or at Auto as many as the check heard the tone needs (one before it).
    unsigned layer_count() const {
        const unsigned asked = std::clamp<unsigned>(layers ? layers : found.heard ? found.layers : 1, 1, 8);
        if (!found.heard) return asked;
        unsigned placed = 0; // fewer where two would share a top velocity
        for (uint8_t top : found.layer_tops[asked - 1]) placed += top != 0;
        return placed ? placed : asked;
    }
    // Takes of each key: the check's, with one velocity layer (MPC plays a keygroup's layers in turn or by velocity, not both).
    unsigned take_count() const { return layer_count() == 1 ? std::clamp<unsigned>(found.takes, 1, 4) : 1; }
    // Each layer's velocity (the loudest of its range), rising, the last 127: where the check heard the tone change
    // (an accent starts a layer), else evenly (127 alone; 63, 127; 42, 84, 127 ...).
    uint8_t velocity(unsigned layer) const {
        const unsigned n = layer_count();
        layer = std::min(layer, n - 1);
        const unsigned asked = std::clamp<unsigned>(layers ? layers : found.heard ? found.layers : 1, 1, 8);
        if (found.heard && found.layer_tops[asked - 1][layer]) return found.layer_tops[asked - 1][layer];
        return uint8_t(std::clamp<unsigned>((layer + 1) * 127 / n, 1, 127));
    }
    // About how long sampling takes, seconds: each note held, then let go until it has fallen silent (TAIL at most),
    // and a moment of quiet before the next, at the speed the check played it. 0: not known before the check (an
    // instrument outside is checked once SAMPLE is pressed).
    float seconds_expected() const {
        if (!found.heard || found.speed <= 0) return 0;
        double total = 0;
        for (uint8_t root : roots()) {
            const AutoZone z = found.zone(root);
            // To silence: 60 dB down. A release's seconds take it 26 dB down (engine/release_fit.h); a second stage
            // starts at its share of the level.
            float fall = z.release * 60.0f / 26.0f;
            if (z.release_slow > 0 && z.release_share > 0)
                fall = std::max(fall, z.release_slow * std::max(0.0f, 60.0f + 20.0f * std::log10(z.release_share)) / 26.0f);
            total += double(layer_count() * take_count()) * (hold_seconds(root) + std::min(tail, fall + 0.2f) + 0.1f);
        }
        return float(total / found.speed);
    }
    // The keys sampled: every STEP from LOW; drums, every key of the range that sounds.
    std::vector<uint8_t> roots() const {
        std::vector<uint8_t> out;
        if (found.kind == AutoKind::Drums) {
            for (int k = low; k <= high && k <= 127; ++k) if (found.sounds(unsigned(k))) out.push_back(uint8_t(k));
            return out;
        }
        for (int k = low; k <= high && k <= 127; k += std::max<int>(step, 1)) out.push_back(uint8_t(k));
        return out;
    }
};

// What the check heard of a plug-in track, or why it could not.
struct AutoProbeResult {
    std::string project_path;
    unsigned track = 0;
    AutoFindings found;
    std::string error;
    int key = -1;      // the key the error is about (an input that clips there), -1 none
    std::string heard; // what it measured, for `rhythmer-print status`
};

// What the auto sampler made: "<folder>/<name>.xpm" and its WAVs beside it, or why not.
struct AutoSampleResult {
    std::string project_path;
    unsigned track = 0;
    std::string program; // the .xpm's path
    std::string name;
    std::string error;   // empty: made
    int key = -1;        // the key the error is about (an input that clips there), -1 none
    unsigned samples = 0;
    unsigned loops = 0;  // samples given a loop
    unsigned centred = 0; // samples whose DC offset was taken out
    unsigned mono = 0;   // samples written as one channel (both sides the same)
    unsigned layers = 0; // the most velocity layers a keygroup has (over four: the track plays the .xty)
    // The release fitted to how its notes fell (engine/release_fit.h): seconds; a second stage's seconds and share (0: none).
    float release = 0, release_slow = 0, release_share = 0;
    std::string release_note; // what the fit went by, for `rhythmer-print status`
    float gain_db = 0;   // the program's gain (normalize)
    bool post_fx = false;
    double seconds = 0;
};

} // namespace rhythmer
