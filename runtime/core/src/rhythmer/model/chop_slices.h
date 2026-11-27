// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

// Chop's slices as plain data (parity phase 5; shell/chop.h cuts them), and a
// sample's chop: its slices, as the MPC keeps them in the sample's WAV file
// (shell/mpc_wav_slices.h), read when the sample loads and written back when
// Chop changes them; the region they span, and the type and amount they were
// cut with while the project is open.
namespace rhythmer {

enum class ChopMode : uint8_t { Threshold, Regions, Beats, Manual, Auto, Count };
inline const char* chop_mode_name(ChopMode m) {
    static const char* names[] {"Threshold", "Regions", "Beats", "Manual", "Auto"};
    return names[unsigned(m) % unsigned(ChopMode::Count)];
}
constexpr unsigned max_slices = 128;   // the MPC's: its Sample Slice note modifier picks slices 0-127
constexpr unsigned manual_slices = 16;   // the MPC's: "up to 16 slices per sample"
constexpr uint32_t min_slice = 882;      // 20 ms: no slice shorter

// Each slice its own start and end, as the MPC 3 keeps a sample's slices (its WAV's "atem" chunk, "Slice 0"...): slices
// cut from one region touch, one ending where the next starts; slices read from an MPC file may leave gaps between
// them. Where two touch, moving the edge they share moves both.
struct Slices {
    std::array<uint32_t, max_slices> start {};
    std::array<uint32_t, max_slices> stop {}; // where each ends (past its last frame); 0: the sample's end
    // Each slice's own Reverse (Chop's Shift + Loop): it plays the other way round from its pad, the rest as the pad
    // plays. Rhythmer's own (the WAV's "mlch" chunk); the MPC keeps no such mark.
    std::array<bool, max_slices> reverse {};
    unsigned count = 0;
    uint32_t end(unsigned n, uint32_t frames) const {
        uint32_t e = n < count ? stop[n] : 0;
        return e && e <= frames ? e : frames;
    }
    bool touches(unsigned n, uint32_t frames) const { return n + 1 < count && end(n, frames) == start[n + 1]; } // slice n and the next
    bool reversed(unsigned n) const { return n < count && reverse[n]; }
    bool any_reversed() const {
        for (unsigned n = 0; n < count; ++n) if (reverse[n]) return true;
        return false;
    }
    bool same(const Slices& o) const {
        if (count != o.count) return false;
        for (unsigned n = 0; n < count; ++n) if (start[n] != o.start[n] || stop[n] != o.stop[n] || reverse[n] != o.reverse[n]) return false;
        return true;
    }
    // One slice, from `from` to `to` (0: the sample's end).
    void whole(uint32_t from, uint32_t to) { start = {}; stop = {}; reverse = {}; start[0] = from; stop[0] = to; count = 1; }
    // A cut inside a slice: it ends there and a new slice runs on to where it ended (reversed if the one cut was).
    // False when full, outside every slice, or too close to either edge.
    bool add(uint32_t frame, uint32_t frames, unsigned most = max_slices) {
        if (!count || count >= std::min<unsigned>(most, max_slices)) return false;
        unsigned at = 0;
        while (at < count && !(start[at] < frame && frame < end(at, frames))) ++at;
        if (at == count || start[at] + min_slice > frame || frame + min_slice > end(at, frames)) return false;
        for (unsigned n = count; n > at + 1; --n) { start[n] = start[n - 1]; stop[n] = stop[n - 1]; reverse[n] = reverse[n - 1]; }
        start[at + 1] = frame; stop[at + 1] = stop[at]; stop[at] = frame; reverse[at + 1] = reverse[at]; ++count;
        return true;
    }
    // Takes slice n away; what it held (and any gap before it) joins the slice before (the MPC's Merge, and
    // Erase + pad), which keeps its own Reverse. The first slice has none before it.
    void remove(unsigned n) {
        if (n == 0 || n >= count) return;
        stop[n - 1] = stop[n];
        for (unsigned k = n + 1; k < count; ++k) { start[k - 1] = start[k]; stop[k - 1] = stop[k]; reverse[k - 1] = reverse[k]; }
        --count;
        start[count] = stop[count] = 0; reverse[count] = false;
    }
    // A new chop of the same region (another type or amount): a slice starting where a reversed one of `was` did
    // (within `reach` frames: Zero Snap moves cuts a little) stays reversed; the rest play as their pad.
    void keep_reverse(const Slices& was, uint32_t reach) {
        for (unsigned k = 0; k < count; ++k) {
            reverse[k] = false;
            for (unsigned n = 0; n < was.count && !reverse[k]; ++n)
                reverse[k] = was.reverse[n] && (was.start[n] > start[k] ? was.start[n] - start[k] : start[k] - was.start[n]) <= reach;
        }
    }
    // Slice n cut in half (Split); false when it is too short or the slices are full.
    bool split(unsigned n, uint32_t frames) {
        if (n >= count) return false;
        uint32_t a = start[n], b = end(n, frames);
        return b - a >= 2 * min_slice && add(a + (b - a) / 2, frames);
    }
    // K1 and K2: slice n's start or end; a neighbour touching it there moves with it.
    void move_start(unsigned n, uint32_t frame, uint32_t frames) {
        if (n >= count) return;
        const bool joined = n && touches(n - 1, frames);
        uint32_t lo = !n ? 0 : joined ? start[n - 1] + min_slice : end(n - 1, frames), hi = end(n, frames);
        hi = hi > min_slice ? hi - min_slice : 0;
        start[n] = std::clamp(frame, lo, std::max(lo, hi));
        if (joined) stop[n - 1] = start[n];
    }
    void move_end(unsigned n, uint32_t frame, uint32_t frames) {
        if (n >= count) return;
        uint32_t lo = start[n] + min_slice;
        if (touches(n, frames)) {
            uint32_t hi = end(n + 1, frames);
            hi = hi > min_slice ? hi - min_slice : 0;
            stop[n] = start[n + 1] = std::clamp(frame, lo, std::max(lo, hi));
        } else {
            uint32_t hi = n + 1 < count ? start[n + 1] : frames;
            stop[n] = std::clamp(frame, std::min(lo, hi), hi);
        }
    }
};

// Cuts made or moved by hand on Chop, as Simpler keeps its manual slices: a new chop by type or amount keeps them,
// and leaves out the automatic cuts that were moved away or taken out by hand. Kept with the chop while the project
// is open, not in the WAV file (the MPC has no such mark).
constexpr uint32_t hand_reach = 441; // 10 ms: an automatic cut this near a dropped one is that one (Zero Snap moves cuts a little)
struct HandCuts {
    std::array<uint32_t, max_slices> pins {}; // cuts made or moved by hand
    std::array<uint32_t, 32> drops {};        // automatic cuts moved away or taken out (the oldest goes when full)
    uint8_t pin_count = 0, drop_count = 0;
    static bool near(uint32_t a, uint32_t b) { return (a > b ? a - b : b - a) <= hand_reach; }
    bool pinned(uint32_t frame) const {
        for (unsigned n = 0; n < pin_count; ++n) if (near(pins[n], frame)) return true;
        return false;
    }
    bool dropped(uint32_t frame) const {
        for (unsigned n = 0; n < drop_count; ++n) if (near(drops[n], frame)) return true;
        return false;
    }
    bool empty() const { return !pin_count && !drop_count; }
    void clear() { pin_count = drop_count = 0; pins = {}; drops = {}; }
    bool same(const HandCuts& o) const {
        if (pin_count != o.pin_count || drop_count != o.drop_count) return false;
        for (unsigned n = 0; n < pin_count; ++n) if (pins[n] != o.pins[n]) return false;
        for (unsigned n = 0; n < drop_count; ++n) if (drops[n] != o.drops[n]) return false;
        return true;
    }
    void pin(uint32_t frame) {
        unpin(frame);
        if (pin_count < max_slices) pins[pin_count++] = frame;
    }
    void unpin(uint32_t frame) {
        unsigned kept = 0;
        for (unsigned n = 0; n < pin_count; ++n) if (!near(pins[n], frame)) pins[kept++] = pins[n];
        for (unsigned n = kept; n < pin_count; ++n) pins[n] = 0;
        pin_count = uint8_t(kept);
    }
    void drop(uint32_t frame) {
        if (dropped(frame)) return;
        if (size_t(drop_count) == drops.size()) { // the oldest makes room
            for (unsigned n = 1; n < drops.size(); ++n) drops[n - 1] = drops[n];
            --drop_count;
        }
        drops[drop_count++] = frame;
    }
    // A cut taken out (Erase, Merge): whichever it was, a new chop leaves it out.
    void take_out(uint32_t frame) { unpin(frame); drop(frame); }
    // A cut moved (K1, K2, the wheel): it stays where it went, and an automatic one is not cut again where it was.
    void move(uint32_t from, uint32_t to) {
        if (from == to) return;
        if (!pinned(from)) drop(from);
        unpin(from);
        pin(to);
    }
};

// A sample's chop (roadmap P3): the region of it Chop cut (to 0: the sample's end), how, and the slices.
struct SampleChop {
    uint32_t from = 0, to = 0;
    ChopMode mode = ChopMode::Threshold;
    int16_t amount = 0;
    Slices slices {};
    HandCuts hand {}; // the cuts made by hand, while the project is open
    bool unwritten = false; // changed here (or read from an older project file) and not yet in its WAV file
    bool used() const { return slices.count > 0; }
};

} // namespace rhythmer
