# RHYTHMER OS 0.3.0 — Projects, tracks and patterns

![System structure](../images/rhythmer/os-structure.png)

## The four things you select

| Item | Meaning | How to select it |
| --- | --- | --- |
| Project | The complete session: tracks, patterns, mix and arrangement | SHIFT + Pad 16 → SAVE / OPEN / NEW; SHIFT + B1 SAVE AS chooses another folder/name |
| Track | One part of the session; its instrument/audio, patterns and mix | PAD BANK + pad 1–16, or MIXER |
| Pattern | Notes and supported motion for one track; up to 128 per track | SEQ → B1 PATTERNS → pattern pad |
| Sequence | A pattern number launched across tracks | SEQ → B1 SEQUENCES → sequence pad |

A track with no notes in a launched sequence is silent. Changing just one track's pattern is done on PATTERNS. Select the track, choose its pattern, then press SAMPLE to play or record its instrument.

## What a track can contain

- **Drum / Sample Kit:** independently loaded audio pads. Load samples or an MPC kit.
- **Keygroup:** one sample or multisampled instrument played across notes.
- **Plugin:** one synth/percussion instrument. **Rhythmer Kit is a plug-in** with sixteen different pad sounds; other instruments focus on one sound across pitches.
- **MIDI, Full:** notes/control sent to an external instrument. It needs external audio routing to be heard.
- **Audio, Full:** an input take recorded against a pattern.
- **EMPTY:** no instrument. TRACK TYPE can make a track EMPTY while keeping its patterns. DELETE TRACK separately removes its sound and patterns after confirmation.

A first install starts in Basic. Settings → FEATURES = FULL shows MIDI/Audio and advanced pages such as Sends and Motion/Mod/Macro. Existing project content is not deleted when switching modes.

## Where a change applies

| Control | Applies to |
| --- | --- |
| P11 Volume / P11 source parameter | Selected Rhythmer Kit pad 11 |
| KIT CUTOFF / KIT RESONANCE | The whole Rhythmer Kit |
| Individual engine CUTOFF / RESONANCE | The current instrument; linked between PRESETS and EDIT |
| Track LEVEL / PAN / PERFORM | The selected track |
| SEND A–D | Sends from a track/pad to shared buses, according to track type |
| Project key/scale/tempo | Shared project settings |

## Save the right thing

- **Project:** the session, including notes, arrangement and instrument state. SAVE keeps the named project; idle autosave updates a separate recovery copy. SAVE AS makes a separate project.
- **User Sound:** a reusable instrument sound, without the song's notes.
- **User Kit:** a reusable kit and its edited pad sounds, without the song's notes.
- **SD backup:** keeps source samples and all project/preset files. Saving a preset is not a source-audio backup.

[Full manual](MANUAL_020_EN.md) · [First Groove](FIRST_GROOVE_020_EN.md) · [Controls](QUICK_REFERENCE_020_EN.md)
