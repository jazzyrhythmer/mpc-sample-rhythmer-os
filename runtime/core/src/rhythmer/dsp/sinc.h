// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

// The band-limited rate converter, as sample-base has it in dsp/frame_ring.h,
// so that the core needs no hardware runtime: Rhythmer's sample loading
// (engine/sample_data.cpp) and plug-in rate bridge (plugin/polyphase.h)
// convert with it. Kept the same as the base's, so a sample converted here
// sounds as USB audio converted there.
namespace rhythmer {


// The kernel SincResampler interpolates: sin(pi u) / (pi u) under a Kaiser
// window that closes `zero_crossings` crossings from the centre, tabulated
// from u = 0 at `steps` points a crossing, each with the slope to the next,
// and zero for one crossing beyond. One table, built on first use, serves
// every converter and every rate.
class SincKernel {
public:
    static constexpr unsigned zero_crossings = 32, steps = 256;
    static constexpr unsigned column_points = zero_crossings + 2;
    struct Point { float value, slope; };
    static const Point* table() { return kernel().points.data(); }
    // The same points by their place within a crossing: column(q)[k] is
    // table()[k * steps + q], for k up to zero_crossings + 1 (zero past the
    // table). A kernel that is not stretched puts every tap at the same place
    // within its crossing, so its taps read one column in a row.
    static const Point* column(unsigned q) { return kernel().columns.data() + q * column_points; }

private:
    static const SincKernel& kernel() {
        static const SincKernel built;
        return built;
    }
    SincKernel() {
        constexpr double pi = 3.14159265358979323846, beta = 9.2;
        auto bessel_i0 = [](double x) {
            double sum = 1, term = 1;
            for (int k = 1; k < 100 && term > 1e-17 * sum; ++k) { term *= x * x / (4.0 * k * k); sum += term; }
            return sum;
        };
        const double unity = bessel_i0(beta);
        for (unsigned n = 0; n < points.size(); ++n) {
            double u = double(n) / steps, edge = u / zero_crossings;
            if (edge >= 1) break;
            double sinc = n % steps ? std::sin(pi * u) / (pi * u) : n ? 0.0 : 1.0; // Exact zeros: equal rates pass bit for bit.
            points[n].value = float(sinc * bessel_i0(beta * std::sqrt(1 - edge * edge)) / unity);
        }
        for (unsigned n = 0; n + 1 < points.size(); ++n) points[n].slope = points[n + 1].value - points[n].value;
        for (unsigned q = 0; q < steps; ++q)
            for (unsigned k = 0; k < column_points; ++k)
                if (k * steps + q < points.size()) columns[q * column_points + k] = points[k * steps + q];
    }
    std::array<Point, (zero_crossings + 1) * steps + 1> points {};
    std::array<Point, steps * column_points> columns {};
};

// Band-limited rate converter for frames of `Channels` samples, for a
// computer's USB clock against the AK4558's. Each output frame is the input
// under a windowed sinc centred on its position: everything up to 0.907 of
// the lower rate's Nyquist frequency (20 kHz at 44.1 kHz) passes flat, and
// whatever the conversion would fold or image back into that band is at
// least 90 dB down, wherever between two input frames the output falls.
// Converting down, the kernel stretches over more input frames so that the
// passband follows the output's rate. Each frame is normalised by its
// weights, so DC passes exactly. Output lags input by `half` frames (32 at
// equal rates, under a millisecond); before the first frame the input is
// taken to have held it. No allocation; reset() builds the shared kernel on
// first use.
//
// Each output frame works out its weights once and then takes one dot
// product per channel over that channel's own history, four taps at a time.
// A channel whose input has been exactly silent for the whole kernel gives
// silence without one, so stems that play nothing cost almost nothing.
template <unsigned Channels> class SincResampler {
public:
    // The widest kernel the history holds, in input frames each side: the
    // full stopband converting down by up to 4:1. Beyond that the band stays
    // a quarter of the input's, and what lies above the output's folds back.
    static constexpr unsigned max_half = 128;

    SincResampler() { reset(0, 0); }
    void reset(unsigned input_rate, unsigned output_rate) {
        kernel = SincKernel::table();
        base = step = input_rate && output_rate ? double(input_rate) / output_rate : 1.0;
        double band = std::max(std::min(1.0, 1.0 / base), double(SincKernel::zero_crossings) / max_half);
        half = std::min(unsigned(std::ceil(SincKernel::zero_crossings / band)), max_half);
        width = (2 * half + 3) & ~3u; // Whole groups of four; the extra taps, the oldest frames, weigh nothing.
        scale = band * SincKernel::steps;
        weights.fill(0.0f);
        phase = 0; pending = half; primed = false;
    }
    // Scales the conversion by `factor` from the next frame on, without a
    // jump: above 1 it reads input slightly faster.
    void trim(double factor) { step = base * factor; }
    // `emit` gets each converted frame as a pointer to `Channels` samples.
    template <class Emit> void feed(const float* frame, Emit&& emit) {
        if (!primed) {
            for (unsigned c = 0; c < Channels; ++c) {
                history[c].fill(frame[c]);
                quiet[c] = frame[c] == 0.0f ? 2 * max_half : 0;
            }
            newest = 0; primed = true;
            return;
        }
        newest = (newest + 1) & (history_frames - 1);
        for (unsigned c = 0; c < Channels; ++c) {
            history[c][newest] = history[c][newest + history_frames] = frame[c];
            quiet[c] = frame[c] == 0.0f ? std::min(quiet[c] + 1, 2 * max_half) : 0;
        }
        if (pending > 1) { --pending; return; }
        pending = 0;
        const unsigned taps = 2 * half, start = newest + history_frames + 1 - width;
        float* tap = weights.data() + (width - taps);
        while (phase < 1.0) {
            // Tap j sits (half - 1 - j + phase) input frames from the output.
            const double first = (half - 1 + phase) * scale;
            if (scale == SincKernel::steps) unstretched(first, tap, taps);
            else
                for (unsigned j = 0; j < taps; ++j) {
                    double at = std::abs(first - j * scale);
                    unsigned point = unsigned(at);
                    tap[j] = kernel[point].value + float(at - point) * kernel[point].slope;
                }
            float sum = 0;
            for (unsigned j = 0; j < taps; ++j) sum += tap[j];
            const float gain = 1.0f / sum;
            for (unsigned c = 0; c < Channels; ++c)
                mixed[c] = quiet[c] >= taps ? 0.0f : dot(weights.data(), history[c].data() + start, width) * gain;
            emit(static_cast<const float*>(mixed.data()));
            phase += step;
        }
        phase -= 1.0;
    }
    // A stereo converter fed and emitting left and right.
    template <class Emit> void feed(float left, float right, Emit&& emit) {
        static_assert(Channels == 2, "left and right are for a stereo converter");
        const float frame[2] {left, right};
        feed(frame, [&](const float* out) { emit(out[0], out[1]); });
    }

private:
    // The weights of a kernel that is not stretched (converting up, or at
    // equal rates), the same as the general loop gives: the taps at or before
    // `first` steps sit `r` steps into their crossings, those after it 256 - r,
    // so each side reads one column of the kernel in a row.
    static void unstretched(double first, float* tap, unsigned taps) {
        constexpr unsigned steps = SincKernel::steps;
        const unsigned whole = unsigned(first) / steps; // The last tap at or before the output
        const double r = first - double(whole * steps);
        const unsigned q = unsigned(r);
        const float f = float(r - q);
        const SincKernel::Point* left = SincKernel::column(q);
        for (unsigned j = 0; j <= whole && j < taps; ++j) tap[j] = left[whole - j].value + f * left[whole - j].slope;
        if (r == 0) { // The taps after it fall on whole crossings too.
            const SincKernel::Point* right = SincKernel::column(0);
            for (unsigned j = whole + 1; j < taps; ++j) tap[j] = right[j - whole].value + 0.0f * right[j - whole].slope;
            return;
        }
        const double s = steps - r;
        const unsigned q2 = unsigned(s);
        const float f2 = float(s - q2);
        const SincKernel::Point* right = SincKernel::column(q2);
        for (unsigned j = whole + 1; j < taps; ++j) tap[j] = right[j - whole - 1].value + f2 * right[j - whole - 1].slope;
    }
    // `count`, a multiple of four, weights against as many samples.
    static float dot(const float* w, const float* x, unsigned count) {
#if defined(__ARM_NEON)
        float32x4_t a = vdupq_n_f32(0), b = vdupq_n_f32(0);
        unsigned j = 0;
        for (; j + 8 <= count; j += 8) {
            a = vfmaq_f32(a, vld1q_f32(w + j), vld1q_f32(x + j));
            b = vfmaq_f32(b, vld1q_f32(w + j + 4), vld1q_f32(x + j + 4));
        }
        if (j < count) a = vfmaq_f32(a, vld1q_f32(w + j), vld1q_f32(x + j));
        return vaddvq_f32(vaddq_f32(a, b));
#else
        float s[4] {};
        for (unsigned j = 0; j < count; j += 4)
            for (unsigned k = 0; k < 4; ++k) s[k] += w[j + k] * x[j + k];
        return (s[0] + s[1]) + (s[2] + s[3]);
#endif
    }
    // Each channel's own, twice over, so that its last 2 * half frames
    // always lie in a row.
    static constexpr unsigned history_frames = 2 * max_half;
    const SincKernel::Point* kernel = nullptr;
    double base = 1.0, step = 1.0, phase = 0, scale = 0;
    unsigned half = 0, width = 0, newest = 0, pending = 0;
    bool primed = false;
    std::array<float, 2 * max_half> weights {}; // `width` of them, the first `width - 2 * half` zero
    std::array<float, Channels> mixed {};
    std::array<unsigned, Channels> quiet {}; // Exact zeros in a row each channel was fed, up to 2 * max_half
    // A cache line more than the frames, so that the channels' windows do not
    // all fall in the same few sets of a small data cache (the A55's 32 KB).
    std::array<std::array<float, 2 * history_frames + 16>, Channels> history {};
};

} // namespace rhythmer
