// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/strip.h"
#include <array>
#include <cstdint>

// The mixer's strip as the audio thread plays it (model/strip.h; rhythmer-sample docs/MIXER_PLAN.md): trim, nine peak
// bands an octave apart, a compressor and width, in that order, on one channel's stereo sound in place. A strip as it
// comes does nothing and costs nothing. Every change glides, so a turned knob or an on/off never clicks: gains move
// at most 120 dB a second, the compressor's blend and width a full swing in 10 ms. No allocation, no locks.
namespace rhythmer {

class ChannelStrip {
public:
    // The values to glide to (a command's). Audio thread.
    void set(const Strip& strip);
    // One block of the channel, in place. `key_l`/`key_r`: what the compressor hears when it is keyed (Strip::comp_key,
    // found by the host), else nullptr: the channel itself, after the bands.
    void process(float* l, float* r, unsigned frames, const float* key_l = nullptr, const float* key_r = nullptr);
    // The compressor's gain reduction in the last block, at its deepest, in dB (0: none), for the meters.
    float reduction_db() const { return reduction_; }
    // Nothing to do: every part as it comes and settled. process() returns at once then.
    bool idle() const { return idle_; }
    // The running state back to silence, every value at its target at once (a project just loaded).
    void reset();
    const Strip& values() const { return strip_; }

    static constexpr float band_q = 1.4142f;     // one octave wide
    static constexpr float glide_db = 120;        // dB a second, at most
    static constexpr float glide_swing = 0.01f;   // seconds for the blend or width's full swing
    static constexpr unsigned comp_step = 16;     // frames between the compressor's gain workings; eased between
    static constexpr float band_rest = 0.05f;     // seconds a band stays in the chain at 0 dB, its ringing gone, before it leaves

private:
    struct Band {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // this block's coefficients
        float z[2][2] {};                              // each side's state
        float gain = 0, target = 0;                    // dB: where the glide is, where it goes
        bool running = false;                          // in the chain: not at 0 dB, or not for long enough yet
        unsigned quiet = 0;                            // frames it has been at 0 dB
    };
    void work_out(Band& band, unsigned n);
    void speeds();
    void check_idle();

    Strip strip_ {};
    float rate_ = 0;                             // the sample rate the coefficients were worked out at
    std::array<Band, eq_bands> bands_ {};
    float trim_db_ = 0, trim_target_ = 0;
    float width_ = 1, width_target_ = 1;         // 1 as it is
    float blend_ = 0, blend_target_ = 0;         // the compressor's share against the dry: 0 .. its mix
    float makeup_db_ = 0, makeup_target_ = 0;
    float threshold_db_ = 0, slope_ = 0;         // slope: 1 - 1 / ratio
    float attack_ = 0, release_ = 0;            // the envelope's follow a frame (Auto's release moves with the reduction)
    float auto_release_[13] {};                  // Auto's release for 0 .. 12 dB of reduction
    float env_ = 0;                              // the level heard, linear
    float gain_ = 1;                             // the compressor's gain now, linear
    float reduction_ = 0;
    bool idle_ = true;
};

} // namespace rhythmer
