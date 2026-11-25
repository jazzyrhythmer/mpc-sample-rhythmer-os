// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

// Moving audio between clock domains: PCM encoding and decoding, rate
// conversion, drift correction and a lock-free ring from one producer thread
// to one consumer thread. Portable and allocation-free; drivers/usb_audio
// uses them in both directions.

// Signed little-endian PCM sample of 2, 3 or 4 bytes as -1..1.
inline float pcm_sample(const uint8_t* bytes, unsigned width) {
    if (width == 2) return float(int16_t(uint16_t(bytes[0] | bytes[1] << 8))) / 32768.0f;
    if (width == 3) {
        uint32_t value = uint32_t(bytes[0]) << 8 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 24;
        return float(int32_t(value) >> 8) / 8388608.0f;
    }
    uint32_t value = uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
    return float(double(int32_t(value)) / 2147483648.0);
}

// The reverse of pcm_sample(): -1..1 as a signed little-endian sample of 2, 3
// or 4 bytes, rounded to the nearest step. Anything louder clips. The width
// as a template argument lets a loop over many samples store each in one go.
template <unsigned Width> inline void pcm_store(float sample, uint8_t* bytes) {
    static_assert(Width >= 2 && Width <= 4, "2, 3 or 4 bytes");
    constexpr double scale = double(1u << (8 * Width - 1));
    double value = std::clamp(double(sample) * scale + (sample < 0 ? -0.5 : 0.5), -scale, scale - 1);
    uint32_t bits = uint32_t(int32_t(value));
    for (unsigned n = 0; n < Width; ++n) bytes[n] = uint8_t(bits >> (8 * n));
}
inline void pcm_store(float sample, uint8_t* bytes, unsigned width) {
    if (width == 2) pcm_store<2>(sample, bytes);
    else if (width == 3) pcm_store<3>(sample, bytes);
    else pcm_store<4>(sample, bytes);
}

// Linear-interpolating rate converter for frames of `Channels` samples. Cheap
// enough for an audio thread, but it dulls the top octave by up to
// cos(pi f / rate) as its read position moves between frames, and converting
// between 44.1 and 48 kHz it folds images back into the audible band.
// SincResampler below does neither. Output lags input by one frame.
template <unsigned Channels> class FrameResampler {
public:
    void reset(unsigned input_rate, unsigned output_rate) {
        base = step = input_rate && output_rate ? double(input_rate) / output_rate : 1.0;
        phase = 0; primed = false;
    }
    // Scales the conversion by `factor` from the next frame on, without a
    // jump: above 1 it reads input slightly faster. See drift_trim().
    void trim(double factor) { step = base * factor; }
    // `emit` gets each converted frame as a pointer to `Channels` samples.
    template <class Emit> void feed(const float* frame, Emit&& emit) {
        if (!primed) { std::copy(frame, frame + Channels, last.begin()); primed = true; return; }
        while (phase < 1.0) {
            float t = float(phase);
            for (unsigned c = 0; c < Channels; ++c) mixed[c] = last[c] + (frame[c] - last[c]) * t;
            emit(static_cast<const float*>(mixed.data()));
            phase += step;
        }
        phase -= 1.0;
        std::copy(frame, frame + Channels, last.begin());
    }
private:
    double base = 1.0, step = 1.0, phase = 0;
    std::array<float, Channels> last {}, mixed {};
    bool primed = false;
};

// The stereo converter, fed and emitting left and right.
class LinearResampler : public FrameResampler<2> {
public:
    using FrameResampler<2>::feed;
    template <class Emit> void feed(float left, float right, Emit&& emit) {
        const float frame[2] {left, right};
        feed(frame, [&](const float* out) { emit(out[0], out[1]); });
    }
};

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
    // jump: above 1 it reads input slightly faster. See drift_trim().
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

// Two crystals never agree exactly: the AK4558's clock and a computer's USB
// clock drift apart by tens of parts per million. When a FrameRing joins them,
// this turns the ring's average fill into a trim() factor for the converter on
// either side of it: a ring filling above `target` makes the converter read
// its input faster, so that one draining the ring empties it sooner and one
// feeding it adds fewer frames; a ring draining below `target` the reverse.
// Proportional only, so a steady drift settles with the fill offset by
// drift / gain, 50 frames for 100 ppm, with a time constant of about 11
// seconds at 44.1 kHz. The factor stays within ±0.2 %.
inline double drift_trim(double average_fill, double target) {
    constexpr double gain = 2e-6, limit = 2e-3;
    return 1.0 + std::clamp((average_fill - target) * gain, -limit, limit);
}

// The other way to join the two clocks, when the computer takes feedback: a
// UAC2 gadget in asynchronous mode tells the computer how many frames to send
// in each microframe, scaled by its "Capture Pitch 1000000" control, so the
// AK4558 sets the pace and nothing is converted. This turns the ring's average
// fill into that pitch, in parts per million over the nominal rate: a ring
// above `target` asks for fewer frames, one below it for more. Proportional,
// 5 ppm a frame, plus the integral of the same over 20 seconds, so a steady
// drift is taken up and the fill settles on `target` itself rather than off
// it, which matters with a ring of a few hundred frames. At 44.1 kHz it
// settles in about 5 seconds without overshooting much. The pitch stays within
// ±0.2 %. The other way round, the gadget's "Playback Pitch 1000000" sets how
// many frames it sends the computer a microframe, so a ring the gadget drains
// takes the pitch with its sign turned: a ring above target sends faster.
class PitchLock {
public:
    void reset() { integral = 0; }
    // The ring's average fill over the last `seconds`; returns the pitch in ppm.
    int update(double average_fill, double target, double seconds) {
        constexpr double gain = 5.0, reset_time = 20.0, limit = 2000.0; // ppm a frame, seconds, ppm
        double error = average_fill - target;
        integral = std::clamp(integral + gain * error * seconds / reset_time, -limit, limit);
        return int(std::lround(std::clamp(-(gain * error + integral), -limit, limit)));
    }

private:
    double integral = 0; // ppm
};

// Float frames of `Channels` interleaved samples (stereo unless stated) from
// one producer thread to one consumer thread, either
// way round the audio thread. The consumer waits until `prime` frames are
// buffered before it starts, and again after it runs dry, so a clock-domain
// hand-off such as USB costs one short silence rather than a stream of clicks.
// A full ring drops the newest frames.
template <unsigned Capacity, unsigned Channels = 2> class FrameRing {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    unsigned push(const float* frames, unsigned count) {
        auto w = write.load(std::memory_order_relaxed), r = read.load(std::memory_order_acquire);
        unsigned room = Capacity - (w - r), n = std::min(count, room);
        for (unsigned i = 0; i < n; ++i) {
            unsigned slot = (w + i) & (Capacity - 1);
            std::copy(frames + Channels * i, frames + Channels * (i + 1), data.begin() + Channels * slot);
        }
        write.store(w + n, std::memory_order_release);
        if (n < count) overflows.fetch_add(1, std::memory_order_relaxed);
        return n;
    }
    // Writes `count` frames, real or silent; true when they were real.
    bool pull(float* frames, unsigned count, unsigned prime) {
        if (flush_requested.exchange(false, std::memory_order_acq_rel)) {
            read.store(write.load(std::memory_order_acquire), std::memory_order_release); primed = false;
        }
        auto r = read.load(std::memory_order_relaxed), w = write.load(std::memory_order_acquire);
        unsigned available = w - r;
        if (!primed && available >= std::max(prime, count)) primed = true;
        if (!primed || available < count) {
            if (primed) { underruns.fetch_add(1, std::memory_order_relaxed); primed = false; }
            std::fill(frames, frames + Channels * count, 0.0f);
            return false;
        }
        for (unsigned i = 0; i < count; ++i) {
            unsigned slot = (r + i) & (Capacity - 1);
            std::copy(data.begin() + Channels * slot, data.begin() + Channels * (slot + 1), frames + Channels * i);
        }
        read.store(r + count, std::memory_order_release);
        return true;
    }
    // Any thread: the consumer discards everything buffered on its next pull.
    void flush() { flush_requested.store(true, std::memory_order_release); }
    unsigned buffered() const { return write.load(std::memory_order_acquire) - read.load(std::memory_order_acquire); }
    std::atomic<unsigned> overflows {0}, underruns {0};
private:
    std::array<float, Capacity * Channels> data {};
    std::atomic<unsigned> read {0}, write {0};
    std::atomic<bool> flush_requested {false};
    bool primed = false; // Consumer only.
};
