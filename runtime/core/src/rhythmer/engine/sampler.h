// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/dsp.h"
#include "rhythmer/engine/effects.h"
#include "rhythmer/engine/engine.h"
#include "rhythmer/engine/punch.h"
#include "rhythmer/engine/sample_data.h"
#include "rhythmer/engine/stream_pager.h"
#include "rhythmer/engine/voice_tap.h"
#include "rhythmer/model/chop_slices.h"
#include "rhythmer/model/pad_params.h"
#include "rhythmer/model/sampler.h"
#include <array>
#include "rhythmer/engine/break_manipulator.h"
#include <memory>

// SAMPLER: a kit of 128 pads, each a region of a sample (up to four, its
// layers, picked by velocity, in turn or at random), written for
// sample-base after the rhythmer-ios sampler's behaviour and the MPC
// Sample's Sample Mode: gate (Note On) or one shot, loop and reverse, mute
// groups (pads in a group cut each other), mono or poly, a linked pad,
// offset, velocity sensitivity, its own pitch, level, pan, envelope, filter
// and filter envelope, warp, and one filter on the whole kit. Notes 36..163
// play pads 1..128 (bank A first). A muted pad is not heard. Stereo. Audio thread only; the sample
// table belongs to the engine host. A pad may hold a multisampled instrument
// (engine/multisample.h): its note and velocity pick zones, read through the
// stream pager, everything else about the pad staying the pad's.
namespace rhythmer {

namespace sampler {
enum Param : int8_t {
    Tune, Filter, Resonance, Decay, Level,      // the kit
    FilterType, Follow,                         // Filter, Follow pages
    Cloud, Position, Size, Density, Scan, Spread, // unused: Cloud is each pad's now (PadSound::cloud); old files' move there on load
    FollowHow, PitchHow,                        // Follow: Beats / Tones / Tape; a pitch change moves speed or is stretched
    Count
};
}

class SamplerEngine final : public Engine {
public:
    // The short loop for plain runs (run_plain()); off, every frame takes the full one (the tests compare the two).
    static inline bool plain_runs = true;
    static inline thread_local unsigned plain_taken = 0; // runs it played, on this thread (for the tests)
    // Within a short run, the frames of a voice that only holds its sustain, without the tests that cannot change
    // (held_run() in sampler.cpp). Off: frame by frame as before, for the tests that compare the two.
    static inline bool held_runs = true;
    static inline thread_local unsigned held_taken = 0;
    static constexpr unsigned max_voices = 32;
    // Past max_voices a new voice takes the oldest's place, which fades out (a choke's quick fade) in one of
    // these spare voices instead of stopping dead (a click).
    static constexpr unsigned spare_voices = 8;
    using Table = std::array<const SampleData*, sample_slots + 1>; // the last: preview_slot

    SamplerEngine();
    BreakManipulator manipulator;
    void break_live_on(int key,int velocity);
    void configure_break(unsigned bars,unsigned bank){manipulator.configure(bars,bank);}
    void set_samples(const Table* table) { samples = table; }
    // Issue 24: each sample slot's chop (the engine host's), which a slice hit plays a slice of (model/slice_hits.h).
    using Chops = std::array<const Slices*, sample_slots>;
    void set_chops(const Chops* table) { chops = table; }
    void set_pager(StreamPager* streams) { pager = streams; } // nullptr: instruments' frames are all safe to read (tests)
    // A pad's sound. What the pad plays now hears it too (at the next render, gliding there: refresh), while it
    // plays the same sample: its level, pan, tune, envelopes, filter, speed, play mode, region and loop.
    void set_pad(unsigned pad, const PadSound& sound);
    bool pad_used(unsigned pad) const { return pad < sampler_pads && pads[pad].used(); } // a sample on it
    // Round 3 "instrument C": the track's zone trims (model/sampler.h), as a whole or one; a zone of pad `pad`
    // at UINT32_MAX lets every trim of the pad go. A zone sounding hears its trim at once.
    void set_trims(const ZoneTrims& all) { trims = all; retune_all = true; }
    void set_trim(unsigned pad, uint32_t zone, unsigned slot, int tune, int level) {
        if (zone == UINT32_MAX) clear_trims(trims, pad);
        else rhythmer::set_trim(trims, pad, zone, slot, tune, level);
        mark(pad);
    }
    // Motion on a pad's own values (model/pad_params.h): the value plays
    // until the pad's lane moves again or restore_pads() gives every pad its
    // own values back (the pattern's start, Stop). What the pad plays hears it.
    void move_pad(unsigned pad, PadParam which, int value) {
        if (pad >= sampler_pads) return;
        set_pad_param(pads[pad], which, value);
        moved = true;
        mark(pad);
    }
    void restore_pads() { if (moved) { pads = base; moved = false; retune_all = true; } }
    // One of a pad's own values back to the kit's (a Motion lock ending, roadmap P1).
    void restore_pad(unsigned pad, PadParam which) {
        if (pad < sampler_pads) { set_pad_param(pads[pad], which, pad_param_value(base[pad], which)); mark(pad); }
    }
    // Mute: a muted pad does not start and what it plays fades out now (`fade` false: plays on, issue 27's Notes).
    void set_pad_mute(unsigned pad, bool on, bool fade = true);
    void set_pad_mutes(const std::array<uint64_t, (sampler_pads + 63) / 64>& bits, bool fade = true);
    bool pad_muted(unsigned pad) const { return pad < sampler_pads && ((muted[pad / 64] >> (pad % 64)) & 1); }
    // A region of a sample on no pad, for Chop's audition (plays one shot). With kit pad `pad` holding the sample, at
    // that pad's speed and pitch (Warp, Semi, Fine) as its slice's pad plays it; -1 as the file is.
    void audition(uint16_t sample, uint32_t start, uint32_t end, int velocity, int pad = -1, bool reverse = false, uint16_t own_tempo = 0);
    // A sample is about to change: whatever plays it stops now.
    void sample_gone(uint16_t sample);
    // A kit pad shifted by `semitones`, its filter at `cutoff` (0..100; -1 its
    // own): 16 Levels; or slice `slice` of its sample's chop (-1 its own region): Chop under Seq Record (issue 24).
    // note_off for the pad's note releases it.
    void play_pad(unsigned pad, int velocity, int semitones, int cutoff = -1, int slice = -1);
    // The next hit's own sound, a sequenced note's (step SOUND and PLAY): `sample` a slot played through the pad in
    // place of its own (-1 its own), `play` StepPlay Forward or Reverse (else the pad's way), `pick` the draw a SLICE
    // RND note takes its slice by. Taken by that hit alone (note_on just after).
    void step_hit(int sample, unsigned play, unsigned pick) { next_sample = sample; next_play = uint8_t(play); next_pick = uint16_t(pick); }
    // Note Repeat holds a kit pad: on a pad of Cycle hold or Random hold, every repeat plays the layer the
    // first one picked, until the hold ends (model/sampler.h LayerPlay).
    void hold_layer(unsigned pad, bool on) { if (pad < sampler_pads) held_layer[pad] = int8_t(on ? -2 : -1); }
    void release_layers() { held_layer.fill(-1); }

    // A KEYGROUP track's sampler (model/project.h): note k plays pad 1 at k - 60 semitones from its root, as a kit's
    // instrument pad plays key 60 + its pitch + those (zones_on()); a kit's notes are pads (pad_note()). A single
    // sample with a note of its own plays from that note instead: k - set_keyed_root()'s key (model/scales.h keyed_root()).
    void set_keyed(bool on) { keyed = on; }
    // A change lets go of what is held: its note offs would no longer find it.
    void set_keyed_root(int key) {
        key = std::clamp(key, 0, 127);
        if (key == keyed_root) return;
        if (keyed) all_notes_off();
        keyed_root = key;
    }
    bool is_keyed() const { return keyed; }
    void note_on(int key, int velocity) override;
    void set_accent(float gain) override { accent = gain; }
    void note_off(int key) override;
    // A Slide (`glide`) or Tie step on a KEYGROUP track: the held note goes on at `to` without a new attack, its pitch
    // sliding there over the program's portamento (else slide_seconds) or, tied, at once.
    static constexpr float slide_seconds = 0.06f;
    void slide(int from, int to, int velocity, bool glide) override;
    void all_notes_off() override;
    // A keyboard's pitch wheel: every voice of the kit bent up to `bend_range` semitones either way, its speed with it
    // (a stretched voice its pitch only), gliding there as any change does. New hits start bent.
    static constexpr float bend_range = 2.0f;
    void set_bend(float amount) override;
    // The sustain pedal (CC 64 at 64 or more): a note let go while it is down goes on until it comes up.
    void control(int cc, int value) override;
    void silence() override;
    void set_param(int id, int value) override;
    void render(float* out, unsigned frames) override; // mono fold, for callers that want one channel
    bool relieve(bool steal) override; // the overload guard: released pads choked, with `steal` the oldest voice too
    bool stereo() const override { return true; }
    void render_stereo(float* left, float* right, unsigned frames) override;
    bool sounding() const override;
    // What the voices show the screen (engine/voice_tap.h): the newest
    // TrackTaps::voices of them, then the cloud's grains. Audio thread, after render.
    void taps(TrackTaps& out) const;
    // The project's tempo, for Follow (tenths of a BPM).
    void set_tempo(unsigned tenths) { if (tenths != tempo) { tempo = tenths; tempo_moved = true; manipulator.tempo(tenths); } }
    // PadFX: each pad's own chain (the host's). A pad with a chain, a send of
    // its own or a pad stem plays through a lane of its own, up to `lanes` pads
    // at once; a chain's lane stays with its pad after the pad falls silent, for tails.
    using PadEffects = std::array<std::array<Effect, pad_slots>, sampler_pads>;
    void set_pad_effects(PadEffects* effects) { pad_effects = effects; }
    static constexpr unsigned lanes = 32;
    // Pad sends (PadSound::sends): a pad's lane, after its chain, also plays into bus `bus`'s l / r at its send into
    // it, eased, added to what is there. The host hands this sub-block's place in its own buffers before each render,
    // as for Knob FX; nullptr: none (no pad sends into that bus).
    static constexpr unsigned send_buses = 4;
    void set_send_out(unsigned bus, float* l, float* r) { if (bus < send_buses) { send_l[bus] = l; send_r[bus] = r; } }
    unsigned sends_fed() const { return send_used; } // a bit a bus: the last render put something there
    // Pads sent to a submix (PadSound::out): a pad's voices, after its own chain and punch (a cloud's grains as they
    // are), play into submix n's l / r instead of the kit, added to what is there. The host hands this sub-block's
    // place in its own buffers before each render, as for the sends; nullptr: they play in the kit.
    static constexpr unsigned sub_outs = 8;
    void set_sub_out(unsigned n, float* l, float* r) { if (n < sub_outs) { sub_l[n] = l; sub_r[n] = r; } }
    unsigned subs_fed() const { return sub_used; } // a bit a submix: the last render put something there
    uint8_t routes() const { return sub_routes; } // a bit a submix a pad with a sound is sent to
    // Knob FX (parity phase 7): the voices of these pads (a bit a pad) play
    // into the host's l / r instead, after their own chain and punch, added to
    // what is there, for the host to put through Knob FX. No bits: none. Like
    // the Sidechain's key pad, the host hands this sub-block's place in its own
    // buffers before each render; nullptr: none.
    void set_knob_pads(uint64_t low, uint64_t high) { knob_mask[0] = low; knob_mask[1] = high; }
    void set_knob_out(float* l, float* r) { knob_l = l; knob_r = r; }
    bool knob_fed() const { return knob_used; } // the last render put something there
    // Sidechain: this kit pad's voices (after its own chain and punch) play into l / r instead of the kit,
    // added to what is there; the host hands this sub-block's place in its own buffers before each render
    // (a render can be part of the host's block). -1: none.
    void set_key_pad(int pad, float* l, float* r) { key_pad = l && r ? pad : -1; key_l = l; key_r = r; }
    // Pad stems (engine/bounce.h StemRing): a pad whose entry in `rows` is a pad stem's number n plays into that
    // stem's row too, after its own chain and punch (a cloud's grains as they are), added to what is there: left at
    // base + 2n * stride + at, right a stride after. The host hands this sub-block's place before each render, as
    // for the sends; nullptr: none. Its own filter, chain and strip are the kit track's, after; the stems have none.
    void set_stem_out(const int16_t* rows, float* base, unsigned stride, unsigned at) {
        stem_rows = rows && base ? rows : nullptr; stem_base = base; stem_stride = stride; stem_at = at;
    }
    // A song's render starts (EngineHost, BounceArm): the random picks (layers, clouds) and Cycle's turns start
    // again, so each render of the song plays the same hits.
    void reseed() { seed = 99; layer_turn.fill(0); }

private:
    struct Voice {
        int pad = -1; // -1 free; sampler_pads: the audition voice's region
        int trigger = -1; // the pad whose hit started it: its own, or one linked to it (its note off releases it)
        const SampleData* sample = nullptr;
        const float* l = nullptr;
        const float* r = nullptr;
        uint16_t slot = no_sample;
        double position = 0, step = 1;
        uint32_t start = 0, end = 0;
        bool reverse = false, held = false, gate = false, choked = false, cloud = false;
        double next_grain = 0, scan = 0; // Cloud: samples to the next grain; how far the place has moved
        double life = 0; // Cloud, a one shot: output frames left of the time the pad would play for (0: until let go or hit again)
        // Stretch (Follow by Beats or Tones, or a pitch kept to length): `place`
        // moves through the sample at `advance`; taps read from where they
        // began at `step` (the pitch), each faded over `window`, a new one every `hop`.
        bool stretch = false, beats = false;
        double place = 0, advance = 1;
        // Beats uses taps 0 and 1; Tones up to four (two fading out at a hit while two start on it).
        std::array<double, 4> tap {};
        std::array<uint32_t, 4> tap_age {};
        std::array<bool, 4> tap_on {};
        std::array<double, 4> tap_from {};             // Tones: where each tap began
        std::array<float, 4> tap_gain {1, 1, 1, 1};    // Tones: 1, or falling to 0 after a hit cut it
        // Tones' hits: the next hit ahead of `place` (in its own lap on a kit pad's loop), found by reading
        // ahead to `hit_scan`, and the last one cut on. Taps do not read across a hit ahead of them; when the place
        // reaches it, those playing fade out over a short edge and a tap starts on it at full.
        bool hits = false, hit_ahead = false;
        double hit_at = 0, hit_scan = 0, hit_last = 0;
        float hit_before = 0;
        // Tones: the next tap's start, worked out ahead at `prepare_at` frames into the hop (a voice's own point in it,
        // so a chord's voices do not all search in the same block).
        bool prepared = false;
        uint32_t prepare_at = 0;
        double next_start = 0;
        uint32_t window = 1, hop = 1, since_hop = 0;
        unsigned next_tap = 0;
        uint8_t group = 0;
        float gain = 0, left = 0.7071f, right = 0.7071f, fade = 1;
        uint32_t age = 0;
        float rate = 44100; // Hz: its frames' (its sample's own, or its zone part's)
        double scale = 1;   // its frames a 44.1 kHz frame: `rate` over 44.1 kHz
        uint32_t sounded = 0;   // output frames it has played (Offset's wait not counted), for its tap
        bool wrapped = false;   // went round its loop in this block
        dsp::Envelope env;
        dsp::Envelope::Shape shape;
        // Loop: after the end (or, reversed, the loop point) it goes on from the loop point (or the end).
        bool loop = false;
        uint32_t loop_at = 0;
        // Roadmap P3, a kit pad's loop seam crossfaded over `xfade` frames of what it reads: 1 with what lies before
        // the loop point (reversed: past the end), at the seam's near side; 2 after the turn, with what follows the
        // end (reversed: before the loop point); 3, with neither in the file, a dip either side. `turned` once it has
        // gone round; `total` the frames it may read.
        uint8_t xfade_kind = 0;
        bool turned = false;
        uint32_t xfade = 0, total = 0;
        // A one shot's region edges inside the file (a chop's slices, a trimmed
        // pad): faded over slice_edge so a cut mid-wave does not click.
        bool edge_in = false, edge_out = false;
        // A zone of an instrument: its note, what cuts it, its loop's ends, and its stream.
        bool zone = false, loop_held = false, capped = false, starved = false, played = false;
        int key = 0, semis = 0, stream = -1;
        uint32_t off_by = 0, loop_hi = 0, head_end = 0;
        float duck = 1, duck_to = 1;        // a gap's short fades
        uint32_t wait = 0;                  // Offset: frames before it starts
        float tail = 0, filter_tail = 0;    // Decay from the end: frames of output it fades (and the filter closes) over
        // The pad's filter: two state-variable stages a side, and its envelope.
        PadFilter filter = PadFilter::Off;
        float cutoff = 0, depth = 0;        // octaves above 20 Hz; how far the envelope opens it
        float k1 = 1.4f, k2 = 1.4f, fg = 0.1f;
        float fs[2][2][2] {};               // [stage][side][s1, s2]
        unsigned since_tune = 0;
        dsp::Envelope filter_env;
        dsp::Envelope::Shape filter_shape;
        // What its pad and the kit ask of it now (aim): a turned knob, Motion, a modulator or the tempo moves these
        // while it plays, and the values above glide there over `gliding` more frames (glide), so the change is
        // heard at once, without a step. What it was started with stays: how hard, the note, 16 Levels' cutoff,
        // the sequencer's accent, its layer and zone, a release sample's fall.
        struct Aim {
            double step = 1, advance = 1;
            float gain = 0, left = 0.7071f, right = 0.7071f;
            float cutoff = 0, depth = 0, k1 = 1.4f, k2 = 1.4f, tail = 0, filter_tail = 0;
            bool beats = false; // stretched, it would play Beats' whole segments (else Tones' windows)
        } aim;
        uint32_t gliding = 0;
        uint8_t velocity = 0;
        int8_t layer = -1;                  // the pad's layer it plays (layers_on), -1 the pad itself
        int8_t slice = -1;                  // issue 24: the slice of its sample's chop it plays (a slice hit), -1 the pad's region
        uint8_t play = 0;                   // step PLAY: StepPlay Forward or Reverse it keeps over the pad's Reverse (0 the pad's)
        int16_t levels_cutoff = -1;         // 16 Levels' cutoff it was hit with, -1 the pad's own
        float accent = 1, fall = 1;         // the sequencer's accent; a release sample's rt_decay
        const SampleGrowth* grow = nullptr; // its sample is still being read: it reads only what landed (admit_grown)
        bool follows = false;               // its speed comes from the tempo (Warp's Seq, Follow)
        Multisample* multi = nullptr;       // a zone's instrument, and the zone's place in it
        uint32_t zone_index = 0;
        // Portamento (a mono instrument's glide, a Slide step): the pitch sits `porta` semitones from its key and
        // moves `porta_rate` of them a frame back to it.
        float porta = 0, porta_rate = 0;
    };
    // Each pad whose sound changed since the last render (a bit a pad); `retune_all` for the kit's values.
    std::array<uint64_t, (sampler_pads + 63) / 64> retune {};
    bool retune_all = false, tempo_moved = false;
    void mark(unsigned pad) { if (pad < sampler_pads) retune[pad / 64] |= uint64_t(1) << (pad % 64); }
    bool marked(int pad) const { return retune_all || ((retune[unsigned(pad) / 64] >> (unsigned(pad) % 64)) & 1); }
    // The glide: the share of the way left it goes a frame, and how many frames it takes, at the device's rate now.
    float glide_rate = 0, glide_step = 0;
    uint32_t glide_frames = 0;
    void glide_setup();
    void aim(Voice& v, const PadSound& sound, const SampleZone* zone, Multisample* multi, int key);
    void settle(Voice& v);
    void glide(Voice& v);
    void reach(Voice& v, const PadSound& sound);      // a kit pad's region as the pad has it now, round where it plays
    void loop_setup(Voice& v, const PadSound& sound); // its loop over its region, the edges' fades, the seam's crossfade
    void begin_stretch(Voice& v, bool live);
    void refresh(Voice& v);
    void retune_voices();
    void hit(int pad, int velocity, int semitones, int cutoff, int slice = -1);
    void start(Voice& v, const PadSound& sound, int pad, int velocity, int semitones = 0, int cutoff = -1, int trigger = -1,
               const SampleZone* zone = nullptr, Multisample* multi = nullptr, int key = 60);
    void play(const PadSound& sound, int pad, int velocity, int semitones, int cutoff, int trigger);
    void zones_on(Multisample& multi, const PadSound& sound, int pad, int velocity, int semitones, int cutoff, int trigger);
    void layers_on(const PadSound& sound, int pad, int velocity, int semitones, int cutoff, int trigger);
    void release_zones(int pad, int semitones); // roadmap P3: an instrument's release samples for the key let go
    // A mono instrument's held note on `pad` carried to `semitones` (a new key), its pitch sliding there over `seconds`
    // (0: at once). False when no note of the pad is held.
    bool carry(int pad, int semitones, float seconds);
    void porta_from(Voice& v, int from_key, float seconds); // a new voice's pitch starting at `from_key`, sliding to its own
    std::array<int8_t, sampler_pads> mono_key = [] { std::array<int8_t, sampler_pads> a {}; a.fill(-1); return a; }(); // a mono instrument pad's last key
    // A mono instrument's keys still held, oldest first (the last sixteen): when the key that sounds is let go the note
    // goes back to the newest of them, as a mono synth's does (carried there when its program is tied, else started
    // again).
    struct HeldKey { int16_t pad = -1; int8_t semis = 0; uint8_t velocity = 0; };
    std::array<HeldKey, 16> mono_held {};
    unsigned mono_held_count = 0;
    void hold_key(int pad, int semitones, int velocity); // pressed: the newest
    void drop_key(int pad, int semitones);               // let go
    const HeldKey* newest_held(int pad) const;
    Multisample* multi_of(const PadSound& sound) const;
    uint32_t model_frames(uint16_t slot) const; // sample slot `slot`'s length as the project counts it (44.1 kHz); 0 unknown
    void end_stream(Voice& v);
    void stream_from(Voice& v);   // a long sample's voice moved: its stream again from there
    void follow_stream(Voice& v); // and after its block: where it reads, or round its loop
    void came_round(Voice& v);          // a zone's loop went back to its start
    bool admit(Voice& v, unsigned frames); // whether its next frames are safe to read
    void pass(Voice& v, unsigned frames);  // a block with nothing safe to read: its time goes on
    bool admit_grown(Voice& v, unsigned frames); // the same for a sample still being read (SampleGrowth)
    void play_zone(Voice& v, float* to_l, float* to_r, unsigned from, unsigned frames);
    float frames_left(const Voice& v, double at, double speed) const; // output frames to the end, for the fades
    static float edge_gain(const Voice& v, double at);                // the region edges' fades, 0..1
    static void seam(const Voice& v, const float* l, const float* r, double at, float& out_l, float& out_r); // a loop's crossfade
    void voice_out(Voice& v, float l, float r, float a, float left_frames, float* to_l, float* to_r, unsigned f);
    // A kit voice's next `frames` without anything to check frame by frame (no glide, duck, choke, seam, and clear of
    // its edge fades and tail): run_plain() plays them in a shorter loop, sample for sample as the full one would.
    bool plain_run(const Voice& v, unsigned frames) const;
    void run_plain(Voice& v, const float* l, const float* r, float* to_l, float* to_r, unsigned from, unsigned frames, double span);
    // The same for a zone voice (play_zone()).
    bool zone_plain(const Voice& v, unsigned frames) const;
    const float* tones_table(uint32_t window);              // stretch(): Tones' window weights, worked out once
    std::array<float, 5760> tones_weights {};               // 60 ms at 96 kHz
    uint32_t tones_window = 0;                              // the window they are for (0 none yet)
    void zone_run_plain(Voice& v, const float* l, const float* r, float* to_l, float* to_r, unsigned from, unsigned frames);
    Voice& free_voice();

    struct Grain {
        const SampleData* sample = nullptr;
        double position = 0, step = 1;
        uint32_t age = 0, length = 0;
        float gain = 0, left = 0.7071f, right = 0.7071f;
        const float* l = nullptr;
        const float* r = nullptr;
        uint32_t low = 0, top = 1;      // the frames it may read, [low, top): its pad's region, or what an instrument holds
        uint32_t serial = 0;            // for the screen (taps): which grain, where it began, whose
        double from = 0;
        int16_t pad = -1;
        uint16_t slot = 0xffff;
        double scale = 1; // its voice's (Voice::scale)
    };
    void spawn(Voice& v);
    std::array<Grain, 48> grains {};
    // Some grain may be playing: spawn() starts one, the render that sees none left clears it. While it is clear nothing
    // reads the grains (3 kB a kit each block, waited for on a cold cache).
    bool grains_live = false;
    uint32_t grain_serial = 0;
    unsigned tempo = 1200;
    uint16_t audition_tempo=0;
    uint32_t seed = 99;
    // step_hit(): the next hit's sample, way and slice draw; `starting_play` the way the voices start() takes.
    int next_sample = -1;
    uint8_t next_play = 0, starting_play = 0;
    uint16_t next_pick = 0;
    // A choke's fade and an instrument zone's duck move this far a frame: 64 44.1 kHz frames (1.5 ms) at any rate.
    float edge_step = 1.0f / 64;
    const Table* samples = nullptr;
    const Chops* chops = nullptr;
    StreamPager* pager = nullptr;
    std::array<PadSound, sampler_pads> pads {}, base {}; // as they play (motion), and as the kit holds them
    ZoneTrims trims {};
    std::array<uint8_t, sampler_pads> layer_turn {};    // Cycle: the layer each pad plays next
    std::array<int8_t, sampler_pads> held_layer = [] { std::array<int8_t, sampler_pads> a {}; a.fill(-1); return a; }(); // -1 not held, -2 held (none picked yet), else the layer kept
    // Roadmap P3, an instrument's release samples: each key's last press, how hard and when (frames played).
    std::array<uint8_t, 128> key_velocity {};
    std::array<uint64_t, 128> key_since {};
    uint64_t played_frames = 0;
    bool moved = false;
    std::array<uint64_t, (sampler_pads + 63) / 64> muted {};
    std::array<Voice, max_voices + spare_voices> voices {};
    std::array<int, sampler::Count> p {};
    uint32_t clock = 0;
    float accent = 1; // the sequencer's, for the voices started next (set_accent)
    float bend = 0;   // semitones, set_bend
    bool pedal = false;
    bool keyed = false; // set_keyed()
    int keyed_root = 60; // set_keyed_root()
    int keyed_note(int key) const;
    void release_key(int key); // note_off() in the kit's encoding
    std::array<int32_t, max_voices> pedal_offs {}; // the note offs the sustain pedal holds back, in order
    unsigned pedal_count = 0;
    float g = 1, k = 2, s1[2] {}, s2[2] {};
    bool filtered = false;
    // A pad's chain plays on in its lane after the pad falls silent, until its own tail has been quiet for
    // lane_quiet_seconds (longer than any echo's time, so a late repeat is not lost) or lane_ring_seconds have gone.
    static constexpr float lane_quiet_seconds = 1.5f, lane_ring_seconds = 30.0f, lane_floor = 1e-5f;
    struct Lane { int pad = -1; uint32_t quiet = 0, ringing = 0; bool fed = false; float l[128] {}, r[128] {}; };
    PadEffects* pad_effects = nullptr;
    uint64_t knob_mask[2] {};
    bool knob_used = false;
    float* knob_l = nullptr;
    float* knob_r = nullptr;
    int key_pad = -1;
    float* key_l = nullptr;
    float* key_r = nullptr;
    float* send_l[send_buses] {};
    float* send_r[send_buses] {};
    unsigned send_used = 0;
    float* sub_l[sub_outs] {};
    float* sub_r[sub_outs] {};
    unsigned sub_used = 0;
    std::array<unsigned, sub_outs> sub_members {}; // only set_pad changes a pad's sample or output
    uint8_t sub_routes = 0;
    int routed(int pad) const { // the submix the pad plays into, or -1: the kit
        if (pad < 0 || pad >= int(sampler_pads)) return -1;
        const unsigned out = pads[size_t(pad)].out;
        return out && out <= sub_outs && sub_l[out - 1] && sub_r[out - 1] ? int(out - 1) : -1;
    }
    const int16_t* stem_rows = nullptr; // set_stem_out()'s
    float* stem_base = nullptr;
    unsigned stem_stride = 0, stem_at = 0;
    float* stem_out(int pad, unsigned side) const { // the pad's pad stem row, or nullptr
        if (!stem_rows || pad < 0 || pad >= int(sampler_pads) || stem_rows[pad] < 0) return nullptr;
        return stem_base + size_t(2 * stem_rows[pad] + side) * stem_stride + stem_at;
    }
    std::array<std::array<float, send_buses>, sampler_pads> send_now {}; // each pad's sends as heard, easing to its own
    bool knobbed(int pad) const { return pad >= 0 && pad < 128 && ((knob_mask[pad / 64] >> (pad % 64)) & 1); }
    bool chained(int pad) const { return pad_effects && (*pad_effects)[size_t(pad)][0].kind() != EffectKind::None; }
    bool sends_heard(int pad) const; // its sends, or what is left of them easing away
    // On the heap: thirty-two lanes are 32 kB a kit, and the host keeps sixteen kits (made with the engine, not on the audio thread).
    std::unique_ptr<std::array<Lane, lanes>> lane = std::make_unique<std::array<Lane, lanes>>();
    Lane* lane_for(int pad); // the pad's lane, a free one, or nullptr
    void stretch(Voice& v, const float* l, const float* r, float* to_l, float* to_r, unsigned from, unsigned frames);
    // Tones' next tap (WSOLA): where near `natural` it reads most like the playing tap over their overlap.
    // `from` and `age`: where the playing tap will read, and its age, when the new one starts.
    double align(const Voice& v, const float* l, const float* r, double from, uint32_t age, double natural);
    void scan_hits(Voice& v, const float* l, const float* r);   // Tones: read ahead for the next hit
    void cut_on_hit(Voice& v);                                    // Tones: the place reached the hit ahead
    static float hit_energy(const Voice& v, const float* l, const float* r, long at); // 64 frames from `at`, its way
    unsigned open_tap(Voice& v, double at, uint32_t age);
    static int playing_tap(const Voice& v);
    static double loop_place(const Voice& v, double at);           // Tones on a kit pad's loop: into the loop's frames
    static double same_lap(const Voice& v, double at);             // Tones on a kit pad's loop: onto the place's lap
    void band_read(const Voice& v, const float* l, const float* r, double at, bool pad_loop, float& out_l, float& out_r);
    // Tones' band-limited reads, a table a voice for its speed: 33 places between two frames, each a row of weights
    // summed to one, over `count` frames from `half` - 1 before the frame read. Worked out again when the speed moves
    // more than 0.5 %. On the heap: 10 kB a voice.
    struct BandRows {
        double step = 0;
        int half = 0, count = 0;
        std::array<float, 33 * 76> weights {};
    };
    std::unique_ptr<std::array<BandRows, max_voices + spare_voices>> band_rows = std::make_unique<std::array<BandRows, max_voices + spare_voices>>();         // Tones: a free tap (else the oldest) from `at`
    static constexpr double align_seconds = 0.012; // how far either side of its place a tap may start
    struct AlignScratch {
        std::array<float, 384> ref {};      // what the playing tap reads over the overlap, a point every 8 frames
        std::array<float, 8192> span {};    // the mono sound around where it starts, on the same points' grid
    };
    std::unique_ptr<AlignScratch> aligning = std::make_unique<AlignScratch>(); // on the heap, as `lane`
};

} // namespace rhythmer
