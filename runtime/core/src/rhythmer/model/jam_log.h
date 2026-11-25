// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/project.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// Jam capture, after Rhythmer FM-1's jam to song. While the transport plays outside Song, each bar is logged: the
// pattern each used track plays (or nothing), which tracks are not heard (Mute, or another track's Solo), the tempo,
// and a sequence that took over on that bar. Each run, PLAY to STOP, is a jam of its own; the project keeps the
// newest (JamList). TAKE on Song's JAMS lays a jam's bars after the song's end: a clip for each run of the same
// pattern on a lane (muted where the track was not heard), a marker where a sequence took over, and the tempo lane
// where the tempo changed.
namespace rhythmer {

constexpr uint8_t jam_silent = 255;  // JamBar::pattern: the track played nothing that bar
constexpr unsigned jams_kept = 8;    // the newest; a ninth makes the oldest go
constexpr unsigned jam_shortest = 2; // bars: a shorter run (a PLAY and STOP by mistake) is not kept

struct JamBar {
    std::array<uint8_t, track_count> pattern; // jam_silent: nothing
    uint16_t muted = 0;  // a bit a track: not heard
    uint16_t tempo = 0;  // tenths of a BPM; 0: not known
    int16_t scene = -1;  // a sequence that took over on this bar
    JamBar() { pattern.fill(jam_silent); }
    bool operator==(const JamBar& o) const { return pattern == o.pattern && muted == o.muted && tempo == o.tempo && scene == o.scene; }
};

struct JamLog {
    uint16_t number = 0; // its number among the project's jams, from 1
    std::vector<JamBar> bars;
    bool full() const { return bars.size() >= song_bars; }
    bool operator==(const JamLog& o) const { return number == o.number && bars == o.bars; }
};

// The project's jams, oldest first.
struct JamList {
    std::vector<JamLog> jams;
    uint16_t next = 1; // the next jam's number
    // A run that ended: kept when it lasted jam_shortest bars or more, the oldest going past jams_kept. True: kept.
    bool keep(JamLog&& run) {
        if (run.bars.size() < jam_shortest) return false;
        run.number = next;
        next = uint16_t(next == 0xffff ? 1 : next + 1);
        jams.push_back(std::move(run));
        if (jams.size() > jams_kept) jams.erase(jams.begin(), jams.begin() + long(jams.size() - jams_kept));
        return true;
    }
    bool erase(unsigned n) {
        if (n >= jams.size()) return false;
        jams.erase(jams.begin() + long(n));
        return true;
    }
    bool operator==(const JamList& o) const { return next == o.next && jams == o.jams; }
};

struct JamTake {
    unsigned at = 0;       // the song's bar the jam starts on
    unsigned bars = 0;     // bars of it laid out
    unsigned clips = 0;
    unsigned left_out = 0; // bars of the part past Song's room (999 bars, 256 clips)
};

// Bars [from, to) of a jam after the song's end (bar 1 when the song is empty). A run of a pattern with no notes
// leaves no clip.
inline JamTake jam_to_song(Project& project, const PatternBank& bank, const JamLog& jam, unsigned from = 0, unsigned to = song_bars) {
    auto& song = project.song;
    JamTake take;
    take.at = song.length();
    to = std::min<unsigned>(to, unsigned(jam.bars.size()));
    from = std::min(from, to);
    const JamBar* log = jam.bars.data() + from;
    const unsigned logged = to - from;
    unsigned limit = take.at < song_bars ? std::min(logged, song_bars - take.at) : 0;

    // Each lane's runs: the same pattern, heard or not, bar after bar.
    std::vector<Clip> clips;
    for (unsigned t = 0; t < track_count; ++t) {
        if (!project.tracks[t].used()) continue;
        for (unsigned b = 0; b < limit;) {
            const uint8_t p = log[b].pattern[t];
            const bool muted = log[b].muted >> t & 1;
            unsigned e = b + 1;
            while (e < limit && log[e].pattern[t] == p && bool(log[e].muted >> t & 1) == muted) ++e;
            if (p != jam_silent && !bank.at[t][p % pattern_count].empty())
                clips.push_back({uint8_t(t), uint8_t(p % pattern_count), uint16_t(take.at + b), uint16_t(e - b), muted});
            b = e;
        }
    }
    // Song's clips run out: the jam ends where the first clip with no room would begin.
    std::stable_sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) { return a.bar < b.bar; });
    const unsigned room = song_clips - std::min<unsigned>(song.count, song_clips);
    if (clips.size() > room) {
        limit = std::min(limit, unsigned(clips[room].bar) - take.at);
        clips.resize(room);
    }
    for (auto clip : clips) {
        if (clip.bar >= take.at + limit) continue;
        clip.bars = uint16_t(std::min<unsigned>(clip.end(), take.at + limit) - clip.bar);
        if (song.place(clip)) ++take.clips;
    }
    for (unsigned b = 0; b < limit; ++b) {
        const int n = log[b].scene;
        const unsigned bar = take.at + b;
        if (n < 0 || song.marker_count == song.markers.size() || song.marker_at(bar) >= 0) continue;
        Marker marker;
        marker.bar = uint16_t(bar);
        const auto& scene = project.scenes[unsigned(n) % pattern_count];
        if (scene.name[0]) std::copy(std::begin(scene.name), std::end(scene.name), std::begin(marker.name));
        else std::snprintf(marker.name, sizeof(marker.name), "SEQ %u", unsigned(n) % pattern_count + 1);
        song.markers[song.marker_count++] = marker;
    }
    // The tempo lane: a step where the jam's tempo differs from what the song would play there.
    unsigned before = unsigned(std::lround(song.tempo.at(double(take.at), project.tempo)));
    for (unsigned b = 0; b < limit; ++b) {
        const unsigned tempo = log[b].tempo;
        if (!tempo || tempo == before) continue;
        song.tempo.set(take.at + b, tempo, false);
        before = tempo;
    }
    take.bars = limit;
    take.left_out = logged - limit;
    return take;
}

// The jams in a project file: a "JAMS" section after the file's own (model/project_file.h), which load_project
// skips. Version 1: the next number, the count, then each jam's number, its bars, and each bar's track count, the
// patterns, the muted tracks, the tempo and the sequence. Nothing is written for no jams.
inline void write_jams(std::vector<uint8_t>& file, const JamList& list) {
    if (list.jams.empty()) return;
    auto u8 = [&](unsigned v) { file.push_back(uint8_t(v)); };
    auto u16 = [&](unsigned v) { u8(v & 0xff); u8(v >> 8 & 0xff); };
    const char tag[] = "JAMS";
    file.insert(file.end(), tag, tag + 4);
    const size_t size_at = file.size();
    u16(0); u16(0);
    u8(1); // version
    u16(list.next);
    u8(unsigned(list.jams.size()));
    for (const auto& jam : list.jams) {
        u16(jam.number);
        u16(unsigned(jam.bars.size()));
        for (const auto& bar : jam.bars) {
            u8(track_count);
            for (auto p : bar.pattern) u8(p);
            u16(bar.muted); u16(bar.tempo); u16(uint16_t(bar.scene));
        }
    }
    const uint32_t n = uint32_t(file.size() - size_at - 4);
    for (unsigned k = 0; k < 4; ++k) file[size_at + k] = uint8_t(n >> (8 * k));
}

// The jams a project file holds; an empty list when it has none. False: the section is damaged (the list is empty).
inline bool read_jams(const std::vector<uint8_t>& file, JamList& list) {
    list = JamList {};
    if (file.size() < 8 || std::memcmp(file.data(), "MELODEE1", 8) != 0) return true;
    for (size_t at = 8; at + 8 <= file.size();) {
        const size_t size = size_t(file[at + 4]) | size_t(file[at + 5]) << 8 | size_t(file[at + 6]) << 16 | size_t(file[at + 7]) << 24;
        if (at + 8 + size > file.size()) return true;
        if (std::memcmp(file.data() + at, "JAMS", 4) != 0) { at += 8 + size; continue; }
        const uint8_t* p = file.data() + at + 8;
        const uint8_t* end = p + size;
        bool ok = true;
        auto u8 = [&]() -> unsigned { if (p >= end) { ok = false; return 0; } return *p++; };
        auto u16 = [&]() { unsigned a = u8(); return a | u8() << 8; };
        if (u8() != 1) return true; // a later version: left out
        JamList read;
        read.next = uint16_t(std::max(1u, u16()));
        const unsigned count = std::min(u8(), jams_kept);
        for (unsigned j = 0; j < count && ok; ++j) {
            JamLog jam;
            jam.number = uint16_t(u16());
            const unsigned bars = std::min<unsigned>(u16(), song_bars);
            jam.bars.resize(bars);
            for (auto& bar : jam.bars) {
                const unsigned tracks = u8();
                for (unsigned t = 0; t < tracks; ++t) {
                    unsigned v = u8();
                    if (t < track_count) bar.pattern[t] = uint8_t(v == jam_silent ? jam_silent : v % pattern_count);
                }
                bar.muted = uint16_t(u16()); bar.tempo = uint16_t(u16()); bar.scene = int16_t(uint16_t(u16()));
                if (bar.scene >= int(pattern_count)) bar.scene = -1;
                if (!ok) break;
            }
            if (ok) read.jams.push_back(std::move(jam));
        }
        if (!ok) return false;
        list = std::move(read);
        return true;
    }
    return true;
}

} // namespace rhythmer
