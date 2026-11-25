// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/engine/auto_probe.h"
#include <algorithm>
#include <cmath>

namespace rhythmer {

namespace {
constexpr float pi = 3.14159265358979f;
float semitones(float hz, float to) { return 12.0f * std::log2(hz / to); }
size_t span(const std::vector<float>& x, size_t from, size_t n) { return from >= x.size() ? 0 : std::min(n, x.size() - from); }
} // namespace

float probe_pitch(const std::vector<float>& x, size_t from, size_t n, float rate) {
    n = span(x, from, n);
    const size_t low = size_t(rate / 2000.0f), high = std::min(size_t(rate / 30.0f), n / 2);
    if (n < 64 || high <= low + 2) return 0;
    const float* s = x.data() + from;
    // The normalized square difference (McLeod): 1 where the sound repeats exactly after `lag`.
    std::vector<float> nsdf(high + 2, 0.0f);
    for (size_t lag = low; lag <= high + 1 && lag < n; ++lag) {
        double r = 0, m = 0;
        for (size_t i = 0; i + lag < n; ++i) { r += double(s[i]) * s[i + lag]; m += double(s[i]) * s[i] + double(s[i + lag]) * s[i + lag]; }
        nsdf[lag] = m > 1e-12 ? float(2 * r / m) : 0.0f;
    }
    float best = 0;
    for (size_t lag = low + 1; lag <= high; ++lag) best = std::max(best, nsdf[lag]);
    if (best < 0.8f) return 0; // nothing that repeats clearly enough: a noise, a drum, silence
    // The first peak near the best: the period, not a multiple of it.
    for (size_t lag = low + 1; lag <= high; ++lag) {
        if (nsdf[lag] < 0.9f * best || nsdf[lag] < nsdf[lag - 1] || nsdf[lag] < nsdf[lag + 1]) continue;
        const float a = nsdf[lag - 1], b = nsdf[lag], c = nsdf[lag + 1];
        const float d = a - 2 * b + c, shift = std::fabs(d) > 1e-9f ? 0.5f * (a - c) / d : 0.0f;
        return rate / (float(lag) + std::clamp(shift, -0.5f, 0.5f));
    }
    return 0;
}

float probe_db(const std::vector<float>& x, size_t from, size_t n) {
    n = span(x, from, n);
    double sum = 0;
    for (size_t i = 0; i < n; ++i) sum += double(x[from + i]) * x[from + i];
    return n && sum > 1e-14 * n ? float(10.0 * std::log10(sum / double(n))) : -120.0f;
}

float probe_brightness(const std::vector<float>& x, size_t from, size_t n, float rate) {
    n = span(x, from, n);
    double level = 0, slope = 0;
    for (size_t i = 1; i < n; ++i) {
        const double d = double(x[from + i]) - x[from + i - 1];
        level += double(x[from + i]) * x[from + i]; slope += d * d;
    }
    return level > 1e-14 ? float(std::sqrt(slope / level) * rate / (2 * pi)) : 0.0f;
}

float probe_tone_db(const std::vector<float>& x, size_t from, size_t n, float hz, float rate) {
    n = span(x, from, n);
    if (!n || hz <= 0) return -120.0f;
    double power = 0;
    for (int h = 1; h <= 2; ++h) { // the fundamental and its octave (a fifth above has neither)
        const double w = 2 * pi * hz * h / rate, k = 2 * std::cos(w);
        double s1 = 0, s2 = 0;
        for (size_t i = 0; i < n; ++i) {
            const double window = 0.5 - 0.5 * std::cos(2 * pi * double(i) / double(n)); // Hann: a neighbour's leak kept out
            const double s0 = x[from + i] * window + k * s1 - s2;
            s2 = s1; s1 = s0;
        }
        power += s1 * s1 + s2 * s2 - k * s1 * s2;
    }
    return power > 1e-14 ? float(10.0 * std::log10(power / (double(n) * n))) : -120.0f;
}

ProbeGlide probe_glide(const std::vector<float>& x, const std::vector<float>& alone, size_t onset, float from_hz, float to_hz, float rate) {
    ProbeGlide g;
    if (from_hz <= 0 || to_hz <= 0 || onset >= x.size()) return g;
    const size_t window = size_t(0.03f * rate), hop = size_t(0.004f * rate);
    // Soon after the second key: mono when the first key's pitch is gone, where alone it still sounds (a poly
    // instrument plays it on under the second, however it decays).
    // Over a stretch long enough to tell the two keys' tones apart (a Hann window's reach is two over its length:
    // low keys lie close in frequency, C2 and G2 33 Hz, and the second would read as the first still sounding):
    // 60 ms, or six periods of the difference between them, 0.3 s at most.
    const float apart = std::fabs(to_hz - from_hz);
    const size_t at = onset + size_t(0.15f * rate), n = size_t(std::clamp(apart > 0 ? 6.0f / apart : 0.06f, 0.06f, 0.3f) * rate);
    const float kept = probe_tone_db(alone, at, n, from_hz, rate), heard = probe_tone_db(x, at, n, from_hz, rate);
    g.mono = kept > -90 && heard < kept - 10;
    if (!g.mono) return g;
    // The pitch from the second key on: the time it takes to be at the second key's, and stay there.
    size_t there = 0;
    unsigned steady = 0;
    for (size_t at = onset; at + window < x.size() && at < onset + size_t(2.0f * rate); at += hop) {
        const float hz = probe_pitch(x, at, window, rate);
        const bool near = hz > 0 && std::fabs(semitones(hz, to_hz)) < 0.4f;
        if (near) { if (!steady++) there = at; }
        else steady = 0;
        if (steady >= 4) break;
    }
    if (steady < 4) return g; // never settled: no time to give
    // The window's middle is where its pitch was; a jump reads there in the first window (its middle, 15 ms on).
    const float seconds = float(there - onset + window / 2) / rate;
    g.seconds = seconds > 0.025f ? seconds : 0.0f;
    return g;
}

bool probe_slides_apart(const std::vector<float>& x, size_t onset, float from_hz, float to_hz, float rate) {
    if (from_hz <= 0 || to_hz <= 0) return false;
    const float hz = probe_pitch(x, onset + size_t(0.005f * rate), size_t(0.025f * rate), rate);
    return hz > 0 && std::fabs(semitones(hz, to_hz)) > 1.0f && std::fabs(semitones(hz, from_hz)) < std::fabs(semitones(hz, to_hz));
}

int probe_retriggers(const std::vector<float>& both, const std::vector<float>& alone, const std::vector<float>& fresh, size_t onset, float rate) {
    const size_t win = size_t(0.04f * rate), hop = size_t(0.02f * rate), count = 14; // to 0.3 s
    const size_t reach = (count - 1) * hop + win;
    if (!win || both.size() < onset + reach || alone.size() < onset + reach || fresh.size() < reach) return -1;
    // Level in dB and brightness as dB (a doubling 6): nothing told of the brightness of a window near silence.
    struct Heard { float db, bright; };
    auto heard = [&](const std::vector<float>& x, size_t from) {
        const float db = probe_db(x, from, win), hz = db > -80.0f ? probe_brightness(x, from, win, rate) : 0.0f;
        return Heard {std::max(db, -100.0f), hz > 0 ? 20.0f * std::log10(hz) : 0.0f};
    };
    auto apart = [](const Heard& a, const Heard& b) { return Heard {a.db - b.db, a.bright != 0 && b.bright != 0 ? a.bright - b.bright : 0.0f}; };
    // What the second key itself changes against the first, each from its start.
    Heard key {0, 0};
    for (size_t i = 0; i < count; ++i) {
        const Heard d = apart(heard(fresh, i * hop), heard(alone, i * hop));
        key.db += d.db / float(count); key.bright += d.bright / float(count);
    }
    // How far the held note has moved on from its start, and how much of that the legato note takes back.
    double moved = 0, back = 0;
    for (size_t i = 0; i < count; ++i) {
        const Heard start = heard(alone, i * hop), later = heard(alone, onset + i * hop), legato = heard(both, onset + i * hop);
        const Heard on = apart(start, later);
        Heard jump = apart(legato, later);
        jump.db -= key.db;
        if (jump.bright != 0) jump.bright -= key.bright;
        moved += double(on.db) * on.db + double(on.bright) * on.bright;
        back += double(jump.db) * on.db + double(jump.bright) * on.bright;
    }
    if (moved / double(count) < 4.0) return -1;
    return back > 0.5 * moved ? 1 : 0;
}

ProbeCharacter probe_character(const std::vector<float>& x, float hz, float rate) {
    ProbeCharacter c;
    const size_t early = size_t(0.02f * rate), span_of = size_t(0.2f * rate);
    if (x.size() < size_t(1.15f * rate) || hz <= 0) return c;
    c.level = std::max(probe_db(x, early, span_of), -100.0f);
    c.fade = c.level - std::max(probe_db(x, size_t(0.925f * rate), size_t(0.225f * rate)), -100.0f);
    const float bright = c.level > -80.0f ? probe_brightness(x, early, span_of, rate) : 0.0f;
    c.bright = bright > 0 ? 20.0f * std::log10(bright / hz) : 0.0f;
    return c;
}

float probe_apart(const ProbeCharacter& a, const ProbeCharacter& b) {
    return std::fabs(a.level - b.level) + std::fabs(a.fade - b.fade) + std::fabs(a.bright - b.bright);
}

float probe_velocity(const std::vector<uint8_t>& velocities, const std::vector<float>& db) {
    if (velocities.size() < 2 || velocities.size() != db.size()) return 1.0f;
    size_t top = 0;
    for (size_t i = 1; i < velocities.size(); ++i) if (velocities[i] > velocities[top]) top = i;
    if (db[top] < -90.0f) return 1.0f;
    // Least squares of level = 1 - amount x (1 - velocity / 127), against the loudest velocity's.
    double with = 0, over = 0;
    for (size_t i = 0; i < velocities.size(); ++i) {
        if (i == top) continue;
        const double level = std::pow(10.0, double(db[i] - db[top]) / 20.0), short_of = 1.0 - velocities[i] / 127.0;
        with += (1.0 - level) * short_of;
        over += short_of * short_of;
    }
    if (over <= 0) return 1.0f;
    // Under a fifth (2 dB from the softest press to the hardest) it is the sound's own wobble from hit to hit, not
    // velocity: none.
    const double amount = std::clamp(with / over, 0.0, 1.0);
    return amount < 0.2 ? 0.0f : float(amount);
}

bool probe_sustains(const std::vector<float>& x, size_t from, size_t to, float rate, float most) {
    to = std::min(to, x.size());
    if (to <= from + size_t(0.4f * rate)) return false;
    const size_t quarter = (to - from) / 4;
    const float start = probe_db(x, from, quarter), end = probe_db(x, to - quarter, quarter);
    return end > -80.0f && start - end < most;
}

float probe_fade_seconds(const std::vector<float>& x, float rate) {
    const size_t piece = size_t(0.1f * rate);
    if (!piece || x.size() < piece) return float(x.size()) / std::max(rate, 1.0f);
    std::vector<float> level;
    for (size_t at = 0; at + piece <= x.size(); at += piece) level.push_back(probe_db(x, at, piece));
    const float floor = *std::max_element(level.begin(), level.end()) - 45.0f;
    size_t last = level.size(); // the last piece still over the floor, then one more
    while (last > 0 && level[last - 1] < floor) --last;
    return float(std::min(last + 1, level.size()) * piece) / rate;
}

uint8_t probe_accent(const std::vector<uint8_t>& velocities, const std::vector<float>& db, const std::vector<float>& bright) {
    const size_t n = std::min({velocities.size(), db.size(), bright.size()});
    if (n < 4) return 0;
    // How far each step up moves the sound: its level, and its brightness either way (an accent can open the filter
    // wider and so sound darker in its first moments), a semitone of brightness half a dB.
    std::vector<float> step;
    for (size_t i = 0; i + 1 < n; ++i) {
        const float louder = std::fabs(db[i + 1] - db[i]);
        const float brighter = bright[i] > 0 && bright[i + 1] > 0 ? std::fabs(semitones(bright[i + 1], bright[i])) : 0.0f;
        step.push_back(louder + 0.5f * brighter);
    }
    const size_t at = size_t(std::max_element(step.begin(), step.end()) - step.begin());
    std::vector<float> others;
    for (size_t i = 0; i < step.size(); ++i) if (i != at) others.push_back(step[i]);
    std::nth_element(others.begin(), others.begin() + others.size() / 2, others.end());
    const float usual = others[others.size() / 2];
    // A jump of its own: well over the usual step and over the steps either side (a velocity curve's first steps are
    // big too, but so are their neighbours).
    const float beside = std::max(at > 0 ? step[at - 1] : 0.0f, at + 1 < step.size() ? step[at + 1] : 0.0f);
    if (!(step[at] >= 1.0f && step[at] >= 4.0f * std::max(usual, 0.25f) && step[at] >= 3.0f * std::max(beside, 0.25f))) return 0;
    // And it stays: the velocities over it are, on average, that far from those under it. A sound that differs a
    // little from hit to hit (a random phase, a free LFO) has a big step somewhere and is back the hit after.
    auto mean = [&](const std::vector<float>& v, size_t from, size_t to) {
        double sum = 0;
        unsigned counted = 0;
        for (size_t i = from; i < to; ++i) if (v[i] > 0 || &v == &db) { sum += v[i]; ++counted; }
        return counted ? float(sum / counted) : 0.0f;
    };
    const float under_db = mean(db, 0, at + 1), over_db = mean(db, at + 1, n);
    const float under_bright = mean(bright, 0, at + 1), over_bright = mean(bright, at + 1, n);
    const float shift = std::fabs(over_db - under_db)
                      + (under_bright > 0 && over_bright > 0 ? 0.5f * std::fabs(semitones(over_bright, under_bright)) : 0.0f);
    return shift >= 0.6f * step[at] ? velocities[at + 1] : 0;
}

std::vector<uint8_t> probe_layers(const std::vector<uint8_t>& velocities, const std::vector<float>& db, const std::vector<float>& bright,
                                  float amount, uint8_t accent, unsigned count) {
    constexpr unsigned most = 8;
    const size_t n = std::min({velocities.size(), db.size(), bright.size()});
    if (n == 0 || count == 1) { // nothing heard: evenly; one layer: the hardest hit
        const unsigned k = std::clamp(count ? count : 1u, 1u, most);
        std::vector<uint8_t> tops;
        for (unsigned l = 0; l < k; ++l) tops.push_back(uint8_t(std::clamp<unsigned>((l + 1) * 127 / k, 1, 127)));
        return tops;
    }
    // How far hit i is from the sample of hit t played at hit i's velocity. Under -70 dB is silence, however far under.
    auto level = [&](unsigned v) { return std::max(1e-3, 1.0 - std::clamp(double(amount), 0.0, 1.0) * (1.0 - v / 127.0)); };
    auto loud = [&](size_t i) { return std::max(double(db[i]), -70.0); };
    auto apart = [&](size_t i, size_t t) {
        double d = std::fabs(loud(i) - std::max(-70.0, loud(t) + 20.0 * std::log10(level(velocities[i]) / level(velocities[t]))));
        if (bright[i] > 0 && bright[t] > 0) d += 0.5 * std::fabs(semitones(bright[i], bright[t]));
        return d;
    };
    constexpr double never = 1e30;
    // Hits a..b as one layer, its sample hit b: the sum of squares, or never across the accent. Where hits sound
    // alike (a sound whose level alone follows velocity) a hair on the layer's width squared keeps the layers even.
    auto cost = [&](size_t a, size_t b) {
        if (accent && velocities[a] < accent && velocities[b] >= accent) return never;
        double sum = 0;
        for (size_t i = a; i <= b; ++i) sum += apart(i, b) * apart(i, b);
        const double wide = double(velocities[b]) - (a ? velocities[a - 1] : 0);
        return sum + 1e-7 * wide * wide;
    };
    // The last hit of each of k layers, least squares; `far`: the hit furthest from its layer's sample. Empty when
    // k layers cannot keep the accent apart.
    auto place = [&](unsigned k, double& far) {
        std::vector<std::vector<double>> best(k + 1, std::vector<double>(n, never));
        std::vector<std::vector<size_t>> from(k + 1, std::vector<size_t>(n, 0));
        for (size_t b = 0; b < n; ++b) best[1][b] = cost(0, b);
        for (unsigned j = 2; j <= k; ++j)
            for (size_t b = j - 1; b < n; ++b)
                for (size_t a = j - 1; a <= b; ++a) {
                    const double c = best[j - 1][a - 1] + cost(a, b);
                    if (c < best[j][b]) { best[j][b] = c; from[j][b] = a; }
                }
        std::vector<size_t> ends;
        far = 0;
        if (best[k][n - 1] >= never) return ends;
        size_t b = n - 1;
        for (unsigned j = k; j >= 1; --j) {
            const size_t a = j == 1 ? 0 : from[j][b];
            ends.insert(ends.begin(), b);
            for (size_t i = a; i <= b; ++i) far = std::max(far, apart(i, b));
            if (j > 1) b = a - 1;
        }
        return ends;
    };
    const unsigned top = unsigned(std::min<size_t>(most, n));
    std::vector<size_t> ends;
    double far = 0;
    if (count) ends = place(std::min(count, top), far);
    else
        for (unsigned k = 1; k <= top; ++k) { // as few as hold every hit within 1.5 dB
            auto made = place(k, far);
            if (made.empty()) continue;
            ends = std::move(made);
            if (far <= 1.5) break;
        }
    if (ends.empty()) ends = place(top, far);
    // Each layer's top: its last hit's velocity; under an accent the velocity before it; the last 127.
    std::vector<uint8_t> tops;
    for (size_t e : ends) {
        unsigned v = velocities[e];
        if (accent && v < accent && e + 1 < n && velocities[e + 1] >= accent) v = accent - 1u;
        if (e + 1 == n) v = 127;
        if (tops.empty() || v > tops.back()) tops.push_back(uint8_t(std::clamp(v, 1u, 127u)));
    }
    if (tops.empty() || tops.back() != 127) tops.push_back(127);
    return tops;
}

bool probe_drums(const std::vector<uint8_t>& keys, const std::vector<float>& hz) {
    const size_t n = std::min(keys.size(), hz.size());
    if (n < 3) return false;
    // Each pitched key against each other: in tune with the keys between them, an octave either way allowed (a pitch
    // read an octave off).
    unsigned best = 0;
    for (size_t r = 0; r < n; ++r) {
        if (hz[r] <= 0) continue;
        unsigned follow = 0;
        for (size_t k = 0; k < n; ++k) {
            if (hz[k] <= 0) continue;
            float off = semitones(hz[k], hz[r]) - float(int(keys[k]) - int(keys[r]));
            off -= 12.0f * std::round(off / 12.0f);
            follow += std::fabs(off) < 0.7f;
        }
        best = std::max(best, follow);
    }
    return best * 2 < n + 1; // fewer than half in tune with each other
}

bool probe_varies(const std::vector<std::vector<float>>& hits, float rate) {
    if (hits.size() < 2) return false;
    // The level over 300 ms, every 10 ms, each over 50 ms: several periods even of a low note, so where its waveform
    // happens to start (an oscillator's free phase) does not move it.
    const size_t hop = size_t(0.01f * rate), span = size_t(0.05f * rate), frames = 26;
    auto envelope = [&](const std::vector<float>& x) {
        std::vector<float> e;
        for (size_t f = 0; f < frames; ++f) e.push_back(probe_db(x, f * hop, span));
        return e;
    };
    // Any two of them apart in level, or the brightest and the dullest in brightness: with more hits the answer for a
    // sound near the line is the same from one check to the next.
    std::vector<std::vector<float>> envelopes;
    float dull = 0, bright = 0;
    for (const auto& hit : hits) {
        envelopes.push_back(envelope(hit));
        const float b = probe_brightness(hit, 0, size_t(0.1f * rate), rate);
        if (b <= 0) continue;
        dull = dull > 0 ? std::min(dull, b) : b;
        bright = std::max(bright, b);
    }
    if (dull > 0 && bright / dull - 1.0f > 0.08f) return true;
    for (size_t a = 0; a < envelopes.size(); ++a)
        for (size_t b = a + 1; b < envelopes.size(); ++b) {
            double apart = 0;
            unsigned counted = 0;
            for (size_t f = 0; f < frames; ++f) {
                if (envelopes[a][f] < -40 || envelopes[b][f] < -40) continue; // the quiet ends, where noise is all there is
                apart += std::fabs(envelopes[a][f] - envelopes[b][f]);
                ++counted;
            }
            if (counted && apart / counted > 1.5) return true;
        }
    return false;
}

} // namespace rhythmer
