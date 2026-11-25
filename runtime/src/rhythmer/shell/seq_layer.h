// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/shell/ui_state.h"
#include "surface/controls.h"

// Seq (docs/RHYTHMER.md, Sequencing): tap to open the SEQUENCE hub (Sequences, Patterns, Roll, Motion: the one last
// open; inside it home, Sequences; on Step Edit back to the page under it, shell/page_stack.h), hold to steer,
// Shift + Seq for Step Edit. Held, on any page, the pads are the selected track's patterns, as on the Patterns page:
//   pad              launches that pattern on the selected track only (next bar; stopped, at once); an empty one
//                    starts as long as the one playing; pads tapped after the first in one hold chain the patterns
//   Shift + pad      launches that sequence: its pattern on every used track (shell/sequences.h); Shift + pads
//                    tapped after the first in one hold chain the sequences. With Shift held the pads and the
//                    sheet show the sequences.
//   pad held + pad   copies the selected track's first pattern onto the second
//   Erase + pad      clears that pattern of the selected track, asked first
//   Minus / Plus     the previous / next pattern
//   wheel            the tempo (Push: whole BPM or tenths)
//   K1-K3            length, Q, swing of the selected track's pattern (shell/bindings.cpp; Shift as on Patterns)
//   B1-B3            Sequences, Roll, Motion
//   Seq Record       a new take: the next empty pattern, armed (recording at once while playing)
// After seq_sheet_ms the screen shows the Sequence sheet (pages/seq_sheet.cpp); a release
// that quick with nothing touched is a tap. Shift + Seq: Step Edit. MIDI thread.
namespace rhythmer {

class Context;
constexpr uint32_t seq_sheet_ms = 300;

void seq_press(Context& c, bool pressed);
// A pad while Seq is held, or the release of a pad pressed then: the layer's. False when not.
bool seq_pad(Context& c, unsigned pad, bool down);
// A button while Seq is held (B1-B3, Minus / Plus, Seq Record, Push): the layer's. False when not.
bool seq_button(Context& c, SurfaceButton button);
// The wheel while Seq is held: the tempo. False when not held.
bool seq_wheel(Context& c, int delta);
// A pad while a pattern copy (Shift + Erase with Seq held) waits: the pattern to copy, then where.
bool pattern_copy_pad(Context& c, unsigned pad);

// Whether the screen shows the Sequence sheet now.
inline bool seq_sheet_shown(const UiState& ui, uint32_t now) {
    return ui.seq_held && !ui.seq_left && now - ui.seq_down >= seq_sheet_ms && !ui.name.open() && !ui.exit_open;
}
// Whether the pads are the selected track's patterns now (and light as such).
inline bool patterns_on_pads(const UiState& ui) { return ui.seq_held || ui.copying == UiState::Copying::Patterns; }

} // namespace rhythmer
