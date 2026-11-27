// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// PATTERNS (the SEQUENCE hub's B1 pressed again, after Sequences): the selected track's sixteen patterns of the bank in view, drawn
// as the pads sit (pad 1 bottom left, 13 top left), so a cell is the pad that launches it. The pads launch them
// as Seq held does (shell/seq_layer.h): at the next bar (stopped, at once), hold one and tap another to copy,
// Erase + pad clears (asked first); they light as the cells. Pad Bank steps the bank (A-H), as Seq held + Pad Bank.
// In each cell: the number, ▶ playing, NEXT queued, a tinted cell for notes, an arrow for a follow action, and the
// sequences that keep the pattern (S1 S2; model/scene_links.h): an edit reaches each of them.
// Right: the eight tracks in view, each with the pattern it plays and how far along it is, OFF for a track off in
// its sequence; a track off what the sequence in hand keeps for it carries a not-equal mark (shell/sequences.h).
// The wheel selects the track (the list on the right follows it, eight at a time). The header shows the sequence in
// hand, and Push launches it again (every track back on what it keeps); Shift + wheel moves the playhead by Q (stopped: where Shift + Play carries on from). Minus / Plus move the track. K1-K3: the pattern's length, Q, swing (Shift: any bars, time signature,
// the metronome's level). B1-B3 PATTERNS / ROLL / MOTION (B1 again: SCENES); Shift + B1 SCENE (K1 the scene's tempo, Push
// names it), Shift + B2 FOLLOW (the pattern's follow action, Shift + K1 OR, Push puts it on its column), Shift + B3
// VARY (a variation of the pattern into the next empty one; the wheel picks, Push or B3 makes it).
#include "rhythmer/model/chord_gen_access.h"
#include "rhythmer/pages/common.h"
#include "rhythmer/pages/pages.h"
#include "rhythmer/model/generators.h"
#include "rhythmer/shell/features.h"
#include "rhythmer/shell/sequences.h"
#include "rhythmer/shell/step_lane.h"
#include "rhythmer/model/step_order.h"
#include "rhythmer/ui/widgets.h"
#include <algorithm>

namespace rhythmer {
namespace {
using namespace ui;

constexpr float cell_w = 47, cell_h = 32.5f, cell_gap = 3, grid_x = zone::pad, grid_y = zone::picture_y + 3;
constexpr float list_x = 214, list_w = zone::width - zone::pad - list_x;

// A track's direction as its play mark: forward, backward (REV), shuffled (RND, Shift + Loop).
Icon direction_icon(const Track& track) {
    switch (direction_of(track.direction)) {
    case PlayDirection::Reverse: return Icon::PlayBack;
    case PlayDirection::Random: return Icon::Dice;
    default: return Icon::Play;
    }
}

// The sequence in view: the sequence in hand.
unsigned scene_of(const Project& project, const UiState& ui) { return sequence_in_view(project, ui); }

void draw_cell(Paint& p, const Snapshot& s, unsigned k) {
    unsigned t = s.ui.track % track_count, n = s.ui.pad_pattern(k);
    float x = grid_x + (k % 4) * (cell_w + cell_gap), y = grid_y + (3 - k / 4) * (cell_h + cell_gap);
    auto c = track_colour(t);
    const auto& track = s.project.tracks[t];
    const auto& sketch = s.sketches[t][n];
    bool notes = track.used() && sketch.notes;
    unsigned live = s.playing ? s.playing_patterns[t] : track.pattern;
    bool playing = notes && n == live && !track.off && (!s.playing || s.positions[t] >= 0);
    bool queued = track.used() && s.playing && n == track.pattern && n != live;
    juce::Colour ink = notes ? colour::text : colour::dim;
    if (playing) {
        p.fill(x, y, cell_w, cell_h, c, 4);
        ink = colour::bg;
    } else if (queued) {
        bool blink = (s.now / 150) % 2;
        p.fill(x, y, cell_w, cell_h, c.withAlpha(blink ? 0.35f : 0.15f), 4);
        p.ring(x + 0.75f, y + 0.75f, cell_w - 1.5f, cell_h - 1.5f, c, 4, 1.5f);
    } else if (notes) {
        p.fill(x, y, cell_w, cell_h, c.withAlpha(0.22f), 4);
    } else {
        p.fill(x, y, cell_w, cell_h, colour::quiet, 4);
    }
    auto number = juce::String(n + 1);
    auto style = plain(ink, type::small, Weight::Bold);
    p.text(number, x + 5, y + 3, 24, 13, style);
    if (playing) icon(p, direction_icon(track), x + cell_w - 12, y + 4, 8, colour::bg, 1.4f);
    else if (queued) p.text("NEXT", x + cell_w - 34, y + 3, 30, 13, plain(c, type::small, Weight::Bold), juce::Justification::centredRight);
    else if (notes && sketch.follows) icon(p, Icon::ArrowRight, x + cell_w - 12, y + 4, 8, colour::sec, 1.4f); // a follow action
    if (notes && sketch.follows && playing) icon(p, Icon::ArrowRight, x + cell_w - 22, y + 4, 8, colour::bg, 1.4f);
    // The sequences that keep this pattern: the first two, then how many more.
    unsigned keeping[2] {};
    unsigned count = track.used() ? scenes_keeping(track, n, keeping, 2) : 0;
    if (count) {
        juce::String text = "S" + juce::String(keeping[0] + 1);
        if (count == 2) text += " S" + juce::String(keeping[1] + 1);
        else if (count > 2) text += " +" + juce::String(count - 1);
        auto foot = plain(playing ? colour::bg : colour::sec, type::small, Weight::SemiBold);
        p.text(text, x + 5, y + cell_h - 15, cell_w - 8, 13, p.fit(text, foot, cell_w - 8, type::floor));
    }
    if (s.ui.pattern_held == int(n)) p.ring(x - 1.5f, y - 1.5f, cell_w + 3, cell_h + 3, colour::text, 5, 2);
}

// The eight tracks in view: number, ▶ the pattern it plays (– none), how far along.
void draw_tracks(Paint& p, const Snapshot& s, unsigned first) {
    float rh = (zone::picture_h - 4) / 8.0f, y0 = grid_y - 1;
    for (unsigned r = 0; r < 8; ++r) {
        unsigned t = first + r;
        const auto& track = s.project.tracks[t];
        auto c = track_colour(t);
        float y = y0 + r * rh, cy = y + rh / 2;
        bool selected = t == s.ui.track;
        if (selected) { p.fill(list_x, y + 1, list_w, rh - 2, colour::surf, 3); p.fill(list_x + 2, y + 2.5f, 15, rh - 5, c, 3); }
        p.text(juce::String(t + 1), list_x + 2, y, 15, rh, plain(selected ? colour::bg : c, type::small, Weight::Bold), juce::Justification::centred);
        if (!track.used()) continue;
        unsigned live = s.playing ? s.playing_patterns[t] : track.pattern;
        if (track.off) { // off in its sequence (SEQ + STOP, or the sequence keeps it off)
            p.fill(list_x + 22, cy - 3, 6, 6, colour::sec, 1);
            p.text("OFF", list_x + 31, y, 30, rh, plain(colour::sec, type::small, Weight::SemiBold));
            if (own_pattern(s.project, s.ui, t)) icon(p, Icon::NotEqual, list_x + 56, cy - 4.5f, 9, colour::text, 1.3f);
            continue;
        }
        bool stopped = s.playing && s.positions[t] < 0;
        if (!s.sketches[t][live].notes || stopped) {
            p.text(juce::String::fromUTF8("\xe2\x80\x93"), list_x + 22, y, 20, rh, plain(colour::dim, type::small));
            continue;
        }
        icon(p, direction_icon(track), list_x + 21, cy - 3.5f, 7, c, 1.2f); // its direction: forward, REV, RND
        float nw = p.text(juce::String(live + 1), list_x + 31, y, 22, rh, plain(colour::text, type::small, Weight::SemiBold));
        float after = list_x + 33 + nw;
        if (track.speed != speed_normal) // its speed when not x1 (Shift + K3)
            after += 2 + p.text(juce::String::fromUTF8(speed_name(track.speed)), after, y, 40, rh, plain(colour::sec, type::small, Weight::SemiBold));
        bool own = own_pattern(s.project, s.ui, t); // off what the sequence in hand keeps for it
        if (own) { icon(p, Icon::NotEqual, after, cy - 4.5f, 9, colour::text, 1.3f); after += 11; }
        float bx = std::max(list_x + 54, after + 4), bw = list_x + list_w - 4 - bx;
        if (bw < 12) continue; // no room left for how far along
        p.fill(bx, cy - 1, bw, 2, colour::line, 1);
        uint32_t length = std::max<uint32_t>(s.sketches[t][live].length, 1);
        float done = s.playing && s.positions[t] >= 0 ? float(uint32_t(s.positions[t]) % length) / length : 0;
        if (direction_of(track.direction) == PlayDirection::Random) { // shuffled: where it plays, as a mark
            if (s.playing && s.positions[t] >= 0) p.fill(bx + (bw - 3) * done, cy - 2, 3, 4, c, 1);
        } else if (done > 0) p.fill(bx, cy - 1, bw * done, 2, c, 1);
    }
}

void draw(Paint& p, const Snapshot& s) {
    unsigned first = s.ui.track / 8 * 8, column = scene_of(s.project, s.ui);
    auto c = colour_of(s);
    Header h = header_of(s);
    // After the pads chip (PATTERNS) the header has room for one more: the sequence in view (FOLLOW: the pattern in
    // hand). The bank follows and shows only where there is room; the cells' numbers say it too (1-16 is bank A).
    const auto& scene = s.project.scenes[column];
    if (s.ui.patterns_cards == 2) // FOLLOW: the pattern in hand's
        h.left.push_back(chip("T" + juce::String(s.ui.track + 1) + " P" + juce::String(s.track().pattern % pattern_count + 1), c));
    else
        {
        juce::String id = "SEQ " + juce::String(column + 1), name(scene.name);
        h.left.push_back(chip(name.isEmpty() || name == id ? id : id + " " + name, colour::amber)); // a name of its own beside its number
    }
    h.left.push_back(bank_pill(s));
    // Rec Quantize (Shift + Pad 10) on or off, after the scene: the header leaves it out when there is no room. Q is K2's card.
    h.left.push_back(pill(Icon::Magnet, s.ui.settings.rec_quantize ? "ON" : "OFF", s.ui.settings.rec_quantize ? colour::mid : colour::dim));
    const auto direction = direction_of(s.track().direction); // its speed (Shift + K3) and direction (Shift + Loop)
    if (s.track().speed != speed_normal || direction != PlayDirection::Forward) {
        juce::String speed = s.track().speed != speed_normal ? juce::String::fromUTF8(speed_name(s.track().speed)) : juce::String(direction_name(direction));
        h.right.push_back(direction == PlayDirection::Forward ? pill(speed, c) : pill(direction_icon(s.track()), speed, c));
    }
    h.right.push_back(tag_of(s, s.ui.track));
    SoftKeys keys {{"PATTERNS", "ROLL", motion_key(s)}, 0, c};
    keys.dots[0] = 2; keys.dot_at[0] = 1;
    if (s.ui.sheet_open) keys = SoftKeys {{"BACK", s.ui.gen8_mode==0?"VARY":s.ui.gen8_mode==1?"SETUP":s.ui.gen8_mode==5?"PAGE":s.ui.gen8_mode==4?"SETTINGS":"GEN", s.ui.gen8_mode==3?(s.project.gen8.assign?"SELECT":"DONE"):((s.ui.gen8_mode==0&&s.ui.sheet_row>=4)||(s.ui.gen8_mode==2&&s.ui.sheet_row>=5))?"OPEN":"APPLY"}, 2, c, {KeyIcon::Back, KeyIcon::None, KeyIcon::Check}};
    if(s.ui.sheet_open&&s.ui.gen8_mode==5){keys.dots[1]=5;keys.dot_at[1]=s.ui.chord_gen_page%5;}
    else if (s.ui.shift&&!s.ui.sheet_open) {
        keys = SoftKeys {{"SEQUENCE", follow_shown(s.ui) ? "FOLLOW" : "", "GEN"}, -1, c};
        if (s.ui.patterns_cards == 1) keys.tints[0] = colour::amber;
        if (s.ui.patterns_cards == 2) keys.tints[1] = colour::amber;
    }
    if(s.ui.sheet_open && s.ui.gen8_mode==2 && s.ui.break_pending>=0 && s.ui.break_job_track==s.ui.track)
        keys=SoftKeys{{"CANCEL","",""},-1,c,{KeyIcon::Back,KeyIcon::None,KeyIcon::None}};
    frame(p, s, keys, h);

    for (unsigned k = 0; k < pattern_pads; ++k) draw_cell(p, s, k);
    draw_tracks(p, s, first);

    if (s.ui.sheet_open) { // Push: VARY, a variation of the pattern into the next empty one (roadmap P1)
        p.dim_zone(zone::picture_y, zone::picture_h, 0.7f);
        MenuSheet m;
        m.title = "INTO THE NEXT EMPTY PATTERN"; m.accent = c;
        bool kit = s.track().engine == EngineKind::Sampler || drum_pads(s.track());
        for (unsigned v = 0; v < unsigned(Variation::Count); ++v) {
            bool here = variation_on_kit(Variation(v)) || !kit;
            m.rows[v] = {variation_name(Variation(v)), v < 3 ? juce::String(s.ui.vary_amount) + " %" : juce::String(), false, false, !here};
        }
        if(s.ui.gen8_mode!=1) {
            static const char* gen[] {"GENERATE SONG","MUTATE SONG","GENERATE TRACK","MUTATE TRACK","ENGINE GEN","GEN SETUP"};
            static const char* brk[] {"GENRE CHOPS","STRAIGHT","RANDOM CHOPS","MUTATE CHOPS","TAIL FILL","BROWSE SAMPLE","BRK PERF"};
            bool br=s.track().engine==EngineKind::Sampler&&s.track().break_bars;
            bool acid=s.track().engine==EngineKind::Plugin&&s.track().plugin<max_plugins&&std::string(s.project.plugins[s.track().plugin].name)=="Acid Engine";
            m.count=(s.ui.gen8_mode==4||s.ui.gen8_mode==5)?2:s.ui.gen8_mode==2?7:6;
            for(unsigned i=0;i<m.count;i++) m.rows[i]={s.ui.gen8_mode==2?brk[i]:gen[i],{},false,false,false};
            if(s.ui.gen8_mode==0&&!s.project.gen8.assign){m.rows[0]={"GENERATE MIDI",{},false,false,false};m.rows[1]={"MUTATE MIDI",{},false,false,false};}
            if(s.ui.gen8_mode==0)m.rows[4]={br?"BREAK GEN":acid?"ACID GEN":chord_gen_available(s.project,s.ui.track)?"CHORD GEN":"VARIATIONS",{},false,false,false};
            if(s.ui.gen8_mode==4){m.rows[0]={"GENERATE ACID",{},false,false,false};m.rows[1]={"MUTATE ACID",{},false,false,false};}
            m.title=s.ui.gen8_mode==4?"ACID ENGINE / TRACK":s.ui.gen8_mode==2?"16 SLICES / TEMPO SYNC":(s.project.gen8.assign?juce::String("T1 KIT / T2-4 INSTRUMENTS"):"T"+juce::String(s.project.gen8.first+1)+"-"+juce::String(s.project.gen8.last+1)+" MIDI ONLY");
            if(s.ui.gen8_mode==0){
                const auto target="T"+juce::String(s.ui.track+1);
                m.rows[2]={"GENERATE "+target,{},false,false,false};m.rows[3]={"MUTATE "+target,{},false,false,false};
                if(s.ui.sheet_row==2||s.ui.sheet_row==3)m.title=target+" / TRACK GEN / KEEP SOUND";
                else if(s.project.gen8.assign)m.title="T"+juce::String(gen_song_first(s.project.gen8)+1)+"-"+juce::String(gen_song_first(s.project.gen8)+4)+(s.ui.sheet_row==1?" / KEEP SOUNDS":" / NEW SOUNDS");
                else m.title="MIDI T"+juce::String(std::min(s.project.gen8.first,s.project.gen8.last)+1)+"-"+juce::String(std::max(s.project.gen8.first,s.project.gen8.last)+1);
            }
            if(s.ui.gen8_mode==0&&s.ui.sheet_row>=4)m.title=s.ui.sheet_row==5?"TARGET / SOUND OR MIDI":br?"LOOP / CHOP GENERATOR":acid?"ACID / TRACK GENERATOR":chord_gen_available(s.project,s.ui.track)?"HARMONY / PLAY STYLE":"PATTERN / VARIATIONS";
            if(s.ui.gen8_mode==2){
                bool ready=true;for(unsigned i=0;i<16;++i)ready&=s.track().pads[(s.track().bank%sampler_banks)*16+i].used();
                m.title="T"+juce::String(s.ui.track+1)+(ready?" / 16 CHOPS":" / LOAD A LOOP");
                if(!ready)for(unsigned i=0;i<5;++i)m.rows[i].off=true;
                if(s.ui.break_pending>=0 && s.ui.break_job_track==s.ui.track){m.title="LOADING LOOP / PLEASE WAIT";for(unsigned i=0;i<m.count;++i)m.rows[i].off=true;}
            }
            if(s.ui.gen8_mode==5){m.title="T"+juce::String(s.ui.track+1)+" / "+juce::String(s.ui.chord_gen_page%5+1)+" OF 5";m.rows[0]={"NEW CHORDS",{},false,false,false};m.rows[1]={"MUTATE CHORDS",{},false,false,false};}
            if(s.ui.gen8_mode==4)m.title="T"+juce::String(s.ui.track+1)+" / SETTINGS "+juce::String(s.ui.acid_gen_page%2+1)+" OF 2";
            if(s.ui.gen8_mode==3){m.count=s.project.gen8.assign?4:1;
                for(unsigned i=0;i<m.count;i++){m.rows[i]={s.project.gen8.assign?"T"+juce::String(i*4+1)+" - T"+juce::String(i*4+4):juce::String("RETURN TO GEN"),s.project.gen8.assign?"KIT T"+juce::String(i*4+1):juce::String(),false,false,false};}m.title=s.project.gen8.assign?"T"+juce::String(gen_song_first(s.project.gen8)+1)+" KIT / T"+juce::String(gen_song_first(s.project.gen8)+2)+"-"+juce::String(gen_song_first(s.project.gen8)+4):"MIDI: FROM - TO";}
        } else m.count = unsigned(Variation::Count);
        m.current = s.ui.sheet_row % m.count;
        m.detail = {};
        if(s.ui.gen8_mode==5)m.detail=chord_gen_param_text(s.track().chord_gen,13,s.project.root,s.project.scale)+" "+chord_gen_param_text(s.track().chord_gen,14,s.project.root,s.project.scale);
        wide_sheet(p, m, s.ui.gen8_mode==5?"CHORD GEN":s.ui.gen8_mode==4?"ACID GEN":s.ui.gen8_mode==3?"GEN SETUP":s.ui.gen8_mode==1?"VARY":s.ui.gen8_mode==2?"BREAK":"GEN", c); // two columns: the cards (K1 the amount) stay in view
        return;
    }

}

void gen_push(Context& c) { // VARY: Push opens it on the row last used; there Push makes it
    if (c.ui.sheet_open) {
        unsigned mode=c.ui.gen8_mode, row=c.ui.sheet_row;
        if(mode==2 && c.ui.break_pending>=0 && c.ui.break_job_track==c.ui.track){c.notice("Loading BREAK sample");return;}
        if(mode==3){if(c.project.gen8.assign){c.project.gen8.group=uint8_t(std::min(row,3u));c.dirty=true;}c.ui.gen8_mode=0;c.ui.sheet_row=0;return;}
        if(mode==0&&row>=4) {
            bool br=c.track().engine==EngineKind::Sampler&&c.track().break_bars;
            bool acid=c.track().engine==EngineKind::Plugin&&c.track().plugin<max_plugins&&std::string(c.project.plugins[c.track().plugin].name)=="Acid Engine";
            c.ui.gen8_mode=row==5?3:br?2:acid?4:chord_gen_available(c.project,c.ui.track)?5:1;if(c.ui.gen8_mode==5)c.ui.chord_gen_page=0;c.ui.sheet_row=c.ui.gen8_mode==3&&c.project.gen8.assign?c.project.gen8.group:0;if(c.ui.gen8_mode==4)c.ui.acid_gen_page=0;if(c.ui.gen8_mode==2){bool ready=true;for(unsigned i=0;i<16;++i)ready&=c.track().pads[(c.track().bank%sampler_banks)*16+i].used();if(!ready)c.ui.sheet_row=5;}return;
        }
        if(mode==5){if(row<2)c.chord_gen_action(row==1);else c.ui.sheet_row=0;return;}
        if(mode==4){
            // An obsolete replay-row cursor must not execute a different action.
            if(row>=2){c.ui.sheet_row=0;return;}
            c.gen8_action(row==1?3:2);return;
        }
        if(mode==1)c.vary(Variation(row%unsigned(Variation::Count)));
        else c.gen8_action(mode==2?4+row:row);
        return; // leave the sheet open: turn seed/amount, Generate or Mutate again
    }
    c.ui.sheet_open = true;c.ui.gen8_mode=0;c.ui.sheet_row=0;
}

// Push: the sequence in hand again (every track back on what it keeps); on SEQUENCE its name, on FOLLOW the follow
// action onto the patterns the other tracks are on; on VARY the variation.
void push(Context& c) {
    auto& ui = c.ui;
    if (ui.sheet_open) { gen_push(c); return; }
    const unsigned scene = scene_of(c.project, ui);
    if (ui.patterns_cards == 1) { c.name_scene(scene); return; }
    if (ui.patterns_cards == 2) { c.follow_to_column(); return; }
    c.launch_scene(scene);
}

// The wheel: the track (the VARY sheet: its rows; Shift: the playhead).
void wheel(Context& c, int delta) {
    auto& ui = c.ui;
    if(c.ui.sheet_open && c.ui.gen8_mode==2 && c.ui.break_pending>=0 && c.ui.break_job_track==c.ui.track)return;
    if (c.ui.sheet_open) { int count=c.ui.gen8_mode==3?(c.project.gen8.assign?4:1):(c.ui.gen8_mode==4||c.ui.gen8_mode==5)?2:c.ui.gen8_mode==2?7:6; c.ui.sheet_row = uint8_t(std::clamp<int>(c.ui.sheet_row + delta, 0, count - 1)); return; }
    if (ui.shift) { ui.shift_used = true; c.locate(delta); return; } // Shift: the playhead by Q (stopped: where Shift + Play carries on)
    c.select_track(unsigned(std::clamp<int>(int(ui.track) + delta, 0, int(track_count) - 1)));
}

// The pads launch patterns before the page sees them (shell/pad_context.h SeqLayer); nothing is left for it.
void pad(Context& c, unsigned pad, uint8_t velocity, bool down) {if(c.ui.sheet_open)play_pad(c,pad,velocity,down);}

void nudge(Context& c, int way) { if(c.ui.sheet_open){wheel(c,way);return;} c.select_track(unsigned(std::clamp<int>(c.ui.track + way, 0, int(track_count) - 1))); }

void soft(Context& c, int key) {
    auto& ui = c.ui;
    if (c.ui.sheet_open) { // BACK, -, DO IT
        if(key==0){c.back();return;}
        if(c.ui.gen8_mode==2 && c.ui.break_pending>=0 && c.ui.break_job_track==c.ui.track){c.notice("Loading BREAK sample");return;}
        if(key==1&&c.ui.gen8_mode==5){c.ui.chord_gen_page=uint8_t((c.ui.chord_gen_page+1)%5);return;}
        if(key==1&&c.ui.gen8_mode==4){c.ui.acid_gen_page=uint8_t((c.ui.acid_gen_page+1)%2);return;}
        if (key == 1) {c.ui.gen8_mode=c.ui.gen8_mode==0?1:c.ui.gen8_mode==1?3:0;c.ui.sheet_row=c.ui.gen8_mode==3&&c.project.gen8.assign?c.project.gen8.group:0;}
        if (key == 2) gen_push(c);
        return;
    }
    if (ui.shift) { // SCENE, FOLLOW: their cards (again: the pattern's); VARY: its sheet
        ui.shift_used = true;
        if (key == 0) ui.patterns_cards = ui.patterns_cards == 1 ? 0 : 1;
        if (key == 1 && follow_shown(ui)) ui.patterns_cards = ui.patterns_cards == 2 ? 0 : 2;
        if (key == 2) { ui.patterns_cards = 0; ui.sheet_open = true;ui.gen8_mode=0;ui.sheet_row=0; }
        return;
    }
    if (key == 0) { ui.patterns_cards = 0; c.open(PageId::Scenes); return; } // B1 again: round to its first page, Sequences
    seq_hub_soft(c, key);
}
} // namespace

const PageSpec patterns_page {draw, pad, wheel, push, soft, nudge};

} // namespace rhythmer
