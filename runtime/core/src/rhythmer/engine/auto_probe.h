// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// The auto sampler's check (shell/print_engine.cpp probe_track()): what a
// plug-in is, heard from a few notes, so AUTO SAMPLE needs no choice and reads
// nothing from the plug-in. Each finding is worked out here from the audio
// alone (mono, at the device's rate), so the tests can hand it made-up sounds.
namespace rhythmer {

// A fundamental by autocorrelation over x[from, from + n): 30 Hz to 2 kHz; 0 when the sound has none clear enough
// (a noise, a drum, silence).
float probe_pitch(const std::vector<float>& x, size_t from, size_t n, float rate);
// The level of x[from, from + n) in dB (RMS), -120 for silence.
float probe_db(const std::vector<float>& x, size_t from, size_t n);
// How bright: the RMS of the sound's slope against its RMS, as a frequency (Hz). A brighter sound reads higher.
float probe_brightness(const std::vector<float>& x, size_t from, size_t n, float rate);
// The level of one frequency and its octave in x[from, from + n), dB.
float probe_tone_db(const std::vector<float>& x, size_t from, size_t n, float hz, float rate);

// Two keys played legato (`x`: the second pressed at `onset` while the first, at `from_hz`, is held; it sounds `to_hz`
// on its own), and the first key held alone the same way (`alone`): mono when the first key's pitch is gone soon after
// the second, as it is not alone, and then the seconds its pitch took to slide to the second's (0: at once).
struct ProbeGlide {
    bool mono = false;
    float seconds = 0;
};
ProbeGlide probe_glide(const std::vector<float>& x, const std::vector<float>& alone, size_t onset, float from_hz, float to_hz, float rate);
// The second of two keys played apart (it starts at `onset`): whether it starts at the first key's pitch and slides
// (a portamento that is not legato).
bool probe_slides_apart(const std::vector<float>& x, size_t onset, float from_hz, float to_hz, float rate);
// Two keys played legato on a mono instrument: whether the second starts the sound again, as a press alone does, or
// the first's note carries on at the new pitch (a 303's slide, a synth's single trigger). `both`: the second pressed
// at `onset` while the first is held; `alone`: the first key held alone; `fresh`: the second key alone, from frame 0.
// A held note moves on from how it starts (it fades, its filter closes, it swells): over the 0.3 s after `onset`,
// 40 ms at a time in level and brightness, a note started again is back where the sound starts and one carried on is
// where the held note has got to. 1: it starts again; 0: it carries on; -1: not told, the held note being by then
// within 2 dB of how it starts (either way sounds the same).
int probe_retriggers(const std::vector<float>& both, const std::vector<float>& alone, const std::vector<float>& fresh, size_t onset, float rate);

// A key held alone for 1.2 s or more (`x`, sounding `hz`): how it sounds, to tell one sound from another along the
// keyboard: its level over its first 0.2 s, how much quieter it is a second in, and its brightness against its pitch,
// each in dB (a brightness doubled is 6). A key next to it of the same sound reads all but the same.
struct ProbeCharacter {
    float level = -120.0f, fade = 0, bright = 0;
};
ProbeCharacter probe_character(const std::vector<float>& x, float hz, float rate);
// How unlike two keys sound: their three differences added, dB.
float probe_apart(const ProbeCharacter& a, const ProbeCharacter& b);

// One key played at rising velocities (each with its level in dB): how far its level follows velocity, 0..1 as a
// pad's Velocity plays it (level 1 - amount x (1 - velocity / 127)): 0 for a sound as loud at every velocity, 1 for
// one half as loud at half the velocity (or steeper still). Under 0.2 it reads 0: that little is the sound's own
// difference from hit to hit.
float probe_velocity(const std::vector<uint8_t>& velocities, const std::vector<float>& db);

// A key held (x[from, to): the held part, past its attack): whether it sustains, so a loop there plays on for as long
// as the key is held: its level over the last quarter within `most` dB of its level over the first (a tremolo's
// swings averaged out), and not silent. A pluck or a 303 fading as it is held does not.
bool probe_sustains(const std::vector<float>& x, size_t from, size_t to, float rate, float most = 4.0f);

// A key held for all of `x`: the seconds from its start until it has fallen 45 dB under its loudest tenth of a
// second and stays there; x's whole length when it never does.
float probe_fade_seconds(const std::vector<float>& x, float rate);

// One key at rising velocities (`velocities`, each with its level in dB and brightness): the velocity a step up starts
// the accent at, where the sound jumps far more than between any other two; 0 for none.
uint8_t probe_accent(const std::vector<uint8_t>& velocities, const std::vector<float>& db, const std::vector<float>& bright);

// One key at rising velocities (as probe_accent's) as velocity layers: each layer is the hit at its top velocity,
// played softer within the layer by Velocity `amount` (0..1, as probe_velocity's). A hit is as far from its layer's
// sample as its level apart once Velocity has taken it down, plus its brightness apart at 6 dB an octave (half a dB
// a semitone, as probe_accent weighs them). `count` layers (1-8) placed so the hits are as near as they can be
// (least squares), or with `count` 0 as few as keep every hit within 1.5 dB, up to 8. An accent (its velocity, 0
// none) always starts a layer, unless there is one layer only. The top velocity of each layer, rising, the last 127.
std::vector<uint8_t> probe_layers(const std::vector<uint8_t>& velocities, const std::vector<float>& db, const std::vector<float>& bright,
                                  float amount, uint8_t accent, unsigned count);

// Keys played one at a time, each with the pitch heard (0 none): drums when most do not follow the keys as a
// pitched instrument does (an octave up twice the frequency).
bool probe_drums(const std::vector<uint8_t>& keys, const std::vector<float>& hz);

// The same key hit a few times: whether the hits differ (any two apart in level over 300 ms by 1.5 dB on average, or
// the brightest 8 % over the dullest), so each key wants takes of its own. Phase alone does not count.
bool probe_varies(const std::vector<std::vector<float>>& hits, float rate);

} // namespace rhythmer
