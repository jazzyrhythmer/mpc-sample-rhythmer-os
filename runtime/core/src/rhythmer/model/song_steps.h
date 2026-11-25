// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/scene_links.h"
#include <algorithm>
#include <cstdio>
#include <vector>

// An MPC song as Rhythmer's Song. The MPC keeps a song as a list of steps, each a sequence played a number of times.
// Here the steps lie end to end from bar 1: each step is a clip on every used track whose pattern in that sequence
// (model/scene_links.h) has notes, as long as the sequence times its repeats (the pattern repeats inside the clip).
// A track with no notes there gets no clip, so it is silent, as on the MPC. A marker on each step's bar takes the
// sequence's name, or SEQ n. With `tempos` (the MPC's tempo per sequence, the song's Ignore Tempo off), the tempo
// lane steps to each step's sequence tempo where it changes, since the song plays one tempo unless its lane has
// points. With none of them different from the project's, the lane stays empty.
namespace rhythmer {

struct SongStep {
    uint8_t sequence = 0;  // its scene and pattern number
    uint16_t repeats = 1;
    uint16_t bars = 1;     // the sequence's length
};

// The song's steps in `project.song`, which it replaces. Returns the steps left out: those past song_bars, or with
// no room left for their clips, and every step after them.
inline unsigned song_from_steps(Project& project, const PatternBank& bank, const std::vector<SongStep>& steps, bool tempos) {
    auto& song = project.song;
    song = Song {};
    auto tempo_of = [&](unsigned n) { return unsigned(project.pattern_tempo[n] ? project.pattern_tempo[n] : project.tempo); };
    bool lane = false;
    if (tempos)
        for (const auto& step : steps) lane = lane || tempo_of(step.sequence % pattern_count) != project.tempo;
    unsigned bar = 0, placed = 0, last_tempo = 0;
    for (const auto& step : steps) {
        const unsigned n = step.sequence % pattern_count;
        const unsigned bars = unsigned(std::max<uint16_t>(step.bars, 1)) * std::max<uint16_t>(step.repeats, 1);
        std::array<int, track_count> pattern;
        unsigned clips = 0;
        for (unsigned t = 0; t < track_count; ++t) {
            pattern[t] = -1;
            const auto& track = project.tracks[t];
            if (!track.used()) continue;
            int p = scene_target(track, n);
            if (p == scene_plays_on) p = int(n); // a sequence that keeps nothing: the MPC plays its own number
            if (p >= 0 && !bank.at[t][unsigned(p)].empty()) { pattern[t] = p; ++clips; }
        }
        if (bar + bars > song_bars || song.count + clips > song_clips) break;
        for (unsigned t = 0; t < track_count; ++t) {
            if (pattern[t] < 0) continue;
            Clip clip;
            clip.track = uint8_t(t);
            clip.pattern = uint8_t(pattern[t]);
            clip.bar = uint16_t(bar);
            clip.bars = uint16_t(bars);
            song.place(clip);
        }
        if (song.marker_count < song.markers.size()) {
            Marker marker;
            marker.bar = uint16_t(bar);
            const auto& scene = project.scenes[n];
            if (scene.name[0]) std::copy(std::begin(scene.name), std::end(scene.name), std::begin(marker.name));
            else std::snprintf(marker.name, sizeof(marker.name), "SEQ %u", n + 1);
            song.markers[song.marker_count++] = marker;
        }
        if (lane && (!placed || tempo_of(n) != last_tempo)) song.tempo.set(bar, tempo_of(n), false);
        last_tempo = tempo_of(n);
        bar += bars;
        ++placed;
    }
    return unsigned(steps.size()) - placed;
}

} // namespace rhythmer
