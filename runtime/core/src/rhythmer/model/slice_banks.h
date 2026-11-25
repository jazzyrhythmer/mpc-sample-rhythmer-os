// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <cstdint>

// Chop's pads hold sixteen slices at a time, as Push's grid holds them all: Pad Bank steps through them (A slices
// 1-16, B 17-32 ...), and the bank in view is the selected slice's. The bank of slice n, how many banks `count`
// slices fill (one at least), and the slice under `pad` (0-15) while slice `selected` is in hand.
namespace rhythmer {

inline unsigned slice_bank(unsigned slice) { return slice / 16; }
inline unsigned slice_banks(unsigned count) { return count > 16 ? (count + 15) / 16 : 1; }
inline unsigned pad_slice(unsigned selected, unsigned pad) { return slice_bank(selected) * 16 + pad % 16; }

// Manual's take: the region plays from `from` since `at` ms (0: no take). Where it is heard at `now`, in frames as the
// project counts them (44.1 a ms), and whether it still plays (not yet at `end`).
inline uint32_t take_frame(uint32_t from, uint32_t at, uint32_t now) { return from + uint32_t(uint64_t(now - at) * 441 / 10); }
inline bool take_live(uint32_t at, uint32_t from, uint32_t end, uint32_t now) { return at && now >= at && take_frame(from, at, now) < end; }
// The red pad walks: while a take plays, the pads show the bank of the slice the next tap makes (`count`, the slices
// so far), and that slice's pad blinks red. After slice 16 they turn to bank B by themselves and its pad 1 is red, up
// to `most` slices; then the newest slice's bank, and no red.
inline unsigned view_slice(unsigned selected, unsigned count, bool taking, unsigned most) { return taking && count < most ? count : selected; }
inline bool red_slice(unsigned slice, unsigned count, unsigned most) { return slice == count && count < most; }

} // namespace rhythmer
