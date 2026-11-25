// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The undo list over the page (shell/history_list.h), Shift + Minus held: the steps kept, newest on top, each with
// its glyph, its track and how long ago; the state in hand marked NOW. The rows above the cursor are dim: they are
// undone once B3 (UNDO n / REDO n) or Push goes there. B1 BACK.
#include "rhythmer/pages/common.h"
#include "rhythmer/pages/page.h"
#include "rhythmer/shell/history_list.h"
#include "rhythmer/ui/widgets.h"
#include <algorithm>

namespace rhythmer {
using namespace ui;

namespace {
Icon icon_of(HistoryKind kind) {
    switch (kind) {
    case HistoryKind::Pad: return Icon::Pads;
    case HistoryKind::Pattern: return Icon::Pattern;
    case HistoryKind::Sound: return Icon::Knob;
    case HistoryKind::Mix: return Icon::Fader;
    case HistoryKind::Effects: return Icon::Sliders;
    case HistoryKind::Chop: return Icon::Scissors;
    case HistoryKind::Tempo: return Icon::Metro;
    case HistoryKind::Song: return Icon::Bars;
    case HistoryKind::Track: return Icon::Swap;
    case HistoryKind::Plugin: return Icon::Plug;
    case HistoryKind::Other: break;
    }
    return Icon::Undo;
}
} // namespace

void draw_history(Paint& p, const Snapshot& s) {
    const unsigned redos = s.history_redos, kept = unsigned(s.history.size()) - std::min<unsigned>(redos, unsigned(s.history.size()));
    const unsigned rows = history_rows(redos, kept), cursor = std::min<unsigned>(s.ui.history_row, rows - 1);
    const int step = history_step(cursor, redos);
    p.fill(0, 0, zone::width, zone::height, colour::bg);
    Header h = header_of(s); // the transport and the tempo as every page shows them; the sheet's own chips alone
    h.left.clear(); h.right.clear(); h.lead = 0;
    h.left.push_back(chip("HISTORY", colour::text));
    h.left.push_back(pill(juce::String(redos + kept) + (redos + kept == 1 ? " step" : " steps")));
    h.right.push_back(tag_of(s, s.ui.track));
    juce::String go = step < 0 ? "UNDO " + juce::String(-step) : step > 0 ? "REDO " + juce::String(step) : juce::String();
    SoftKeys keys {{"BACK", "", go}, -1, colour_of(s), {KeyIcon::Back, KeyIcon::None, KeyIcon::None}};
    if (step < 0) keys.glyphs[2] = Icon::Undo;
    frame(p, s, keys, h);
    // Eight rows: the picture and the knob strip's room (K1-K3 wait while the list is open).
    constexpr unsigned visible = 8;
    unsigned first = cursor < 3 ? 0 : std::min(cursor - 3, rows > visible ? rows - visible : 0);
    for (unsigned n = 0; n < visible && first + n < rows; ++n) {
        const unsigned row = first + n;
        ListRow r;
        r.current = row == cursor;
        r.dim = history_row_dim(row, cursor) && !r.current;
        if (row < s.history.size()) {
            const auto& e = s.history[row];
            r.name = juce::String(e.what);
            r.icon = icon_of(e.kind);
            r.accent = track_colour(e.track);
            r.badge = juce::String(unsigned(e.track) + 1);
            r.fact = juce::String(history_age(s.now, e.at));
        } else { // START: before every step kept
            r.name = "Start";
            r.icon = Icon::Undo;
            r.accent = colour::text;
        }
        if (row == history_now(redos)) r.chip = "NOW";
        list_row(p, zone::pad, zone::picture_y + 0.5f + n * (list_row_h + list_row_gap), zone::width - 2 * zone::pad, r);
    }
}

} // namespace rhythmer
