// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/engine/loop_find.h"
#include <algorithm>
#include <cmath>

namespace rhythmer {

namespace {
constexpr float silence = 0.000316f; // -70 dB
constexpr float good_enough = 0.6f;  // the weakest match kept

// The rising zero crossings of `m` in [from, to).
std::vector<uint32_t> rising(const std::vector<float>& m, uint64_t from, uint64_t to) {
    std::vector<uint32_t> out;
    for (uint64_t f = std::max<uint64_t>(from, 1); f < to && f < m.size(); ++f)
        if (m[f - 1] < 0 && m[f] >= 0) out.push_back(uint32_t(f));
    return out;
}

// How alike the `w` frames before a and before b are: their correlation, normalised, 0..1.
float alike(const std::vector<float>& m, uint32_t a, uint32_t b, unsigned w) {
    double ab = 0, aa = 0, bb = 0;
    for (unsigned i = 1; i <= w; ++i) {
        double x = m[a - i], y = m[b - i];
        ab += x * y; aa += x * x; bb += y * y;
    }
    if (aa <= 0 || bb <= 0) return 0;
    return float(std::max(0.0, ab / std::sqrt(aa * bb)));
}

// The level of the `w` frames before `at`, dB.
float level_before(const std::vector<float>& m, uint32_t at, unsigned w) {
    double sum = 0;
    for (unsigned i = 1; i <= w; ++i) sum += double(m[at - i]) * m[at - i];
    return sum > 1e-14 * w ? float(10 * std::log10(sum / w)) : -140.0f;
}

// A stretch's level, and how bright it is (its slope's level against its own), in dB, from running sums.
struct Levels {
    std::vector<double> power, slope; // sums up to each frame
    explicit Levels(const std::vector<float>& m) : power(m.size() + 1, 0.0), slope(m.size() + 1, 0.0) {
        for (size_t f = 0; f < m.size(); ++f) {
            const double d = f ? double(m[f]) - m[f - 1] : 0.0;
            power[f + 1] = power[f] + double(m[f]) * m[f];
            slope[f + 1] = slope[f] + d * d;
        }
    }
    float level(size_t a, size_t b) const {
        const double sum = power[b] - power[a];
        return b > a && sum > 1e-14 * double(b - a) ? float(10 * std::log10(sum / double(b - a))) : -140.0f;
    }
    float bright(size_t a, size_t b) const {
        const double p = power[b] - power[a], d = slope[b] - slope[a];
        return p > 1e-14 && d > 1e-16 ? float(10 * std::log10(d / p)) : -140.0f;
    }
};

// The most a run of values climbs back from any low before it: nothing for one that only falls (a fading note).
float most_rise(const std::vector<float>& y) {
    float low = y.empty() ? 0.0f : y[0], rise = 0;
    for (float v : y) { low = std::min(low, v); rise = std::max(rise, v - low); }
    return rise;
}

// Whether the held sound moves: past its first half second (a quarter of it, when that is more), its level or its
// brightness, a tenth of a second at a time, climbs 1.5 dB or more back from a low. Detuned oscillators beating, an
// LFO on the level or the filter, a chorus: a short loop would freeze one moment of it. A note that only fades, at
// whatever curve, does not move.
bool moves(const Levels& of, size_t held, float rate) {
    const size_t piece = size_t(0.1f * rate), from = std::max(size_t(0.5f * rate), held / 4);
    float loudest = -140.0f;
    for (size_t at = 0; at + piece <= held; at += piece) loudest = std::max(loudest, of.level(at, at + piece));
    std::vector<float> level, bright;
    for (size_t at = from; at + piece <= held; at += piece) {
        if (of.level(at, at + piece) < loudest - 50.0f) break; // it has died away: what is left is not the sound
        level.push_back(of.level(at, at + piece)); bright.push_back(of.bright(at, at + piece));
    }
    return level.size() >= 8 && (most_rise(level) >= 1.5f || most_rise(bright) >= 1.5f);
}
} // namespace

LoopPoints find_loop(std::vector<float>& left, std::vector<float>& right, uint64_t held, float rate, bool always) {
    LoopPoints out;
    const size_t frames = std::min(left.size(), right.size());
    held = std::min<uint64_t>(held, frames);
    if (rate <= 0 || held < uint64_t(0.3f * rate)) return out;
    std::vector<float> m(static_cast<size_t>(held));
    float peak = 0;
    for (size_t f = 0; f < m.size(); ++f) { m[f] = 0.5f * (left[f] + right[f]); peak = std::max(peak, std::fabs(m[f])); }
    if (peak < silence) return out;
    const uint64_t shortest = uint64_t(0.1f * rate), longest = std::min<uint64_t>(uint64_t(1.0f * rate), held * 3 / 4);
    const unsigned window = unsigned(std::min<uint64_t>(uint64_t(0.02f * rate), shortest / 2)); // 20 ms compared
    // The end: the last rising crossing at least 10 ms before the key was let go (its release starts after).
    const uint64_t latest = held - uint64_t(0.01f * rate);
    auto ends = rising(m, latest > uint64_t(0.05f * rate) ? latest - uint64_t(0.05f * rate) : 0, latest);
    if (ends.empty()) return out;
    const uint32_t end = ends.back();
    if (end < shortest + window) return out;
    // A sound that moves: a long loop, from a place where it was in the state it is in at the end. The state: its level
    // and brightness over the 0.4 s before, a tenth of a second at a time (so where it is heading counts too). The
    // start is looked for from a fifth of the hold on (0.3 s at least), leaving a third of the hold (0.4 s at least)
    // to loop. Then the rising crossing within 20 ms of it whose wave is most like the end's, and a crossfade of up
    // to a second: straight where the two stretches are alike in wave, else equal power.
    const Levels of(m);
    const size_t piece = size_t(0.1f * rate);
    { // A note that has died away by the time its key is let go (50 dB under its loudest) has nothing to loop.
        float loudest = -140.0f;
        for (size_t at = 0; at + piece <= size_t(held); at += piece) loudest = std::max(loudest, of.level(at, at + piece));
        if (end >= piece && of.level(end - piece, end) < loudest - 50.0f) return out;
    }
    const size_t first = std::max({4 * piece, size_t(held / 5), size_t(0.3f * rate)});
    const size_t room = std::max(size_t(held / 3), size_t(0.4f * rate));
    if (moves(of, size_t(held), rate) && end >= 4 * piece && size_t(end) > first + room) {
        auto apart = [&](size_t at) {
            float sum = 0;
            for (size_t k = 0; k < 4; ++k) {
                const size_t a = at - (k + 1) * piece, b = at - k * piece, ea = end - (k + 1) * piece, eb = end - k * piece;
                sum += std::fabs(of.level(a, b) - of.level(ea, eb)) + 0.5f * std::fabs(of.bright(a, b) - of.bright(ea, eb));
            }
            return sum / 4;
        };
        const size_t hop = std::max<size_t>(1, size_t(0.01f * rate)), last = size_t(end) - room;
        float nearest = 1e9f;
        for (size_t at = first; at <= last; at += hop) nearest = std::min(nearest, apart(at));
        size_t place = first;
        if (nearest <= 1.5f) { // it comes round to where it ends (an LFO's turns): the earliest such place, whole turns
            for (size_t at = first; at <= last; at += hop) if (apart(at) <= nearest + 0.5f) { place = at; break; }
        } else { // it never does within the hold (oscillators drifting apart): the longest loop, from the place in the
                 // first fifth of the stretch most like the end; the crossfade's second carries the rest, as one more
                 // of the swells the sound makes anyway
            float best = 1e9f;
            for (size_t at = first; at <= first + (last - first) / 5; at += hop) {
                const float d = apart(at);
                if (d < best) { best = d; place = at; }
            }
        }
        uint32_t start = uint32_t(place);
        float match = 0;
        const size_t reach = size_t(0.02f * rate);
        for (uint32_t c : rising(m, place > reach + window ? place - reach : window, place + reach)) {
            const float a = alike(m, c, end, window);
            if (a >= match) { match = a; start = c; }
        }
        const unsigned fade = unsigned(std::min<uint64_t>({uint64_t((end - start) / 3), uint64_t(rate), start}));
        const bool straight = fade > 0 && alike(m, start, end, std::min<unsigned>(fade, unsigned(0.2f * rate))) >= 0.5f;
        for (unsigned i = 0; i < fade; ++i) {
            const float t = float(i + 1) / float(fade + 1);
            const float keep = straight ? 1 - t : std::cos(t * 1.5707963f), take = straight ? t : std::sin(t * 1.5707963f);
            const size_t at = end - fade + i, from = start - fade + i;
            left[at] = keep * left[at] + take * left[from];
            right[at] = keep * right[at] + take * right[from];
        }
        out.start = start;
        out.end = end;
        out.match = match;
        out.moving = true;
        return out;
    }
    const uint64_t earliest = std::max<uint64_t>({end > longest ? end - longest : 0, held / 4, uint64_t(window) * 2});
    auto starts = rising(m, earliest, end - shortest);
    // Many crossings (noise): every one compared would be slow; an even spread of 400 is enough.
    const size_t stride = std::max<size_t>(1, starts.size() / 400);
    // Alike in shape and in level: a note fading as it is held is joined where the two places are nearest in level
    // (a shorter loop), each dB apart counting as much as 0.1 of the match.
    const float end_db = level_before(m, end, window);
    float best = -1, best_shape = 0;
    uint32_t start = 0;
    for (size_t i = 0; i < starts.size(); i += stride) {
        const float shape = alike(m, starts[i], end, window);
        const float score = shape - 0.1f * std::fabs(level_before(m, starts[i], window) - end_db);
        if (score > best) { best = score; best_shape = shape; start = starts[i]; }
    }
    // Nothing alike enough: none, or (always) the long loop, its join smeared rather than matched.
    bool smeared = false;
    if (best_shape < good_enough || !start) {
        if (!always) return out;
        auto from = rising(m, std::max<uint64_t>(held / 3, uint64_t(window) * 2), end - shortest);
        if (from.empty()) return out;
        start = from.front();
        smeared = true;
    }
    // What it faded by over the loop, made up: a gain rising across the loop to that much at its end, and the frames
    // after it (the release) raised the same, so the loop goes round at one level and nothing after it jumps.
    const float faded = std::clamp(level_before(m, start, window) - end_db, 0.0f, 6.0f);
    if (faded > 0.05f) {
        const double span = double(end - start), top = std::pow(10.0, faded / 20.0);
        for (size_t f = start; f < frames; ++f) {
            const double g = f < end ? std::pow(10.0, faded * (double(f - start) / span) / 20.0) : top;
            left[f] = float(left[f] * g); right[f] = float(right[f] * g);
            if (f < m.size()) m[f] = float(m[f] * g);
        }
    }
    // The crossfade: the frames before the end turn into those before the start. Matched: up to 50 ms, straight.
    // Smeared: up to half a second, equal power (the two places are unlike, so their powers add, not their waves).
    const unsigned fade = unsigned(std::min<uint64_t>({uint64_t((end - start) / (smeared ? 3 : 4)), uint64_t((smeared ? 0.5f : 0.05f) * rate), start}));
    for (unsigned i = 0; i < fade; ++i) {
        const float t = float(i + 1) / float(fade + 1);
        const float keep = smeared ? std::cos(t * 1.5707963f) : 1 - t, take = smeared ? std::sin(t * 1.5707963f) : t;
        const size_t at = end - fade + i, from = start - fade + i;
        left[at] = keep * left[at] + take * left[from];
        right[at] = keep * right[at] + take * right[from];
    }
    out.start = start;
    out.end = end;
    out.match = best_shape;
    return out;
}

bool sound_moves(const std::vector<float>& sound, uint64_t held, float rate) {
    held = std::min<uint64_t>(held, sound.size());
    if (rate <= 0 || !held) return false;
    const std::vector<float> m(sound.begin(), sound.begin() + long(held));
    return moves(Levels(m), size_t(held), rate);
}

} // namespace rhythmer
