// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// AUTO SAMPLE (shell/auto_sample.h): Resample (Shift + Pad 11) on a plug-in
// track. The keys from C-1 to G9 as a strip, the range sampled in the
// track's colour, each root a mark over it. Beside: how many samples, the
// keys, about how long sampling takes (once the check has played it) and what
// they take on the card at most. K1-K3 a row of three, B2 the
// next: KEYS (LOW, HIGH, STEP), NOTE (LAYERS, HOLD, TAIL), SOUND (TAP, LOOP,
// LEVEL). B3 or Push SAMPLE: the roots light up as they are done, the one
// playing blinks; under the keys its note and velocity, the note drawn as it
// records (hold, then release), the speed (not for an instrument outside,
// which plays in real time), the time left and the job's bar
// with a light running along it; B1 calls it off and its files go. The pads
// play the plug-in to try it; Minus / Plus their octave. Shift + Pad 11 again
// closes it. Done, it goes back and the new KEYGROUP track is in hand.
#include "rhythmer/engine/midi_engine.h"
#include "rhythmer/model/scales.h"
#include "rhythmer/pages/common.h"
#include "rhythmer/pages/input_meter.h"
#include "rhythmer/pages/pages.h"
#include <cmath>

namespace rhythmer {
using namespace ui;

namespace {
const char* const rows[] {"KEYS", "NOTE", "SOUND"};

void block(Paint& p, float x, float y, float w, const juce::String& caps, const juce::String& value, const juce::String& unit,
           juce::Colour value_colour) {
    p.text(caps, x, y, w, 12, caps_text(colour::mid));
    float vw = p.text(value, x, y + 13, w, 24, p.fit(value, plain(value_colour, type::value, Weight::SemiBold), w, type::list));
    if (unit.isNotEmpty()) p.text(unit, x + vw + 5, y + 21, w - vw - 5, 14, p.fit(unit, plain(colour::sec, type::body), w - vw - 5, type::floor));
}

juce::String clock(float seconds) {
    int s = int(std::lround(std::max(0.0f, seconds)));
    return juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
}

bool black(unsigned key) { const unsigned n = key % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }

void draw(Paint& p, const Snapshot& s) {
    const auto& sheet = s.ui.auto_sheet;
    const auto& a = sheet.settings;
    const unsigned t = sheet.track % track_count;
    const auto c = track_colour(t);
    const auto& job = s.print;
    const bool sampling = sheet.sampling, ours = sampling && job.busy && job.autosample && job.track == t;
    // The check: on its own as the sheet opens (a plug-in), or the first part of sampling (an instrument outside,
    // heard only once SAMPLE asks for it).
    const bool checking = sampling ? ours && job.checking : sheet.checking, busy = sampling || checking;
    const bool outside = s.project.tracks[t].engine == EngineKind::Midi; // a MIDI track: over its MIDI, from its INPUT, in real time
    // What the check heard: on the sheet once it is done; while it runs, as far as it has got.
    const AutoFindings& found = checking ? job.found : a.found;
    const bool drums = found.kind == AutoKind::Drums;
    const auto roots = a.roots();
    const unsigned layers = a.layer_count(), takes = a.take_count(), samples = unsigned(roots.size()) * layers * takes;
    const Icon kind_icon = !a.found.heard ? Icon::Keyboard : a.found.kind == AutoKind::Mono ? Icon::Slide : a.found.kind == AutoKind::Drums ? Icon::Pads : Icon::Keyboard;

    Header h = header_of(s);
    // The check's chip: OPENING until its plug-in is open (the track's own, then the print host's copy; an earlier
    // check called off, until its copy has opened), WAITING while the print engine has another job, then CHECKING.
    const bool check_live = job.busy && job.checking && job.track == t;
    const char* const check_word = sampling ? "CHECKING"
                                 : sheet.check_wanted ? (job.busy && !job.checking ? "WAITING" : "OPENING")
                                 : check_live && !job.opened ? "OPENING" : "CHECKING";
    if (checking) h.left.push_back(chip(Icon::Clock, check_word, colour::ice));
    else if (sampling) h.left.push_back(pill(kind_icon, ours ? "SAMPLING " + juce::String(std::min<unsigned>(job.through + 1, job.count)) + "/" + juce::String(job.count)
                                                            : juce::String("SAMPLING"), colour::ice));
    else if (sheet.error[0]) h.left.push_back(chip(Icon::Warn, juce::String::fromUTF8(sheet.error).toUpperCase(), colour::amber)); // why the last run failed
    else h.left.push_back(chip(kind_icon, !a.found.heard ? "AUTO SAMPLE" : a.found.split ? "SPLIT" : a.found.kind == AutoKind::Mono ? "MONO" : drums ? "DRUMS" : "KEYS", colour::ice));
    h.right.push_back(tag_of(s, t));
    SoftKeys keys {{busy ? "CANCEL" : "BACK", busy ? "" : rows[sheet.row % 3], busy ? "" : "SAMPLE"}, -1, c,
                   {KeyIcon::Back, KeyIcon::None, KeyIcon::None}};
    if (!busy) {
        keys.dots[1] = 3; keys.dot_at[1] = uint8_t(sheet.row % 3);
        keys.glyphs[2] = Icon::Rec;
        keys.tints[2] = a.low <= a.high ? colour::rec : colour::dim;
    }
    frame(p, s, keys, h);

    // The keys, 0-127 across the width (drums: the range alone, a key wider each): white and black, the range in the
    // track's colour (drums: the keys that sound), a mark a root.
    const bool zoom = drums && a.found.heard && !checking;
    const unsigned first = zoom ? a.low : 0, last = zoom ? std::max(a.low, a.high) : 127, count = last - first + 1;
    const float x0 = zone::pad, kw = (zone::width - 2.0f * zone::pad) / float(count), ky = zone::picture_y + 18, kh = 30;
    for (unsigned k = first; k <= last; ++k) {
        const bool in = k >= a.low && k <= a.high && (!drums || found.sounds(k));
        juce::Colour fill = black(k) ? colour::quiet : colour::line;
        if (in) fill = black(k) ? c.withAlpha(0.45f) : c.withAlpha(0.8f);
        if (!busy && int(k) == sheet.error_key) fill = colour::amber; // the key the last run failed on
        p.fill(x0 + (k - first) * kw, ky, std::max(1.0f, kw - (zoom ? 1.5f : 0.4f)), black(k) ? kh * 0.62f : kh, fill, zoom ? 2.0f : 0.0f);
    }
    // A split: a line where the upper sound starts.
    if (found.split && found.split >= first && found.split <= last) p.fill(x0 + (found.split - first) * kw - 1.0f, ky - 3, 1.5f, kh + 6, colour::text);
    const unsigned now_key = ours && job.pattern >= 0 ? unsigned(job.pattern) : 128;
    for (uint8_t r : checking ? std::vector<uint8_t> {} : roots) {
        if (r < first || r > last) continue;
        const float rx = x0 + (r - first + 0.5f) * kw;
        const bool done = ours && ((job.done[r / 64 % job.done.size()] >> (r % 64)) & 1);
        const bool playing = r == now_key;
        juce::Colour mark = sampling ? (done ? colour::ice : playing ? colour::text : colour::dim) : colour::text;
        if (playing && (s.now / 250) % 2) mark = c;
        p.fill(rx - 1.5f, ky - 9, 3, 7, mark, 1.5f);
    }
    if (!busy && sheet.error_key >= int(first) && sheet.error_key <= int(last))
        p.fill(x0 + (unsigned(sheet.error_key) - first + 0.5f) * kw - 1.5f, ky - 9, 3, 7, colour::amber, 1.5f);
    // Octave names under the keys: every second C (C-1, C1, C3 ...); zoomed, each C in view (11 px).
    for (unsigned k = (first + 11) / 12 * 12; k <= last; k += zoom && count <= 48 ? 12 : 24)
        p.text(juce::String(note_name(int(k))), x0 + (k - first) * kw, ky + kh + 3, 34, 12, plain(colour::mid, type::small));

    // Under them: what it makes, or how it goes.
    const float y = ky + kh + 20;
    if (checking) {
        // The check's findings, a row each, ticked as each is heard; dots while it listens.
        const bool live = job.busy && job.checking && job.track == t;
        const unsigned step = live ? job.check_step : 0;
        const juce::String none = juce::String::fromUTF8("\xe2\x80\x94");
        // Mono: whether a key pressed while another is held carries the note on (legato) or starts it again.
        // A split: each side's, the upper one from its first key.
        auto kind_of = [](const AutoZone& z) { return z.kind == AutoKind::Mono ? (z.tied ? juce::String("Mono, legato") : juce::String("Mono, retrigger")) : juce::String("Keys"); };
        auto held_of = [](const AutoZone& z, bool brief) {
            return z.moves ? juce::String(brief ? "Moves" : "Moves, long loops") : z.sustains ? juce::String(brief ? "Steady" : "Steady, loops") : juce::String("Fades");
        };
        const AutoZone under = found.zone(0), over = found.zone(found.split);
        const juce::String type = drums ? juce::String("Drums")
                                : found.split ? (under.kind == AutoKind::Mono ? "Mono" : "Keys") + juce::String(", ") + (over.kind == AutoKind::Mono ? "Mono" : "Keys")
                                                + " from " + juce::String(note_name(int(found.split)))
                                : kind_of(under);
        const AutoZone& gliding = under.kind == AutoKind::Mono || !found.split ? under : over; // the mono side's
        const juce::String second_cap = drums ? "SOUNDS" : "GLIDE";
        const juce::String second = drums ? juce::String(found.sounding()) + " keys"
                                  : gliding.kind == AutoKind::Mono && gliding.glide > 0 ? juce::String(int(std::lround(gliding.glide * 1000))) + " ms"
                                  : juce::String("None");
        // Velocity: how many layers the tone needs (an accent's velocity with them), or one where the hits differ in
        // level only (Velocity follows it), or not even that.
        const juce::String layered = juce::String(int(found.layers)) + " layers";
        const juce::String velocity = found.layers > 1 ? (found.accent ? "Accent at " + juce::String(int(found.accent)) + ", " + layered : "Tone, " + layered)
                                    : found.accent ? "Accent at " + juce::String(int(found.accent))
                                    : found.velocity > 0 && !drums ? juce::String("Level only") : juce::String("Same");
        const juce::String each = found.accent ? none : found.takes > 1 ? juce::String::fromUTF8("Varies \xc3\x97") + juce::String(int(found.takes)) : juce::String("Same");
        const juce::String hold = drums ? none : found.split ? held_of(under, true) + ", " + held_of(over, true) : held_of(under, false);
        const juce::String caps[] {"TYPE", second_cap, "HOLD", "VELOCITY", "EACH HIT"};
        const juce::String values[] {type, second, hold, velocity, each};
        const unsigned heard_at[] {1, 2, 2, 4, 4}; // the check's step each is heard by (the layers once the takes are known)
        // Rows from just under the keys down past the knob strip (empty while it checks): five of them.
        const float ry = ky + kh + 16, rh = 15, gap = 3, rw = zone::width - 2.0f * zone::pad;
        for (unsigned i = 0; i < 5; ++i) {
            const float y0 = ry + i * (rh + gap);
            p.fill(x0, y0, rw, rh, colour::panel, 4);
            p.text(caps[i], x0 + 6, y0, 90, rh, caps_text(colour::mid));
            if (step >= heard_at[i]) {
                p.text(values[i], x0 + 100, y0, rw - 130, rh, plain(colour::text, type::body, Weight::SemiBold));
                icon(p, Icon::Check, x0 + rw - 18, y0 + 2, 11, c);
            } else {
                for (unsigned d = 0; d < 3; ++d)
                    p.circle(x0 + 104 + d * 8, y0 + rh / 2, 1.8f, d == (s.now / 200) % 3 ? colour::text : colour::dim);
            }
        }
    } else if (sampling) {
        // Left: the note and its velocity. Right: the note as it records, hold then release, a bar a slice of time,
        // the line at its end where it is now. Under them the speed and time left, and the whole job's bar.
        const juce::String dash = juce::String::fromUTF8("\xe2\x80\x94");
        const bool live = ours && job.pattern >= 0;
        block(p, x0, y, 80, "NOTE", live ? juce::String(note_name(job.pattern)) : dash, "", colour::text);
        if (live && job.velocity) p.text("VEL " + juce::String(int(job.velocity)), x0, y + 40, 80, 12, caps_text(colour::mid));
        const float bx = x0 + 88, bw = zone::width - zone::pad - bx, by = y - 2, bh = 70;
        p.fill(bx, by, bw, bh, colour::panel, 5);
        const unsigned count = live ? std::min<unsigned>(job.wave_count, unsigned(job.wave.size())) : 0;
        const unsigned hold = std::max<unsigned>(1, job.wave_hold);
        // The bars across: the hold on 60% at first; a longer release pushes the hold left.
        const float span = std::max({float(count), hold / 0.6f, 1.0f});
        const float ix = bx + 4, iw = bw - 8, cy = by + 14 + (bh - 18) / 2, amp = (bh - 22) / 2;
        const float split = ix + iw * std::min(1.0f, hold / span);
        p.text("HOLD", ix, by + 3, std::max(0.0f, split - ix - 2), 12, caps_text(colour::dim));
        if (ix + iw - split > 50) p.text("RELEASE", split + 4, by + 3, ix + iw - split - 4, 12, caps_text(colour::dim));
        p.fill(split, by + 4, 1, bh - 8, colour::line);
        const float step = iw / span;
        for (unsigned b = 0; b < count; ++b) {
            const float a = std::max(0.04f, job.wave[b] / 255.0f) * amp;
            const juce::Colour bar = b < hold ? c : c.withAlpha(0.5f);
            p.fill(ix + b * step, cy - a, std::max(1.0f, step - 0.6f), 2 * a, bar, 0);
        }
        if (live) p.fill(std::min(ix + count * step, ix + iw - 1), by + 4, 1.5f, bh - 8, colour::text);
        // Speed and time left on one line, then the job's bar with a light running along what is done. An
        // instrument outside plays in real time, so it has no speed.
        const float fy = by + bh + 6;
        if (!outside) {
            float vw = p.text(ours && job.times_real > 0 ? juce::String::fromUTF8("\xc3\x97") + juce::String(job.times_real, 1) : dash,
                              x0, fy, 70, 16, plain(colour::ice, type::list, Weight::SemiBold));
            p.text("SPEED", x0 + vw + 6, fy, 60, 16, caps_text(colour::mid));
        }
        const juce::String left = ours && job.through ? clock(job.seconds_left) : dash;
        const float lw = p.width("LEFT", caps_text(colour::mid));
        p.text("LEFT", zone::width - zone::pad - lw, fy, lw, 16, caps_text(colour::mid));
        p.text(left, zone::width - zone::pad - lw - 66, fy, 60, 16, plain(colour::text, type::list, Weight::SemiBold), juce::Justification::centredRight);
        const float gy = fy + 22, gw = zone::width - 2.0f * zone::pad;
        p.fill(x0, gy, gw, 6, colour::quiet, 3);
        const float done = ours && job.count ? std::clamp((float(job.through) + job.fraction) / float(job.count), 0.0f, 1.0f) : 0;
        if (done > 0) {
            const float dw = gw * done;
            p.fill(x0, gy, dw, 6, colour::ice, 3);
            const float run = dw + 30, at = std::fmod(float(s.now % 100000) * 0.12f, run) - 15; // 120 px a second
            for (int k = -12; k <= 12; ++k) {
                const float sx = x0 + at + k;
                if (sx > x0 + 1 && sx < x0 + dw - 2) p.fill(sx, gy + 1, 1, 4, juce::Colours::white.withAlpha(0.8f * (1.0f - std::abs(k) / 12.0f)));
            }
        }
    } else {
        // Four across, each as wide as its longest (Rubik, measured): SAMPLES "512 128 x 4", KEYS "C-1-G9", TIME
        // "12:30", CARD "<= 120 MB".
        const float sw = 84, kw4 = 78, tw = 64, cw = zone::width - 2.0f * zone::pad - sw - kw4 - tw;
        const unsigned per = layers * takes;
        block(p, x0, y, sw, drums ? "SOUNDS" : "SAMPLES", juce::String(drums ? unsigned(roots.size()) : samples),
              drums || per < 2 ? juce::String() : juce::String(roots.size()) + juce::String::fromUTF8(" \xc3\x97 ") + juce::String(per), colour::text);
        block(p, x0 + sw, y, kw4, "KEYS", juce::String(note_name(a.low)) + juce::String::fromUTF8("\xe2\x80\x93") + juce::String(note_name(a.high)), "", colour::text);
        // About how long it takes, at the speed the check played it; a dash until the check has heard it.
        const float expected = a.seconds_expected();
        block(p, x0 + sw + kw4, y, tw, "TIME", expected > 0 ? clock(expected) : juce::String::fromUTF8("\xe2\x80\x94"), "", colour::text);
        // At most: each note its hold and its whole tail, 24-bit stereo at the device's rate (a mono sound takes half).
        // From 1000 MB on, in GB.
        const double mb = double(samples) * (a.hold_most() + a.tail) * std::max(1u, s.audio_rate) * 6.0 / (1024.0 * 1024.0);
        const double room = mb >= 1000 ? mb / 1024.0 : mb;
        block(p, x0 + sw + kw4 + tw, y, cw, "CARD", juce::String::fromUTF8("\xe2\x89\xa4 ") + (room >= 10 ? juce::String(int(std::lround(room))) : juce::String(room, 1)),
              mb >= 1000 ? "GB" : "MB", colour::text);
        // What the check heard, under them: the glide, the layers split at the accent, the takes.
        const AutoZone side = a.found.kind == AutoKind::Mono || !a.found.split ? a.found.zone(0) : a.found.upper; // a split's mono side
        const bool mono = side.kind == AutoKind::Mono, loops = a.loops_any();
        if (outside && !a.found.heard) {
            // An instrument outside, before it is sampled: its input's level, both sides (-60 to 0 dB; amber from
            // -3 dB, where it is about to clip), or that the track has no INPUT yet.
            const float sy = y + 42, sh = 20, sw = zone::width - 2.0f * zone::pad;
            p.fill(x0, sy, sw, sh, colour::panel, 5);
            const int input = s.project.tracks[t].params[midi_track::AudioIn];
            icon(p, Icon::Jack, x0 + 6, sy + 4, 12, input ? c : colour::amber);
            if (!input) p.text("NO INPUT", x0 + 22, sy, 120, sh, caps_text(colour::amber));
            else {
                const float lx = x0 + 22 + p.text("IN", x0 + 22, sy, 20, sh, caps_text(colour::mid)) + 6, lw = x0 + sw - 6 - lx;
                for (int side = 0; side < 2; ++side) {
                    const float db = db_of(side ? s.take.right : s.take.left), level = std::clamp((db + 60) / 60, 0.0f, 1.0f);
                    const float by = sy + 6 + side * 5;
                    p.fill(lx, by, lw, 3.5f, colour::line, 1.75f);
                    if (level > 0) p.fill(lx, by, lw * level, 3.5f, db > -3 ? colour::amber : c, 1.75f);
                }
            }
        }
        if (a.found.heard && (mono || a.found.accent || takes > 1 || loops || a.found.sweep_count)) {
            const float sy = y + 42, sh = 20, sw = zone::width - 2.0f * zone::pad;
            p.fill(x0, sy, sw, sh, colour::panel, 5);
            float x = x0 + 6;
            if (mono) {
                icon(p, Icon::Slide, x, sy + 4, 12, c);
                x += 16;
                const juce::String g = side.glide > 0 ? "GLIDE " + juce::String(int(std::lround(side.glide * 1000))) + " ms" : juce::String("NO GLIDE");
                x += p.text(g, x, sy, 120, sh, caps_text(colour::text)) + 12;
            }
            if (loops) { // each note's held part loops: it plays on while the key is held
                icon(p, Icon::Loop, x, sy + 4, 12, c);
                x += 16;
                x += p.text("LOOPS", x, sy, 60, sh, caps_text(colour::text)) + 12;
            }
            if (takes > 1) {
                icon(p, Icon::Layers, x, sy + 4, 12, c);
                x += 16;
                x += p.text(juce::String::fromUTF8("TAKES \xc3\x97") + juce::String(takes), x, sy, 90, sh, caps_text(colour::text)) + 12;
            }
            // VEL: the velocity layers along 0-127. A bar per hit of the check's sixteen, as tall as its tone (its
            // brightness over the dullest hit's, an octave the whole height at most), the layers in turn full and
            // faint with a gap between; the accent's in amber, ACCENT at the end.
            const unsigned hits = a.found.sweep_count;
            const float right = x0 + sw - 6, lw = a.found.accent ? p.width("ACCENT", caps_text(colour::amber)) : 0.0f;
            const float bx = x + 26, bw = right - (lw > 0 ? lw + 6 : 0.0f) - bx;
            if (hits && bw >= 24) { // room for sixteen bars a pixel and a half wide
                p.text("VEL", x, sy, 24, sh, caps_text(colour::mid));
                float dull = 0, bright = 0;
                for (unsigned i = 0; i < hits; ++i)
                    if (const float b = a.found.sweep_bright[i]; b > 0) { dull = dull > 0 ? std::min(dull, b) : b; bright = std::max(bright, b); }
                const float octaves = dull > 0 && bright > dull ? std::max(1.0f, std::log2(bright / dull)) : 1.0f;
                unsigned layer = 0, before = 0;
                for (unsigned i = 0; i < hits; ++i) {
                    const unsigned v = a.found.sweep_velocity[i], from = i ? a.found.sweep_velocity[i - 1] : 0;
                    while (layer + 1 < layers && v > a.velocity(layer)) ++layer;
                    const bool gap = i && layer != before;
                    before = layer;
                    const float b = a.found.sweep_bright[i];
                    const float tone = dull > 0 && b > 0 ? std::clamp(std::log2(b / dull) / octaves, 0.0f, 1.0f) : 0.0f;
                    const float l = bx + bw * float(from) / 127.0f + (gap ? 2.5f : 0.5f), r = bx + bw * float(v) / 127.0f - 0.5f;
                    const float h = 3 + (sh - 8) * tone;
                    const juce::Colour ink = a.found.accent && v >= a.found.accent ? colour::amber : c;
                    p.fill(l, sy + sh - 4 - h, std::max(1.0f, r - l), h, layer % 2 ? ink.withAlpha(0.42f) : ink, 1);
                }
                if (lw > 0) p.text("ACCENT", right - lw, sy, lw, sh, caps_text(colour::amber));
            }
        }
    }
    knob_strip(p, cards_of(s)); // the row of three (shell/bindings.cpp)
}

void soft(Context& c, int key) {
    if (key == 0) c.auto_sheet_cancel();
    if (key == 1) c.auto_sheet_row(1);
    if (key == 2) c.auto_sheet_go();
}

void push(Context& c) { c.auto_sheet_go(); }
void wheel(Context& c, int delta) { if (delta) c.auto_sheet_row(delta > 0 ? 1 : -1); } // the wheel walks the rows too

// The pads play the track to try it; not an instrument outside while it is sampled (it would be in the samples).
void pad(Context& c, unsigned pad, uint8_t velocity, bool down) {
    const auto& sheet = c.ui.auto_sheet;
    if (sheet.sampling && c.project.tracks[sheet.track % track_count].engine == EngineKind::Midi) return;
    play_pad(c, pad, velocity, down);
}
} // namespace

const PageSpec auto_sample_page {draw, pad, wheel, push, soft, octave};

} // namespace rhythmer
