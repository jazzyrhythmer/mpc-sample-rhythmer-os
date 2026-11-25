// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/project.h"

// Which audio track's input the engine takes (its source) and whether it is heard (shell/sample_record.cpp). One
// input at a time: the track a take waits or runs for; else the selected audio track; else, while a track that is
// not an audio track is selected and the Record page is closed, the first audio track whose Monitor is On. Its
// Monitor then says: Auto heard while Seq Record arms it or it records, On all the time, Off never.
namespace rhythmer {

struct AudioMonitorPick {
    int track = -1; // -1: none, Input Config's source and Monitor
    bool heard = false;
};

// `take_track`: the track a take waits or runs for (-1: none); `armed`: Seq Record armed or recording.
inline AudioMonitorPick audio_monitor_pick(const Project& project, int take_track, unsigned selected, bool armed, bool record_page) {
    auto pick = [&](unsigned t, bool own_armed) {
        const auto monitor = project.tracks[t].audio.monitor;
        return AudioMonitorPick {int(t), monitor == TrackMonitor::On || (monitor == TrackMonitor::Auto && own_armed)};
    };
    if (take_track >= 0) return pick(unsigned(take_track) % track_count, true);
    selected %= track_count;
    if (project.tracks[selected].is_audio()) return pick(selected, armed);
    if (!record_page)
        for (unsigned t = 0; t < track_count; ++t)
            if (project.tracks[t].is_audio() && project.tracks[t].audio.monitor == TrackMonitor::On) return pick(t, false);
    return {};
}

} // namespace rhythmer
