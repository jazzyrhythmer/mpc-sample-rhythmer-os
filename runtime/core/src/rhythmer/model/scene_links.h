// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/project.h"

// Sequence links. A scene (a sequence on screen) keeps a pattern, or off, for each track (Track::in_scene):
// - A pattern picked by hand on a track is kept in the scene in hand (Project::scene).
// - Launching a scene puts each track on its kept pattern, or stops it where it keeps off; a track the scene keeps
//   nothing for plays on. A scene that keeps nothing lets every track play on.
// - On a pick, every used track the scene keeps nothing for gets what it is on now (off when it is stopped).
// Patterns stay shared: a pattern kept in two scenes is the same pattern in both.
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
// keeps nothing for has been given what it is on now. True when the scene kept nothing before.
inline bool keep_pick(Project& project, unsigned n, unsigned t, int target, uint32_t stopped) {
    n %= pattern_count; t %= track_count;
    const bool first = !scene_kept(project, n);
    for (unsigned k = 0; k < track_count; ++k) {
        auto& track = project.tracks[k];
        if (k != t && track.used() && !track.in_scene[n]) track.in_scene[n] = scene_value(scene_now(project, k, stopped));
    }
    project.tracks[t].in_scene[n] = scene_value(target);
    return first;
}

// CAPTURE: scene n keeps what every used track is on now. The number of tracks kept.
inline unsigned keep_all(Project& project, unsigned n, uint32_t stopped) {
    n %= pattern_count;
    unsigned count = 0;
    for (unsigned k = 0; k < track_count; ++k) {
        auto& track = project.tracks[k];
        if (!track.used()) continue;
        track.in_scene[n] = scene_value(scene_now(project, k, stopped));
        ++count;
    }
    return count;
}

// Scene n keeps nothing again (ERASE on Sequences).
inline void forget_scene(Project& project, unsigned n) {
    for (auto& track : project.tracks) track.in_scene[n % pattern_count] = 0;
}

// A project from before links, or from the MPC: each scene with notes on a used track's pattern n, or with a name,
// tempo or colour, keeps pattern n on every used track (as it played: a track with no notes there is silent); every
// other scene keeps nothing.
inline void keep_columns(Project& project, const PatternBank& bank) {
    for (unsigned n = 0; n < pattern_count; ++n) {
        bool kept = !project.scenes[n].plain();
        for (unsigned t = 0; t < track_count && !kept; ++t)
            kept = project.tracks[t].used() && !bank.at[t][n].empty();
        for (auto& track : project.tracks) track.in_scene[n] = kept && track.used() ? uint8_t(n + 1) : uint8_t(0);
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

// The pattern an export writes for track t in scene n: its kept pattern, -1 where it keeps off, and where it keeps
// nothing for the track, pattern n (as before links).
inline int export_pattern(const Project& project, unsigned t, unsigned n) {
    const int target = scene_target(project.tracks[t % track_count], n);
    return target == scene_stops ? -1 : target == scene_plays_on ? int(n % pattern_count) : target;
}

} // namespace rhythmer
