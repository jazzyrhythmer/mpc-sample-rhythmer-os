// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/auto_chop.h"
#include "rhythmer/engine/onsets.h"
#include "rhythmer/engine/sample_data.h"
#include "rhythmer/model/chop_slices.h"
#include "rhythmer/model/pattern.h"
#include "rhythmer/model/sampler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Chop: where the slices of a pad's region start and end, as the MPC's Chop
// Mode cuts it (parity phase 5). THRESHOLD keeps the hits at least as strong
// as the threshold asks; REGIONS cuts it into equal parts; BEATS puts a
// slice on every division (1/16 ... 4 bars, as Simpler's) at the sample's own tempo, counted
// from its beat nearest the region's start (a lead-in before that beat a
// slice of its own), each cut on its tracked beat or a hit near it, or at the
// project's tempo from the region's start where the sample has none
// (Rhythmer's); MANUAL is what the
// pads tapped while it played, or what was edited by hand. Slices touch: one
// ends where the next starts, the last at `last`. The result lands on pads as
// regions of the same file, or one slice as a file of its own (Extract).
namespace rhythmer {

// Beats' divisions, as Simpler's Division: 1/16, 1/8, 1/4 (a beat), 1/2, 1 bar, 2 bars, 4 bars. Shift + K3 picks
// one; the slice is that many 16ths long, a bar as many as the project's time signature holds.
constexpr unsigned beat_divisions = 7, quarter_division = 2;
inline unsigned division_sixteenths(unsigned division, unsigned bar_sixteenths) {
    static constexpr unsigned notes[] {1, 2, 4, 8};
    division = std::min(division, beat_divisions - 1);
    return division < 4 ? notes[division] : std::max(1u, bar_sixteenths) << (division - 4);
}
// Its name on the card: the value, and "bar" or "bars" after it from 1 bar on.
inline const char* division_value(unsigned division) {
    static const char* names[] {"1/16", "1/8", "1/4", "1/2", "1", "2", "4"};
    return names[std::min(division, beat_divisions - 1)];
}
inline const char* division_unit(unsigned division) { return division < 4 ? "" : division == 4 ? "bar" : "bars"; }

// The cut nearest a tapped frame: the strongest hit from 50 ms before it to
// 10 ms after, else the tap itself (a tap comes a little after the sound).
inline uint32_t snap_to_hit(uint32_t frame, const std::array<Hit, max_hits>& hits, unsigned hit_count) {
    uint32_t from = frame > 2205 ? frame - 2205 : 0, to = frame + 441;
    int best = -1;
    for (unsigned n = 0; n < hit_count; ++n)
        if (hits[n].frame >= from && hits[n].frame <= to && (best < 0 || hits[n].strength > hits[size_t(best)].strength)) best = int(n);
    return best < 0 ? frame : hits[size_t(best)].frame;
}

// A Threshold amount (0..100) as a hit's strength (out of 255): the hits that chop would cut at.
inline uint8_t hit_floor(int threshold_amount) { return uint8_t(std::clamp(threshold_amount, 0, 100) * 255 / 100); }
// The hits Chop marks and SHIFT + the wheel jumps to: as strong as Threshold's amount, or 25% where that is higher (a
// strong amount leaves few hits on a busy sample; the ones heard should still count).
inline uint8_t mark_floor(int threshold_amount) { return hit_floor(std::min(threshold_amount, 25)); }
// The hit after (`way` > 0) or before `frame` at least 10 ms from it, as strong as `floor`; `frame` itself where there
// is none. 10 ms: a cut pulled onto a zero crossing just before a hit still counts as on it, so the next turn moves on.
inline uint32_t next_hit(uint32_t frame, int way, const std::array<Hit, max_hits>& hits, unsigned count, uint8_t floor) {
    constexpr uint32_t clear = 441;
    uint32_t best = frame;
    bool found = false;
    for (unsigned n = 0; n < count && n < max_hits; ++n) {
        const uint32_t h = hits[n].frame;
        if (!h || hits[n].strength < floor) continue;
        if (way > 0 ? h > frame + clear : h + clear < frame)
            if (!found || (way > 0 ? h < best : h > best)) { best = h; found = true; }
    }
    return best;
}

// The same inside a cut's room (`lo`..`hi`, short of the neighbouring cuts): `frame` itself where the next hit is not.
inline uint32_t next_hit_within(uint32_t frame, int way, const std::array<Hit, max_hits>& hits, unsigned count, uint8_t floor,
                                uint32_t lo, uint32_t hi) {
    const uint32_t h = next_hit(frame, way, hits, count, floor);
    return h != frame && h > lo && h < hi ? h : frame;
}

// Zero Snap (always on; Chop's B2 switch is gone): a cut pulled onto
// the nearest place the wave crosses zero, so a slice starts and stops
// without a click. The two sides are summed (left and right cross at
// different places; the sum is what a mono listener hears). It looks up to
// 5 ms before the cut and 1 ms after, and a crossing after counts double the
// distance: a cut sits on or just before a hit, and moving it into the hit
// would blunt it. Where nothing crosses (a DC offset, a deep bass held over
// the window) it takes the frame nearest zero. The slice edges' 2 ms fades
// (engine/sampler.cpp, Extract) catch what is left.
constexpr uint32_t zero_back = 220, zero_ahead = 44; // 5 ms and 1 ms at 44.1 kHz
inline uint32_t zero_crossing(const float* left, const float* right, uint32_t frames, uint32_t frame) {
    if (!frames || frame >= frames) return frame;
    auto sum = [&](uint32_t i) { return left[i] + right[i]; };
    uint32_t lo = frame > zero_back ? frame - zero_back : 0, hi = std::min(frames - 1, frame + zero_ahead);
    auto cost = [&](uint32_t i) { return i < frame ? frame - i : 2 * (i - frame); };
    uint32_t best = frame, quiet = frame;
    bool found = false;
    for (uint32_t i = lo; i <= hi; ++i) {
        float m = sum(i);
        if (std::fabs(m) < std::fabs(sum(quiet)) || (std::fabs(m) == std::fabs(sum(quiet)) && cost(i) < cost(quiet))) quiet = i;
        // A crossing between i - 1 and i (or a zero on i): the side of it nearer zero.
        if (i == 0) continue;
        float before = sum(i - 1);
        if (!(m == 0 || (before < 0) != (m < 0))) continue;
        uint32_t at = m == 0 || std::fabs(m) <= std::fabs(before) || i - 1 < lo ? i : i - 1;
        if (!found || cost(at) < cost(best)) { best = at; found = true; }
    }
    return found ? best : quiet;
}
// The same on a sample, `frame` and what comes back as the project counts them (44.1 kHz), read in its own frames.
inline uint32_t zero_crossing(const SampleData& sample, uint32_t frame) {
    const uint32_t own = sample.own(frame);
    const uint32_t at = zero_crossing(sample.left.data(), sample.right.data(), std::min<uint32_t>(sample.frames, uint32_t(sample.left.size())), own);
    return at == own ? frame : sample.model(at);
}

// The wheel's Zero Snap: the first crossing at or past `frame` going `way` (+1 later, -1 earlier), within 50 ms;
// `frame` itself where none is. The frame of the pair nearer zero, as above, but never back past `frame`.
constexpr uint32_t zero_reach = 2205;
inline uint32_t zero_crossing_toward(const float* left, const float* right, uint32_t frames, uint32_t frame, int way) {
    if (!frames || frame >= frames) return frame;
    auto sum = [&](uint32_t i) { return left[i] + right[i]; };
    for (uint32_t d = 0; d <= zero_reach; ++d) {
        int64_t i = way < 0 ? int64_t(frame) - d : int64_t(frame) + d;
        if (i < 1 || i >= int64_t(frames)) break;
        float m = sum(uint32_t(i)), before = sum(uint32_t(i - 1));
        if (!(m == 0 || (before < 0) != (m < 0))) continue;
        // The crossing lies between i - 1 and i: the side nearer zero, unless that steps back past `frame`.
        uint32_t at = m == 0 || std::fabs(m) <= std::fabs(before) ? uint32_t(i) : uint32_t(i - 1);
        if (way > 0 && at < frame) at = uint32_t(i);
        if (way < 0 && at > frame) at = uint32_t(i - 1);
        return at;
    }
    return frame;
}
inline uint32_t zero_crossing_toward(const SampleData& sample, uint32_t frame, int way) {
    const uint32_t own = sample.own(frame);
    const uint32_t at = zero_crossing_toward(sample.left.data(), sample.right.data(), std::min<uint32_t>(sample.frames, uint32_t(sample.left.size())),
                                             own, way);
    if (at == own) return frame;
    const uint32_t model = sample.model(at);
    return way > 0 ? std::max(model, frame) : std::min(model, frame); // never back past `frame`, as above
}

// The cut for a beat expected at `want`, a `step` after the cut before (`beat` frames a beat): the sample's tracked
// beat within an eighth of a beat of it and within 6% of the step, or within an eighth of a beat of `on_grid`, the
// same beat counted on the region's grid (so the slices follow a take that drifts, and a beat a drummer played
// late after one played early, yet a tracker that found the off-beats, a 16th or more away, or squeezed its last
// beats to reach a hit, never pulls them off the region's own beat), else the hit within 30 ms (a quarter of the step
// where that is less), else `want` itself. `tracked`: it was a beat.
inline uint32_t beat_cut(double want, double step, double beat, const BeatGrid& grid, const std::array<Hit, max_hits>& hits,
                         unsigned hit_count, bool& tracked, double on_grid = -1) {
    auto gap = [&](uint32_t frame) { return std::fabs(double(frame) - want); };
    tracked = false;
    auto b = std::lower_bound(grid.at.begin(), grid.at.end(), uint32_t(want));
    uint32_t best = uint32_t(want);
    double nearest = 1e300;
    for (auto it : {b, b == grid.at.begin() ? b : b - 1, b + 1 < grid.at.end() ? b + 1 : b})
        if (it != grid.at.end() && gap(*it) < nearest
            && (gap(*it) <= std::min(beat / 8, step * 0.06) || (on_grid >= 0 && std::fabs(double(*it) - on_grid) <= beat / 8))) {
            nearest = gap(*it); best = *it; tracked = true;
        }
    if (tracked) return best;
    nearest = std::min(1323.0, step / 4); // 30 ms, or a quarter of the step where that is less (a fast 1/16)
    for (unsigned n = 0; n < hit_count; ++n)
        if (hits[n].frame && gap(hits[n].frame) <= nearest) { nearest = gap(hits[n].frame); best = hits[n].frame; }
    return best;
}

// The slices of [from, to) of a sample of `frames`. `amount`: THRESHOLD
// 0..100 (as on the MPC, the higher, the fewer slices: 0 keeps every hit), REGIONS the number of slices 2..64, BEATS
// 16ths a slice 1..256 (division_sixteenths(); `grid`: the sample's own, else `tempo_tenths`, the project's). MANUAL starts as one slice, the whole region.
// `capped`: set when a cut was left out because the slices were full (64), so the last slice runs on past where it should end.
inline Slices chop(ChopMode mode, int amount, uint32_t from, uint32_t to, uint32_t frames, const std::array<Hit, max_hits>& hits,
                   unsigned hit_count, unsigned tempo_tenths, const BeatGrid* grid = nullptr, bool* capped = nullptr) {
    Slices s;
    if (capped) *capped = false;
    if (!frames) return s;
    to = to && to <= frames ? to : frames;
    from = std::min(from, to > min_slice ? to - min_slice : 0);
    s.whole(from, to);
    auto cut = [&](uint32_t frame) { // a cut, or a note that there was no room for it
        if (s.count >= max_slices) { if (capped) *capped = true; return; }
        s.add(frame, frames);
    };
    switch (mode) {
    case ChopMode::Threshold: {
        int threshold = std::clamp(amount, 0, 100) * 255 / 100;
        for (unsigned n = 0; n < hit_count; ++n)
            if (hits[n].frame > from && hits[n].strength >= threshold) cut(hits[n].frame);
        break;
    }
    case ChopMode::Regions: {
        unsigned parts = unsigned(std::clamp(amount, 2, int(max_slices)));
        for (unsigned n = 1; n < parts; ++n) s.add(from + uint32_t(uint64_t(to - from) * n / parts), frames);
        break;
    }
    case ChopMode::Beats: {
        if (grid && grid->found()) { // a slice every `amount` of the sample's beats, from the beat nearest the region's start
            const double beat = 44100.0 * 600 / grid->tempo, step = beat * std::clamp(amount, 1, 256) / 4;
            const double reach = std::min(beat / 8, step * 0.06); // a beat this near the end is the end
            // Counted from the beat nearest the region's start, or where the steady beat starts if that is later
            // (a free intro before it is one slice); the last cut where its last beat ends (a free outro after it
            // is one slice).
            double start = double(grid->pulse_from) > from + beat / 2 ? double(grid->pulse_from) : double(from);
            double at = grid->first + std::round((start - grid->first) / beat) * beat;
            const double last = std::min(double(to), double(grid->pulse_to) + beat * 1.5);
            bool on_beat = false;
            uint32_t anchor = beat_cut(at, step, beat, *grid, hits, hit_count, on_beat);
            if (on_beat) at = anchor;
            if (at > from + reach && at + reach < to) cut(anchor); // a lead-in before that beat: a slice of its own
            double counted = at; // the same beats on the region's grid
            for (at += step; at + reach < last;) {
                bool tracked = false;
                counted += step;
                uint32_t frame = beat_cut(at, step, beat, *grid, hits, hit_count, tracked, counted);
                if (frame >= last) break;
                cut(frame);
                at = (tracked ? frame : at) + step; // the next from a tracked beat; from the grid past a hit
            }
            break;
        }
        double beat = 44100.0 * 60.0 / (std::max(tempo_tenths, 10u) / 10.0) * std::clamp(amount, 1, 256) / 4; // a slice's length
        for (double at = from + beat; at < to; at += beat) cut(uint32_t(at));
        break;
    }
    default: break;
    }
    return s;
}
// A new chop with the cuts made by hand (Simpler's manual slices kept whatever the sensitivity): those first, then
// each of `cut`'s own not taken out by hand and not near a hand cut: closer to one than a quarter of the type's own
// spacing there (the shorter way to its neighbours in `cut`), it is that cut, placed by hand, and gives way, so no
// sliver is left between a beat line and a hand cut just after it. The region stays `cut`'s. `capped` as chop()'s:
// set when a cut found no room.
// How near a hand cut the type's cut n (of `cut`) gives way to it: a quarter of the type's own spacing there, 20 ms at least.
inline uint32_t give_way_reach(const Slices& cut, unsigned n, uint32_t frames) {
    const uint32_t at = cut.start[n], next = n + 1 < cut.count ? cut.start[n + 1] : cut.end(cut.count - 1, frames);
    const uint32_t step = std::min(at - cut.start[n - 1], next > at ? next - at : 0);
    return std::max<uint32_t>(2 * hand_reach, step / 4);
}
// `keep_clear`: a type's cut nearer a hand cut than this gives way too (Auto: auto_keep_clear()).
inline Slices with_hand(const Slices& cut, const HandCuts& hand, uint32_t frames, bool* capped = nullptr, uint32_t keep_clear = 0) {
    if (hand.empty() || !cut.count) return cut;
    Slices s;
    s.whole(cut.start[0], cut.stop[cut.count - 1]);
    for (unsigned n = 0; n < hand.pin_count; ++n) s.add(hand.pins[n], frames);
    for (unsigned n = 1; n < cut.count; ++n) {
        if (hand.dropped(cut.start[n])) continue;
        const uint32_t at = cut.start[n], reach = std::max(give_way_reach(cut, n, frames), keep_clear);
        bool near_hand = false;
        for (unsigned k = 0; k < hand.pin_count && !near_hand; ++k)
            near_hand = (hand.pins[k] > at ? hand.pins[k] - at : at - hand.pins[k]) < reach;
        if (near_hand) continue;
        if (s.count >= max_slices) { if (capped) *capped = true; break; }
        s.add(cut.start[n], frames);
    }
    // Where the type leaves gaps (Auto's SHOTS and VOCAL: a slice ends where its sound does), a slice that starts
    // inside one of the type's ends no later than that one does.
    for (unsigned k = 0; k < s.count; ++k)
        for (unsigned n = 0; n < cut.count; ++n)
            if (cut.start[n] <= s.start[k] && s.start[k] < cut.end(n, frames)) {
                if (!cut.touches(n, frames) && cut.end(n, frames) < s.end(k, frames)) s.stop[k] = cut.stop[n];
                break;
            }
    return s;
}

// A chop read back from its WAV keeps its slices, not which cuts were made by hand. Against `autos` (what its type and
// amount cut now): a cut of its own with none of the type's within 20 ms (Zero Snap moves cuts a little) was made or
// moved by hand and is pinned again; one of the type's with none of its own near, and no cut of its own it gave way
// to, was taken out by hand. A Manual chop's type cuts nothing: every cut by hand.
inline void infer_hand(HandCuts& hand, const Slices& kept, const Slices& autos, uint32_t frames) {
    auto nearest = [](const Slices& s, uint32_t at) {
        uint32_t best = UINT32_MAX;
        for (unsigned n = 1; n < s.count; ++n) best = std::min(best, s.start[n] > at ? s.start[n] - at : at - s.start[n]);
        return best;
    };
    for (unsigned n = 1; n < kept.count; ++n)
        if (nearest(autos, kept.start[n]) > 2 * hand_reach) hand.pin(kept.start[n]);
    for (unsigned n = 1; n < autos.count; ++n) {
        const uint32_t d = nearest(kept, autos.start[n]);
        if (d > 2 * hand_reach && d >= give_way_reach(autos, n, frames)) hand.take_out(autos.start[n]);
    }
}

// TO PADS' playback, as Simpler's Playback: POLY the slices overlap (as before), MONO they share a mute group so one
// plays at a time, THRU that too and each plays on to the region's end. KIT lays one pad a sound (kit_sounds()).
// TO PADS asks, starting on the last one.
enum class SlicePlay : uint8_t { Poly, Mono, Thru, Kit, Count };
inline const char* slice_play_name(SlicePlay p) {
    static const char* names[] {"POLY", "MONO", "THRU", "KIT"};
    return names[unsigned(p) % unsigned(SlicePlay::Count)];
}

// KIT: one pad a sound. A slice's sound is its first hit's (engine/auto_chop.h HitSound, the hit within 30 ms of
// its start): a kick has more low than mid and high, a hat more high than mid (by 5 dB) and low, a snare more mid
// than low; anything else, or a slice with no hit there, is other. A hat that rings 150 ms or more is an open hat.
// Slices of one kind that sound alike (their bands within 6 dB of the same balance, ringing about as long) are one
// sound, and the longest of them (the most of its own tail) goes on its pad. Kicks first, then snares, hats, open
// hats and the rest; each kind's sounds the most heard first. `sound_of`: each slice's sound (its place in the list),
// for TO PADS' pattern.
enum class KitKind : uint8_t { Kick, Snare, Hat, OpenHat, Other };
struct KitSound { unsigned slice = 0; KitKind kind = KitKind::Other; unsigned count = 0; };
inline KitKind kit_kind(const HitSound& h) {
    const int low = h.low, mid = h.mid, high = h.high;
    if (low >= std::max(mid, high) && low > 0) return KitKind::Kick;
    if (high >= mid + 20 && high > low) return h.ring >= 15 ? KitKind::OpenHat : KitKind::Hat;
    if (mid >= low && mid > 0) return KitKind::Snare;
    return KitKind::Other;
}
inline std::vector<KitSound> kit_sounds(const Slices& s, uint32_t frames, const std::array<Hit, max_hits>& hits, unsigned hit_count,
                                        const AutoRead& read, std::array<int, max_slices>* sound_of = nullptr) {
    struct Group { KitSound sound; HitSound like; bool known; unsigned id; };
    std::vector<Group> groups;
    std::array<unsigned, max_slices> in {}; // each slice's group, in the order the groups were found
    for (unsigned n = 0; n < s.count && n < max_slices; ++n) {
        int hit = -1;
        uint32_t gap = 1324;
        for (unsigned k = 0; k < hit_count && k < max_hits && k < read.sounds.size(); ++k) {
            const uint32_t d = hits[k].frame > s.start[n] ? hits[k].frame - s.start[n] : s.start[n] - hits[k].frame;
            if (d < gap) { gap = d; hit = int(k); }
        }
        const bool known = hit >= 0;
        const HitSound h = known ? read.sounds[size_t(hit)] : HitSound {};
        const KitKind kind = known ? kit_kind(h) : KitKind::Other;
        auto alike = [&](const Group& g) {
            if (!known || !g.known || g.sound.kind != kind) return false;
            const int low = (h.low - h.mid) - (g.like.low - g.like.mid), high = (h.high - h.mid) - (g.like.high - g.like.mid);
            return std::abs(low) <= 25 && std::abs(high) <= 25 && std::abs(int(h.ring) - int(g.like.ring)) <= std::max(3, int(g.like.ring) / 2);
        };
        auto it = std::find_if(groups.begin(), groups.end(), alike);
        if (it == groups.end()) {
            in[n] = unsigned(groups.size());
            groups.push_back({KitSound {n, kind, 1}, h, known, in[n]});
            continue;
        }
        in[n] = it->id;
        ++it->sound.count;
        const unsigned kept = it->sound.slice;
        if (s.end(n, frames) - s.start[n] > s.end(kept, frames) - s.start[kept]) it->sound.slice = n;
    }
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        return a.sound.kind != b.sound.kind ? a.sound.kind < b.sound.kind : a.sound.count > b.sound.count;
    });
    std::vector<KitSound> out;
    for (const auto& g : groups) out.push_back(g.sound);
    if (sound_of) {
        sound_of->fill(-1);
        std::vector<int> place(groups.size());
        for (size_t k = 0; k < groups.size(); ++k) place[groups[k].id] = int(k);
        for (unsigned n = 0; n < s.count && n < max_slices; ++n) (*sound_of)[n] = place[in[n]];
    }
    return out;
}
// Slice n's pad, from the chopped pad's sound, as `play` lays it; `group` the mute group MONO and THRU share.
inline PadSound slice_pad(const PadSound& parent, const Slices& s, unsigned n, uint32_t frames, SlicePlay play, uint8_t group) {
    PadSound one = slice_sound(parent, s.start[n], play == SlicePlay::Thru ? s.end(s.count - 1, frames) : s.end(n, frames),
                                region_frames(parent, frames));
    if (play != SlicePlay::Poly) one.group = group;
    return one;
}
// TO PADS' pattern, as Live's Slice to New MIDI Track: a note for each slice where it falls in the region, so Play
// gives the loop back as the slices' pads play it. Ticks a frame of the region at the pad's speed: Warp's Seq or
// Follow plays it at the region's own tempo (by # Beats, else own_tempo) whatever the project's, 50..200 % that much
// longer at the pattern's tempo, else as it was recorded at the pattern's tempo.
inline double slice_ticks_per_frame(const PadSound& pad, bool follow, uint32_t region_frames, unsigned tempo_tenths) {
    constexpr double per_second = ticks_per_quarter / 60.0, rate = 44100; // ticks a second at 1 BPM; frames a second
    double own = 0; // the region's own tempo where the pad follows the project's
    if (pad.warp == warp_seq) own = pad.beats && region_frames ? pad.beats * 60.0 * rate / region_frames : pad.own_tempo / 10.0;
    else if (pad.warp < warp_min && follow) own = pad.own_tempo / 10.0;
    if (own > 0) return own * per_second / rate;
    const double longer = pad.warp >= warp_min ? pad.warp / 100.0 : 1.0;
    return longer * (std::max(tempo_tenths, 10u) / 10.0) * per_second / rate;
}
// The pattern: slice n's note on `keys[n]` (-1 none) at velocity 127, as long as the slice; the length the region
// at the nearest whole bar when within an eighth of one, else the bars that hold it all, at most `most` ticks.
inline Pattern slice_pattern(const Slices& s, uint32_t frames, const std::array<int, max_slices>& keys, double ticks_per_frame,
                             unsigned bar_ticks, uint32_t most) {
    Pattern p;
    if (!s.count || !bar_ticks) return p;
    const uint32_t from = s.start[0], to = s.end(s.count - 1, frames);
    const double bars = (to - from) * ticks_per_frame / bar_ticks;
    double whole = std::round(bars);
    if (whole < 1 || std::fabs(bars - whole) > 0.125) whole = std::ceil(bars);
    p.length = uint32_t(std::min<double>(std::max(1.0, whole) * bar_ticks, most));
    p.free = false; // its length is set
    for (unsigned n = 0; n < s.count && n < max_slices; ++n) {
        if (keys[n] < 0 || keys[n] > 255) continue; // a kit's keys run 36 .. 163
        const uint32_t tick = uint32_t(std::lround((s.start[n] - from) * ticks_per_frame));
        if (tick >= p.length) break;
        Event e;
        e.tick = tick;
        e.length = uint16_t(std::clamp<long>(std::lround((s.end(n, frames) - s.start[n]) * ticks_per_frame), 1, 0xffff));
        e.key = uint8_t(keys[n]); e.velocity = 127;
        p.add(e);
    }
    return p;
}
// The mute group MONO and THRU put the slices in: the chopped pad's own, else the first none of the track's pads
// holds (16 when every one is taken).
template <class Pads> inline uint8_t slice_group(const PadSound& parent, const Pads& pads) {
    if (parent.group) return parent.group;
    uint32_t taken = 0;
    for (const auto& p : pads) if (p.used() && p.group) taken |= 1u << (p.group % 32);
    for (uint8_t g = 1; g <= 16; ++g) if (!(taken & (1u << g))) return g;
    return 16;
}

// Auto (engine/auto_chop.h): a sample with a steady beat (DRUMS, MUSIC) is cut on its tracked beats (BeatGrid::at,
// each on its hit): every beat, every 2, 4, 8 or 16, or eighths (each beat, and its strongest hit from 40% to 60%
// of the way to the next beat, where there is one), whichever comes nearest SLICES (on a tie, the longer). A
// lead-in before the first beat, or a tail after the last, is a slice of its own where it is at least 80% of an
// eighth note long, else it joins its neighbour. Anything else is cut by its hits (auto_slices()).
inline bool auto_on_beat(const AutoRead& read, const BeatGrid& beats) {
    return (read.kind == SoundKind::Drums || read.kind == SoundKind::Music) && beats.found();
}
inline Slices auto_chop(const AutoRead& read, unsigned want, uint32_t from, uint32_t to, uint32_t frames, const std::array<Hit, max_hits>& hits,
                        unsigned hit_count, const BeatGrid& beats, bool* capped = nullptr) {
    if (!auto_on_beat(read, beats)) return auto_slices(read, want, from, to, frames, hits, hit_count, capped);
    if (capped) *capped = false;
    to = to && to <= frames ? to : frames;
    const auto least = uint32_t(0.8 * 44100.0 * 600 / beats.tempo / 2); // 80% of an eighth note
    // The beats from the region's start (one there counts, so every 2 beats counts from it), cut where a slice of
    // 80% of an eighth note or more is left either side.
    // Before the first tracked beat, beats counted back by the tempo to the region's start: they count, and their
    // eighths' hits may be cut, but they are not cut themselves (no hit is known on them).
    const auto beat = uint32_t(44100.0 * 600 / beats.tempo);
    std::vector<uint32_t> at;
    std::vector<bool> tracked;
    for (uint32_t b : beats.at) if (b + 1323 >= from && b < to) { at.push_back(b); tracked.push_back(true); }
    // The hits half a beat after the tracked beats clearly stronger (by 30%, the strongest within 30 ms of each):
    // the tracker took the off-beats, and those hits are the beats.
    auto strongest_near = [&](uint32_t t) {
        int best = -1;
        for (unsigned n = 0; n < hit_count && n < max_hits; ++n)
            if ((hits[n].frame > t ? hits[n].frame - t : t - hits[n].frame) <= 1323 && (best < 0 || hits[n].strength > hits[size_t(best)].strength)) best = int(n);
        return best;
    };
    unsigned on = 0, off = 0;
    auto count_in = [&](unsigned& sum, uint32_t t) { const int h = strongest_near(t); sum += h >= 0 ? hits[size_t(h)].strength : 0; };
    for (uint32_t b : at) { count_in(on, b); count_in(off, b + beat / 2); }
    // The beat before the first tracked one, and the off-beat before it, where they fall in the region (30 ms early counts).
    auto before = [&](uint32_t back) { return !at.empty() && uint64_t(at.front()) + 1323 >= uint64_t(from) + back; };
    auto back_from = [&](uint32_t back) { return at.front() > from + back ? at.front() - back : from; };
    if (before(beat)) count_in(on, back_from(beat));
    if (before(beat / 2)) count_in(off, back_from(beat / 2));
    if (off * 10 > on * 13) {
        std::vector<uint32_t> moved;
        std::vector<bool> real;
        if (before(beat / 2)) { // the one before the first tracked beat
            const int h = strongest_near(back_from(beat / 2));
            moved.push_back(h >= 0 ? hits[size_t(h)].frame : back_from(beat / 2));
            real.push_back(h >= 0);
        }
        for (uint32_t b : at) {
            const int h = strongest_near(b + beat / 2);
            if (b + beat / 2 >= to) continue;
            moved.push_back(h >= 0 ? hits[size_t(h)].frame : b + beat / 2);
            real.push_back(h >= 0);
        }
        at = std::move(moved);
        tracked = std::move(real);
    }
    while (!at.empty() && uint64_t(at.front()) + 1323 >= uint64_t(from) + beat) { // within 30 ms of the region's start counts
        at.insert(at.begin(), at.front() > beat ? at.front() - beat : 0);
        tracked.insert(tracked.begin(), false);
    }
    auto room = [&](size_t i) { return tracked[i] && at[i] >= from + least && at[i] + least <= to; };
    auto fits = [&](uint32_t cut) { return cut >= from + least && cut + least <= to; };
    std::vector<std::vector<uint32_t>> ways;
    std::vector<uint32_t> eighths;
    for (size_t i = 0; i < at.size(); ++i) { // eighths: the beat, then the strongest hit near the middle of it (the last beat's by the tempo)
        if (room(i)) eighths.push_back(at[i]);
        const uint32_t next = i + 1 < at.size() ? at[i + 1] : at[i] + beat;
        const uint32_t lo = at[i] + uint32_t(0.4 * (next - at[i])), hi = at[i] + uint32_t(0.6 * (next - at[i]));
        int best = -1;
        for (unsigned n = 0; n < hit_count && n < max_hits; ++n)
            if (hits[n].frame >= lo && hits[n].frame <= hi && (best < 0 || hits[n].strength > hits[size_t(best)].strength)) best = int(n);
        if (best >= 0 && fits(hits[size_t(best)].frame)) eighths.push_back(hits[size_t(best)].frame);
    }
    for (unsigned every : {16u, 8u, 4u, 2u, 1u}) {
        std::vector<uint32_t> w;
        for (size_t i = 0; i < at.size(); i += every) if (room(i)) w.push_back(at[i]);
        ways.push_back(std::move(w));
    }
    ways.push_back(std::move(eighths));
    const std::vector<uint32_t>* best = nullptr;
    unsigned gap = UINT32_MAX;
    for (const auto& w : ways) { // longest first: on a tie the longer stays
        const unsigned count = unsigned(w.size()) + 1, off = count > want ? count - want : want - count;
        if (off < gap) { gap = off; best = &w; }
    }
    Slices s;
    s.whole(from, to == frames ? 0 : to);
    if (!best) return s;
    uint32_t last = from; // a cut that would leave under 80% of an eighth note after the one before (beats tracked unevenly) is left out
    for (uint32_t cut : *best) {
        if (cut < last + least || cut + least > to) continue;
        if (s.count >= max_slices) { if (capped) *capped = true; break; }
        if (s.add(cut, frames)) last = cut;
    }
    return s;
}
// How far from a hand cut Auto keeps its own: an eighth note at the sample's tempo, else auto_least (150 ms).
inline uint32_t auto_keep_clear(const AutoRead& read, const BeatGrid& beats) {
    return auto_on_beat(read, beats) ? uint32_t(44100.0 * 600 / beats.tempo / 2) : auto_least;
}

// The whole sample, as before parity phase 5.
inline Slices chop(ChopMode mode, int amount, uint32_t frames, const std::array<Hit, max_hits>& hits, unsigned hit_count, unsigned tempo_tenths,
                   const BeatGrid* grid = nullptr) {
    return chop(mode, amount, 0, frames, frames, hits, hit_count, tempo_tenths, grid);
}

} // namespace rhythmer
