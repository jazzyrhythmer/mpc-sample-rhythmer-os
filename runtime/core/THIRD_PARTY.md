# Sources and attribution

## The core

ellic-core: Copyright (c) 2026 Kerem Kilic (Ellic Studio), MIT (`LICENSE`).
The original upstream core files are Ellic Studio's work under MIT.
This Rhythmer fork additionally contains GPL/AGPL adaptations with their
own per-file notices; see "Rhythmer fork additions" below. The upstream
core's native engines, sequencer and effects are distinct from the added
FM-1-derived Break and generation code. `src/melodee/dsp/sinc.h` and `src/melodee/engine/spsc_queue.h`
are copies of Ellic Studio's own sample-base code, relicensed here.

## JUCE

The core includes JUCE's `juce_core` and `juce_audio_formats` (XML, JSON,
files; AIFF, FLAC, Ogg and MP3 decoding in `shell/sample_decode.cpp`), from
the product's own JUCE checkout: JUCE is not in this repository. JUCE is
licensed under the AGPLv3 or a commercial JUCE licence, so the MIT licence
here covers the core's source, not a binary built with JUCE. A product that
ships a binary under the AGPL (melodee-sample, melodee-move) uses JUCE under
the AGPLv3; one that does not needs a commercial JUCE licence, or the files
that include JUCE written without it:

- `shell/sample_decode.cpp` (`juce_audio_formats`)
- `shell/mpc_program.cpp`, `mpc_project.cpp`, `mpc3_export.cpp`,
  `mpc_wav_slices.cpp`, `dspreset_file.cpp`, `progression_file.cpp`,
  `expansion_install.cpp` (`juce_core`)

## MPC 3 template

`src/melodee/shell/mpc3_template.json` is a project MPC 3 saved, cut down by
`tools/mpc3_template.py` to empty tracks, one pad and MPC 3's own settings,
so that an export opens in MPC 3. It holds no samples, names or notes.

## Rhythmer fork additions

The Rhythmer fork adds an AGPL-3.0-only service-thread file-copy helper and GPL-3.0-only FM-1-derived Break and generation sources with preserved per-file notices. These additions are not relicensed under the core's MIT licence. Their original grants and upstream credits are retained in the product's fm1-gen and fm1-break directories and THIRD_PARTY.md. This is a modified core, not the unmodified ellic-core release.
