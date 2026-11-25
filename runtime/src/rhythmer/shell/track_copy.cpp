// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/track_copy.h"
#include "rhythmer/engine/engine_host.h"
#include "rhythmer/shell/context.h"
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/shell/print_engine.h"
#include "rhythmer/plugin/plugin_rack.h"
#include "rhythmer/shell/mod_jobs.h"
#include "rhythmer/shell/track_swap.h"
#include <cstring>

namespace rhythmer {

namespace {
// Track t as it is now, whole, to the audio thread (its patterns' motion in them).
void send_track(Context& c, unsigned t) {
    ++c.heavy_serial; // the track's kit and patterns, whole
    auto* pack = new TrackPack(t, c.project.tracks[t], c.patterns.at[t]);
    Command load {Command::Type::TrackLoad, uint8_t(t)};
    load.track_pack = pack;
    if (!c.host.send(load)) delete pack;
    c.refreeze(t); // freeze plan F2b: the audio thread's new track plays live until its prints are checked again
    c.audio_stale |= 1u << (t % track_count); // freeze plan F3: and an audio track's takes are sent again
}
} // namespace

TrackState track_state(const Context& c, unsigned t) {
    t %= track_count;
    return {c.project.tracks[t], c.patterns.at[t]};
}

void invalidate_track_jobs(Context& c,unsigned t) {
    t%=track_count;++c.track_epoch[t];
    if(c.kit_target==int(t))c.kit_target=-2;
    if(c.bank_target==int(t))c.bank_target=-2;
    unsigned target=unsigned(c.ui.break_job_track>=0?c.ui.break_job_track:c.ui.break_track);
    if(c.ui.break_pending>=0&&target==t){c.ui.break_pending=-1;c.ui.break_job_track=-1;}
    for(auto& p:c.pending_files)if(p.slot>=0&&p.track==t)p=Context::PendingFile{};
    if(c.ui.stems_sheet.running&&c.stems_from.track==t){c.ui.stems_sheet.running=false;if(c.library)c.library->stop_stems();}
    if(c.printer){auto job=c.printer->progress();if(job.track==t&&((c.autosample_track==int(t)&&job.autosample)||(c.auto_probe_track==int(t)&&job.checking)))c.printer->cancel();}
    if(c.ui.auto_sheet.track==t&&c.ui.auto_sheet.checking)c.ui.auto_sheet.checking=c.ui.auto_sheet.check_wanted=false;
}

void restore_track(Context& c, unsigned t, const TrackState& state) {
    t %= track_count;
    invalidate_track_jobs(c,t);
    c.project.tracks[t] = state.track;
    c.patterns.at[t] = state.patterns;
    c.free_retired();
    send_track(c, t);
    c.dirty = true;
}

void copy_track(Context& c, unsigned from, unsigned to) {
    from %= track_count; to %= track_count;
    if (from == to) return;
    if (c.ui.recording) { c.notice("Copy track: not in a take"); return; }
    std::string what = "Copy track " + std::to_string(from + 1) + " to " + std::to_string(to + 1);
    Edit edit = c.begin_edit(what.c_str(), to, -1, false);
    edit.track_before = std::make_unique<TrackState>(track_state(c, to));
    auto next = std::make_unique<TrackState>(track_state(c, from));
    next->track.mix = c.project.tracks[to].mix; // the target keeps its strip
    restore_track(c, to, *next); // the source's motion comes in its patterns
    edit.track_after = std::move(next);
    c.notice(what);
    c.edits.push(std::move(edit));
}

std::string swap_waits(const Context& c, unsigned a, unsigned b) {
    a %= track_count; b %= track_count;
    if (c.ui.recording) return "not in a take";
    for (unsigned l = 0; l < loop_tracks; ++l) {
        uint8_t source = c.project.looper.loops[l].source;
        if (!looper::source_is_track(source) || (source - 1u != a && source - 1u != b)) continue;
        auto state = c.host.looper().view(l).state; // the Looper keeps its source through a take
        if (state == Looper::State::Recording || state == Looper::State::Overdub) return "after L" + std::to_string(l + 1) + "'s take";
    }
    const auto& in = c.ui.settings.input;
    if (in.source == InputSource::Track && (in.track == a || in.track == b) && c.host_recording()) return "after the recording";
    return {};
}

void trade_tracks(Context& c, unsigned a, unsigned b) {
    a %= track_count; b %= track_count;
    if (a == b) return;
    c.stage_tick(UINT32_MAX); // a sound still waiting to load lands on the track it was turned on first
    invalidate_track_jobs(c,a);invalidate_track_jobs(c,b);
    auto changed = swap_tracks(c.project, c.patterns, a, b);
    swap_track_views(c.ui, a, b);
    { // freeze plan F2b: a frozen track stays frozen where it goes; its prints are made again under its new number
        std::swap(c.prints.freeze[a], c.prints.freeze[b]);
        auto& records = c.prints.records;
        records.erase(std::remove_if(records.begin(), records.end(), [&](const PrintRecord& r) { return r.track == a || r.track == b; }),
                      records.end());
    }
    std::swap(c.chain_queued[a], c.chain_queued[b]);
    c.free_retired();
    send_track(c, a);
    send_track(c, b);
    if (changed.song) c.sync_song();
    if (changed.punches) c.sync_punches();
    for (auto [t, n] : changed.kept) c.sync_pattern(t, n); // pad-fx C: other tracks' kept punches on one of the two
    for (unsigned m = 0; m < modulator_count; ++m) // roadmap P4: the modulators whose lanes or source moved
        if ((changed.mods >> m) & 1) send_modulator(c, m);
    for (unsigned l = 0; l < loop_tracks; ++l)
        if ((changed.loops >> l) & 1)
            c.send({Command::Type::Looper, uint8_t(l), 0, uint16_t(looper::Do::Source), int16_t(c.project.looper.loops[l].source)});
    if (changed.knob) { // the tracks it is heard on, and both tracks' pads (send_knob_targets() sends the track in hand's)
        const auto& k = c.project.knob_fx;
        Command tracks {Command::Type::KnobFx, 0, 0, 3};
        tracks.position = k.tracks;
        c.send(tracks);
        for (unsigned t : {a, b})
            for (unsigned quarter = 0; quarter < 4; ++quarter) {
                Command pads {Command::Type::KnobFx, uint8_t(t), uint8_t(quarter), 4};
                pads.position = uint32_t(k.pads[t][quarter / 2] >> (quarter % 2 * 32));
                c.send(pads);
            }
    }
    if (changed.key) c.set_compressor(7, c.project.compressor.key);
    if (changed.fx_keys) // roadmap P4: every chain's keys again (the two tracks' own came with them)
    {
        for (unsigned n = 0; n < mix_chain_count; ++n)
            for (unsigned k = 0; k < max_slots; ++k)
                if (fx_keyable(c.project.chain(n).slots[k].kind))
                    c.send({Command::Type::FxKey, uint8_t(n), 0, uint16_t(k), int16_t(c.project.chain(n).slots[k].key)});
        for (unsigned n = 0; n < strip_channels; ++n) // the mixer strips' compressor keys
            c.send({Command::Type::StripValue, uint8_t(n), 0, uint16_t(StripPart::Key), int16_t(c.project.strip(n).comp_key)});
    }
    const auto& in = c.ui.settings.input; // Input Config's track follows the sound it names
    if (in.source == InputSource::Track && (in.track == a || in.track == b) && !c.host_recording())
        c.set_input_source(InputSource::Track, swapped(in.track, a, b));
    c.dirty = true;
}

void move_track(Context& c, unsigned from, unsigned to) {
    from %= track_count; to %= track_count;
    if (from == to) return;
    if (auto why = swap_waits(c, from, to); !why.empty()) { c.notice("Move track: " + why); return; }
    bool trade = track_clearable(c.project.tracks[to], c.patterns.at[to]); // something there: the two trade places
    std::string a = std::to_string(from + 1), b = std::to_string(to + 1);
    std::string what = trade ? "Swap tracks " + a + " and " + b : "Move track " + a + " to " + b;
    Edit edit = c.begin_edit(what.c_str(), to, -1, false);
    edit.swap_a = int8_t(from); edit.swap_b = int8_t(to);
    trade_tracks(c, from, to);
    c.notice(what);
    c.edits.push(std::move(edit));
}

void clear_track(Context& c, unsigned t) {
    t %= track_count;
    std::string name = "Track " + std::to_string(t + 1);
    if (c.ui.recording) { c.notice("Clear track: not in a take"); return; }
    if (!track_clearable(c.project.tracks[t], c.patterns.at[t])) { c.notice(name + " is empty"); return; }
    if(c.kit_target==int(t))c.kit_target=-2;
    if(c.bank_target==int(t))c.bank_target=-2;
    if(c.ui.break_pending>=0 && unsigned(c.ui.break_job_track>=0?c.ui.break_job_track:c.ui.break_track)==t)c.ui.break_pending=-1;
    std::string what = "Clear track " + std::to_string(t + 1);
    Edit edit = c.begin_edit(what.c_str(), t, -1, false);
    edit.track_before = std::make_unique<TrackState>(track_state(c, t));
    if(c.plugins && c.project.tracks[t].engine==EngineKind::Plugin && c.project.tracks[t].plugin<max_plugins){
        unsigned entry=c.project.tracks[t].plugin;auto states=c.plugins->states();
        edit.gen_plugins_before.push_back({entry,c.project.plugins[entry],states[entry]});
        edit.gen_plugins_after=edit.gen_plugins_before;
    }
    auto next = std::make_unique<TrackState>();
    next->track.mix = c.project.tracks[t].mix; // the strip stays
    for (auto& p : next->patterns) p.length = c.project.bar_ticks();
    restore_track(c, t, *next);
    edit.track_after = std::move(next);
    c.notice(name + " cleared");
    c.edits.push(std::move(edit));
    if(c.ui.track==t && c.ui.page==PageId::TrackActions){c.open(PageId::Stage);c.ui.back_depth=0;}
}

} // namespace rhythmer
