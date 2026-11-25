// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/engine/channel_strip.h"
#include "rhythmer/engine/dsp.h"
#include "rhythmer/engine/rbj.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace rhythmer {

namespace {
float db(float v) { return std::pow(10.0f, v / 20.0f); }
float follow(float seconds, float rate) { return std::exp(-1.0f / std::max(seconds * rate, 1.0f)); }
// `now` toward `target` by at most `step`.
float toward(float now, float target, float step) { return now < target ? std::min(target, now + step) : std::max(target, now - step); }
// Attack and release, seconds, for each speed; Auto's release goes from the first to the second as the reduction deepens.
constexpr float speed_attack[] {0.001f, 0.010f, 0.030f, 0.005f};
constexpr float speed_release[] {0.060f, 0.150f, 0.400f, 0.100f};
constexpr float auto_release_slow = 0.600f;
constexpr bool attack_faster() { // the envelope keeps the higher of its two paces (process())
    for (unsigned n = 0; n < unsigned(CompSpeed::Count); ++n)
        if (speed_attack[n] > speed_release[n]) return false;
    return speed_attack[unsigned(CompSpeed::Auto)] <= auto_release_slow;
}
static_assert(attack_faster());
constexpr float knee_db = 6;
constexpr unsigned auto_steps = 12; // Auto's release, worked out for 0 .. 12 dB of reduction, a dB apart

// log2 and 2^x for the compressor, a few times a block: the exponent from the float's bits, the rest by a polynomial.
// log2 within 1.5e-5 (under 0.0001 dB), for x > 0 and normal; 2^x within 3e-6 of it, for x down to -126.
float log2_of(float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    const float e = float(int(bits >> 23) - 127);
    bits = (bits & 0x007fffffu) | 0x3f800000u;
    float t;
    std::memcpy(&t, &bits, sizeof t);
    t -= 1;
    return e + 1.4390931e-05f + t * (1.4415921f + t * (-0.707253456f + t * (0.411561489f + t * (-0.189832449f + t * 0.0439286269f))));
}
float exp2_of(float x) {
    x = std::max(x, -126.0f);
    const float i = std::floor(x), t = x - i;
    const float p = 1.0000025f + t * (0.693006635f + t * (0.241427496f + t * (0.0520374291f + t * 0.0135206031f)));
    uint32_t bits;
    std::memcpy(&bits, &p, sizeof bits);
    bits += uint32_t(int32_t(i)) << 23;
    float out;
    std::memcpy(&out, &bits, sizeof out);
    return out;
}
constexpr float db_per_log2 = 6.02059991f;   // 20 log10(2)
constexpr float log2_per_db = 0.166096405f;  // log2(10) / 20

// One band's coefficients and both sides' state, for a block.
struct Live { float b0, b1, b2, a1, a2, zl1, zl2, zr1, zr2; };

#if defined(__ARM_NEON)
#define STRIP_INLINE __attribute__((always_inline)) inline

// The bands with NEON. Left and right go side by side in a 64-bit register. A peak's b1 is its a1 (rbj::peak works
// both out the same way), so a band is b0, a1, b2 and a2, and a frame through it is
//   y = b0 x + s1,  s1' = (s2 + a1 x) - a1 y,  s2' = b2 x - a2 y.
// Laid out for the A55, which runs instructions in order (measured on the MPC):
// - Each coefficient in a register of its own, in both lanes: a multiply by one lane of a register issues once a
//   cycle, by a whole register twice a cycle, and a load costs more than the multiplies it feeds. So the bands go in
//   passes of up to four over the block, each pass holding its coefficients for the whole block.
// - In a pass, each band a frame behind the one before it: at step i band k plays frame i - k, so the bands of one
//   step do not wait for each other.
// - A band leaves s1' where its s2 was and s2' where its s1 was; the next frame takes them that way round and puts
//   them back. Frames go in pairs and the states do not move between registers.
template <unsigned K> struct BandPass {
    float32x2_t b0[K], a1[K], b2[K], na2[K]; // the coefficients, each in both lanes (na2: -a2)
    float32x2_t s1[K], s2[K];                 // each band's state, left and right
    float32x2_t carry[K];                     // carry[k]: band k - 1's output at the last step, band k's input

    // Band k on x. Turned: its s1 is in s2[k] and its s2 in s1[k], as the frame before left them.
    template <unsigned k, bool Turned> STRIP_INLINE float32x2_t band(float32x2_t x) {
        float32x2_t& first = Turned ? s2[k] : s1[k];
        float32x2_t& second = Turned ? s1[k] : s2[k];
        const float32x2_t y = vfma_f32(first, x, b0[k]);
        first = vfma_f32(vmul_f32(x, b2[k]), y, na2[k]);
        second = vfms_f32(vfma_f32(second, x, a1[k]), y, a1[k]);
        return y;
    }
    // Bands lo .. hi at step i (all: every band), the last first so each reads the carry the step before left.
    // Lone: a step not in a pair, the states put back after it.
    template <unsigned k, bool Turned, bool Lone>
    STRIP_INLINE void step(float* l, float* r, unsigned i, unsigned lo, unsigned hi, bool all) {
        if (all || (k >= lo && k <= hi)) {
            const float32x2_t x = k == 0 ? vset_lane_f32(r[i], vdup_n_f32(l[i]), 1) : carry[k];
            const float32x2_t y = band<k, Turned>(x);
            if constexpr (Lone) std::swap(s1[k], s2[k]);
            if constexpr (k == K - 1) { vst1_lane_f32(l + i - k, y, 0); vst1_lane_f32(r + i - k, y, 1); }
            else carry[k + 1] = y;
        }
        if constexpr (k > 0) step<k - 1, Turned, Lone>(l, r, i, lo, hi, all);
    }
    STRIP_INLINE void run(Live* live, float* l, float* r, unsigned frames) {
        for (unsigned k = 0; k < K; ++k) {
            b0[k] = vdup_n_f32(live[k].b0); a1[k] = vdup_n_f32(live[k].a1);
            b2[k] = vdup_n_f32(live[k].b2); na2[k] = vdup_n_f32(-live[k].a2);
            s1[k] = vset_lane_f32(live[k].zr1, vdup_n_f32(live[k].zl1), 1);
            s2[k] = vset_lane_f32(live[k].zr2, vdup_n_f32(live[k].zl2), 1);
            carry[k] = vdup_n_f32(0); // set before it is read, but the compiler cannot see that
        }
        unsigned i = 0;
        for (; i < std::min(K - 1, frames); ++i) step<K - 1, false, true>(l, r, i, 0, i, false); // bands starting
        for (; i + 1 < frames; i += 2) {
            step<K - 1, false, false>(l, r, i, 0, K - 1, true);
            step<K - 1, true, false>(l, r, i + 1, 0, K - 1, true);
        }
        for (; i < frames; ++i) step<K - 1, false, true>(l, r, i, 0, K - 1, true);
        for (; i < frames + K - 1; ++i) step<K - 1, false, true>(l, r, i, i - frames + 1, std::min(K - 1, i), false); // finishing
        for (unsigned k = 0; k < K; ++k) {
            live[k].zl1 = vget_lane_f32(s1[k], 0); live[k].zr1 = vget_lane_f32(s1[k], 1);
            live[k].zl2 = vget_lane_f32(s2[k], 0); live[k].zr2 = vget_lane_f32(s2[k], 1);
        }
    }
};

// `N` bands in passes of up to four, as even as they go (nine: three passes of three).
template <unsigned N, unsigned Pass = 0> STRIP_INLINE void band_passes(Live* live, float* l, float* r, unsigned frames) {
    constexpr unsigned passes = (N + 3) / 4;
    if constexpr (Pass < passes) {
        constexpr unsigned from = N * Pass / passes, to = N * (Pass + 1) / passes;
        BandPass<to - from> pass;
        pass.run(live + from, l, r, frames);
        band_passes<N, Pass + 1>(live, l, r, frames);
    }
}

template <unsigned N> void run_bands(Live* live, float* l, float* r, unsigned frames) { band_passes<N>(live, l, r, frames); }
#else
// `N` bands, a frame at a time through all of them, both sides side by side. N fixed, so the compiler keeps the
// states in registers where it can and unrolls the bands.
template <unsigned N> void run_bands(Live* bands, float* l, float* r, unsigned frames) {
    Live s[N];
    for (unsigned k = 0; k < N; ++k) s[k] = bands[k];
    for (unsigned f = 0; f < frames; ++f) {
        float xl = l[f], xr = r[f];
#pragma GCC unroll 16
        for (unsigned k = 0; k < N; ++k) {
            const float yl = s[k].b0 * xl + s[k].zl1, yr = s[k].b0 * xr + s[k].zr1;
            s[k].zl1 = s[k].b1 * xl - s[k].a1 * yl + s[k].zl2; s[k].zr1 = s[k].b1 * xr - s[k].a1 * yr + s[k].zr2;
            s[k].zl2 = s[k].b2 * xl - s[k].a2 * yl;            s[k].zr2 = s[k].b2 * xr - s[k].a2 * yr;
            xl = yl; xr = yr;
        }
        l[f] = xl; r[f] = xr;
    }
    for (unsigned k = 0; k < N; ++k) bands[k] = s[k];
}
#endif
using BandRunner = void (*)(Live*, float*, float*, unsigned);
constexpr BandRunner band_runners[eq_bands + 1] {nullptr, run_bands<1>, run_bands<2>, run_bands<3>, run_bands<4>,
                                                  run_bands<5>, run_bands<6>, run_bands<7>, run_bands<8>, run_bands<9>};

// `l` and `r` times a gain that goes from + d (f + 1) at frame f.
void ramp(float* l, float* r, unsigned frames, float from, float d) {
    unsigned f = 0;
#if defined(__ARM_NEON)
    const float first[4] {1, 2, 3, 4};
    float32x4_t at = vld1q_f32(first);
    const float32x4_t four = vdupq_n_f32(4), from4 = vdupq_n_f32(from);
    for (; f + 4 <= frames; f += 4, at = vaddq_f32(at, four)) {
        const float32x4_t g = vfmaq_n_f32(from4, at, d);
        vst1q_f32(l + f, vmulq_f32(vld1q_f32(l + f), g));
        vst1q_f32(r + f, vmulq_f32(vld1q_f32(r + f), g));
    }
#endif
    for (; f < frames; ++f) { const float g = from + d * float(f + 1); l[f] *= g; r[f] *= g; }
}
} // namespace

void ChannelStrip::set(const Strip& strip) {
    // The speeds worked out again only for a new speed: a band's coefficients follow its gain every block it glides,
    // so a value set often (Motion) costs no sines and powers it does not need.
    if (strip.comp_speed != strip_.comp_speed) rate_ = 0;
    strip_ = strip;
    trim_target_ = strip.trim / 10.0f;
    for (unsigned n = 0; n < eq_bands; ++n) bands_[n].target = strip.eq_on ? strip.eq[n] / 10.0f : 0.0f;
    const bool on = strip.comp_on;
    blend_target_ = on && strip.comp_amount ? strip.comp_mix / 100.0f : 0.0f;
    makeup_target_ = on ? strip.comp_gain / 10.0f : 0.0f;
    threshold_db_ = -float(strip.comp_amount);
    slope_ = 1 - 10.0f / float(std::max<unsigned>(strip.comp_ratio, comp_ratio_min));
    width_target_ = strip.width / 100.0f;
    idle_ = false;
}

void ChannelStrip::reset() {
    if (rate_ != dsp::sample_rate()) speeds();
    trim_db_ = trim_target_; width_ = width_target_; blend_ = blend_target_; makeup_db_ = makeup_target_;
    for (unsigned n = 0; n < eq_bands; ++n) {
        auto& b = bands_[n];
        b.gain = b.target;
        b.z[0][0] = b.z[0][1] = b.z[1][0] = b.z[1][1] = 0;
        b.running = b.gain != 0; b.quiet = 0;
        work_out(b, n);
    }
    env_ = 0; gain_ = 1; reduction_ = 0;
    check_idle();
}

void ChannelStrip::speeds() {
    rate_ = dsp::sample_rate();
    const unsigned s = std::min<unsigned>(strip_.comp_speed, unsigned(CompSpeed::Count) - 1);
    attack_ = follow(speed_attack[s], rate_);
    release_ = follow(speed_release[s], rate_);
    constexpr float quick = speed_release[unsigned(CompSpeed::Auto)];
    for (unsigned n = 0; n <= auto_steps; ++n)
        auto_release_[n] = follow(quick + float(n) / float(auto_steps) * (auto_release_slow - quick), rate_);
    for (unsigned n = 0; n < eq_bands; ++n) work_out(bands_[n], n);
}

void ChannelStrip::work_out(Band& band, unsigned n) {
    float c[5];
    rbj::peak(c, float(eq_band_hz[n]) * std::min(1.0f, 0.45f * dsp::sample_rate() / float(eq_band_hz[n])), band.gain, band_q);
    band.b0 = c[0]; band.b1 = c[1]; band.b2 = c[2]; band.a1 = c[3]; band.a2 = c[4];
}

void ChannelStrip::check_idle() {
    bool bands = false;
    for (const auto& b : bands_) bands |= b.running;
    idle_ = !bands && trim_db_ == 0 && trim_target_ == 0 && width_ == 1 && width_target_ == 1 && blend_ == 0 && blend_target_ == 0
         && makeup_db_ == 0 && makeup_target_ == 0;
    if (idle_) { env_ = 0; gain_ = 1; reduction_ = 0; }
}

void ChannelStrip::process(float* l, float* r, unsigned frames, const float* key_l, const float* key_r) {
    if (idle_ || frames == 0) return;
    if (rate_ != dsp::sample_rate()) speeds();
    const float fr = float(frames);
    const float step_db = glide_db * fr / rate_;
    const float swing = fr / (glide_swing * rate_);

    // Trim.
    if (trim_db_ != 0 || trim_target_ != 0) {
        const float from = db(trim_db_);
        trim_db_ = toward(trim_db_, trim_target_, step_db);
        ramp(l, r, frames, from, (db(trim_db_) - from) / fr);
    }

    // The bands in the chain (run_bands).
    Live live[eq_bands];
    unsigned at[eq_bands], count = 0;
    for (unsigned n = 0; n < eq_bands; ++n) {
        auto& b = bands_[n];
        if (b.gain != b.target) {
            b.gain = toward(b.gain, b.target, step_db);
            b.running = true;
            work_out(b, n);
        }
        if (!b.running) continue;
        live[count] = {b.b0, b.b1, b.b2, b.a1, b.a2, b.z[0][0], b.z[0][1], b.z[1][0], b.z[1][1]};
        at[count++] = n;
    }
    if (count) {
        band_runners[count](live, l, r, frames);
        for (unsigned k = 0; k < count; ++k) {
            auto& b = bands_[at[k]];
            const Live& s = live[k];
            b.z[0][0] = s.zl1; b.z[0][1] = s.zl2; b.z[1][0] = s.zr1; b.z[1][1] = s.zr2;
            // At 0 dB a band's output is its input but for rounding (about -90 dB): once its ringing has had time
            // to go, it leaves the chain.
            b.quiet = b.gain == 0 && b.target == 0 ? b.quiet + frames : 0;
            if (float(b.quiet) >= band_rest * rate_) {
                b.running = false; b.quiet = 0;
                b.z[0][0] = b.z[0][1] = b.z[1][0] = b.z[1][1] = 0;
            }
        }
    }

    // The compressor: the level heard followed a frame at a time, its gain worked out every comp_step frames and
    // eased between, blended against the dry, then the gain after it.
    reduction_ = 0;
    if (blend_ != 0 || blend_target_ != 0 || makeup_db_ != 0 || makeup_target_ != 0) {
        const float* hear_l = key_l ? key_l : l;
        const float* hear_r = key_r ? key_r : r;
        const float blend_from = blend_, makeup_from = db(makeup_db_);
        blend_ = toward(blend_, blend_target_, swing);
        makeup_db_ = toward(makeup_db_, makeup_target_, step_db);
        const float blend_d = (blend_ - blend_from) / fr, makeup_d = (db(makeup_db_) - makeup_from) / fr;
        const bool working = blend_from != 0 || blend_ != 0;
        float lowest = 1; // the gain heard, at its lowest
        for (unsigned f0 = 0; f0 < frames; f0 += comp_step) {
            const unsigned f1 = std::min(frames, f0 + comp_step);
            float red = 0;
            if (working) {
                // The envelope a frame at a time, at the attack's pace and at the release's, the higher kept: that is
                // the attack's when the level is above the envelope (the attack is the faster), so nothing branches
                // on the sound. The level's shares are worked out first, four frames at a time.
                const float att = attack_, rel = release_;
                float rise[comp_step], fall[comp_step];
                unsigned f = f0;
#if defined(__ARM_NEON)
                for (; f + 4 <= f1; f += 4) {
                    const float32x4_t level = vmaxq_f32(vabsq_f32(vld1q_f32(hear_l + f)), vabsq_f32(vld1q_f32(hear_r + f)));
                    vst1q_f32(rise + (f - f0), vmulq_n_f32(level, 1 - att));
                    vst1q_f32(fall + (f - f0), vmulq_n_f32(level, 1 - rel));
                }
#endif
                for (; f < f1; ++f) {
                    const float level = std::fmax(std::fabs(hear_l[f]), std::fabs(hear_r[f]));
                    rise[f - f0] = level * (1 - att); fall[f - f0] = level * (1 - rel);
                }
                float env = env_;
                for (unsigned j = 0; j < f1 - f0; ++j) env = std::fmax(env * att + rise[j], env * rel + fall[j]);
                env_ = env < 1e-12f ? 0 : env;
                const float over = db_per_log2 * log2_of(std::max(env_, 1e-6f)) - threshold_db_;
                red = over <= -knee_db / 2 ? 0 : over >= knee_db / 2 ? over * slope_
                    : slope_ * (over + knee_db / 2) * (over + knee_db / 2) / (2 * knee_db);
                if (strip_.comp_speed == uint8_t(CompSpeed::Auto)) { // a deeper reduction lets go more slowly
                    const float at = std::min(red, float(auto_steps));
                    const unsigned n = std::min(unsigned(at), auto_steps - 1);
                    release_ = auto_release_[n] + (at - float(n)) * (auto_release_[n + 1] - auto_release_[n]);
                }
            }
            const float aim = red != 0 ? exp2_of(-red * log2_per_db) : 1.0f, from = gain_, d = (aim - from) / float(f1 - f0);
            unsigned f = f0;
#if defined(__ARM_NEON)
            const float first[4] {1, 2, 3, 4};
            const float32x4_t four = vdupq_n_f32(4), one = vdupq_n_f32(1), from4 = vdupq_n_f32(from);
            const float32x4_t blend4 = vdupq_n_f32(blend_from), makeup4 = vdupq_n_f32(makeup_from);
            float32x4_t at = vld1q_f32(first), at_all = vaddq_f32(at, vdupq_n_f32(float(f0)));
            for (; f + 4 <= f1; f += 4, at = vaddq_f32(at, four), at_all = vaddq_f32(at_all, four)) {
                const float32x4_t g = vfmaq_n_f32(from4, at, d);
                const float32x4_t bl = vfmaq_n_f32(blend4, at_all, blend_d);
                const float32x4_t out = vmulq_f32(vfmaq_f32(vsubq_f32(one, bl), bl, g), vfmaq_n_f32(makeup4, at_all, makeup_d));
                vst1q_f32(l + f, vmulq_f32(vld1q_f32(l + f), out));
                vst1q_f32(r + f, vmulq_f32(vld1q_f32(r + f), out));
            }
#endif
            for (; f < f1; ++f) {
                const float g = from + d * float(f - f0 + 1);
                const float bl = blend_from + blend_d * float(f + 1);
                const float m = makeup_from + makeup_d * float(f + 1);
                const float out = ((1 - bl) + bl * g) * m;
                l[f] *= out; r[f] *= out;
            }
            gain_ = aim;
            lowest = std::min(lowest, (1 - blend_) + blend_ * aim);
        }
        if (lowest < 1) reduction_ = -20 * std::log10(std::max(lowest, 1e-6f));
        if (!working) { env_ = 0; gain_ = 1; }
    }

    // Width: the side's share, around the mid.
    if (width_ != 1 || width_target_ != 1) {
        const float from = width_;
        width_ = toward(width_, width_target_, swing * 2); // a full swing is 0..2
        const float d = (width_ - from) / fr;
        unsigned f = 0;
#if defined(__ARM_NEON)
        const float first[4] {1, 2, 3, 4};
        const float32x4_t four = vdupq_n_f32(4), half = vdupq_n_f32(0.5f), from4 = vdupq_n_f32(from * 0.5f);
        float32x4_t at = vld1q_f32(first);
        for (; f + 4 <= frames; f += 4, at = vaddq_f32(at, four)) {
            const float32x4_t x = vld1q_f32(l + f), y = vld1q_f32(r + f);
            const float32x4_t mid = vmulq_f32(vaddq_f32(x, y), half), side = vmulq_f32(vsubq_f32(x, y), vfmaq_n_f32(from4, at, d * 0.5f));
            vst1q_f32(l + f, vaddq_f32(mid, side));
            vst1q_f32(r + f, vsubq_f32(mid, side));
        }
#endif
        for (; f < frames; ++f) {
            const float w = from + d * float(f + 1);
            const float mid = (l[f] + r[f]) * 0.5f, side = (l[f] - r[f]) * 0.5f * w;
            l[f] = mid + side; r[f] = mid - side;
        }
    }
    check_idle();
}

} // namespace rhythmer
