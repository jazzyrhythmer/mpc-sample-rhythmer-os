// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/project.h"
#include <cstdint>

// One change for the audio thread. The shell (MIDI thread) is the only
// producer; the engine host drains them at the start of each render().
namespace rhythmer {

struct Command {
    enum class Type : uint8_t {
        NoteOn,      // track, index = key, value = velocity
        NoteOff,     // track, index = key
        AllNotesOff, // track; with track = 255, every track
        Engine,      // track, index = EngineKind (the parameters follow as Param)
        Sound,       // track, index = sound: sets every parameter from the engine's factory sound
        Param,       // track, index = parameter, value
        Wheel,       // track, value 0..1000
        Level, Pan, Send, Mute, Solo, // track, value; Send's index: the bus 0-3 a track's send goes into
        PatternData, // track, pattern, data: the pattern's new copy (the one it replaces goes back to be freed)
        Pattern,     // track, value = pattern to play (on the next bar while playing patterns)
        Play,        // value 1: the song from bar `index`; 0: the patterns from tick `position`. track 1: a bar's count-in first
        Stop,
        Locate,      // position: the patterns playing jump there (the wheel on Patterns)
        Signature,   // index = ticks a beat, value = ticks a bar
        PatternTempo, // index = pattern, value = its tempo (0: the global one)
        SeqTempo,    // value: BPM per pattern on; index = the pattern that leads
        Recorded,    // track, pattern, index = key, position = tick: written just ahead of the playhead, heard live already
        SongClip,    // index, clip
        SongCount,   // value = clips in use
        SongLoop,    // index = first bar, value = bar after the last (equal: no loop)
        Chained,     // roadmap P5: track, value = it plays a pattern chain (its patterns' follow actions wait)
        EditLoop,    // track, position = first tick, index | value << 16 = ticks (0: the whole pattern): an editor's loop
        SceneTempo,  // roadmap P5: value = a scene's tempo (tenths of a BPM), taking over on the bar its patterns launch
        SceneMutes,  // index = the tracks a scene sets the mute of, value = those it mutes (a bit a track), on that bar too
        SongTempo,   // roadmap P5: the song's tempo lane, point `index`: position = its bar, value = tempo, pattern = ramp
        SongTempoCount, // value = the lane's points in use
        Tempo,       // value = tenths of a BPM
        Swing,       // value = percent
        Mod,         // track, index = route, route
        FxKind,      // track = chain, index = slot, value = EffectKind (its initial values come with it)
        FxParam,     // track = chain, index = slot, pattern = parameter, value
        FxBypass,    // track = chain, index = slot, value
        SendBus,     // track, value = bus 0-3
        Return,      // index = bus, value = 0..100
        Pressure,    // track, index = key, value = 0..127 (poly aftertouch)
        Pad,         // track, index = kit pad, pad
        Audition,    // track, pad (sample, start, end), value = velocity: a region on no pad; index = a kit pad + 1 whose speed it takes
        PadNote,     // track, index = kit pad, value = velocity, pattern = semitones + 64, position = cutoff + 1 (0: the pad's): 16 Levels;
                     // micros = a slice + 1 of the pad's sample's chop (0: the pad's region): Chop under Seq Record (issue 24)
        // track = InputSource, index = sample slot, value = threshold dB (-96: at once), pattern = rec length
        // (0 until stopped, 1..128 bars, 255 Seq: the pattern of track `position`, from where it comes round while playing)
        RecordArm,
        RecordGain,  // value = dB
        // track = InputSource: what the meter, Audio Recall's ring and the next take hear; with InputSource::Track,
        // index = the track and value 1 after its chain, level and pan (0: the engine alone)
        RecordSource,
        RecordNow, RecordStop,
        RecordAutoEnd, // value = silence dB, position = frames of it (at the model rate) that end the next Free take; 0 never
        Reload,      // pack: a whole project to switch to; handed back through the host's done queue
        Click,       // value: on; index: level 0..100
        Arp,         // track; index = 0 on, 1 mode, 2 rate, 3 octaves, 4 gate; value
        ArpKey,      // track, index = key, value = velocity (0: let go)
        Repeat,      // Note Repeat: track; index = 0 on, 2 rate, 5 pressure as velocity; value
        RepeatKey,   // Note Repeat: track, index = key, value = velocity (0: let go), position = a 16 Levels cutoff + 1
        // Punch: track = the target (255 the master), index = PunchKind | pad << 8
        // (pad 1..64: that pad alone on a sampler track, 0: the whole target),
        // value = depth 0..100, pattern = snap (0 1/4 .. 3 1/32) | tail << 4 | 64 when it starts at
        // once while playing (Pad FX quantize off), not on the snap grid's next line.
        PunchOn, PunchOff, // PunchOff: track and index's pad as PunchOn's; track 254: whatever punch is on
        PunchDepth,        // index's pad as PunchOn's (0: the master or track punch), value = depth
        // A sampler pad's own chain: track, pattern = pad, index = slot | parameter << 8, value as FxKind/FxParam/FxBypass.
        PadFxKind, PadFxParam, PadFxBypass,
        SongPunchSet,   // index, song_punch
        SongPunchCount, // value = punches in use
        SongRecord,     // value: Seq Record on (a song playing on past its end keeps going, for launches written into it)
        PadMute,        // track, index = kit pad (0xffff: every pad), value: muted
        TrackLoad,      // track_pack: one whole track (sound, kit, patterns with their motion); handed back like Reload's
        Monitor,        // value: the input source heard with the mix (not the mix itself)
        LoopMonitor,    // value: Monitor (Input Config's): when a loop on a hardware input of its own hears it
        BounceArm,      // the next Play renders: value 0 the patterns for `position` ticks, 1 the song to its end and its tail
        // Knob FX (parity phase 7): index 0 the effect (value = EffectKind, its initial values with it), 1 a parameter
        // (pattern = which, value), 2 the state (value: 1 heard now, 2 Rec Input FX on the input), 3 the tracks (position: a bit
        // a track), 4 a kit's pads (track, pattern = 0..3 a quarter of its 128 pads, position: a bit a pad).
        KnobFx,
        Compressor,     // the master compressor: index 0 on, 1 Color, 2 attack, 3 release, 4 amount, 5 In Boost, 6 Sidechain, 7 key track, 8 key pad (-1 the whole track); value
        // Parity phase 7b. Pad FX: up to four punches at once, each in a live slot: PunchOn / PunchOff / PunchDepth with
        // position = slot + 1 (0: slot 1, as before; PunchOff track 254: every slot). PunchParam: index = the MPC's Pad FX
        // 0..15, pattern = which of its K1-K3, value. FlexBeat: index 0 the pad (0 Empty, 1..15), 1 mode (0 One Shot,
        // 1 Loop), 2 mix 0..100, 3 quantize; value.
        PunchParam,
        FlexBeat,
        // Parity phase 9, MIDI and sync. MidiTrack: track, index = the channel its notes go out on (0 none), value = when
        // (PadMidiOut: 0 never, 1 always, 2 only notes with no sound). MidiSync: index 0 MIDI clock out, 1 MTC out,
        // 2 the CV clock out, 3 its pulses a quarter; value. ExtSync (MIDI clock in): index 0 following on / off (value),
        // 1 a clock pulse (position: the frame it lets the sequencer through, low 32 bits), 2 its tempo (value, tenths),
        // 3 Start / Continue came: the next ticks wait for the clock, 4 it stopped. NoteOn / NoteOff with pattern = 1 came
        // in by MIDI and never go out again.
        MidiTrack,
        MidiSync,
        ExtSync,
        // Parity phase 9b, MTC in: index 0 following off; 1 locate (Start or a jump), 2 a time sample. 1 and 2: position =
        // the frame heard when the quarter frame came (low 32 bits), micros = where the master was then (microseconds since
        // 0:00:00:00, the hours left out). The song is placed there through the tempo playing now (1) and kept on time (2).
        MtcIn,
        // The Looper (docs/LOOPER.md): track = the loop 0-3, index = looper::Do, value, position (Latency's frames).
        // Level, Pan, Send, Mute, Solo and SendBus with track 16-19 are the loop tracks L1-L4's strips; their
        // chains are loop_chain(0..3) for FxKind, FxParam and FxBypass.
        Looper,
        // Plug-ins (docs/RHYTHMER.md): track, value = the entry of Project::plugins a PLUGIN track plays (255 none).
        TrackPlugin,
        // Roadmap P1: value 1 while Fill is held (Play held while playing): FILL conditions play, NOT FILL ones not.
        Fill,
        // Roadmap P1: track, value = its speed (model/pattern.h speeds); its patterns come scaled as PatternData.
        TrackSpeed,
        // Roadmap P4: track = chain, index = slot, value = what keys its Compressor or Gate (model/project.h fx_key_*; 0 itself).
        FxKey,
        // Roadmap P4 (docs/MIDI_TRACKS.md M3): a MIDI track echoes what came in: index = the status's type (0xb0 CC,
        // 0xa0 poly or 0xd0 channel pressure), position = its first data byte, value = its second.
        MidiEcho,
        // Roadmap P4 (M5): track, value = the input it plays as its sound (midi_track::AudioChoice; the shell sends none
        // while the ADC is routed elsewhere: IN USE).
        MidiAudio,
        // Roadmap P4, a modulator (model/modulators.h): track = which; pattern 255 its settings, packed by
        // pack_modulator(), else target `pattern` packed by pack_mod_target() (value: where its lane stands as set).
        Modulator,
        // Round 3 "instrument C": track, index = kit pad, position = the zone (UINT32_MAX: every trim of the pad
        // let go), micros = its sample slot, value = level in tenths of a dB, pattern = tune in cents + 128
        // (model/sampler.h ZoneTrim, zone_trim_command())
        ZoneTrim,
        // A project load (EngineHost::park): position = the park's serial. The audio thread fades out, then plays
        // silence touching nothing of its state, and takes no more commands, until resume() lets it go.
        Park,
        // A PLUGIN track's pitch bend (the fader's Pitch Bend): track, value -8192..8191 around the centre.
        Bend,
        // Freeze plan F2b: track, freeze = its prints (engine/print_player.h) to play instead of its engine and chain,
        // on the next bar while playing (value 1) or at once (0); none: it plays live again at once. The pack it
        // replaces goes back to be freed (EngineHost::take_done_freeze).
        Freeze,
        // A controller from MIDI in on a kit or PLUGIN track (a MIDI track's goes out as MidiEcho): track, index = the
        // CC number, value 0..127. Not CC 1: the mod wheel is Wheel.
        Control,
        // Freeze plan F3: track, audio = its patterns' takes (engine/audio_player.h) to play from now; the pack it
        // replaces goes back to be freed (EngineHost::take_done_audio). None: it plays nothing.
        AudioRegions,
        // Freeze plan F3: a take for audio track `track` (EngineHost::audio_take()): value 0 arms it (it records one
        // pass of the track's pattern from where the pattern next comes round), -1 lets one armed go, -2 ends it here.
        AudioRecord,
        // Freeze plan F3: audio track `track`'s takes heard (value 0) or not (1: its monitor hears the input instead).
        AudioQuiet,
        // A KEYGROUP track: value = the key that plays its pad 1 at the file's own pitch (model/scales.h keyed_root():
        // a single sample's note nearest 60, else 60).
        KeyedRoot,
        // Free length (shell/free_length.h): track, pattern: while it plays that pattern, it counts from the top of the
        // pass playing now, or with index < track_count from where that track's pattern began when it plays the same
        // number (a take fitted to its sequence lines up with it). Sent before the pattern's longer copy.
        Rebase,
        // value 1: the input source (the mic or the rear inputs) is on the ADC side a loop or MIDI track took, so
        // Sample Record, Audio Recall, an audio track's take and Monitor hear silence, not the other input (IN USE).
        InputTaken,
        // Issue 24: index = a sample slot, chop = its chop as slice hits play it now (the shell's copy; none: no chop).
        // The one it replaces goes back to be freed (EngineHost::take_retired_chop()).
        Chop,
        // Issue 27, Settings > SEQ > Mute: value 1 Notes, a muted track or kit pad starts no new notes and what it
        // plays rings on (a MIDI track sends no All Notes Off); 0 Audio, a mute silences it at once.
        MuteMode,
        // The mixer (docs/MIXER_PLAN.md): track = a strip number (model/project.h; tracks 0-15 so far), index =
        // StripPart (model/strip.h), value: one value of that channel's strip.
        StripValue,
        // The mixer: track = a submix 0-7, index = SubPart, value: its level 0..127, pan -50..50, mute, solo or a
        // send 0..100.
        SubMix,
        // The mixer: track, value = where it plays (Mix::out: 0 the master, out_sub(s) submix s).
        TrackOut,
        // The mixer: index = bus, value 1: its return muted.
        BusMute,
        // The mixer: value 1: takes, Audio Recall and the input meter hear the input after its strip (else before).
        InputWet,
        // Elektron's per track direction: track, value = its PlayDirection (model/step_order.h).
        TrackDirection

    };
    enum SubPart : uint16_t { SubLevel, SubPan, SubMute, SubSolo, SubSend0 }; // SubMix's index; SubSend0 + bus
    Command() = default;
    Command(Type type, uint8_t track = 0, uint8_t pattern = 0, uint16_t index = 0, int16_t value = 0)
        : type(type), track(track), pattern(pattern), index(index), value(value) {}
    Type type = Type::NoteOn;
    uint8_t track = 0, pattern = 0;
    uint16_t index = 0;
    int16_t value = 0;
    uint32_t position = 0;
    uint32_t micros = 0; // MtcIn
    Pattern* data = nullptr; // with its Motion (roadmap P1: a pattern's own)
    ModRoute route {};
    Clip clip {};
    PadSound pad {};
    SongPunch song_punch {};
    union { // Reload's, or Freeze's (freeze plan F2b): one pointer, so a Command (thousands queued) stays as small
        struct ProjectPack* pack = nullptr;
        struct FreezePack* freeze;
        struct AudioPack* audio; // AudioRegions (freeze plan F3)
        const Slices* chop;      // Chop (issue 24)
    };
    struct TrackPack* track_pack = nullptr;
    // A note played by hand or by MIDI in (EngineHost::send_at): it plays at frame `at` (the low 32 bits of the
    // audio thread's count), a block after the frame it came in at, so hits keep their spacing; whatever was sent
    // after it waits behind it (EngineHost::take_commands).
    bool timed = false;
    uint32_t at = 0;
};

inline Command zone_trim_command(unsigned track, const ZoneTrim& z) {
    Command c {Command::Type::ZoneTrim, uint8_t(track), uint8_t(int(z.tune) + 128), uint16_t(z.pad), int16_t(z.level)};
    c.position = z.zone; c.micros = z.slot;
    return c;
}

// Roadmap P4: a modulator to the audio thread as small commands (a Command stays small: thousands wait in a queue).
inline Command pack_modulator(unsigned n, const Modulator& m) {
    Command c {Command::Type::Modulator, uint8_t(n), 255};
    c.position = uint32_t(m.kind) | uint32_t(m.shape) << 8 | uint32_t(m.sync) << 16 | uint32_t(m.rate) << 24;
    c.micros = uint32_t(m.division) | uint32_t(m.source) << 8 | uint32_t(m.attack) << 16 | uint32_t(m.release) << 24;
    return c;
}
inline Command pack_mod_target(unsigned n, unsigned i, const ModTarget& t) {
    Command c {Command::Type::Modulator, uint8_t(n), uint8_t(i)};
    c.position = uint32_t(t.track) | uint32_t(t.param) << 8 | uint32_t(t.pad) << 16 | uint32_t(uint8_t(t.depth)) << 24;
    c.micros = uint32_t(uint16_t(t.low)) | uint32_t(uint16_t(t.high)) << 16;
    c.value = t.own;
    return c;
}
// And back, onto what the audio thread holds.
inline void unpack_modulator(const Command& c, Modulator& m) {
    if (c.pattern == 255) {
        m.kind = ModKind(std::min<unsigned>(c.position & 0xff, unsigned(ModKind::Count) - 1));
        m.shape = LfoShape(std::min<unsigned>((c.position >> 8) & 0xff, unsigned(LfoShape::Count) - 1));
        m.sync = (c.position >> 16) & 1; m.rate = uint8_t(c.position >> 24);
        m.division = uint8_t(c.micros & 0xff); m.source = uint8_t(c.micros >> 8); m.attack = uint8_t(c.micros >> 16); m.release = uint8_t(c.micros >> 24);
        return;
    }
    if (c.pattern >= modulator_targets) return;
    auto& t = m.targets[c.pattern];
    t.track = uint8_t(c.position); t.param = uint8_t(c.position >> 8); t.pad = uint8_t(c.position >> 16); t.depth = int8_t(uint8_t(c.position >> 24));
    t.low = int16_t(uint16_t(c.micros)); t.high = int16_t(uint16_t(c.micros >> 16)); t.own = c.value;
}

} // namespace rhythmer
