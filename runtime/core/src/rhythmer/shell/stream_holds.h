// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/chop_slices.h"
#include "rhythmer/model/project.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// A long sample (SampleRef::streamed) is read from the card as it plays: the pager reads ahead of each voice, but a
// hit must sound at once, so the chunks where a pad starts are held in memory (SampleLibrary::hold()). Where each pad
// on such a sample needs them, in the project's 44.1 kHz frames: each layer's start (its end, reversed); its loop
// point and, with a crossfaded seam, the frames read across it; a CLOUD pad's region, up to cloud_most_seconds of it;
// Chop's slices while Chop holds the sample; and a zoomed view of one up to view_most frames wide (Trim's, Chop's),
// drawn from its frames in memory. Pure.
namespace rhythmer {

struct HeldRange {
    uint32_t from = 0, to = 0; // [from, to), and at least half a second from `from` on (stream_head_frames())
    bool back = false;         // the half second below `to` instead (a reversed start)
    bool break_loop = false; // random-access performance cache, not streaming anchors
    bool operator==(const HeldRange& o) const { return from == o.from && to == o.to && back == o.back && break_loop == o.break_loop; }
    bool operator<(const HeldRange& o) const { return from != o.from ? from < o.from : to != o.to ? to < o.to : back != o.back ? back < o.back : break_loop < o.break_loop; }
};
using StreamHolds = std::vector<std::pair<uint16_t, std::vector<HeldRange>>>; // by slot, in order; each slot's ranges in order
constexpr uint32_t view_most = 12 * 44100; // wider, a view is drawn from the sample's peaks (SampleLibrary::detail())

// `chop`: Chop's slices of `chop_sample` while Chop is open (nullptr otherwise); `view` of `view_sample`: a zoomed view;
// `sliced`: the samples slice hits play (issue 24, model/slice_hits.h) with their chops, each slice's start held.
using SlicedChops = std::vector<std::pair<uint16_t, const Slices*>>;
inline StreamHolds stream_holds(const Project& project, const Slices* chop = nullptr, unsigned chop_sample = no_sample,
                                unsigned view_sample = no_sample, HeldRange view = {}, const SlicedChops& sliced = {}) {
    StreamHolds out;
    auto add = [&](unsigned slot, HeldRange range) {
        auto at = std::lower_bound(out.begin(), out.end(), slot, [](const auto& e, unsigned s) { return e.first < s; });
        if (at == out.end() || at->first != slot) at = out.insert(at, {uint16_t(slot), {}});
        at->second.push_back(range);
    };
    for (const auto& track : project.tracks) {
        if (!track.sampled()) continue;
        for (const auto& pad : track.pads) {
            if (!pad.used()) continue;
            const uint32_t seam = uint32_t(pad.loop_xfade) * 441 / 10; // its crossfade in 44.1 kHz frames
            for (unsigned n = 0; n < pad_layers; ++n) {
                const PadLayer layer = pad.layer(n);
                if (!layer.used()) continue;
                const auto& ref = project.samples[layer.sample];
                if (!ref.streamed || !ref.frames) continue;
                if (track.break_bars) { add(layer.sample, {0, ref.frames, false, true}); continue; }
                const uint32_t start = std::min(layer.start, ref.frames - 1);
                const uint32_t end = layer.end && layer.end <= ref.frames ? layer.end : ref.frames;
                if (end <= start) continue;
                if (pad.cloud && n == 0) { // its grains, anywhere in the first of its region
                    add(layer.sample, {start, std::min(end, start + cloud_most_seconds * 44100), false});
                    continue;
                }
                if (pad.reverse) add(layer.sample, {end, end, true});
                else add(layer.sample, {start, start, false});
                if (!pad.loop) continue;
                const uint32_t point = n ? start : std::clamp(pad.loop_point(), start, end - 1); // a layer loops from its own start
                if (!pad.reverse) add(layer.sample, {point > seam ? point - seam : 0, point, false});
                if (seam) add(layer.sample, {end, end + seam, false}); // a seam read past the end
            }
        }
    }
    if (chop && chop_sample < sample_slots && project.samples[chop_sample].streamed)
        for (unsigned n = 0; n < chop->count; ++n) add(chop_sample, {chop->start[n], chop->start[n], false});
    for (const auto& [slot, slices] : sliced) // slice hits: a hit starts its slice (a reversed one its end)
        if (slices && slot < sample_slots && project.samples[slot].streamed)
            for (unsigned n = 0; n < slices->count; ++n) {
                add(slot, {slices->start[n], slices->start[n], false});
                const uint32_t end = slices->end(n, project.samples[slot].frames);
                add(slot, {end, end, true});
            }
    if (view_sample < sample_slots && project.samples[view_sample].streamed && view.to > view.from && view.to - view.from <= view_most)
        add(view_sample, view);
    for (auto& [slot, ranges] : out) {
        std::sort(ranges.begin(), ranges.end());
        ranges.erase(std::unique(ranges.begin(), ranges.end()), ranges.end());
    }
    return out;
}

} // namespace rhythmer
