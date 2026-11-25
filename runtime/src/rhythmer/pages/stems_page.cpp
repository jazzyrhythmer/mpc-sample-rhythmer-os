// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// STEMS (shell/stems_jobs.cpp): Shift + B3 on a kit pad's TRIM. The pad's
// sample separated into DRUMS, BASS, MUSIC and VOCALS, each a tile naming the
// empty pad it lands on; under them the sample's length and the time the job
// takes. B2 BEST (its LED lit while on, a setting) has the job look up to
// 16 kHz in two passes, about three times as long; off, up to 11 kHz in one.
// B3 SEPARATE (or Push) starts it: then the tiles fill as it goes, the
// time left counts down and B1 calls it off. The pads play the kit meanwhile,
// and the job goes on when the sheet is left. A project holds 64 samples and
// a job adds four: with fewer free, the samples in use show in amber where
// the time was (61/64) and SEPARATE is dim.
#include "rhythmer/pages/common.h"
#include "rhythmer/pages/pages.h"
#include "rhythmer/shell/stems.h"
#include "rhythmer/shell/stems_room.h"
#include <cmath>

namespace rhythmer {
using namespace ui;

namespace {
juce::String clock(float seconds) {
    int s = int(std::lround(std::max(0.0f, seconds)));
    return juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
}

// A label in capitals over a value.
void block(Paint& p, float x, float y, float w, const juce::String& caps, const juce::String& value, juce::Colour value_colour) {
    p.text(caps, x, y, w, 12, caps_text(colour::mid));
    p.text(value, x, y + 13, w, 24, plain(value_colour, type::value, Weight::SemiBold));
}

void draw(Paint& p, const Snapshot& s) {
    const auto& sheet = s.ui.stems_sheet;
    const unsigned t = sheet.track % track_count;
    const auto& track = s.project.tracks[t];
    const auto c = track_colour(t);
    const bool running = sheet.running;
    const auto& job = s.stems;

    Header h = header_of(s);
    if (running) h.left.push_back(pill(Icon::Layers, "SEPARATING", c));
    else h.left.push_back(chip(Icon::Layers, "STEMS", c));
    h.right.push_back(tag_of(s, t));
    const auto room = stems_room(s.project, t, sheet.pad);
    SoftKeys keys {{running ? "CANCEL" : "BACK", running ? "" : "BEST", running ? "" : "SEPARATE"}, -1, c, {KeyIcon::Back, KeyIcon::None, KeyIcon::None}};
    if (!running) {
        keys.toggles = 2;
        keys.lit = s.ui.settings.stems_best ? 2 : 0;
        keys.glyphs[2] = Icon::Layers;
        if (!room.enough()) keys.tints[2] = colour::dim; // no four empty pads: B3 says so
    }
    frame(p, s, keys, h);

    // The sample, and the pad it is on.
    const auto& ref = s.project.samples[sheet.sample % sample_slots];
    char from[4];
    pad_label(sheet.pad, from);
    const float x0 = zone::pad, full = zone::width - 2.0f * zone::pad, y0 = zone::picture_y + 6;
    const float fw = p.text(from, x0, y0, 40, 18, plain(c, type::list, Weight::SemiBold));
    const juce::String name = name_case(juce::String(ref.name));
    p.text(name, x0 + fw + 8, y0, full - fw - 8, 18, p.fit(name, plain(colour::text, type::list, Weight::SemiBold), full - fw - 8, type::floor));

    // The four stems as tiles, each with the empty pad it lands on: the next four after the sample's pad.
    constexpr float gap = 6, th = 58;
    const float tw = (full - (stem_count - 1) * gap) / stem_count, ty = y0 + 26;
    unsigned after = sheet.pad % sampler_pads;
    const float done = running ? std::clamp(job.fraction, 0.0f, 1.0f) : 0;
    for (unsigned n = 0; n < stem_count; ++n) {
        const float x = x0 + n * (tw + gap);
        int kit = -1;
        for (unsigned k = 1; k < sampler_pads && kit < 0; ++k)
            if (!track.pads[(after + k) % sampler_pads].used()) kit = int((after + k) % sampler_pads);
        if (kit >= 0) after = unsigned(kit);
        p.fill(x, ty, tw, th, c.withAlpha(0.16f), 6);
        // While it runs, the tiles fill from the left as one bar: the job makes the four together.
        if (running) {
            const float filled = std::clamp(done * stem_count - float(n), 0.0f, 1.0f);
            if (filled > 0) p.fill(x, ty, std::max(6.0f, tw * filled), th, c.withAlpha(0.45f), 6);
        }
        p.text(stem_name(n), x, ty + 8, tw, 12, caps_text(running ? colour::text : c), juce::Justification::centred);
        char label[4] = "-";
        if (kit >= 0) pad_label(unsigned(kit), label);
        p.text(label, x, ty + 22, tw, 28, plain(kit >= 0 ? colour::text : colour::dim, type::value, Weight::SemiBold), juce::Justification::centred);
    }

    // Under them: how long the sample is and how long the job takes, or how long is left.
    const float by = ty + th + 10, bw = full / 2;
    block(p, x0, by, bw, "LENGTH", clock(float(sheet.frames) / 44100.0f), colour::text);
    if (running) block(p, x0 + bw, by, bw, "LEFT", job.busy ? clock(float(job.seconds_left)) : juce::String::fromUTF8("\xe2\x80\x94"), c);
    else block(p, x0 + bw, by, bw, "TAKES", clock(float(stems_seconds(sheet.frames, s.ui.settings.stems_best))), colour::text);
}

void soft(Context& c, int key) {
    if (key == 0) c.stems_sheet_cancel();
    if (key == 1) c.stems_sheet_best();
    if (key == 2) c.stems_sheet_go();
}

void push(Context& c) { c.stems_sheet_go(); }
} // namespace

const PageSpec stems_page {draw, play_pad, no_wheel, push, soft, octave};

} // namespace rhythmer
