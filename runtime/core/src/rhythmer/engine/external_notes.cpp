// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/engine/external_notes.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace rhythmer {

namespace {
constexpr unsigned turn = 5;        // milliseconds between looks at the input
constexpr uint32_t piece = 4096;    // frames copied at a time
constexpr float full_scale = 0.999f; // a frame at the input's top
constexpr unsigned clip_run = 3;    // that many in a row on one side: a clip
}

bool ExternalNotes::take(uint64_t from, uint32_t frames, std::vector<float>& left, std::vector<float>& right) {
    const size_t at = left.size();
    left.resize(at + frames); right.resize(at + frames);
    if (!io.copy(from, frames, left.data() + at, right.data() + at)) { left.resize(at); right.resize(at); return false; }
    if (from != run_end) run_l = run_r = 0; // not straight on from the frames taken last: no run carries over
    for (size_t f = at; f < left.size(); ++f) {
        run_l = std::fabs(left[f]) >= full_scale ? run_l + 1 : 0;
        run_r = std::fabs(right[f]) >= full_scale ? run_r + 1 : 0;
        if (!clipping && (run_l >= clip_run || run_r >= clip_run)) { clipping = true; clip_key = sounding; }
    }
    run_end = from + frames;
    return true;
}

std::string ExternalNotes::open(uint8_t key, const std::function<bool()>& keep_going) {
    sounding = -1;
    io.post({Command::Type::AllNotesOff, track}, io.now());
    // Silence: the input with nothing playing, 0.3 s at a time from 0.2 s on, until it has stopped dying away (what
    // was sounding has ended: a stretch within 3 dB of the one before, or nothing at all), 5 s at most.
    const uint32_t stretch = uint32_t(0.3f * rate);
    uint64_t listen = io.now() + uint64_t(0.2f * rate);
    std::vector<float> l, r;
    float noise = 0, before = -1;
    for (unsigned n = 0; n < 16; ++n, listen += stretch) {
        while (io.now() < listen + stretch) { if (keep_going && !keep_going()) return "stopped"; io.wait(turn); }
        l.clear(); r.clear();
        if (!take(listen, stretch, l, r)) return "the input fell behind";
        noise = 0;
        for (size_t f = 0; f < l.size(); ++f) noise = std::max({noise, std::fabs(l[f]), std::fabs(r[f])});
        if (noise < 0.0001f || (before >= 0 && noise >= 0.7f * before)) break;
        before = noise;
    }
    if (noise > 0.05f) return "the input is not quiet";
    quiet = std::max(0.000316f, 2.5f * noise);
    clear = std::max(4.0f * quiet, 0.003f);
    // The delay: a quarter of a second of `key`, and where the input first rises well clear of its silence.
    const uint64_t start = io.now(), heard = start + uint64_t(0.5f * rate);
    sounding = key;
    io.post({Command::Type::NoteOn, track, 0, key, 100}, start);
    io.post({Command::Type::NoteOff, track, 0, key}, start + uint64_t(0.25f * rate));
    while (io.now() < heard) {
        if (keep_going && !keep_going()) { io.forget(); io.post({Command::Type::AllNotesOff, track}, io.now()); return "stopped"; }
        io.wait(turn);
    }
    l.clear(); r.clear();
    if (!take(start, uint32_t(heard - start), l, r)) return "the input fell behind";
    size_t first = l.size();
    for (size_t f = 0; f < l.size(); ++f) if (std::fabs(l[f]) >= clear || std::fabs(r[f]) >= clear) { first = f; break; }
    if (first == l.size()) return "no sound at the input";
    // A millisecond before it; a sound that swells in slowly is found late, so never more than 0.1 s.
    const size_t early = size_t(0.001f * rate);
    delay = uint32_t(std::min(first > early ? first - early : 0, size_t(0.1f * rate)));
    settle(keep_going);
    return {};
}

void ExternalNotes::settle(const std::function<bool()>& keep_going) {
    const uint64_t most = io.now() + uint64_t(4.0f * rate), still = uint64_t(0.1f * rate);
    uint64_t from = io.now();
    std::vector<float> l, r;
    while (io.now() < most) {
        if (keep_going && !keep_going()) return;
        io.wait(turn);
        const uint64_t now = io.now();
        if (now - from < still) continue;
        l.clear(); r.clear();
        if (!take(now - still, uint32_t(still), l, r)) return;
        bool loud = false;
        for (size_t f = 0; f < l.size() && !loud; ++f) loud = std::fabs(l[f]) >= quiet || std::fabs(r[f]) >= quiet;
        if (!loud) return;
        from = now - still / 2; // listened to again half a stretch on
    }
}

NoteAudio ExternalNotes::note(uint8_t key, uint8_t velocity, uint64_t hold, float most_tail, const Heard& heard) {
    NoteAudio a;
    // Heard from the frame it is sent: a note comes back a few milliseconds sooner or later than the one before, so
    // where its sound starts is found once it is in. Its key's release is heard about open()'s delay after it is sent.
    const uint64_t start = io.now(), let_go = hold + delay;
    sounding = key; // its tail too, as the input settles after it
    io.post({Command::Type::NoteOn, track, 0, key, int16_t(std::max<uint8_t>(velocity, 1))}, start);
    io.post({Command::Type::NoteOff, track, 0, key}, start + hold);
    const uint64_t quiet_frames = uint64_t(0.2f * rate), most = uint64_t(std::max(0.0f, most_tail) * rate);
    uint64_t still = 0, loud_until = 0;
    bool done = false;
    a.left.reserve(size_t(let_go + uint64_t(rate))); a.right.reserve(size_t(let_go + uint64_t(rate)));
    while (!done) {
        io.wait(turn);
        const uint64_t now = io.now();
        while (!done && start + a.left.size() < now) {
            const size_t at = a.left.size();
            const uint32_t n = uint32_t(std::min<uint64_t>(piece, now - (start + at)));
            if (!take(start + at, n, a.left, a.right)) { a.error = "the input fell behind"; done = true; break; }
            for (size_t f = at; f < a.left.size(); ++f) {
                const bool loud = std::fabs(a.left[f]) >= quiet || std::fabs(a.right[f]) >= quiet;
                if (loud) loud_until = f + 1;
                if (f < let_go) continue;
                still = loud ? 0 : still + 1;
                if (still >= quiet_frames || f + 1 - let_go >= most) { a.left.resize(f + 1); a.right.resize(f + 1); done = true; break; }
            }
            const size_t got = a.left.size() - at;
            if (heard && got && !heard(a.left.data() + at, a.right.data() + at, unsigned(got))) { a.error = "stopped"; done = true; }
        }
    }
    if (!a.error.empty()) io.forget(); // stopped before its key was let go: let go now
    io.post({Command::Type::NoteOff, track, 0, key}, io.now());
    io.post({Command::Type::AllNotesOff, track}, io.now());
    if (!a.error.empty()) return a;
    // Its start: a millisecond before it first rises clear of the silence, looked for until 50 ms after open()'s
    // note came back; where that one did when it never does (a key with no sound).
    const size_t early = size_t(0.001f * rate), last = std::min(a.left.size(), size_t(delay) + early + size_t(0.05f * rate));
    size_t lead = std::min(size_t(delay), a.left.size());
    for (size_t f = 0; f < last; ++f)
        if (std::fabs(a.left[f]) >= clear || std::fabs(a.right[f]) >= clear) { lead = f > early ? f - early : 0; break; }
    a.left.erase(a.left.begin(), a.left.begin() + std::ptrdiff_t(lead));
    a.right.erase(a.right.begin(), a.right.begin() + std::ptrdiff_t(lead));
    loud_until = loud_until > lead ? loud_until - lead : 0;
    // Its quiet end cut off, with a few milliseconds' fade so it ends on silence.
    size_t keep = size_t(std::max<uint64_t>(std::min<uint64_t>(loud_until, a.left.size()), std::min<uint64_t>(hold, a.left.size())));
    keep = std::min(a.left.size(), keep + size_t(0.005f * rate));
    a.left.resize(keep); a.right.resize(keep);
    const size_t fade = std::min(keep, size_t(0.005f * rate));
    for (size_t f = 0; f < fade; ++f) {
        const float g = float(f) / float(std::max<size_t>(fade, 1));
        a.left[keep - 1 - f] *= g; a.right[keep - 1 - f] *= g;
    }
    a.held = std::min<uint64_t>(hold, keep);
    a.release = keep > a.held ? keep - a.held : 0;
    return a;
}

std::vector<float> ExternalNotes::notes(std::vector<ProbeNote> notes, uint64_t frames, const std::function<bool()>& keep_going) {
    const uint64_t start = io.now(), from = start + delay, until = from + frames;
    sounding = -1; // several keys
    for (const auto& n : notes) {
        if (n.on) io.post({Command::Type::NoteOn, track, 0, n.key, int16_t(std::max<uint8_t>(n.velocity, 1))}, start + n.at);
        else io.post({Command::Type::NoteOff, track, 0, n.key}, start + n.at);
    }
    bool stopped = false;
    while (io.now() < until && !stopped) {
        stopped = keep_going && !keep_going();
        if (!stopped) io.wait(turn);
    }
    if (stopped) io.forget(); // the notes still to come are not played
    io.post({Command::Type::AllNotesOff, track}, io.now());
    std::vector<float> l, r, x;
    if (stopped || !take(from, uint32_t(frames), l, r)) return x;
    x.resize(l.size());
    for (size_t f = 0; f < l.size(); ++f) x[f] = 0.5f * (l[f] + r[f]);
    return x;
}

} // namespace rhythmer
