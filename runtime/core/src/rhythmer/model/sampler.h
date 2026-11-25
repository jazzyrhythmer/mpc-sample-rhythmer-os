// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>

// A sampler track's kit: 128 pads in eight banks of sixteen (A-H, as the
// MPC's Pad Bank steps them), each a region of one of the project's samples
// (pads share files; nothing is copied when a sound is chopped). Plain data,
// like the rest of the model.
namespace rhythmer {

constexpr unsigned sampler_banks = 8, sampler_pads = 16 * sampler_banks;
constexpr unsigned legacy_pads = 64;     // banks A-D: the pads a project file's TRAK section holds
// The places a project has for samples: one for each layer of each pad of each track (16 tracks, 128 pads, 4
// layers), so the pads run out before the places do. What a project can hold is what fits in memory
// (shell/sample_memory.h); the tables kept for each place hold only those in use (model/slot_table.h).
constexpr unsigned sample_slots = 8192;
// The slots a project file's SMPL section names, and all an older build holds (64 samples a project): the slots
// past them are kept in SMPX, which such a build skips (model/project_file.cpp).
constexpr unsigned legacy_sample_slots = 64;
// One more slot on the engine's side only: the browser's preview of a file
// (B3 on Samples), never a pad's sample and never in the project.
constexpr unsigned preview_slot = sample_slots;
constexpr uint16_t no_sample = 0xffff;
constexpr unsigned sampler_first_note = 36; // pad 1 of bank A; pad n of the kit is note 36 + n
// Kit notes run past MIDI's 127 (bank H pad 16 is 163): they stay inside Rhythmer.
constexpr unsigned sampler_last_key = sampler_first_note + sampler_pads - 1;

constexpr unsigned punch_kinds = 47;       // PunchKind::Count (engine/punch.h); 27 before parity phase 7b

enum class PadPlay : uint8_t { Gate, OneShot }; // Gate is the MPC's Note On (Shift + Chop)

// A pad's own filter (parity phase 4, the MPC's types); the kit filter stays the track's.
enum class PadFilter : uint8_t { Off, Classic, Lpf2, Lpf4, Hpf2, Hpf4, Bpf2, Bpf4, Count };
constexpr unsigned pad_filter_count = unsigned(PadFilter::Count);
inline const char* pad_filter_name(PadFilter f) {
    static const char* const names[] {"Off", "Classic", "LPF2", "LPF4", "HPF2", "HPF4", "BPF2", "BPF4"};
    return names[unsigned(f) % pad_filter_count];
}

// Warp (Tune page, K3): off, locked to the sequence's tempo, or 50..200 % of its length.
constexpr uint8_t warp_off = 0, warp_seq = 1, warp_min = 50, warp_max = 200;
constexpr unsigned max_pad_beats = 64; // # Beats: 0 works it out from the length
constexpr unsigned max_loop_xfade = 250; // ms: a loop's seam crossfade (roadmap P3)
// A CLOUD pad on a long sample read from the card takes its grains from the first this many seconds of its region,
// held in memory while it is a cloud (shell/stream_holds.h, engine/sampler.cpp spawn()).
constexpr unsigned cloud_most_seconds = 30;

// Volume as the MPC shows it: 0 is -inf, 1..100 run -74..0 dB, 100..127
// run 0..+6 dB. Pad volume and kit volume both use it.
constexpr float level_floor_db = -74.0f, level_top_db = 6.0f;
inline float volume_db(int level) {
    if (level <= 0) return -INFINITY;
    if (level <= 100) return 37.0f * std::log10(float(level) / 100.0f); // 1 is -74 dB
    return level_top_db * float(std::min(level, 127) - 100) / 27.0f;
}
inline float volume_gain(int level) { return level <= 0 ? 0.0f : std::pow(10.0f, volume_db(level) / 20.0f); }

// Pad layers (roadmap P3): up to four samples on a pad. Layer 1 is the pad's
// own sample and region; layers 2-4 are each a region of another sample with
// its tune, level and pan over the pad's. A hit plays every layer its velocity
// reaches (Velocity, the MPC's default), the next in turn (Cycle) or one at
// random (Random), as the MPC's layers play. Cycle hold and Random hold
// pick the same way for each hit by hand, but Note Repeat keeps the layer its
// first repeat picked until the pad is let go. Every other value of the pad
// (envelope, filter, play, loop, warp) is the pad's and goes over each.
constexpr unsigned pad_layers = 4;
enum class LayerPlay : uint8_t { Velocity, Cycle, Random, CycleHold, RandomHold, Count }; // saved: append only
inline const char* layer_play_name(LayerPlay l) {
    static const char* const names[] {"VELOCITY", "CYCLE", "RANDOM", "CYCLE HOLD", "RANDOM HOLD"};
    return names[unsigned(l) % unsigned(LayerPlay::Count)];
}
struct PadLayer {
    uint16_t sample = no_sample;  // index into Project::samples
    uint32_t start = 0, end = 0;  // 44.1 kHz frames; end 0 means the sample's end
    int8_t pitch = 0, fine = 0;   // semitones and cents over the pad's
    int16_t gain = 0;             // hundredths of a dB over the pad's
    int8_t pan = 0;               // over the pad's, -50..50
    uint8_t low = 1, high = 127;  // the velocities it plays under Velocity
    bool used() const { return sample != no_sample; }
};

struct PadSound {
    uint16_t sample = no_sample;  // index into Project::samples
    uint32_t start = 0, end = 0;  // frames; end 0 means the sample's end
    int8_t pitch = 0;             // semitones
    int8_t fine = 0;              // cents
    uint8_t level = 100;          // 0..127, 100 is unity
    int8_t pan = 0;               // -50..50
    PadPlay play = PadPlay::OneShot;
    bool reverse = false;
    uint8_t group = 0;            // mute group 1..16, 0 none: pads in a group cut each other
    uint8_t attack = 0, decay = 100, sustain = 100, release = 20; // 0..100
    uint16_t own_tempo = 0;       // tenths of a BPM the region was played at, for Follow; 0 unknown
    // Parity phase 4, the MPC's Sample Mode (the file's PAD4 section).
    bool loop = false;            // Loop: after the end it plays on from the loop point
    bool loop_lock = true;        // Loop Lock (Shift + B1 on Trim): the loop point is the start
    uint32_t loop_start = 0;      // frames, when not locked
    bool mono = true;             // a new hit stops the pad's last one at any pitch, 16 Levels too (else Poly: they overlap)
    uint8_t link = 0;             // Pad Link: 1..16 a pad of the same bank played with it, 0 none (it links on: roadmap P3)
    uint8_t offset = 0;           // 0..100: the start waits up to a sixteenth after the hit
    uint8_t velocity_sense = 100; // 0..100: 0 every hit full, 100 soft hits quietest
    bool decay_from_start = false; // One shot: decay from the start (ADSR), else over the decay before the end (MPC's default)
    PadFilter filter = PadFilter::Off;
    uint8_t cutoff = 100, resonance = 0;                                           // 0..100
    uint8_t filter_attack = 0, filter_decay = 50, filter_sustain = 0, filter_release = 20, filter_amount = 0; // 0..100
    uint8_t warp = warp_off;      // warp_off, warp_seq, or 50..200 %
    uint8_t beats = 0;            // # Beats for Warp's Seq, 0 from its length
    int16_t gain = 0;             // hundredths of a dB: Normalize (Shift + B2 on Trim)
    // Roadmap P3: layers 2-4 (the file's LAYR section), layer 1's velocities, and how a hit picks.
    std::array<PadLayer, pad_layers - 1> layers {};
    uint8_t low_velocity = 1, high_velocity = 127;
    LayerPlay layer_play = LayerPlay::Velocity;
    // Roadmap P3: a loop's seam crossfaded over this many milliseconds (0 none), the same time at any rate (PXFD).
    uint8_t loop_xfade = 0;
    // The colour the pad lights and is drawn in, 0xRRGGBB; 0: the track's. Stage's COLOUR sets it, an MPC kit brings
    // its own (shell/mpc_project.h mpc_pad_colours()); kept in PCOL.
    uint32_t colour = 0;
    // The pad's own send into each bus A-D, 0..100 (kept in PSND): taken after its Pad FX, its volume and pan, before
    // the track's chain, at the track's level, pan and mute. The track's sends (Mix::sends) still send the whole kit.
    std::array<uint8_t, 4> sends {};
    // Cloud (Stage's CLOUD, kept in PCLD): the pad played as grains. Where they come from, their size, how many a
    // second, how fast the place moves (50 still), their detune; 0..100. A file from before kept one Cloud for the
    // whole kit: it lands on every pad.
    bool cloud = false;
    uint8_t cloud_position = 38, cloud_size = 50, cloud_density = 24, cloud_scan = 50, cloud_spread = 12;
    bool plain_cloud() const {
        return !cloud && cloud_position == 38 && cloud_size == 50 && cloud_density == 24 && cloud_scan == 50 && cloud_spread == 12;
    }
    bool sending() const { for (auto s : sends) if (s) return true; return false; }
    // Its output (MPC 3's pad output, kept in PSUB): 0 the kit, n the mixer's submix n (1-8). Sent to a submix it plays
    // there after its Pad FX, its volume and pan, at the track's level, pan and mute, as its sends are taken, instead
    // of through the track's chain and strip.
    uint8_t out = 0;
    uint32_t loop_point() const { return loop_lock ? start : loop_start; }
    bool used() const { return sample != no_sample; }
    bool warped() const { return warp == warp_seq || warp >= warp_min; }
    // Layer n (0-3) as a PadLayer: 0 is the pad's own sample and region, at no offset.
    PadLayer layer(unsigned n) const {
        if (n) return layers[(n - 1) % (pad_layers - 1)];
        PadLayer own;
        own.sample = sample; own.start = start; own.end = end; own.low = low_velocity; own.high = high_velocity;
        return own;
    }
    bool layered() const {
        for (const auto& l : layers) if (l.used()) return true;
        return false;
    }
    // Whether any layer plays sample slot `slot`.
    bool plays(uint16_t slot) const {
        if (sample == slot) return true;
        for (const auto& l : layers) if (l.used() && l.sample == slot) return true;
        return false;
    }
    // Whether the layer and play values are as a new pad has them (LAYR need not hold this pad).
    bool plain_layers() const { return !layered() && low_velocity == 1 && high_velocity == 127 && layer_play == LayerPlay::Velocity; }
};
// Pad `sound` as layer n of it plays: that layer's sample and region, its offsets over the pad's.
inline PadSound layer_sound(const PadSound& sound, unsigned n) {
    if (!n) return sound;
    const PadLayer& l = sound.layers[(n - 1) % (pad_layers - 1)];
    PadSound one = sound;
    one.sample = l.sample; one.start = l.start; one.end = l.end;
    one.pitch = int8_t(std::clamp(sound.pitch + l.pitch, -48, 48));
    one.fine = int8_t(std::clamp(sound.fine + l.fine, -99, 99));
    one.gain = int16_t(std::clamp(sound.gain + l.gain, -9600, 6000));
    one.pan = int8_t(std::clamp(sound.pan + l.pan, -50, 50));
    one.loop_lock = true; // a layer loops from its own start
    one.layers = {};
    return one;
}

// A colour (0xRRGGBB) raised to full brightness, its hue and saturation kept; 0 stays 0. A dark kit colour
// (an MPC's 7F0000) still lights: how bright a pad is says how much it matters (shell/pad_light.h).
constexpr uint32_t full_colour(uint32_t rgb) {
    const uint32_t r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    const uint32_t most = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (!most) return 0;
    auto up = [most](uint32_t c) { return (c * 255 + most / 2) / most; };
    return up(r) << 16 | up(g) << 8 | up(b);
}

// The length of `pad`'s region in 44.1 kHz frames, `frames` the sample's (0 unknown): an end of 0, or past the
// sample's end, is the sample's end. 0 when that is not known.
inline uint32_t region_frames(const PadSound& pad, uint32_t frames) {
    const uint32_t end = pad.end && (!frames || pad.end <= frames) ? pad.end : frames;
    return end > pad.start ? end - pad.start : 0;
}
// The tempo (tenths of a BPM) `beats` beats over `region` frames (44.1 kHz) were played at; 0 unknown.
inline uint16_t beats_tempo(unsigned beats, uint32_t region) {
    if (!beats || !region) return 0;
    return uint16_t(std::clamp<long long>(std::llround(beats * 60.0 * 44100.0 * 10.0 / region), 1, 65535));
}

// Chop's slice [start, end) of `parent`'s sample as a pad of its own: the parent pad's values (tune, Warp and the
// tempo it was played at, mute group, play, envelope, filter, level, pan), with what belongs to the parent's region
// alone left out (its layers, Pad Link, a loop point inside it). Warp's Seq by # Beats counts the parent's region
// (`region` frames, 0 unknown): the slice keeps the tempo that gives as its own, so it plays at the parent's speed.
inline PadSound slice_sound(const PadSound& parent, uint32_t start, uint32_t end, uint32_t region) {
    PadSound one = parent;
    one.start = start; one.end = end;
    one.layers = {};
    one.low_velocity = 1; one.high_velocity = 127;
    one.layer_play = LayerPlay::Velocity;
    one.link = 0;
    one.loop_lock = true; one.loop_start = 0;
    if (parent.warp == warp_seq && parent.beats) {
        if (const uint16_t tempo = beats_tempo(parent.beats, region)) one.own_tempo = tempo;
    }
    one.beats = 0;
    return one;
}
// Chop's audition of the slice [start, end) at `parent`'s speed and pitch (Warp, the tempo it was played at, Semi
// and Fine), as the slice's pad plays it; nothing else of the pad's (it stays a plain one shot).
inline PadSound slice_heard(const PadSound& parent, uint32_t start, uint32_t end, uint32_t region) {
    const PadSound full = slice_sound(parent, start, end, region);
    PadSound one;
    one.sample = full.sample; one.start = start; one.end = end;
    one.pitch = full.pitch; one.fine = full.fine;
    one.warp = full.warp; one.own_tempo = full.own_tempo;
    return one;
}

// Round 3 "instrument C": one zone of the instrument on a kit pad, tuned or turned up or down from its file's own
// (Stage's ZONES, K2 and K3), so one sample out of tune or too loud is put right without touching the file. A track
// keeps up to max_zone_trims (the file's ZTRM section); pad 0xff is a free one. `slot` is the instrument it was made
// on: once the pad holds another, the trim is not heard (and is dropped at the next trim or save).
constexpr unsigned max_zone_trims = 64;
constexpr int zone_trim_cents = 100;                  // TUNE: -100..100 cents
constexpr int zone_trim_low = -240, zone_trim_high = 120; // LEVEL: tenths of a dB
struct ZoneTrim {
    uint8_t pad = 0xff;  // kit pad 0..127
    uint16_t slot = 0;   // the project's sample slot holding the instrument
    uint16_t zone = 0;   // the zone's place in its instrument
    int8_t tune = 0;     // cents
    int16_t level = 0;   // tenths of a dB
    bool used() const { return pad != 0xff; }
};
using ZoneTrims = std::array<ZoneTrim, max_zone_trims>;
inline const ZoneTrim* find_trim(const ZoneTrims& trims, unsigned pad, unsigned zone, unsigned slot) {
    for (const auto& t : trims) if (t.used() && t.pad == pad && t.zone == zone && t.slot == slot) return &t;
    return nullptr;
}
// Zone `zone` of pad `pad`'s instrument (sample slot `slot`) at `tune` and `level`: a trim of none is taken away;
// false when every trim is in use.
inline bool set_trim(ZoneTrims& trims, unsigned pad, unsigned zone, unsigned slot, int tune, int level) {
    ZoneTrim* free = nullptr;
    for (auto& t : trims) {
        if (t.used() && t.pad == pad && t.zone == zone) { free = &t; break; }
        if (!t.used() && !free) free = &t;
    }
    const bool none = !tune && !level;
    if (!free) return none;
    if (none) { if (free->used()) *free = ZoneTrim {}; return true; }
    free->pad = uint8_t(pad); free->zone = uint16_t(zone); free->slot = uint16_t(slot);
    free->tune = int8_t(std::clamp(tune, -zone_trim_cents, zone_trim_cents));
    free->level = int16_t(std::clamp(level, zone_trim_low, zone_trim_high));
    return true;
}
inline void clear_trims(ZoneTrims& trims, unsigned pad) { for (auto& t : trims) if (t.used() && t.pad == pad) t = ZoneTrim {}; }

// SampleRef::root when no note is known: not looked for yet (its file still being read), or none (below).
constexpr int8_t root_unread = -1, root_none = -2;

struct SampleRef {
    char path[256] {};  // on the card; empty: free (255 characters: what a project file's text holds)
    char name[28] {};
    uint32_t frames = 0; // at 44.1 kHz, once loaded
    uint16_t zones = 0;  // a multisampled instrument's zones, once read (round 3 "instrument C": Stage's ZONES walks them)
    // The keys its zones sound on when pressed (a release sample's not), once read: 16 Levels' Zones lays them over
    // the pads. Not kept in the file.
    std::array<uint64_t, 2> keys {};
    bool one_key_zones = false; // every zone a press plays on one key of its own, two keys or more: a drum or percussion layout
    bool streamed = false;      // a long sample read from the card as it plays, once loaded (shell/stream_holds.h); not kept in the file
    // The note its sound holds (engine/root_find.h), found once when its file is first read into the slot and kept in
    // the project: a pitch class, 0 C .. 11 B. root_none: no single note (a drum, a phrase, a chord, an instrument),
    // and every sample of a project saved before, which plays as a C as it did; root_unread until its file is read.
    int8_t root = root_unread;
    bool used() const { return path[0] != 0; }
    // A multisampled instrument, by what it is called: an .sfz, an MPC keygroup
    // program (.xpm, the MPC 3's .xty), a Logic EXS24 instrument (.exs), one
    // pad of an MPC drum program or project, "<file>#<pad>", an .sf2's preset
    // "<file>.sf2#<preset>" or a DecentSampler preset (.dspreset; roadmap P3).
    // The pad's or preset's '#' comes after the file's extension: a '#' in a
    // file's own name ("Strings C#.sfz", "Lead F#.dspreset") is part of it.
    bool instrument() const {
        // Whether the three letters before `end` are the extension `want` (lower case).
        auto kind = [](const char* begin, const char* end, const char* want) {
            if (end - begin < 4 || end[-4] != '.') return false;
            for (int k = 0; k < 3; ++k) if (char(end[k - 3] | 32) != want[k]) return false;
            return true;
        };
        const char* end = path + std::strlen(path);
        if (const char* mark = std::strrchr(path, '#'); mark && mark + 1 < end)
            if (kind(path, mark, "xpm") || kind(path, mark, "xty") || kind(path, mark, "xpj") || kind(path, mark, "sf2")) return true;
        if (dspreset()) return true;
        return kind(path, end, "sfz") || kind(path, end, "xpm") || kind(path, end, "xty") || kind(path, end, "exs");
    }
    // A DecentSampler preset (roadmap P3).
    bool dspreset() const {
        const size_t n = std::strlen(path);
        static const char tail[] = ".dspreset";
        if (n < sizeof(tail) - 1) return false;
        for (size_t k = 0; k < sizeof(tail) - 1; ++k) if (char(path[n - (sizeof(tail) - 1) + k] | 32) != tail[k]) return false;
        return true;
    }
};

// A note of a pad, with the semitones it plays at from the pad's root over the key.
constexpr int pad_note(int key, int semitones) { return semitones ? key | (int(uint8_t(int8_t(semitones))) << 8) : key; }
// Roadmap P3: the same with 16 Levels' cutoff (0..100) above them, as the sampler's note_on takes it from a pattern
// (a cutoff + 1 in bits 16-22; -1 none). Note offs need only the key and semitones.
constexpr int pad_note(int key, int semitones, int cutoff) {
    return pad_note(key, semitones) | (cutoff >= 0 ? (cutoff > 100 ? 101 : cutoff + 1) << 16 : 0);
}
// Issue 24: and above those a slice of the pad's sample's chop it plays (a slice + 1 in bits 23-30, so all 128 of a
// chop's slices; -1 none, the pad's own region), as the sampler takes a slice hit (Event::slice). The shell's held
// notes carry it the same way. Bit 31 stays clear: a negative note has no slice.
constexpr int pad_note(int key, int semitones, int cutoff, int slice) {
    return pad_note(key, semitones, cutoff) | (slice >= 0 && slice < 255 ? (slice + 1) << 23 : 0);
}
// -1: none (a negative note is a pad's key and semitones kept in 16 bits, sign and all: no slice).
constexpr int note_slice(int note) { return note < 0 ? -1 : ((note >> 23) & 0xff) - 1; }

// The pad's label as the boards print it: A01 .. D16.
inline void pad_label(unsigned pad, char out[4]) {
    out[0] = char('A' + (pad / 16) % sampler_banks);
    out[1] = char('0' + (pad % 16 + 1) / 10);
    out[2] = char('0' + (pad % 16 + 1) % 10);
    out[3] = 0;
}

} // namespace rhythmer
