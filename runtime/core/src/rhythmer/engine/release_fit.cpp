// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/engine/release_fit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rhythmer {

namespace {
constexpr float silent = -90.0f; // a value for nothing left to hear
constexpr float lowest = -60.0f; // the fit follows the fall this far down

float rms_db(const std::vector<float>& m, size_t from, size_t n) {
    if (from >= m.size()) return -140.0f;
    n = std::min(n, m.size() - from);
    double sum = 0;
    for (size_t f = 0; f < n; ++f) sum += double(m[from + f]) * m[from + f];
    return n && sum > 1e-14 * double(n) ? float(10.0 * std::log10(sum / double(n))) : -140.0f;
}
} // namespace

ReleaseShape release_shape(const std::vector<float>& left, const std::vector<float>& right, uint64_t held, float rate, bool cut,
                           float quiet) {
    ReleaseShape shape;
    shape.whole = !cut;
    const size_t frames = std::min(left.size(), right.size()), up = size_t(std::min<uint64_t>(held, frames));
    if (rate <= 0 || up < size_t(0.1f * rate)) return shape;
    std::vector<float> m(frames);
    for (size_t f = 0; f < frames; ++f) m[f] = 0.5f * (left[f] + right[f]);
    // Its level as the key was let go, and whether it was fading while held: a line through the level of the hold's
    // second half, 50 ms at a time. Fading by 4 dB or more over it: the line's level at the release and its slope
    // (what the note would have gone on doing, held). Else its level over its last tenth of a second: what the
    // envelope starts from when a key is let go.
    const size_t piece = size_t(0.05f * rate);
    std::vector<float> hold;
    for (size_t at = up / 2; at + piece <= up; at += piece) hold.push_back(rms_db(m, at, piece));
    float level = rms_db(m, up - std::min(up / 2, size_t(0.1f * rate)), std::min(up / 2, size_t(0.1f * rate)));
    float fade = 0; // dB a second
    if (hold.size() >= 4) {
        const double n = double(hold.size());
        double st = 0, sy = 0, stt = 0, sty = 0;
        for (size_t i = 0; i < hold.size(); ++i) {
            const double t = (double(up / 2 + i * piece + piece / 2) - double(up)) / rate; // seconds before the release
            st += t; sy += hold[i]; stt += t * t; sty += t * hold[i];
        }
        const double spread = n * stt - st * st;
        if (spread > 1e-9) {
            const double slope = (n * sty - st * sy) / spread, at_release = (sy - slope * st) / n;
            const double span = double(hold.size() * piece) / rate;
            if (-slope * span >= 4.0) { shape.fading = true; fade = float(std::min(-slope, 120.0)); level = float(at_release); }
        }
    }
    if (level < quiet + 15.0f) return shape; // too quiet by then to tell
    // The fall: every 10 ms, the mean power from there on over 30 ms or as long again as the time since the release
    // (ReleaseShape::span()), so echoes count by their energy. A fading note's own fade is added back (what that span
    // would have lost held), for as long as it would still have been heard held (past that nothing can be told).
    const size_t hop = size_t(ReleaseShape::hop * rate);
    const float told = shape.fading ? (level - quiet - 5.0f) / fade : 1e9f; // seconds
    for (size_t at = up, n = 0; at + size_t(ReleaseShape::window * rate) / 2 <= frames; at += hop, ++n) {
        const float t = ReleaseShape::start_of(n), w = ReleaseShape::span(n);
        if (t > told) { shape.whole = false; break; }
        if (at + size_t(w * rate) > frames && n > 0 && w > ReleaseShape::window) { shape.whole = shape.whole && !cut; break; } // no whole span left
        const float db = rms_db(m, at, size_t(w * rate));
        float back = 0;
        if (fade > 0) { // held, its power over the span would have averaged this much under its level at the release
            const double x = double(fade) * w * 0.2302585; // ln(10) / 10
            back = fade * t - float(10.0 * std::log10(x > 1e-6 ? (1.0 - std::exp(-x)) / x : 1.0));
        }
        shape.db.push_back(db < quiet ? silent : std::clamp(db - level + back, silent, 20.0f));
    }
    return shape;
}

float ReleaseModel::db(float seconds) const {
    const double a = std::exp(-3.0 * seconds / std::max(fast, 1e-4f));
    const double b = two() ? std::exp(-3.0 * seconds / std::max(slow, 1e-4f)) : 0.0;
    const double g = two() ? (1.0 - share) * a + share * b : a;
    return g > 1e-7 ? float(20.0 * std::log10(g)) : -140.0f;
}

ReleaseModel fit_release(const std::vector<ReleaseShape>& notes, std::string* report) {
    ReleaseModel model;
    // The notes' fall, a value every 10 ms: their median, each against its own level as its key was let go (what a
    // note usually does). A note that ended in silence counts as silent after its end, one cut short as not known;
    // kept while half the notes or more tell. Then never rising (each value the highest still to come: an echo holds
    // the level until it has sounded), down to the first value under -60 dB.
    size_t usable = 0, longest = 0;
    for (const auto& n : notes) { usable += !n.db.empty(); longest = std::max(longest, n.db.size()); }
    std::vector<float> fall, values;
    for (size_t k = 0; k < longest; ++k) {
        values.clear();
        for (const auto& n : notes) {
            if (n.db.empty()) continue;
            if (k < n.db.size()) values.push_back(n.db[k]);
            else if (n.whole) values.push_back(silent);
        }
        if (values.empty() || values.size() * 2 < usable) break;
        std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
        fall.push_back(std::min(values[values.size() / 2], 0.0f));
    }
    for (size_t n = fall.size(); n-- > 1;) fall[n - 1] = std::max(fall[n - 1], fall[n]);
    for (size_t n = 0; n < fall.size(); ++n) if (fall[n] <= lowest) { fall.resize(n + 1); break; }
    if (fall.empty()) return model;
    // Compared at times a ratio apart (each 12% on), so the first tenth of a second weighs as much as the last seconds.
    std::vector<size_t> at;
    for (size_t k = 0; k < fall.size(); k = std::max(k + 1, size_t(double(k) * 1.12))) at.push_back(k);
    if (at.back() != fall.size() - 1) at.push_back(fall.size() - 1);
    const size_t points = at.size();
    // A model is compared as it would have been measured: its mean power over each value's span. For a level
    // falling as e^(-rate t), that mean over a span from t0 lasting w:
    auto mean = [&](size_t j, double rate) {
        const double t0 = ReleaseShape::start_of(at[j]), w = ReleaseShape::span(at[j]);
        return float((std::exp(-rate * t0) - std::exp(-rate * (t0 + w))) / (rate * w));
    };
    // Release times from 10 ms to 30 s, each 10% on; each one's power (its level squared: e^(-6 t / time)) so measured.
    std::vector<float> times;
    for (float t = 0.01f; t <= 30.0f; t *= 1.1f) times.push_back(t);
    std::vector<std::vector<float>> power(times.size(), std::vector<float>(points));
    for (size_t i = 0; i < times.size(); ++i)
        for (size_t j = 0; j < points; ++j) power[i][j] = mean(j, 6.0 / times[i]);
    auto clipped = [](float db) { return std::max(db, lowest); };
    // A model louder than the fall (a note that lingers) counts twice what one quieter does (a note cut a little short).
    auto miss = [&](size_t j, float p) {
        const float d = clipped(p > 1e-14f ? 10.0f * std::log10(p) : -140.0f) - clipped(fall[at[j]]);
        return d > 0 ? 2 * d * d : d * d;
    };
    // One stage.
    float one = 1e30f;
    for (size_t i = 0; i < times.size(); ++i) {
        float e = 0;
        for (size_t j = 0; j < points; ++j) e += miss(j, power[i][j]);
        if (e < one) { one = e; model.fast = times[i]; }
    }
    // Two: a slow stage three times the fast one's time or more, holding 3 to 40 dB under the level at the release.
    // Their levels add (the same sound twice), so the power has a term for the two together.
    float two = 1e30f, fast = 0, slow = 0, share = 0;
    std::vector<float> both(points);
    for (size_t i = 0; i + 12 < times.size(); ++i)
        for (size_t k = i + 12; k < times.size(); ++k) {
            for (size_t j = 0; j < points; ++j) both[j] = mean(j, 3.0 / times[i] + 3.0 / times[k]);
            for (int db = -3; db >= -40; --db) {
                const float s = std::pow(10.0f, db / 20.0f), a = 1 - s;
                float e = 0;
                for (size_t j = 0; j < points && e < two; ++j) e += miss(j, a * a * power[i][j] + 2 * a * s * both[j] + s * s * power[k][j]);
                if (e < two) { two = e; fast = times[i]; slow = times[k]; share = s; }
            }
        }
    if (report) {
        char text[96];
        *report = "fall=";
        for (float t : {0.0f, 0.1f, 0.2f, 0.3f, 0.5f, 1.0f, 2.0f, 4.0f}) {
            const size_t n = size_t(t / ReleaseShape::hop + 0.5f);
            if (n >= fall.size()) break;
            std::snprintf(text, sizeof(text), "%s%.1f:%.0f", t > 0 ? "," : "", double(t), double(fall[n]));
            *report += text;
        }
        std::snprintf(text, sizeof(text), " one=%.2fs/%.1fdB two=%.2f+%.2fs@%.0fdB/%.1fdB", double(model.fast), double(std::sqrt(one / float(points))),
                      double(fast), double(slow), share > 0 ? 20.0 * std::log10(double(share)) : 0.0, double(std::sqrt(two / float(points))));
        *report += text;
    }
    if (std::sqrt(one / float(points)) > 1.5f && two < 0.7f * one) { model.fast = fast; model.slow = slow; model.share = share; }
    return model;
}

} // namespace rhythmer
