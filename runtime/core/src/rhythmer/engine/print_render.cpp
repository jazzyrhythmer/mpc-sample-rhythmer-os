// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/engine/print_render.h"
#include <algorithm>
#include <cmath>

namespace rhythmer {

namespace {
constexpr float ring_silence = 0.000316f; // -70 dB: a Ring print's tail is over once it stays below this
constexpr float ring_quiet_seconds = 0.2f; // for this long
constexpr float ring_most_seconds = 10;    // or after this long, as a mixdown's tail (engine/bounce.h)

void drain(EngineHost& host) {
    const ProjectPack* pack = nullptr;
    while (host.take_done_pack(pack)) delete pack;
    Pattern* pattern = nullptr;
    while (host.take_retired_pattern(pattern)) delete pattern;
}
} // namespace

uint32_t model_frames_of(uint64_t device_frames, float rate) {
    return uint32_t(std::llround(double(device_frames) * dsp::model_rate / std::max(1.0f, rate)));
}

PrintAudio render_print(EngineHost& host, unsigned track, uint32_t ticks, PrintTap tap, PrintTail tail, const std::function<bool()>& keep_going,
                        uint8_t sends) {
    PrintAudio a;
    a.sends = uint8_t(sends & 15);
    // The pad sends from the first frame played, cut to the print's frames once its window is known.
    std::array<std::vector<float>, 4> heard_l, heard_r;
    const float rate = dsp::sample_rate();
    const unsigned B = EngineHost::block;
    host.set_print_tap(track, tap == PrintTap::PostFx);
    host.send({Command::Type::Play});
    std::vector<int32_t> out(B * 2);
    // Kept: the frames [from, to) of what the host plays, `to` moving on while a Ring print's tail sounds.
    uint64_t from = 0, to = 0, quiet = 0, played = 0;
    bool planned = false, done = false, stopped = false;
    const uint64_t quiet_frames = uint64_t(ring_quiet_seconds * rate), most_frames = uint64_t(ring_most_seconds * rate);
    while (true) {
        if (keep_going && !keep_going()) { a.error = "stopped"; break; }
        // Ring: the block split where the pass ends, so the sequencer stops there and only the tail sounds after it.
        unsigned n = B;
        if (planned && tail == PrintTail::Ring && !stopped && a.pass > played) n = unsigned(std::min<uint64_t>(B, a.pass - played));
        host.render(out.data(), n, 1.0f);
        drain(host);
        if (!planned) { // the first block has Play, the tempo and the track's latency in place
            a.pass = host.tick_frame(std::max<uint32_t>(ticks, 1));
            a.latency = tap == PrintTap::PreFx ? host.engine_latency(track) : host.track_latency(track); // Pre FX: before the chain
            from = (tail == PrintTail::Loop ? a.pass : 0) + a.latency;
            to = from + a.pass;
            a.left.reserve(size_t(a.pass)); a.right.reserve(size_t(a.pass));
            planned = true;
        }
        const float* l = host.print_left();
        const float* r = host.print_right();
        for (unsigned b = 0; b < 4; ++b) {
            if (!((a.sends >> b) & 1)) continue;
            const float* sl = host.print_send_left(b);
            const float* sr = host.print_send_right(b);
            heard_l[b].insert(heard_l[b].end(), sl, sl + n); heard_r[b].insert(heard_r[b].end(), sr, sr + n);
        }
        for (unsigned f = 0; f < n && !done; ++f) {
            uint64_t at = played + f;
            if (at < from) continue;
            if (at >= to && tail != PrintTail::Ring) { done = true; break; }
            a.left.push_back(l[f]); a.right.push_back(r[f]);
            if (at >= to) { // Ring: the tail, until it has been quiet a while
                bool loud = std::fabs(l[f]) >= ring_silence || std::fabs(r[f]) >= ring_silence;
                quiet = loud ? 0 : quiet + 1;
                done = quiet >= quiet_frames || at + 1 - to >= most_frames;
            }
        }
        played += n;
        if (tail != PrintTail::Ring && played >= to) done = true;
        if (planned && tail == PrintTail::Ring && !stopped && played >= a.pass) { host.send({Command::Type::Stop}); stopped = true; }
        if (dsp::sample_rate() != rate) { a.error = "the sample rate changed"; break; }
        if (done) break;
    }
    a.rendered = played;
    host.send({Command::Type::Stop});
    if (a.error.empty() && a.sends) { // the sends' frames as the print's, `shift` sooner: they did not wait for the chain
        const unsigned engine = host.engine_latency(track);
        const uint64_t shift = a.latency > engine ? a.latency - engine : 0;
        const uint64_t first = from > shift ? from - shift : 0;
        for (unsigned b = 0; b < 4; ++b) {
            if (!((a.sends >> b) & 1)) continue;
            a.send_left[b].assign(a.left.size(), 0.0f); a.send_right[b].assign(a.left.size(), 0.0f);
            for (size_t f = 0; f < a.left.size() && first + f < heard_l[b].size(); ++f) {
                a.send_left[b][f] = heard_l[b][size_t(first + f)]; a.send_right[b][f] = heard_r[b][size_t(first + f)];
            }
        }
    }
    return a;
}

NoteAudio render_note(EngineHost& host, unsigned track, uint8_t key, uint8_t velocity, uint64_t hold, PrintTap tap,
                      float most_tail, const std::function<bool()>& keep_going) {
    NoteAudio a;
    const float rate = dsp::sample_rate();
    const unsigned B = EngineHost::block;
    host.set_print_tap(track, tap == PrintTap::PostFx);
    std::vector<int32_t> out(B * 2);
    host.send({Command::Type::NoteOn, uint8_t(track), 0, key, int16_t(std::max<uint8_t>(velocity, 1))});
    const uint64_t quiet_frames = uint64_t(ring_quiet_seconds * rate);
    const uint64_t most = uint64_t(std::max(0.0f, most_tail) * rate);
    uint64_t played = 0, quiet = 0, latency = 0, loud_until = 0;
    bool planned = false, released = false;
    while (true) {
        if (keep_going && !keep_going()) { a.error = "stopped"; break; }
        // The block split where the key is let go, so the note-off lands on frame `hold`.
        unsigned n = B;
        if (!released && hold > played) n = unsigned(std::min<uint64_t>(B, hold - played));
        if (!released && played >= hold) {
            host.send({Command::Type::NoteOff, uint8_t(track), 0, key});
            released = true;
        }
        host.render(out.data(), n, 1.0f);
        drain(host);
        if (!planned) { // the first block has the note and the track's latency in place
            latency = tap == PrintTap::PreFx ? host.engine_latency(track) : host.track_latency(track);
            a.left.reserve(size_t(hold + uint64_t(rate))); a.right.reserve(size_t(hold + uint64_t(rate)));
            planned = true;
        }
        const float* l = host.print_left();
        const float* r = host.print_right();
        bool done = false;
        for (unsigned f = 0; f < n && !done; ++f) {
            uint64_t at = played + f;
            if (at < latency) continue;
            const uint64_t kept = at - latency; // frames since the note-on
            a.left.push_back(l[f]); a.right.push_back(r[f]);
            const bool loud = std::fabs(l[f]) >= ring_silence || std::fabs(r[f]) >= ring_silence;
            if (loud) loud_until = kept + 1;
            if (kept < hold) continue;
            quiet = loud ? 0 : quiet + 1;
            done = quiet >= quiet_frames || kept + 1 - hold >= most;
        }
        played += n;
        if (dsp::sample_rate() != rate) { a.error = "the sample rate changed"; break; }
        if (done) break;
    }
    host.send({Command::Type::AllNotesOff, uint8_t(track)});
    if (!a.error.empty()) return a;
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

void trim_silence(NoteAudio& note, float rate, float silence) {
    size_t last = std::min(note.left.size(), note.right.size());
    while (last > 0 && std::fabs(note.left[last - 1]) < silence && std::fabs(note.right[last - 1]) < silence) --last;
    const size_t keep = std::min(std::min(note.left.size(), note.right.size()), last + size_t(0.005f * rate));
    if (keep >= note.left.size()) return;
    note.left.resize(keep); note.right.resize(keep);
    const size_t fade = std::min(keep, size_t(0.005f * rate));
    for (size_t f = 0; f < fade; ++f) {
        const float g = float(f) / float(std::max<size_t>(fade, 1));
        note.left[keep - 1 - f] *= g; note.right[keep - 1 - f] *= g;
    }
    note.held = std::min<uint64_t>(note.held, keep);
    note.release = keep - note.held;
}

bool remove_dc(std::vector<float>& left, std::vector<float>& right, uint64_t held, float rate) {
    const size_t n = std::min(left.size(), right.size()), h = size_t(std::min<uint64_t>(held, n));
    if (!h || rate <= 0) return false;
    double sum = 0, peak = 0;
    for (size_t f = 0; f < h; ++f) {
        const double m = 0.5 * (double(left[f]) + right[f]);
        sum += m; peak = std::max(peak, std::fabs(m));
    }
    // Off zero by more than 2 % of the note's own peak (a quiet plug-in's offset counts as much as a loud one's).
    if (std::fabs(sum / double(h)) < std::max(0.0002, 0.02 * peak)) return false;
    const double r = 1.0 - 2.0 * 3.14159265358979 * 5.0 / rate;
    for (auto* x : {&left, &right}) {
        double before = 0, out = 0;
        for (size_t f = 0; f < n; ++f) {
            const double in = (*x)[f];
            out = in - before + r * out;
            before = in;
            (*x)[f] = float(out);
        }
    }
    return true;
}

std::vector<float> render_notes(EngineHost& host, unsigned track, std::vector<ProbeNote> notes, uint64_t frames, PrintTap tap,
                                const std::function<bool()>& keep_going) {
    const unsigned B = EngineHost::block;
    host.set_print_tap(track, tap == PrintTap::PostFx);
    std::stable_sort(notes.begin(), notes.end(), [](const ProbeNote& a, const ProbeNote& b) { return a.at < b.at; });
    std::vector<int32_t> out(B * 2);
    std::vector<float> x;
    x.reserve(size_t(frames));
    uint64_t played = 0, latency = 0;
    size_t next = 0;
    bool planned = false;
    while (x.size() < frames) {
        if (keep_going && !keep_going()) { x.clear(); break; }
        while (next < notes.size() && notes[next].at <= played) { // each note on its own frame: blocks split there
            const auto& n = notes[next++];
            if (n.on) host.send({Command::Type::NoteOn, uint8_t(track), 0, n.key, int16_t(std::max<uint8_t>(n.velocity, 1))});
            else host.send({Command::Type::NoteOff, uint8_t(track), 0, n.key});
        }
        unsigned n = B;
        if (next < notes.size()) n = unsigned(std::clamp<uint64_t>(notes[next].at - played, 1, B));
        host.render(out.data(), n, 1.0f);
        drain(host);
        if (!planned) { latency = tap == PrintTap::PreFx ? host.engine_latency(track) : host.track_latency(track); planned = true; }
        const float* l = host.print_left();
        const float* r = host.print_right();
        for (unsigned f = 0; f < n && x.size() < frames; ++f)
            if (played + f >= latency) x.push_back(0.5f * (l[f] + r[f]));
        played += n;
    }
    host.send({Command::Type::AllNotesOff, uint8_t(track)});
    return x;
}

} // namespace rhythmer
