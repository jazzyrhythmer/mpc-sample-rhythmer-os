// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/dsp.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

// Rendering the mix as it plays (Resample, Shift + Pad 11, and the song's
// audio mixdown): the engine host starts it with Play and ends it on a
// clock tick, sample for sample; render() hands each block of the master
// (before main volume) to process(), which copies the frames inside those
// bounds into a ring. The service thread drains the ring as it fills, into
// a sample or straight into a WAV file, so a render may run for as long as
// the song does while the ring stays small (11 s at 96 kHz, 8 MB, made at start).
//
// States: Idle -> Cued (armed, waiting for Play) -> Running -> (Tail: the
// song ended, on until the mix falls silent) -> Done. Stop meanwhile is
// Cancelled; a ring the service thread could not keep up with is Failed.
// The audio thread moves it on, the service thread puts it back to Idle
// once it has taken everything (release()). The render is kept as it plays,
// at the device's rate (never converted): what pop() gives is at rate(), and
// frames() counts it in 44.1 kHz frames, as the pages count everything.
namespace rhythmer {

class Bounce {
public:
    enum class State : uint8_t { Idle, Cued, Running, Tail, Done, Cancelled, Failed };
    static constexpr uint32_t ring_frames = 1u << 20;        // 10.9 s at 96 kHz
    static constexpr float silence_seconds = 0.2f;           // a tail ends after 200 ms below -70 dB
    static constexpr uint32_t tail_seconds = 10;             // or after 10 s

    // `ring` false: none made (a print host's, never cued).
    explicit Bounce(bool ring = true) : ring_(ring ? new float[size_t(ring_frames) * 2]() : nullptr) { set_rate(dsp::sample_rate()); }

    // Audio thread (the engine host).
    void cue() {
        auto s = state_.load();
        if (s != State::Idle || !ring_) return;
        read_.store(0); write_.store(0); frames_.store(0);
        start_at = -1; stop_at = -1; tail_frames = 0; quiet = 0;
        render_rate_.store(device_rate, std::memory_order_relaxed);
        state_.store(State::Cued);
    }
    // Audio thread: the device's rate (EngineHost::set_audio_format). A render meanwhile is cancelled: its
    // frames would change rate partway.
    void set_rate(float device) {
        device_rate = std::max(8000u, unsigned(std::lround(device)));
        cancel();
        if (state_.load() == State::Idle) render_rate_.store(device_rate, std::memory_order_relaxed);
    }
    bool cued() const { return state_.load(std::memory_order_relaxed) == State::Cued; }
    bool running() const { return state_.load(std::memory_order_relaxed) == State::Running; }
    void begin(unsigned offset) { if (cued()) { start_at = int(offset); state_.store(State::Running); } }
    void finish(unsigned offset) { if (running() && stop_at < 0) stop_at = int(offset); } // the first: later ticks of the block are past it
    void tail() { if (running()) { state_.store(State::Tail); tail_frames = 0; quiet = 0; } }
    void cancel() { // a tail is cut short, but kept
        auto s = state_.load();
        if (s == State::Tail) state_.store(State::Done);
        else if (s == State::Cued || s == State::Running) state_.store(State::Cancelled);
    }
    // Roadmap P4: the frames of a block of `n` that process() will take (stems are taken by the same bounds), none when
    // no render runs. Audio thread, before process().
    bool span(unsigned n, unsigned& from, unsigned& to) const {
        auto s = state_.load(std::memory_order_relaxed);
        if (s != State::Running && s != State::Tail) return false;
        from = start_at >= 0 ? std::min<unsigned>(unsigned(start_at), n) : 0;
        to = stop_at >= 0 ? std::min<unsigned>(unsigned(stop_at), n) : n;
        return to > from;
    }
    // After each block is mixed: the frames of it that belong to the render.
    void process(const float* l, const float* r, unsigned n) {
        auto s = state_.load(std::memory_order_relaxed);
        if (s != State::Running && s != State::Tail) return;
        unsigned from = start_at >= 0 ? std::min<unsigned>(unsigned(start_at), n) : 0;
        unsigned to = stop_at >= 0 ? std::min<unsigned>(unsigned(stop_at), n) : n;
        start_at = -1;
        uint64_t w = write_.load(std::memory_order_relaxed), rd = read_.load(std::memory_order_acquire);
        if (to > from && w + (to - from) - rd > ring_frames) { state_.store(State::Failed); return; }
        float loudest = 0;
        uint32_t kept = 0;
        for (unsigned f = from; f < to; ++f, ++kept) {
            loudest = std::max({loudest, std::fabs(l[f]), std::fabs(r[f])});
            size_t i = size_t((w + kept) % ring_frames) * 2;
            ring_[i] = l[f]; ring_[i + 1] = r[f];
        }
        if (kept) {
            write_.store(w + kept, std::memory_order_release);
            frames_.fetch_add(kept, std::memory_order_relaxed);
        }
        if (stop_at >= 0) { stop_at = -1; state_.store(State::Done); return; }
        if (s == State::Tail) {
            tail_frames += kept;
            quiet = loudest < 0.0003f ? quiet + kept : 0;
            if (quiet >= uint32_t(silence_seconds * float(device_rate)) || tail_frames >= tail_seconds * device_rate) state_.store(State::Done);
        }
    }

    // Any thread.
    State state() const { return state_.load(std::memory_order_acquire); }
    unsigned rate() const { return render_rate_.load(std::memory_order_relaxed); } // the frames' rate (the device's at cue())
    uint32_t taken() const { return frames_.load(std::memory_order_relaxed); }    // rendered so far, at rate()
    uint32_t frames() const {                                                      // the same in 44.1 kHz frames
        return uint32_t((uint64_t(taken()) * uint32_t(dsp::model_rate) + rate() / 2) / std::max(1u, rate()));
    }

    // Service thread: up to `max` frames, interleaved, as they come; the count.
    unsigned pop(float* interleaved, unsigned max) {
        uint64_t rd = read_.load(std::memory_order_relaxed), w = write_.load(std::memory_order_acquire);
        unsigned n = unsigned(std::min<uint64_t>(w - rd, max));
        for (unsigned f = 0; f < n; ++f) {
            size_t i = size_t((rd + f) % ring_frames) * 2;
            interleaved[2 * f] = ring_[i]; interleaved[2 * f + 1] = ring_[i + 1];
        }
        read_.store(rd + n, std::memory_order_release);
        return n;
    }
    bool drained() const { return read_.load(std::memory_order_acquire) == write_.load(std::memory_order_acquire); }
    // Service thread, once Done, Cancelled or Failed and taken: ready for the next.
    void release() {
        auto s = state_.load();
        if (s == State::Done || s == State::Cancelled || s == State::Failed) state_.store(State::Idle);
    }

private:
    std::unique_ptr<float[]> ring_;
    unsigned device_rate = unsigned(dsp::model_rate); // audio thread: the device's
    std::atomic<unsigned> render_rate_ {unsigned(dsp::model_rate)};
    std::atomic<State> state_ {State::Idle};
    std::atomic<uint64_t> read_ {0}, write_ {0};
    std::atomic<uint32_t> frames_ {0};
    int start_at = -1, stop_at = -1; // audio thread: this block's bounds
    uint32_t tail_frames = 0, quiet = 0;
};

// Roadmap P4, stems to the card: each track (after its chain, strip, level, pan and mute, as it leaves for the master or
// its submix), each bus's return and each submix (the mixer: after its chain, strip, level, pan and mute, as it joins the
// master), stereo, the same frames as the mixdown beside them; up to stem_count of them, those in `mask` taken. Then the
// pad stems: a kit pad's voices apart (after its own chain, volume and pan, at its track's level, pan and mute, as its
// pad sends are taken), up to pad_most in one render, each the pad set_pad() names. Only the stems taken have rows. Made
// by the service thread when first wanted, grown when a render needs more rows, handed to the engine host, which
// writes; the service thread reads.
class StemRing {
public:
    static constexpr unsigned stem_count = 28;      // tracks 0-15, buses A-D 16-19, submixes 20-27
    static constexpr unsigned pad_most = 96;        // pad stems in one render at most
    static constexpr unsigned row_most = stem_count + pad_most; // a stem's number: 0-27 as above, then pad stem n at 28 + n
    static constexpr unsigned tracks = 16, kit_pads = 128;      // track_count and sampler_pads (checked in engine_host.cpp)
    static constexpr uint32_t ring_frames = 1u << 17; // 2.7 s at 48 kHz: the service thread drains every pass
    // Service thread (or the MIDI thread), before the render is armed: which stems, from the start, and how many pad
    // stems after them (set_pad() says whose). Makes room for its rows (1 MB each, half that past 48 rows).
    void arm(uint32_t mask, unsigned pads = 0) {
        pads = std::min(pads, pad_most);
        unsigned rows = 0;
        for (unsigned s = 0; s < stem_count; ++s) if ((mask >> s) & 1) row_stem_[rows++] = uint8_t(s);
        for (unsigned n = 0; n < pads; ++n) row_stem_[rows++] = uint8_t(stem_count + n);
        frames_ = rows > 48 ? ring_frames / 2 : ring_frames;
        if (size_t(rows) * frames_ * 2 > room_) { room_ = size_t(rows) * frames_ * 2; ring_.reset(new float[room_]()); }
        for (auto& track : pad_row_) track.fill(-1);
        rows_ = rows; pads_ = pads;
        for (auto& d : pad_delay_) d.store(0);
        mask_.store(mask); read_.store(0); write_.store(0); failed_.store(false);
    }
    // Service thread, after arm(): pad stem n is pad `pad` of track `track`.
    void set_pad(unsigned n, unsigned track, unsigned pad) {
        if (n < pads_ && track < tracks && pad < kit_pads) pad_row_[track][pad] = int16_t(n);
    }
    uint32_t mask() const { return mask_.load(std::memory_order_relaxed); }
    unsigned pads() const { return pads_; }
    unsigned rows() const { return rows_; }
    unsigned row_stem(unsigned row) const { return row < rows_ ? row_stem_[row] : 0; } // its stem's number
    // Audio thread: track t's pads, each its pad stem's number or -1.
    const int16_t* pad_rows(unsigned track) const { return pad_row_[track % tracks].data(); }
    // The frames track t's pad stems come early by (taken before its chain and its delay compensation, as its pad
    // sends are): the audio thread says, the writer puts that much silence before them.
    void set_pad_delay(unsigned track, uint32_t frames) { pad_delay_[track % tracks].store(frames, std::memory_order_relaxed); }
    uint32_t pad_delay(unsigned track) const { return pad_delay_[track % tracks].load(std::memory_order_relaxed); }
    // Audio thread: frames [from, to) of this block, stem s's left and right at `l[s]`, `r[s]` (nullptr: silence),
    // for s up to stem_count, or row_most when pads() > 0.
    void write(const float* const* l, const float* const* r, unsigned from, unsigned to) {
        uint64_t w = write_.load(std::memory_order_relaxed), rd = read_.load(std::memory_order_acquire);
        if (w + (to - from) - rd > frames_) { failed_.store(true, std::memory_order_relaxed); return; }
        for (unsigned f = from; f < to; ++f) {
            float* frame = ring_.get() + size_t((w + f - from) % frames_) * rows_ * 2;
            for (unsigned row = 0; row < rows_; ++row) {
                const unsigned s = row_stem_[row];
                frame[2 * row] = l[s] ? l[s][f] : 0.0f; frame[2 * row + 1] = r[s] ? r[s][f] : 0.0f;
            }
        }
        write_.store(w + (to - from), std::memory_order_release);
    }
    // Service thread: up to `max` frames of stem `s` and every other into `out` (a stem's frames apart, interleaved
    // stereo, stem_count blocks of 2 * max floats, a stem not taken silent; no pad stems); the count.
    unsigned pop(float* out, unsigned max) {
        uint64_t rd = read_.load(std::memory_order_relaxed), w = write_.load(std::memory_order_acquire);
        unsigned n = unsigned(std::min<uint64_t>(w - rd, max));
        std::fill(out, out + size_t(stem_count) * 2 * max, 0.0f);
        for (unsigned f = 0; f < n; ++f) {
            const float* frame = ring_.get() + size_t((rd + f) % frames_) * rows_ * 2;
            for (unsigned row = 0; row < rows_; ++row) {
                const unsigned s = row_stem_[row];
                if (s >= stem_count) continue;
                out[size_t(s) * 2 * max + 2 * f] = frame[2 * row]; out[size_t(s) * 2 * max + 2 * f + 1] = frame[2 * row + 1];
            }
        }
        read_.store(rd + n, std::memory_order_release);
        return n;
    }
    // Service thread: the same by row, pad stems too: rows() blocks of 2 * max floats, row_stem() names each.
    unsigned pop_rows(float* out, unsigned max) {
        uint64_t rd = read_.load(std::memory_order_relaxed), w = write_.load(std::memory_order_acquire);
        unsigned n = unsigned(std::min<uint64_t>(w - rd, max));
        for (unsigned f = 0; f < n; ++f) {
            const float* frame = ring_.get() + size_t((rd + f) % frames_) * rows_ * 2;
            for (unsigned row = 0; row < rows_; ++row) {
                out[size_t(row) * 2 * max + 2 * f] = frame[2 * row]; out[size_t(row) * 2 * max + 2 * f + 1] = frame[2 * row + 1];
            }
        }
        read_.store(rd + n, std::memory_order_release);
        return n;
    }
    bool drained() const { return read_.load(std::memory_order_acquire) == write_.load(std::memory_order_acquire); }
    bool failed() const { return failed_.load(std::memory_order_relaxed); }
private:
    std::unique_ptr<float[]> ring_;
    size_t room_ = 0;                                 // floats ring_ holds
    uint32_t frames_ = ring_frames;
    unsigned rows_ = 0, pads_ = 0;
    std::array<uint8_t, row_most> row_stem_ {};
    std::array<std::array<int16_t, kit_pads>, tracks> pad_row_ {};
    std::atomic<uint32_t> mask_ {0};
    std::atomic<uint64_t> read_ {0}, write_ {0};
    std::atomic<bool> failed_ {false};
    std::array<std::atomic<uint32_t>, tracks> pad_delay_ {};
};

} // namespace rhythmer
