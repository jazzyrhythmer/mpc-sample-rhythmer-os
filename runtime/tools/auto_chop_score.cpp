// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
//
// Scores Chop's Auto type (engine/auto_chop.cpp) on sounds whose cuts are known, beside Threshold and Beats.
//
//   c++ -std=c++17 -O2 -Icore/src tools/auto_chop_score.cpp core/src/rhythmer/engine/onsets.cpp
//       core/src/rhythmer/engine/auto_chop.cpp core/src/rhythmer/engine/sample_data.cpp -o build/auto_chop_score
//   build/auto_chop_score <Samples folder> [WAV ...]
//
// The folder is an MPC Sample's factory library (/content/Samples copied off the device; it stays in build/). From its
// single kicks, snares and hats the tool builds two-bar breaks with known hit places (ghost notes, flams, swing,
// some with music over them, some with a crackly lead-in and a cut-off bar after), and rows of single sounds with
// silence between. Its Break, Loops and Phrases folders are read for their kind (from the folder and name) and tempo
// (from the name). Further WAVs given are only described.
#include "rhythmer/engine/auto_chop.h"
#include "rhythmer/engine/onsets.h"
#include "rhythmer/shell/chop.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace rhythmer;
namespace fs = std::filesystem;

namespace {

// A WAV as one side (its sides' mean) at 44.1 kHz; empty where it cannot be read.
std::vector<float> read_wav(const fs::path& path, std::string* atem = nullptr, std::string* mlch = nullptr, unsigned* rate_out = nullptr) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return {};
    auto u16 = [&](size_t i) { return uint16_t(uint8_t(d[i]) | uint8_t(d[i + 1]) << 8); };
    auto u32 = [&](size_t i) { return uint32_t(u16(i)) | uint32_t(u16(i + 2)) << 16; };
    unsigned format = 0, channels = 0, bits = 0, rate = 0;
    size_t data = 0, bytes = 0;
    for (size_t i = 12; i + 8 <= d.size();) {
        const size_t n = u32(i + 4);
        if (!std::memcmp(d.data() + i, "fmt ", 4)) {
            format = u16(i + 8); channels = u16(i + 10); rate = u32(i + 12); bits = u16(i + 22);
            if (format == 0xFFFE && n >= 26) format = u16(i + 32);
        } else if (!std::memcmp(d.data() + i, "data", 4)) { data = i + 8; bytes = std::min(n, d.size() - data); }
        else if (atem && !std::memcmp(d.data() + i, "atem", 4)) atem->assign(d.data() + i + 8, std::min(n, d.size() - i - 8));
        else if (mlch && !std::memcmp(d.data() + i, "mlch", 4)) mlch->assign(d.data() + i + 8, std::min(n, d.size() - i - 8));
        i += 8 + n + (n & 1);
    }
    if (!data || !channels || !rate || !bits) return {};
    if (rate_out) *rate_out = rate;
    const size_t step = channels * bits / 8, count = bytes / step;
    std::vector<float> own(count);
    for (size_t f = 0; f < count; ++f) {
        float sum = 0;
        for (unsigned c = 0; c < channels; ++c) {
            const size_t at = data + f * step + c * bits / 8;
            float v = 0;
            if (format == 3 && bits == 32) std::memcpy(&v, d.data() + at, 4);
            else if (bits == 16) v = int16_t(u16(at)) / 32768.0f;
            else if (bits == 24) v = float(int32_t(u32(at - 1) & 0xffffff00) >> 8) / 8388608.0f;
            else if (bits == 32) v = float(int32_t(u32(at))) / 2147483648.0f;
            else if (bits == 8) v = (uint8_t(d[at]) - 128) / 128.0f;
            sum += v;
        }
        own[f] = sum / float(channels);
    }
    if (rate == 44100) return own;
    std::vector<float> out(size_t(double(count) * 44100 / rate));
    for (size_t n = 0; n < out.size(); ++n) {
        const double at = double(n) * rate / 44100;
        const auto i = size_t(at);
        const float t = float(at - double(i));
        out[n] = i + 1 < count ? own[i] * (1 - t) + own[i + 1] * t : own[std::min(i, count - 1)];
    }
    return out;
}

// Where a single sound's attack starts: its first sample at 2% of its peak.
uint32_t onset_of(const std::vector<float>& x) {
    float peak = 0;
    for (float v : x) peak = std::max(peak, std::fabs(v));
    for (size_t n = 0; n < x.size(); ++n) if (std::fabs(x[n]) >= 0.02f * peak) return uint32_t(n);
    return 0;
}

struct Shot { std::vector<float> x; uint32_t onset = 0; std::string name; };
std::vector<Shot> shots_in(const fs::path& folder, const char* must = nullptr, const char* not_in = nullptr) {
    std::vector<Shot> out;
    if (!fs::is_directory(folder)) return out;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(folder)) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& p : files) {
        const std::string name = p.filename().string();
        if (must && name.find(must) == std::string::npos) continue;
        if (not_in && name.find(not_in) != std::string::npos) continue;
        auto x = read_wav(p);
        if (x.size() < 441 || x.size() > 44100 * 4) continue;
        out.push_back({std::move(x), 0, name});
        out.back().onset = onset_of(out.back().x);
    }
    return out;
}

void mix(std::vector<float>& into, const std::vector<float>& x, double at, float gain) {
    const auto start = int64_t(std::llround(at));
    for (size_t n = 0; n < x.size(); ++n) {
        const int64_t i = start + int64_t(n);
        if (i >= 0 && i < int64_t(into.size())) into[size_t(i)] += x[n] * gain;
    }
}

struct Known {
    std::string what;
    std::vector<float> mono;       // its sides summed, as the hits are found in
    std::vector<uint32_t> cuts;    // where the cuts belong (not the region's start)
    std::string cut_kind;          // breaks: what each cut is (K, S, F a flam, h, o)
    std::vector<Span> spans;       // SHOTS: each sound
    SoundKind kind = SoundKind::None;
    uint32_t bar_from = 0, bar_to = 0;
    double bpm = 0;
};

// Two bars of sixteenths: kick, snare and hat lanes. Snare lane: x a hit, g a ghost note, F a hit with a flam
// before it. Hat lane: x closed, o open.
struct Pattern { const char *kick, *snare, *hat; };
const Pattern patterns[] {
    {"x.........x.......x.x.......x...", "....x..g.x..x..g....x..g.F..x...", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.xo"},
    {"x......x..x.....x......x.x......", "....x.......x.......x..g....x...", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.o..."},
    {"x.x.......xx....x.x.......x.....", "....x..x.x..x..x....x..x.x..F..x", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x."},
    {"x......x...x....x..x......x.....", "........x...............x.......", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"},
    {"x..x..x...x.....x..x..x...x..x..", "....F.......x.g.....x.......x...", "x.x.o.x.x.x.o.x.x.x.o.x.x.x.o.x."},
};

Known make_break(std::mt19937& rng, const std::vector<Shot>& kicks, const std::vector<Shot>& snares,
                 const std::vector<Shot>& hats, const std::vector<Shot>& opens, const std::vector<std::vector<float>>& beds,
                 bool with_bed, bool with_edges) {
    std::uniform_real_distribution<double> u(0, 1);
    auto any = [&](const auto& v) -> const auto& { return v[size_t(u(rng) * double(v.size())) % v.size()]; };
    Known k;
    k.kind = SoundKind::Drums;
    k.bpm = 80 + u(rng) * 80;
    const double beat = 44100 * 60 / k.bpm, sixteenth = beat / 4, swing = u(rng) < 0.5 ? 0 : u(rng) * 0.3;
    const double lead = with_edges ? (0.25 + u(rng) * 0.6) * 44100 : 0;
    const unsigned extra = with_edges ? 2 + unsigned(u(rng) * 5) : 0; // sixteenths of a third bar, cut off
    const auto& p = patterns[size_t(u(rng) * std::size(patterns)) % std::size(patterns)];
    const Shot &kick = any(kicks), &snare = any(snares), &hat = any(hats), &open = any(opens);
    const size_t length = size_t(lead + (32 + extra) * sixteenth + (extra ? 0 : 0.35 * 44100));
    std::vector<float> m(length, 0.0f);
    std::vector<uint32_t> truth;
    std::map<uint32_t, char> kinds_at;
    char kind_now = 'K';
    auto place = [&](const Shot& s, unsigned step, float gain, bool counts, double early = 0) {
        double at = lead + step * sixteenth + (step % 2 ? swing * sixteenth : 0) + (u(rng) - 0.5) * 0.008 * 44100 - early;
        mix(m, s.x, at - s.onset, gain);
        if (counts) truth.push_back(uint32_t(std::max(0.0, at)));
        if (counts) kinds_at[uint32_t(std::max(0.0, at))] = kind_now;
    };
    for (unsigned step = 0; step < 32 + extra; ++step) {
        const unsigned i = step % 32;
        if (p.kick[i] == 'x') { kind_now = 'K'; place(kick, step, 0.9f + 0.1f * float(u(rng)), true); }
        if (p.snare[i] == 'x') { kind_now = 'S'; place(snare, step, 0.8f + 0.1f * float(u(rng)), true); }
        if (p.snare[i] == 'g') place(snare, step, 0.12f + 0.08f * float(u(rng)), false);
        if (p.snare[i] == 'F') { kind_now = 'F'; place(snare, step, 0.35f, true, 0.025 * 44100); place(snare, step, 0.85f, false); }
        if (p.hat[i] == 'x') { kind_now = 'h'; place(hat, step, 0.3f + 0.15f * float(u(rng)), true); }
        if (p.hat[i] == 'o') { kind_now = 'o'; place(open, step, 0.4f + 0.1f * float(u(rng)), true); }
    }
    if (with_edges) { // a crackly lead-in, and the third bar cut off
        for (size_t n = 0; n < size_t(lead); ++n) m[n] += float((u(rng) - 0.5) * 0.002);
        for (double t = 0.01 * 44100; t < lead - 0.05 * 44100; t += (0.02 + u(rng) * 0.12) * 44100) m[size_t(t)] += float((u(rng) - 0.5) * 0.3);
        for (size_t n = 0; n < m.size(); ++n) m[n] += float((u(rng) - 0.5) * 0.002);
        const size_t fade = 220;
        for (size_t n = 0; n < fade && n < m.size(); ++n) m[m.size() - 1 - n] *= float(n) / fade;
    }
    if (with_bed && !beds.empty()) {
        const auto& bed = any(beds);
        const float gain = 0.25f + 0.2f * float(u(rng));
        for (size_t n = size_t(lead); n < m.size(); ++n) m[n] += bed[(n - size_t(lead)) % bed.size()] * gain;
    }
    std::sort(truth.begin(), truth.end());
    for (uint32_t t : truth) {
        if (t >= m.size()) continue;
        if (t < 882) continue; // on the region's start: no cut there
        if (!k.cuts.empty() && t - k.cuts.back() < 1323) { // hits together: one cut, named by the strongest
            if (std::string("hoFSK").find(kinds_at[t]) > std::string("hoFSK").find(k.cut_kind.back())) k.cut_kind.back() = kinds_at[t];
            continue;
        }
        k.cuts.push_back(t);
        k.cut_kind.push_back(kinds_at[t]);
    }
    k.bar_from = uint32_t(lead);
    k.bar_to = uint32_t(lead + 32 * sixteenth);
    k.mono.resize(m.size());
    for (size_t n = 0; n < m.size(); ++n) k.mono[n] = 2 * m[n];
    char what[64];
    std::snprintf(what, sizeof(what), "break %.0f BPM%s%s", k.bpm, with_bed ? " +music" : "", with_edges ? " +edges" : "");
    k.what = what;
    return k;
}

Known make_row(std::mt19937& rng, const std::vector<const std::vector<Shot>*>& kinds) {
    std::uniform_real_distribution<double> u(0, 1);
    Known k;
    k.kind = SoundKind::Shots;
    const unsigned count = 4 + unsigned(u(rng) * 12);
    std::vector<float> m;
    for (unsigned n = 0; n < count; ++n) {
        const auto& set = *kinds[size_t(u(rng) * double(kinds.size())) % kinds.size()];
        const auto& s = set[size_t(u(rng) * double(set.size())) % set.size()];
        const size_t at = m.size() + size_t((0.06 + u(rng) * 0.3) * 44100);
        const float gain = 0.4f + 0.6f * float(u(rng));
        m.resize(at + s.x.size() - s.onset, 0.0f);
        for (size_t i = s.onset; i < s.x.size(); ++i) m[at + i - s.onset] += s.x[i] * gain;
        k.spans.push_back({uint32_t(at), uint32_t(m.size())});
        k.cuts.push_back(uint32_t(at));
    }
    m.resize(m.size() + 4410, 0.0f);
    k.mono.resize(m.size());
    for (size_t n = 0; n < m.size(); ++n) k.mono[n] = 2 * m[n];
    k.what = "row of " + std::to_string(count);
    return k;
}

// A single sound played `semitones` higher (read faster), as a sampler does.
std::vector<float> shifted(const std::vector<float>& x, double semitones) {
    const double rate = std::pow(2.0, semitones / 12);
    std::vector<float> out(size_t(double(x.size()) / rate));
    for (size_t n = 0; n < out.size(); ++n) {
        const double at = double(n) * rate;
        const auto i = size_t(at);
        const float t = float(at - double(i));
        out[n] = i + 1 < x.size() ? x[i] * (1 - t) + x[i + 1] * t : x[std::min(i, x.size() - 1)];
    }
    return out;
}

// A loop of four chords, two beats each (I, IV, V, vi or another of a few), the chord played in eighths or held,
// a bass note at each change; the cuts belong where the chord changes.
Known make_chords(std::mt19937& rng, const std::vector<Shot>& chords, const std::vector<Shot>& basses, bool held) {
    std::uniform_real_distribution<double> u(0, 1);
    static const int progressions[][4] {{0, 5, 7, -3}, {0, -3, 5, 7}, {-3, 5, 0, 7}, {0, 3, 5, -2}, {2, 7, 0, 5}};
    const auto& prog = progressions[size_t(u(rng) * std::size(progressions)) % std::size(progressions)];
    const Shot& chord = chords[size_t(u(rng) * double(chords.size())) % chords.size()];
    const Shot& bass = basses[size_t(u(rng) * double(basses.size())) % basses.size()];
    Known k;
    k.kind = SoundKind::Music;
    k.bpm = 80 + u(rng) * 50;
    const double beat = 44100 * 60 / k.bpm;
    std::vector<float> m(size_t(8 * beat), 0.0f);
    for (int c = 0; c < 4; ++c) {
        const auto up = shifted(chord.x, prog[c]), low = shifted(bass.x, prog[c] - 12);
        const double at = c * 2 * beat;
        for (int e = 0; e < (held ? 1 : 4); ++e) {
            const double t = at + e * beat / 2;
            const size_t until = size_t(held ? 2 * beat : beat / 2);
            for (size_t n = 0; n < std::min(until, up.size() - std::min(up.size(), size_t(chord.onset))); ++n) {
                const size_t i = size_t(t) + n;
                if (i < m.size()) m[i] += up[chord.onset + n] * (e ? 0.5f : 0.6f) * float(std::min<size_t>(1, until - n > 220 ? 1 : 0) + (until - n <= 220 ? (until - n) / 220.0 : 0));
            }
        }
        for (size_t n = 0; n < std::min(size_t(2 * beat), low.size() - std::min(low.size(), size_t(bass.onset))); ++n)
            if (size_t(at) + n < m.size()) m[size_t(at) + n] += low[bass.onset + n] * 0.5f;
        if (c) k.cuts.push_back(uint32_t(at));
    }
    k.mono.resize(m.size());
    for (size_t n = 0; n < m.size(); ++n) k.mono[n] = 2 * m[n];
    char what[64];
    std::snprintf(what, sizeof(what), "chords %.0f BPM%s", k.bpm, held ? " held" : "");
    k.what = what;
    return k;
}

// How many of `got` lie within `reach` of one of `want` (each matched once).
unsigned matched(const std::vector<uint32_t>& got, const std::vector<uint32_t>& want, uint32_t reach = 882) {
    std::vector<bool> used(want.size(), false);
    unsigned m = 0;
    for (uint32_t g : got) {
        int best = -1;
        uint32_t gap = reach + 1;
        for (size_t i = 0; i < want.size(); ++i) {
            const uint32_t d = g > want[i] ? g - want[i] : want[i] - g;
            if (!used[i] && d <= reach && d < gap) { gap = d; best = int(i); }
        }
        if (best >= 0) { used[size_t(best)] = true; ++m; }
    }
    return m;
}
std::vector<uint32_t> cuts_of(const Slices& s) {
    std::vector<uint32_t> c;
    for (unsigned n = 1; n < s.count; ++n) c.push_back(s.start[n]);
    return c;
}

// Slices' lengths against a floor: the shortest, and how many fall under 80% of it.
struct Lengths {
    double shortest = 1e18;
    unsigned under = 0, all = 0;
    void add(const Slices& s, uint32_t frames, uint32_t floor) {
        for (unsigned n = 0; n < s.count; ++n) {
            const double len = double(s.end(n, frames) - s.start[n]);
            shortest = std::min(shortest, len);
            under += len < 0.8 * floor;
            ++all;
        }
    }
    std::string text() const {
        char t[96];
        std::snprintf(t, sizeof(t), "shortest %.0f ms, %u of %u under 80%% of the floor", all ? shortest / 44.1 : 0.0, under, all);
        return t;
    }
};

struct Score {
    double got = 0, want = 0, hit = 0;
    void add(const std::vector<uint32_t>& g, const std::vector<uint32_t>& w) { got += double(g.size()); want += double(w.size()); hit += matched(g, w); }
    std::string text() const {
        const double p = got ? hit / got : 0, r = want ? hit / want : 0, f = p + r > 0 ? 2 * p * r / (p + r) : 0;
        char t[64];
        std::snprintf(t, sizeof(t), "P %.2f R %.2f F %.2f", p, r, f);
        return t;
    }
};

struct Read {
    std::array<Hit, max_hits> hits {};
    unsigned count = 0;
    BeatGrid beats;
    AutoRead autos;
};
Read read_of(const std::vector<float>& mono) {
    Read r;
    r.count = find_hits_in(mono, r.hits, r.beats, &r.autos);
    return r;
}

double name_bpm(const std::string& name) { // "Break-KitA10 92 SLab.wav", "BassLp-Bay Am100 PZ.WAV": the last number
    double bpm = 0;
    for (size_t i = 0; i < name.size(); ++i)
        if (std::isdigit(uint8_t(name[i])) && (i == 0 || !std::isdigit(uint8_t(name[i - 1])))) {
            const double v = std::atof(name.c_str() + i);
            if (v >= 50 && v <= 200 && name[i - 1] != '-' && !std::isalpha(uint8_t(name[i + std::to_string(int(v)).size()]))) bpm = v;
        }
    return bpm;
}

std::map<std::string, std::map<std::string, int>> kinds_by;

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: auto_chop_score <Samples folder> [WAV ...]\n"); return 2; }
    const fs::path root = argv[1];
    std::mt19937 rng(7);
    const auto kicks = shots_in(root / "Kicks"), snares = shots_in(root / "Snares");
    const auto hats = shots_in(root / "Hi-Hats", nullptr, "Open"), opens_found = shots_in(root / "Hi-Hats", "Open");
    const auto claps = shots_in(root / "Claps"), toms = shots_in(root / "Toms"), percs = shots_in(root / "Perc");
    const auto& opens = opens_found.empty() ? hats : opens_found;
    std::printf("one shots: %zu kicks, %zu snares, %zu hats, %zu open hats, %zu claps, %zu toms, %zu perc\n", kicks.size(),
                snares.size(), hats.size(), opens_found.size(), claps.size(), toms.size(), percs.size());
    if (kicks.empty() || snares.empty() || hats.empty()) return 1;
    std::vector<std::vector<float>> beds;
    for (const char* prefix : {"KeysLp", "PianoLp", "StrgLp", "PadLp", "SynthLp"})
        for (const auto& e : fs::directory_iterator(root / "Loops"))
            if (e.path().filename().string().rfind(prefix, 0) == 0) { auto x = read_wav(e.path()); if (!x.empty()) beds.push_back(std::move(x)); }

    // Synthetic breaks: Auto at SLICES 16 (two bars: eighths), its cuts against the hits placed (P: the share on a
    // real hit) and its slices against an eighth note; Threshold 25 % and Beats 1/16 beside it.
    for (int set = 0; set < 3; ++set) {
        const bool bed = set == 1, edges = set == 2;
        Score autos16, th25, beats16;
        Lengths l_auto, l_th25, l_b16;
        std::map<size_t, int> kit_sizes;
        unsigned kick_first = 0, kind_right = 0, n = 0;
        std::map<std::string, int> kinds;
        for (int i = 0; i < 40; ++i, ++n) {
            const Known k = make_break(rng, kicks, snares, hats, opens, beds, bed, edges);
            const Read r = read_of(k.mono);
            kinds[sound_kind_name(r.autos.kind)]++;
            kind_right += r.autos.kind == k.kind;
            const auto frames = uint32_t(k.mono.size());
            const auto eighth = uint32_t(44100 * 30 / k.bpm);
            const Slices sixteen = auto_chop(r.autos, 16, 0, frames, frames, r.hits, r.count, r.beats);
            autos16.add(cuts_of(sixteen), k.cuts);
            l_auto.add(sixteen, frames, eighth);
            const auto kit = kit_sounds(sixteen, frames, r.hits, r.count, r.autos);
            kit_sizes[std::min<size_t>(kit.size(), 9)]++;
            kick_first += !kit.empty() && kit[0].kind == KitKind::Kick;
            const Slices t25 = chop(ChopMode::Threshold, 25, 0, frames, frames, r.hits, r.count, 920);
            th25.add(cuts_of(t25), k.cuts);
            l_th25.add(t25, frames, eighth);
            const Slices b16 = chop(ChopMode::Beats, 1, 0, frames, frames, r.hits, r.count, 920, &r.beats);
            beats16.add(cuts_of(b16), k.cuts);
            l_b16.add(b16, frames, eighth);
        }
        std::printf("\nbreaks%s (%u): kind DRUMS %u/%u (", bed ? " with music" : edges ? " with lead-in and cut-off bar" : "", n, kind_right, n);
        for (const auto& [name, c] : kinds) std::printf(" %s %d", name[0] ? name.c_str() : "-", c);
        std::printf(" )\n  Auto, SLICES 16: %s; %s\n  Threshold 25 %%:  %s; %s\n  Beats 1/16:      %s; %s\n",
                    autos16.text().c_str(), l_auto.text().c_str(), th25.text().c_str(), l_th25.text().c_str(), beats16.text().c_str(), l_b16.text().c_str());
        std::printf("  KIT of SLICES 16 (one kick, snare, hat and open hat sample each): kick first %u/%u, sounds:", kick_first, n);
        for (const auto& [size, c] : kit_sizes) std::printf(" %zu x%d", size, c);
        std::printf("\n");
    }

    // Chord loops: where the chords change.
    {
        std::vector<Shot> chords, basses;
        for (const char* kind : {"Keys", "Piano", "EP", "Pad", "Strings", "Synth", "Pluck", "Guitar"})
            for (auto& sh : shots_in(root / "Melodic", kind)) if (sh.name.rfind(kind, 0) == 0) chords.push_back(std::move(sh));
        for (auto& sh : shots_in(root / "Melodic", "Bass-")) basses.push_back(std::move(sh));
        for (int held = 0; held < 2 && !chords.empty() && !basses.empty(); ++held) {
            unsigned n = 0, kind_right = 0;
            Score music, hits_way;
            std::map<std::string, int> kinds;
            for (int i = 0; i < 40; ++i, ++n) {
                const Known k = make_chords(rng, chords, basses, held);
                const Read r = read_of(k.mono);
                kinds[sound_kind_name(r.autos.found)]++;
                kind_right += r.autos.found == SoundKind::Music;
                const auto frames = uint32_t(k.mono.size());
                AutoRead as = r.autos;
                as.kind = SoundKind::Music;
                music.add(cuts_of(auto_chop(as, 4, 0, frames, frames, r.hits, r.count, r.beats)), k.cuts);
                as.kind = SoundKind::Hits;
                hits_way.add(cuts_of(auto_slices(as, 4, 0, frames, frames, r.hits, r.count)), k.cuts);
            }
            std::printf("\nchord loops%s (%u): kind MUSIC %u/%u (", held ? ", chords held" : ", chords in eighths", n, kind_right, n);
            for (const auto& [name, c] : kinds) std::printf(" %s %d", name[0] ? name.c_str() : "-", c);
            std::printf(" )\n  4 slices on the chord changes: as MUSIC %s, as HITS %s\n", music.text().c_str(), hits_way.text().c_str());
        }
    }

    // Rows of single sounds.
    {
        std::vector<const std::vector<Shot>*> sets {&kicks, &snares, &hats};
        if (!claps.empty()) sets.push_back(&claps);
        if (!toms.empty()) sets.push_back(&toms);
        if (!percs.empty()) sets.push_back(&percs);
        unsigned kind_right = 0, count_right = 0, n = 0, into_next = 0;
        double start_error = 0, worst = 0;
        unsigned starts = 0;
        std::map<std::string, int> kinds;
        for (int i = 0; i < 40; ++i, ++n) {
            const Known k = make_row(rng, sets);
            const Read r = read_of(k.mono);
            kinds[sound_kind_name(r.autos.kind)]++;
            kind_right += r.autos.kind == SoundKind::Shots;
            AutoRead as_shots = r.autos;
            as_shots.kind = SoundKind::Shots;
            const auto frames = uint32_t(k.mono.size());
            const Slices s = auto_slices(as_shots, 16, 0, frames, frames, r.hits, r.count);
            count_right += s.count == k.spans.size();
            if (std::getenv("ROWS") && s.count != k.spans.size()) {
                std::printf("  row %d: %zu sounds, %u slices\n   want:", i, k.spans.size(), s.count);
                for (const auto& sp : k.spans) std::printf(" %.0f-%.0f", sp.from / 44.1, sp.to / 44.1);
                std::printf("\n   got: ");
                for (unsigned j = 0; j < s.count; ++j) std::printf(" %.0f-%.0f", s.start[j] / 44.1, s.end(j, frames) / 44.1);
                std::printf("\n");
            }
            for (unsigned j = 0; j < s.count; ++j) {
                uint32_t best = UINT32_MAX;
                for (const auto& sp : k.spans) best = std::min(best, s.start[j] > sp.from ? s.start[j] - sp.from : sp.from - s.start[j]);
                start_error += best; worst = std::max(worst, double(best)); ++starts;
                if (j + 1 < s.count) {
                    for (const auto& sp : k.spans) if (sp.from > s.start[j] + 441 && sp.from < s.end(j, frames)) { ++into_next; break; }
                }
            }
        }
        std::printf("\nrows of single sounds (%u): kind SHOTS %u/%u (", n, kind_right, n);
        for (const auto& [name, c] : kinds) std::printf(" %s %d", name[0] ? name.c_str() : "-", c);
        std::printf(" )\n  one slice a sound: %u/%u, start off by %.1f ms on average (at most %.1f ms), slices running into the next sound: %u\n",
                    count_right, n, starts ? start_error / starts / 44.1 : 0, worst / 44.1, into_next);
    }

    // The factory folders: kind by folder and name, tempo by name.
    auto expect = [](const std::string& folder, const std::string& name) -> std::string {
        if (folder == "Break") return name.find("Crazy") != std::string::npos || name.find("Beet") != std::string::npos || name.find("OKitC") != std::string::npos ? "DRUMS|HITS" : "DRUMS";
        const std::string prefix = name.substr(0, name.find('-'));
        if (folder == "Loops") {
            if (prefix == "HatLp" || prefix == "PercLp") return "DRUMS";
            if (prefix == "FullLp") return "DRUMS|MUSIC";
            if (prefix == "VoxLp") return "VOCAL";
            return "MUSIC";
        }
        if (prefix == "VoxPh") return "VOCAL";
        if (prefix == "FullPh") return "DRUMS|MUSIC";
        return "MUSIC|HITS";
    };
    for (const char* folder : {"Break", "Loops", "Phrases"}) {
        std::map<std::string, std::map<std::string, int>> by_prefix;
        std::map<std::string, std::array<double, 6>> features;
        unsigned right = 0, n = 0, tempo_right = 0, tempo_n = 0;
        if (!fs::is_directory(root / folder)) continue;
        for (const auto& e : fs::directory_iterator(root / folder)) {
            const std::string name = e.path().filename().string();
            const auto mono1 = read_wav(e.path());
            if (mono1.empty()) continue;
            std::vector<float> mono(mono1.size());
            for (size_t i = 0; i < mono.size(); ++i) mono[i] = 2 * mono1[i];
            const Read r = read_of(mono);
            const std::string got = sound_kind_name(r.autos.found), want = expect(folder, name);
            const bool ok = ("|" + want + "|").find("|" + got + "|") != std::string::npos;
            right += ok; ++n;
            by_prefix[std::string(folder) == "Break" ? "Break" : name.substr(0, name.find('-'))][got.empty() ? "-" : got]++;
            const double bpm = name_bpm(name);
            if (bpm > 0 && r.beats.found()) {
                ++tempo_n;
                const double t = r.beats.tempo / 10.0;
                tempo_right += std::fabs(t / bpm - 1) < 0.03 || std::fabs(t / bpm - 2) < 0.06 || std::fabs(t / bpm - 0.5) < 0.015;
            }
            if (folder != std::string("Break")) {
                auto& f = features[name.substr(0, name.find('-'))];
                f[0] += r.autos.percussive; f[1] += r.autos.voice; f[2] += 0; f[3] += r.beats.found(); f[4] += double(r.autos.phrases.size()); f[5] += 1;
            }
            if (argc > 2 && std::strcmp(argv[2], "-v") == 0)
                std::printf("  %-40s %-6s want %-12s hits %.2f voice %.2f spans %zu phrases %zu tempo %.1f\n", name.c_str(), got.c_str(), want.c_str(),
                            r.autos.percussive, r.autos.voice, r.autos.spans.size(), r.autos.phrases.size(), r.beats.tempo / 10.0);
        }
        std::printf("\nfactory %s (%u): kind as expected %u/%u, tempo as named (or double, half) %u/%u\n", folder, n, right, n, tempo_right, tempo_n);
        for (const auto& [prefix, kinds] : by_prefix) {
            std::printf("  %-10s", prefix.c_str());
            for (const auto& [kind, c] : kinds) std::printf(" %s %d", kind.c_str(), c);
            if (features.count(prefix)) {
                const auto& f = features[prefix];
                std::printf("   | hits %.2f voice %.2f beat %.2f phrases %.1f", f[0] / f[5], f[1] / f[5], f[3] / f[5], f[4] / f[5]);
            }
            std::printf("\n");
        }
    }

    // WAVs given: Auto at SLICES 16 as it reads them. Where a file holds Rhythmer's chop chunk, its hand cuts and
    // cuts taken out ("Hand", "Out": what was tapped) are the truth.
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-v")) continue;
        std::string mlch;
        unsigned rate = 44100;
        const auto mono1 = read_wav(argv[i], nullptr, &mlch, &rate);
        if (mono1.empty()) continue;
        std::vector<float> mono(mono1.size());
        for (size_t n = 0; n < mono.size(); ++n) mono[n] = 2 * mono1[n];
        const Read r = read_of(mono);
        const auto frames = uint32_t(mono.size());
        const Slices s = auto_chop(r.autos, 16, 0, frames, frames, r.hits, r.count, r.beats);
        std::printf("\n%s: %s (cut as %s), hits %.2f, tempo %.1f, %u slices:", fs::path(argv[i]).filename().c_str(), sound_kind_name(r.autos.found),
                    sound_kind_name(r.autos.kind), r.autos.percussive, r.beats.tempo / 10.0, s.count);
        for (unsigned n = 0; n < s.count; ++n) std::printf(" %.0f", (s.end(n, frames) - s.start[n]) / 44.1);
        std::printf(" ms\n");
        std::vector<uint32_t> truth;
        for (const char* key : {"\"Hand\"", "\"Out\""}) {
            const size_t at = mlch.find(key);
            if (at == std::string::npos) continue;
            const size_t open = mlch.find('[', at), close = mlch.find(']', at);
            for (size_t p = open + 1; p < close;) {
                char* end = nullptr;
                const double v = std::strtod(mlch.c_str() + p, &end);
                if (end == mlch.c_str() + p) { ++p; continue; }
                truth.push_back(uint32_t(v * 44100 / rate));
                p = size_t(end - mlch.c_str());
            }
        }
        std::sort(truth.begin(), truth.end());
        if (truth.empty()) continue;
        Score a, b4, t25;
        a.add(cuts_of(s), truth);
        b4.add(cuts_of(chop(ChopMode::Beats, 4, 0, frames, frames, r.hits, r.count, 920, &r.beats)), truth);
        t25.add(cuts_of(chop(ChopMode::Threshold, 25, 0, frames, frames, r.hits, r.count, 920)), truth);
        std::printf("  against the %zu cuts tapped: Auto %s, Beats 1/4 %s, Threshold 25 %% %s\n", truth.size(), a.text().c_str(), b4.text().c_str(), t25.text().c_str());
    }
    return 0;
}
