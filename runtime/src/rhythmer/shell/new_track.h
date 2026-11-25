// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/shell/ui_state.h"
#include "surface/controls.h"
#include <algorithm>
#include <cstdint>

// An empty track's Stage: what it can become, as a carousel (pages/stage_page.cpp),
// in two steps as on the MPC: the track type (Drum, Keygroup, Plugin, MIDI, Audio), then
// where it comes from (a Drum's samples, a MIDI track's port). The wheel moves
// along it, Push opens the type's sources (a type with one goes straight on) or
// makes the source in the middle, B1 BACK goes back to the types. Picked with a pad
// (Tracks, Pad Bank held, Song's lanes) or Push on Tracks, the track goes straight
// there and, once made, back to the page it was picked on (B1 BACK goes back
// empty): a kit or a MIDI track at once, a plug-in once one is picked from the
// card, a kit from Samples once its first sample loads, a keygroup once its
// instrument (or WAV) loads from Samples, an audio track at once, on Stage's
// INPUT (freeze plan F3). B1 CANCEL at the top of Samples leaves that kit or
// keygroup empty again.
namespace rhythmer {

class Context;

enum class NewChoice : uint8_t { Plugin, Kit, Samples, Keygroup, Record, MidiOut, MidiUsb, MidiAllUsb, Audio, Count };
constexpr unsigned new_choice_count = unsigned(NewChoice::Count);

// The track types, in the MPC's order, and each one's sources (the first is the one the carousel opens on,
// a type with one source makes it at once).
enum class NewType : uint8_t { Drum, Keygroup, Plugin, Midi, Audio, Count };
constexpr unsigned new_type_count = unsigned(NewType::Count);
struct NewSources { NewChoice items[3]; uint8_t count, first; };
constexpr NewSources type_sources(NewType type) {
    switch (type) {
    case NewType::Drum: return {{NewChoice::Samples, NewChoice::Kit, NewChoice::Record}, 3, 0};
    case NewType::Keygroup: return {{NewChoice::Keygroup}, 1, 0};
    case NewType::Plugin: return {{NewChoice::Plugin}, 1, 0};
    case NewType::Midi: return {{NewChoice::MidiOut, NewChoice::MidiUsb, NewChoice::MidiAllUsb}, 3, 1}; // USB-C first
    case NewType::Audio: return {{NewChoice::Audio}, 1, 0};
    default: return {{NewChoice::Plugin}, 1, 0};
    }
}
// The cards on the carousel now: the types, or the sources of the type opened.
inline unsigned new_pick_count(const UiState& ui) {
    return ui.new_sources ? type_sources(NewType(ui.new_type % new_type_count)).count : new_type_count;
}
// The source in the middle (the types: none, Count).
inline NewChoice new_pick_choice(const UiState& ui) {
    if (!ui.new_sources) return NewChoice::Count;
    auto list = type_sources(NewType(ui.new_type % new_type_count));
    return list.items[ui.new_pick % list.count];
}

// Where the carousel is drawn at `now`: the card picked, or on the way there (an ease-out over carousel_ms
// from new_pick_from, set as the wheel moves, at the ends a bump back, and a slide in as the carousel opens).
constexpr uint32_t carousel_ms = 220;
inline float new_pick_shown(const UiState& ui, uint32_t now) {
    float to = float(ui.new_pick % new_pick_count(ui));
    uint32_t gone = now - ui.new_pick_at;
    if (!ui.new_pick_at || gone >= carousel_ms) return to;
    float t = 1 - float(gone) / float(carousel_ms);
    return to + (ui.new_pick_from - to) * t * t * t; // 1 - (1 - x)^3 of the way
}
// The wheel `delta` detents along the carousel (at `now`).
inline void new_pick_turn(UiState& ui, int delta, uint32_t now) {
    float shown = new_pick_shown(ui, now);
    const int count = int(new_pick_count(ui));
    int to = std::clamp<int>(int(ui.new_pick % unsigned(count)) + delta, 0, count - 1);
    ui.new_pick_from = to == int(ui.new_pick) ? float(to) + (delta < 0 ? -0.18f : 0.18f) : shown; // at an end: a bump
    ui.new_pick = uint8_t(to);
    ui.new_pick_at = now ? now : 1;
}

// An empty track in hand on its carousel (or the plug-in picker it opened): the track is made, or another
// picked (Pad Bank + pad, or B1 CANCEL), before anything else. The other buttons wait; Play, Stop and Tap
// Tempo still work, on the carousel Pad Bank (+ pad) too, and the MENU (the encoder held) with its B1-B3 and
// GO's pads, the way to Project and Settings in a new project, whose tracks are all empty.
bool new_track_locked(const Context& c);
// A button pressed or let go while locked: true when the lock took it (a taken press's release goes too).
bool new_track_lock(Context& c, SurfaceButton button, bool pressed);
// The used track nearest the selected one (the lower of two as near), -1 when every track is empty.
int nearest_used_track(const Context& c);
// B1 CANCEL on the carousel: the nearest used track, back on the page the empty one was picked on.
void cancel_new_track(Context& c);

// The selected track empty: its Stage, the page in view kept to come back to.
void offer_new_track(Context& c);
// Push on the carousel: a type's sources opened (one source: made at once), or the source in the middle made.
void new_track_push(Context& c);
// B1 BACK on the sources: the types again, the type opened in the middle.
void new_track_types(Context& c);
// A source made: the track made, one undo step.
void make_new_track(Context& c, NewChoice choice);
// Replace only the sound/type; retain every pattern and mixer/insert settings.
void change_track_type(Context& c, EngineKind kind);
void open_track_actions(Context& c);
// A sample loaded: a kit made for Samples goes back to the page it was picked on.
void new_track_landed(Context& c);
// B1 CANCEL at the top of Samples: back, and a kit just made there for its first sample empty again.
void samples_cancel(Context& c);

} // namespace rhythmer
