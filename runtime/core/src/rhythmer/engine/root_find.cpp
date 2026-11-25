// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/engine/root_find.h"
#include "rhythmer/engine/dsp.h"
#include "rhythmer/engine/fft.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

// A sample's note: its pitch in windows of 93 ms over its loud part, by the
// normalized square difference (McLeod and Wyvill, 2005, as probe_pitch() in
// engine/auto_probe.cpp, here through the FFT), read at half of 44.1 kHz. A
// window counts when its sound repeats clearly; the sample has a note when most
// of its loud windows count and most of those agree on one pitch class (an
// octave either way is the same note). A sound with no one pitch, such as a
// chord (a minor chord never repeats exactly), is read as a chord instead: the
// pitch classes of its spectrum's peaks matched to chord shapes window by
// window, its root where the chord holds steady. A drum, a noise, a phrase of
// several notes and a sound whose pitch glides have none: they play as a C.
// Measured: of 172 multisampled synth notes (the auto sampler's, named by their
// keys) 150 right, 22 none, none wrong; of 222 MPC factory basses, keys, pads
// and stabs named with a note 203 right, 14 none, 5 another note (three of
// them sit half a semitone off the name, one is a phrase, one a chord on
// another root of the key named); of 337 factory drums, cymbals and effects
// the pitched kicks, toms and congas have a note and no hat, snare or noise.
namespace rhythmer {
namespace {

using Complex = std::complex<float>;
constexpr unsigned factor = 2;                         // read at 22.05 kHz
constexpr float rate = dsp::model_rate / factor;
constexpr unsigned window = 2048;                      // 93 ms: two periods of the lowest note
constexpr unsigned size = 2 * window;                  // the FFT: padded, so the sound does not wrap onto itself
constexpr unsigned hop = window / 2;
constexpr unsigned most_windows = 48;                  // spread over the loud part when it has more
constexpr unsigned most_frames = unsigned(8 * dsp::model_rate); // the first 8 seconds
constexpr float lowest_hz = 26.0f, highest_hz = 4200.0f; // A0 (27.5 Hz) to C8 (4186 Hz), with room
constexpr float loud = 0.0316f;                        // -30 dB under the loudest window: the rest is its tail or silence
constexpr float clear = 0.8f;                          // how exactly a window repeats to count (1: exactly)
constexpr float counted = 0.5f, agreeing = 0.7f;       // the shares of loud windows that count and of those that agree

// The sound low-passed under 10 kHz and kept every second frame: a windowed sinc, 63 taps.
std::vector<float> decimate(const std::vector<float>& mono, size_t frames) {
    constexpr int taps = 63, half = taps / 2;
    static const std::vector<float> kernel = [] {
        std::vector<float> k(taps);
        const double cut = 10000.0 / dsp::model_rate;
        double sum = 0;
        for (int n = 0; n < taps; ++n) {
            const double x = n - half, sinc = x == 0 ? 2 * cut : std::sin(2 * M_PI * cut * x) / (M_PI * x);
            const double blackman = 0.42 - 0.5 * std::cos(2 * M_PI * n / (taps - 1)) + 0.08 * std::cos(4 * M_PI * n / (taps - 1));
            k[size_t(n)] = float(sinc * blackman);
            sum += sinc * blackman;
        }
        for (auto& v : k) v = float(v / sum);
        return k;
    }();
    std::vector<float> out(frames / factor);
    for (size_t m = 0; m < out.size(); ++m) {
        const long centre = long(m * factor);
        float acc = 0;
        for (int n = 0; n < taps; ++n) {
            const long at = centre + n - half;
            if (at >= 0 && size_t(at) < frames) acc += mono[size_t(at)] * kernel[size_t(n)];
        }
        out[m] = acc;
    }
    return out;
}

// One window's pitch in Hz and how clearly it repeats (0..1); 0 Hz when it does not.
struct Heard { float hz = 0, clarity = 0; };
Heard window_pitch(const float* x, std::vector<Complex>& buffer, const std::vector<Complex>& twiddle, std::vector<float>& nsdf) {
    for (unsigned n = 0; n < size; ++n) buffer[n] = n < window ? Complex(x[n], 0.0f) : Complex(0.0f, 0.0f);
    fft(buffer, twiddle);
    for (auto& z : buffer) z = Complex(std::norm(z), 0.0f); // its power; the inverse of it is the autocorrelation
    fft(buffer, twiddle);                                    // a real, even spectrum: forward is the inverse times `size`
    const unsigned high = std::min(unsigned(std::ceil(rate / lowest_hz)), window / 2), low = unsigned(rate / highest_hz);
    double m = 0;
    for (unsigned n = 0; n < window; ++n) m += 2.0 * double(x[n]) * x[n];
    nsdf.assign(high + 2, 0.0f);
    for (unsigned lag = 0; lag < high + 2; ++lag) {
        if (lag) m -= double(x[lag - 1]) * x[lag - 1] + double(x[window - lag]) * x[window - lag];
        nsdf[lag] = m > 1e-12 ? float(2.0 * buffer[lag].real() / size / m) : 0.0f;
    }
    // The peaks of each rise above zero after the first fall below it; the first within 0.9 of the highest is the
    // period (a later one is a multiple of it).
    unsigned lag = 1;
    while (lag <= high && nsdf[lag] > 0) ++lag;
    float best = 0;
    std::vector<unsigned> peaks;
    while (lag <= high) {
        while (lag <= high && nsdf[lag] <= 0) ++lag;
        unsigned top = 0;
        for (; lag <= high && nsdf[lag] > 0; ++lag) if (!top || nsdf[lag] > nsdf[top]) top = lag;
        if (top >= 2 && top < high) { peaks.push_back(top); best = std::max(best, nsdf[top]); }
    }
    if (best < clear) return {};
    for (unsigned top : peaks) {
        if (nsdf[top] < 0.9f * best) continue;
        if (top < low) return {}; // higher than the range: a later peak would be a multiple of it, a wrong note
        const float a = nsdf[top - 1], b = nsdf[top], c = nsdf[top + 1];
        const float d = a - 2 * b + c, shift = std::fabs(d) > 1e-9f ? std::clamp(0.5f * (a - c) / d, -0.5f, 0.5f) : 0.0f;
        const float hz = rate / (float(top) + shift);
        return hz >= lowest_hz && hz <= highest_hz ? Heard {hz, b} : Heard {};
    }
    return {};
}

// How far apart two pitch classes are (semitones, 0..6), an octave either way being the same.
float apart(float a, float b) {
    const float d = std::fmod(std::fabs(a - b), 12.0f);
    return std::min(d, 12.0f - d);
}


// A chord's root (chord_root()): each window 186 ms, Hann, padded to twice that, its peaks from 50 Hz to 5 kHz.
constexpr unsigned chord_span = 4096, chord_size = 2 * chord_span;
constexpr float chord_fit = 0.8f;   // how closely a window's pitch classes fit a chord shape (cosine, 1: exactly)
constexpr float bass_band = 0.05f;  // shapes within this of the best fit: the one whose root the bass plays wins
constexpr float steady = 0.95f;     // how alike one window's pitch classes are to the next's (cosine): a held chord
constexpr float low_share = 0.2f;   // of the sound under 5 kHz at least: a hat or a noise is mostly over it

// Pitch classes over the root: major, minor, seventh, minor seventh, major seventh, sus4, sus2, a fifth, minor ninth
// and major ninth.
const std::array<std::vector<int>, 10>& chord_shapes() {
    static const std::array<std::vector<int>, 10> shapes {{
        {0, 4, 7}, {0, 3, 7}, {0, 4, 7, 10}, {0, 3, 7, 10}, {0, 4, 7, 11}, {0, 5, 7}, {0, 2, 7}, {0, 7}, {0, 2, 3, 7, 10}, {0, 2, 4, 7, 11},
    }};
    return shapes;
}

// The chord a sound holds where it has no one pitch: its spectrum's peaks as pitch classes, moved by how far they
// sit between semitones on the whole (its tuning), fitted to the chord shapes in each window; of shapes that fit
// about as well (Am7 and C6 hold the same notes), the one on the bass's note. A root when the windows hold the same
// pitch classes from one to the next and most of them agree.
SampleRoot chord_root(const std::vector<float>& x, const std::vector<unsigned>& chosen) {
    if (chosen.size() < 3) return {};
    std::vector<Complex> twiddle(chord_size / 2), buffer(chord_size);
    for (unsigned k = 0; k < chord_size / 2; ++k) twiddle[k] = std::polar(1.0f, float(-2 * M_PI * k / chord_size));
    struct Peak { float note, weight; };
    std::vector<std::vector<Peak>> heard;
    std::vector<float> lows;
    std::vector<float> magnitude(chord_size / 2 + 1);
    const float bin_hz = rate / chord_size;
    const unsigned from = std::max(2u, unsigned(50 / bin_hz)), to = std::min(unsigned(5000 / bin_hz), chord_size / 2 - 1);
    for (unsigned w : chosen) {
        const size_t start = size_t(w) * hop;
        for (unsigned k = 0; k < chord_size; ++k) {
            const float v = k < chord_span && start + k < x.size() ? x[start + k] * float(0.5 - 0.5 * std::cos(2 * M_PI * k / chord_span)) : 0.0f;
            buffer[k] = Complex(v, 0.0f);
        }
        fft(buffer, twiddle);
        float top = 0;
        double under = 0, all = 0;
        for (unsigned k = 0; k <= chord_size / 2; ++k) {
            magnitude[k] = std::abs(buffer[k]);
            top = std::max(top, magnitude[k]);
            const double power = double(magnitude[k]) * magnitude[k];
            all += power;
            if (k < to) under += power;
        }
        lows.push_back(all > 0 ? float(under / all) : 0.0f);
        std::vector<Peak> peaks;
        for (unsigned k = from; k < to; ++k) {
            if (magnitude[k] <= magnitude[k - 1] || magnitude[k] < magnitude[k + 1] || magnitude[k] < 0.01f * top) continue; // -40 dB
            const float a = std::log(magnitude[k - 1] + 1e-12f), b = std::log(magnitude[k] + 1e-12f), c = std::log(magnitude[k + 1] + 1e-12f);
            const float d = a - 2 * b + c, shift = std::fabs(d) > 1e-9f ? std::clamp(0.5f * (a - c) / d, -0.5f, 0.5f) : 0.0f;
            peaks.push_back({69.0f + 12.0f * std::log2((float(k) + shift) * bin_hz / 440.0f), magnitude[k] / top});
        }
        heard.push_back(std::move(peaks));
    }
    std::sort(lows.begin(), lows.end());
    if (lows[lows.size() / 2] < low_share) return {};
    double re = 0, im = 0;
    for (const auto& peaks : heard)
        for (const auto& p : peaks) {
            const double off = p.note - std::round(p.note);
            re += p.weight * std::cos(2 * M_PI * off);
            im += p.weight * std::sin(2 * M_PI * off);
        }
    const float tuning = float(std::atan2(im, re) / (2 * M_PI)); // semitones, -0.5 .. 0.5
    std::vector<int> roots;
    std::array<float, 12> before {};
    double alike = 0;
    unsigned compared = 0;
    for (size_t w = 0; w < heard.size(); ++w) {
        std::array<float, 12> chroma {}, bass {};
        for (const auto& p : heard[w]) {
            const auto pc = size_t((std::lround(p.note - tuning) % 12 + 12) % 12);
            chroma[pc] += p.weight;
            if (p.note - tuning < 55) bass[pc] += p.weight; // under G3
        }
        double norm = 0, dot = 0, norm_before = 0;
        for (size_t k = 0; k < 12; ++k) { norm += double(chroma[k]) * chroma[k]; dot += double(chroma[k]) * before[k]; norm_before += double(before[k]) * before[k]; }
        if (w && norm > 0 && norm_before > 0) { alike += dot / std::sqrt(norm * norm_before); ++compared; }
        before = chroma;
        if (norm <= 0) continue;
        norm = std::sqrt(norm);
        float best = 0;
        std::array<float, 12> fit {}; // each root's best shape
        for (int r = 0; r < 12; ++r)
            for (const auto& shape : chord_shapes()) {
                double sum = 0;
                for (int t : shape) sum += chroma[size_t((r + t) % 12)];
                fit[size_t(r)] = std::max(fit[size_t(r)], float(sum / (norm * std::sqrt(double(shape.size())))));
                best = std::max(best, fit[size_t(r)]);
            }
        if (best < chord_fit) continue;
        int root = int(std::max_element(fit.begin(), fit.end()) - fit.begin());
        float bass_sum = 0;
        for (float v : bass) bass_sum += v;
        const int bass_pc = int(std::max_element(bass.begin(), bass.end()) - bass.begin());
        if (bass_sum > 0 && bass[size_t(bass_pc)] > 0.3f * bass_sum && fit[size_t(bass_pc)] >= best - bass_band) root = bass_pc;
        roots.push_back(root);
    }
    if (!compared || alike / compared < steady) return {}; // what it holds changes: a phrase, a hit, a noise
    if (roots.size() < 3 || float(roots.size()) < counted * float(chosen.size())) return {};
    std::array<unsigned, 12> votes {};
    for (int r : roots) ++votes[size_t(r)];
    const auto top = size_t(std::max_element(votes.begin(), votes.end()) - votes.begin());
    if (float(votes[top]) < agreeing * float(roots.size())) return {};
    SampleRoot out;
    out.pitch_class = int(top);
    out.cents = int(std::lround(tuning * 100));
    return out;
}

} // namespace

SampleRoot find_root(const std::vector<float>& model_mono) {
    const auto x = decimate(model_mono, std::min<size_t>(model_mono.size(), most_frames));
    if (x.size() < window) return {};
    const unsigned count = unsigned((x.size() - window) / hop + 1);
    std::vector<float> level(count);
    float loudest = 0;
    for (unsigned w = 0; w < count; ++w) {
        double sum = 0;
        for (unsigned n = 0; n < window; ++n) sum += double(x[w * hop + n]) * x[w * hop + n];
        level[w] = float(std::sqrt(sum / window));
        loudest = std::max(loudest, level[w]);
    }
    if (loudest < 1e-4f) return {}; // silence
    std::vector<unsigned> chosen;
    for (unsigned w = 0; w < count; ++w) if (level[w] >= loud * loudest) chosen.push_back(w);
    if (chosen.size() > most_windows) {
        std::vector<unsigned> spread(most_windows);
        for (unsigned k = 0; k < most_windows; ++k) spread[k] = chosen[size_t(k) * chosen.size() / most_windows];
        chosen.swap(spread);
    }
    std::vector<Complex> twiddle(size / 2), buffer(size);
    for (unsigned k = 0; k < size / 2; ++k) twiddle[k] = std::polar(1.0f, float(-2 * M_PI * k / size));
    std::vector<float> nsdf, classes; // each window that counts: its pitch as a MIDI note, within an octave (0..12)
    for (unsigned w : chosen) {
        const Heard heard = window_pitch(&x[size_t(w) * hop], buffer, twiddle, nsdf);
        if (heard.hz <= 0) continue;
        const float note = 69.0f + 12.0f * std::log2(heard.hz / 440.0f);
        classes.push_back(std::fmod(std::fmod(note, 12.0f) + 12.0f, 12.0f));
    }
    if (classes.size() < 3 || float(classes.size()) < counted * float(chosen.size())) return chord_root(x, chosen); // mostly no one pitch
    // The pitch class most windows lie within half a semitone of, then their mean (around it, not across 0).
    size_t most = 0;
    float centre = 0;
    for (float c : classes) {
        size_t near = 0;
        for (float o : classes) near += apart(c, o) <= 0.5f;
        if (near > most) { most = near; centre = c; }
    }
    if (float(most) < agreeing * float(classes.size())) return chord_root(x, chosen); // several notes: a chord, or a phrase
    double sum = 0;
    for (float o : classes) {
        if (apart(centre, o) > 0.5f) continue;
        float d = o - centre;
        if (d > 6) d -= 12;
        if (d < -6) d += 12;
        sum += d;
    }
    const double mean = centre + sum / double(most);
    const double nearest = std::round(mean);
    SampleRoot out;
    out.pitch_class = int((long(nearest) % 12 + 12) % 12);
    out.cents = int(std::lround((mean - nearest) * 100.0));
    return out;
}

} // namespace rhythmer
