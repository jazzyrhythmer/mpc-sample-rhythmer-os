// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/project.h"
#include <vector>

// Sequence links. A scene (a sequence on screen) keeps a pattern, or off, for each track (Track::in_scene):
// - A pattern picked by hand on a track is kept in the scene in hand (Project::scene).
// - Launching a scene puts each track on its kept pattern, or stops it where it keeps off; a track the scene keeps
//   nothing for plays on. A scene that keeps nothing lets every track play on.
// - On a pick, every used track the scene keeps nothing for gets what it is on now (off when it is stopped).
// Patterns stay shared: a pattern kept in two scenes is the same pattern in both; a scene copied (copy_scene_links)
// gets copies of its patterns.
// A scene keeps each track's mute too (Track::scene_mutes): where it keeps a pattern or off for a track, launching it
// sets the track's mute to the one kept. A mute set by hand is kept in the scene in hand (keep_mute).
namespace rhythmer {

constexpr uint8_t scene_off = 255;              // Track::in_scene: the track stops
constexpr int scene_plays_on = -1, scene_stops = -2; // scene_target() besides a pattern

// What track `track` does when scene n launches: a pattern, scene_stops, or scene_plays_on.
inline int scene_target(const Track& track, unsigned n) {
    const uint8_t v = track.in_scene[n % pattern_count];
    return v == 0 ? scene_plays_on : v == scene_off ? scene_stops : int(v - 1);
}
inline uint8_t scene_value(int target) {
    return target == scene_stops ? scene_off : target < 0 ? uint8_t(0) : uint8_t(unsigned(target) % pattern_count + 1);
}

// Whether track `track` is muted in scene n (where the scene keeps a pattern or off for it).
inline bool scene_muted(const Track& track, unsigned n) {
    n %= pattern_count;
    return (track.scene_mutes[n / 64] >> (n % 64)) & 1;
}
inline void set_scene_muted(Track& track, unsigned n, bool on) {
    n %= pattern_count;
    const uint64_t bit = uint64_t(1) << (n % 64);
    track.scene_mutes[n / 64] = on ? track.scene_mutes[n / 64] | bit : track.scene_mutes[n / 64] & ~bit;
}

// Whether scene n keeps a pattern or off for any used track.
inline bool scene_kept(const Project& project, unsigned n) {
    for (const auto& t : project.tracks) if (t.used() && t.in_scene[n % pattern_count]) return true;
    return false;
}

// What track t is on now, as a scene keeps it: scene_stops while it is off or stopped (`stopped`, a bit a track:
// a follow action's STOP), else its pattern.
inline int scene_now(const Project& project, unsigned t, uint32_t stopped) {
    const auto& track = project.tracks[t % track_count];
    return track.off || (stopped >> (t % track_count) & 1) ? scene_stops : int(track.pattern % pattern_count);
}

// A pick kept in scene n: `target` (a pattern or scene_stops) for track t, after every other used track the scene
// keeps nothing for has been given what it is on now. Each keeps its mute as it is now. True when the scene kept
// nothing before.
inline bool keep_pick(Project& project, unsigned n, unsigned t, int target, uint32_t stopped) {
    n %= pattern_count; t %= track_count;
    const bool first = !scene_kept(project, n);
    for (unsigned k = 0; k < track_count; ++k) {
        auto& track = project.tracks[k];
        if (k == t || !track.used() || track.in_scene[n]) continue;
        track.in_scene[n] = scene_value(scene_now(project, k, stopped));
        set_scene_muted(track, n, track.mix.mute);
    }
    project.tracks[t].in_scene[n] = scene_value(target);
    set_scene_muted(project.tracks[t], n, project.tracks[t].mix.mute);
    return first;
}

// A mute set by hand on track t (`on`), kept in scene n; where the scene keeps nothing for the track it keeps what the
// track is on now, as a pick does. True when the scene kept nothing before.
inline bool keep_mute(Project& project, unsigned n, unsigned t, bool on, uint32_t stopped) {
    n %= pattern_count; t %= track_count;
    auto& track = project.tracks[t];
    const bool first = !track.in_scene[n] && keep_pick(project, n, t, scene_now(project, t, stopped), stopped);
    set_scene_muted(track, n, on);
    return first;
}

// CAPTURE: scene n keeps what every used track is on now, and its mute. The number of tracks kept.
inline unsigned keep_all(Project& project, unsigned n, uint32_t stopped) {
    n %= pattern_count;
    unsigned count = 0;
    for (unsigned k = 0; k < track_count; ++k) {
        auto& track = project.tracks[k];
        if (!track.used()) continue;
        track.in_scene[n] = scene_value(scene_now(project, k, stopped));
        set_scene_muted(track, n, track.mix.mute);
        ++count;
    }
    return count;
}

// Scene n keeps nothing again (ERASE on Sequences).
inline void forget_scene(Project& project, unsigned n) {
    for (auto& track : project.tracks) { track.in_scene[n % pattern_count] = 0; set_scene_muted(track, n, false); }
}

// The mutes scene n sets as it launches: `set` the tracks (a bit each) it keeps a pattern or off for, `muted` those of
// them it keeps muted.
struct SceneMutes {
    uint16_t set = 0, muted = 0;
};
static_assert(track_count <= 16, "a bit a track");
inline SceneMutes scene_mutes_of(const Project& project, unsigned n) {
    SceneMutes out;
    for (unsigned t = 0; t < track_count; ++t) {
        const auto& track = project.tracks[t];
        if (!track.used() || !track.in_scene[n % pattern_count]) continue;
        out.set = uint16_t(out.set | 1u << t);
        if (scene_muted(track, n)) out.muted = uint16_t(out.muted | 1u << t);
    }
    return out;
}

// A project from before scenes kept mutes (SEQL before version 2, or none): every scene keeps the mute each track was
// saved with, so launching one changes no mute.
inline void mutes_from_mix(Project& project) {
    for (auto& track : project.tracks) {
        track.scene_mutes = {};
        if (!track.mix.mute) continue;
        for (unsigned n = 0; n < pattern_count; ++n) if (track.in_scene[n]) set_scene_muted(track, n, true);
    }
}

// A project from before links, or from the MPC: each scene with notes on a used track's pattern n, or with a name,
// tempo or colour, keeps pattern n on every used track (as it played: a track with no notes there is silent); every
// other scene keeps nothing.
inline void keep_columns(Project& project, const PatternBank& bank) {
    for (unsigned n = 0; n < pattern_count; ++n) {
        bool kept = !project.scenes[n].plain();
        for (unsigned t = 0; t < track_count && !kept; ++t)
            kept = project.tracks[t].used() && !bank.at[t][n].empty();
        for (auto& track : project.tracks) {
            track.in_scene[n] = kept && track.used() ? uint8_t(n + 1) : uint8_t(0);
            set_scene_muted(track, n, track.in_scene[n] && track.mix.mute); // as the track was saved
        }
    }
}

// How many used tracks play a pattern with notes in scene n (none kept: none).
inline unsigned scene_with(const Project& project, const PatternBank& bank, unsigned n) {
    unsigned count = 0;
    for (unsigned t = 0; t < track_count; ++t) {
        const int target = scene_target(project.tracks[t], n);
        count += project.tracks[t].used() && target >= 0 && !bank.at[t][unsigned(target)].empty();
    }
    return count;
}

// Scenes that keep pattern p of track t, lowest first, up to `most` of them in `out`; returns how many keep it.
inline unsigned scenes_keeping(const Track& track, unsigned p, unsigned* out, unsigned most) {
    unsigned count = 0;
    for (unsigned n = 0; n < pattern_count; ++n)
        if (track.in_scene[n] == p % pattern_count + 1) { if (count < most) out[count] = n; ++count; }
    return count;
}

// Whether a pattern holds nothing a sequence copy could overwrite: no notes, Motion, punches, Euclid lanes or follow
// action.
inline bool blank_pattern(const Pattern& p) {
    return p.empty() && p.motion.empty() && p.punches.empty() && p.euclids.empty() && !p.follow.on();
}

// Where a sequence copy puts track t's pattern `from`: the first blank pattern after it (round) that no scene but `but`
// keeps, no song clip plays, and the track is not on or chaining; -1 when there is none.
inline int free_pattern(const Project& project, const PatternBank& bank, unsigned t, unsigned from, unsigned but) {
    t %= track_count; but %= pattern_count;
    const auto& track = project.tracks[t];
    for (unsigned k = 1; k < pattern_count; ++k) {
        const unsigned p = (from + k) % pattern_count;
        if (p == track.pattern % pattern_count || !blank_pattern(bank.at[t][p])) continue;
        bool taken = false;
        for (unsigned n = 0; n < pattern_count && !taken; ++n) taken = n != but && track.in_scene[n] == p + 1;
        for (unsigned c = 0; c < track.chain_count && c < max_chain && !taken; ++c) taken = track.chain[c] % pattern_count == p;
        for (unsigned c = 0; c < project.song.count && !taken; ++c) taken = project.song.clips[c].track == t && project.song.clips[c].pattern == p;
        if (!taken) return int(p);
    }
    return -1;
}

// A pattern a sequence copy makes: track `track`'s pattern `from` into pattern `into`.
struct PatternCopy {
    uint8_t track = 0, from = 0, into = 0;
};

// SHIFT + ERASE on Sequences: scene `to` becomes a copy of scene `from`, its name, tempo and colour, what it keeps for
// each track and the mutes. Each pattern it keeps goes to a copy in the track's free_pattern(), listed in `copies` for
// the caller to make, so the two scenes are edited apart. Returns how many tracks had no free pattern: those keep the
// same pattern in both.
inline unsigned copy_scene_links(Project& project, const PatternBank& bank, unsigned from, unsigned to, std::vector<PatternCopy>& copies) {
    from %= pattern_count; to %= pattern_count;
    if (from == to) return 0;
    unsigned shared = 0;
    project.scenes[to] = project.scenes[from];
    for (unsigned t = 0; t < track_count; ++t) {
        auto& track = project.tracks[t];
        uint8_t value = track.in_scene[from];
        const int target = scene_target(track, from);
        if (track.used() && target >= 0) {
            const int into = free_pattern(project, bank, t, unsigned(target), to);
            if (into >= 0) {
                copies.push_back({uint8_t(t), uint8_t(target), uint8_t(into)});
                value = scene_value(into);
            } else {
                ++shared;
            }
        }
        track.in_scene[to] = value;
        set_scene_muted(track, to, scene_muted(track, from));
    }
    return shared;
}

// The pattern an export writes for track t in scene n: its kept pattern, -1 where it keeps off, and where it keeps
// nothing for the track, pattern n (as before links).
inline int export_pattern(const Project& project, unsigned t, unsigned n) {
    const int target = scene_target(project.tracks[t % track_count], n);
    return target == scene_stops ? -1 : target == scene_plays_on ? int(n % pattern_count) : target;
}

} // namespace rhythmer
