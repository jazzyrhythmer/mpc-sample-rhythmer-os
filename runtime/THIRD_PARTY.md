# Sources and attribution

## Project licence

Melodee: Copyright (C) 2026 Kerem Kilic (Ellic Studio), AGPL-3.0-only. The
full text is in `LICENSE`.

## Boot art

The boot animation's two images are not covered by the AGPL, and neither
is licensed with this code:

- The Melodee logo (`src/melodee/ui/boot_logo.cpp`): Copyright (C) 2026
  Kerem Kilic (Ellic Studio), all rights reserved.
- The night city backdrop (`src/melodee/ui/boot_backdrop.cpp`): Copyright
  (C) Yasmine Elsen (https://www.smallcutekitty.net), all rights reserved.
  Made for Kerem Kilic (Ellic Studio), the only one permitted to use it.

A fork may keep both files as they are. No public release of a fork, or of
any other project built from this code (a binary, an SD card zip, an image
or any other download), may contain either one: before releasing, replace
both files with art of the fork's own (`tools/boot_art.py` writes them from
any two 320 x 240 PNGs).

## sample-base

The runtime, drivers, build and install scripts come from sample-base
(https://github.com/keremimo/sample-base), the `base/` submodule, under
AGPL-3.0-only. Its own `THIRD_PARTY.md` lists its sources.

## ellic-core

Melodee's engine, model and shared files come from ellic-core
(https://github.com/keremimo/ellic-core), the `core/` submodule: Copyright
(c) 2026 Kerem Kilic (Ellic Studio), MIT. Its licence is in release zips as
`licenses/ellic-core-LICENSE.txt`; its own `THIRD_PARTY.md` lists its
sources.

## JUCE

JUCE 9.0.5, Raw Material Software Limited, pinned commit
`d60b7a78030661574a615b586d9325ba7a7fa7dd`, used under AGPLv3 (JUCE also
offers a commercial licence). The base's `tools/dependencies.py` fetches it
into ignored `build/deps/JUCE`, whose `LICENSE.md` and `JUCE.spdx.json`
travel with it; release zips include `LICENSE.md`. Its MP3 decoder
(`JUCE_USE_MP3AUDIOFORMAT`, for an MPC expansion's previews) is on: JUCE's
own code, under the same licence; the MP3 patents ran out in 2017.

## Plug-in hosting

- The VST3 SDK, Steinberg Media Technologies GmbH, MIT licence, as bundled
  in JUCE (`juce_audio_processors_headless/format_types/VST3_SDK`); release
  zips include its `LICENSE.txt`.
- The CLAP headers, free-audio/clap 1.2.9, Alexandre Bique and the CLAP
  contributors, MIT licence (`build/deps/clap`, fetched by
  `src/melodee/dependencies.py`); release zips include its `LICENSE`.
- LV2: lilv, serd, sord and sratom (David Robillard) and the LV2 headers,
  ISC licence, as bundled in JUCE
  (`juce_audio_processors_headless/format_types/LV2_SDK`); release zips
  include their `COPYING` files.
- VST2: `src/melodee/plugin/vst2_abi.h` is Melodee's own description of the
  VST 2.4 binary interface, written from its public layout. No Steinberg
  VST2 SDK file is used.
- The GUI-library stubs (`tools/plugin_support/make_stubs.py`) are
  generated: functions returning 0 under the names Debian's libGL, libX11
  and others export. None of those libraries' code is copied.
- Libraries plug-ins use for sound and the MPC lacks
  (`tools/plugin_support/SURVEY.md`): Debian bookworm's aarch64 libsndfile
  (LGPL-2.1+) with libFLAC, libogg, libvorbis, libvorbisenc, libopus
  (BSD-style) and libmp3lame (LGPL-2.0+), libsamplerate (BSD-2-Clause)
  and FFTW 3 (GPL-2+), unchanged, in the same pack;
  their copyright files travel in `licenses/armhf-runtime/` too.
- The 32-bit bridge's runtime: Debian bookworm's armhf loader, glibc
  (LGPL-2.1+), libstdc++ and libgcc_s (GPL-3 with the GCC Runtime Library
  Exception), unchanged, packed into the binary by
  `tools/plugin_support/build.py`. Release zips carry their copyright
  files, the licence texts and the package versions in
  `licenses/armhf-runtime/`; the source is those Debian packages' source.
- Plug-ins themselves are not part of Melodee and none ship with it: each
  is the user's own, under its own licence.

## Stem separator

Stems (`src/melodee/shell/stems.h`) runs a program of its own,
`melodee-stems`, with a model file beside it. Release zips carry both as
files beside Melodee (`autorun/melodee/stems/`); neither is linked into
Melodee's binary. `tools/stems/build.py` builds them from sources
`src/melodee/dependencies.py` fetches into ignored `build/deps/` at pinned
commits and checksums:

- The program's driver, `tools/stems/stems_engine.c`, is Melodee's own
  (AGPL-3.0-only). It is linked with the network and STFT code of
  SpleeterRT, James Fung, GPL-3.0, commit
  `d818fb9ca826d31cb7dda60409f3f14e6e789597` of
  https://github.com/james34602/SpleeterRT, unchanged (`spleeter.c`,
  `gemm.c`, `im2col_dilated.c`, `codelet.c`, `stftFix.c`, `cpthread.c`), and
  with dr_wav (David Reid, public domain or MIT No Attribution), which
  SpleeterRT carries.
- OpenBLAS 0.3.28, the OpenBLAS project, BSD-3-Clause, commit
  `5ef8b1964658f9cb6a6324a06f6a1a022609b0c5`, linked in statically.
- The model, `spleeter-4stems.f16`: the 4-stem model of Spleeter, trained by
  Deezer, MIT licence, from
  https://github.com/deezer/spleeter/releases/download/v1.4.0/4stems.tar.gz
  (checked against a pinned SHA-256). `tools/stems/convert_model.py` lays
  its weights out as SpleeterRT's network code reads them, as 16-bit
  floats; the weights themselves are Deezer's.

Release zips carry the GPL, MIT and BSD texts and these addresses in
`licenses/stems/`. The idea of running Spleeter on a sampler this way
comes from Charles Vestal's Stems module for Schwung
(https://github.com/charlesvestal/schwung-stems, MIT); none of its code or
binaries is used.

## Rubik

Melodee embeds Rubik, Copyright 2015 The Rubik Project Authors, under the
SIL Open Font License 1.1, from google/fonts commit
`5bb28733cec313b63fbd415b574b5ae35c8aa8b8` (`ofl/rubik/Rubik[wght].ttf`).
`src/melodee/dependencies.py` fetches it and its `OFL.txt` into ignored
`build/deps/rubik/` and checks both against pinned SHA-256 digests; the
font is not tracked here. Release zips include `OFL.txt`.

## Screen fonts

Settings > SCREEN > Font offers these, each under the SIL Open Font License
1.1, from the same google/fonts commit: Inter Tight (The Inter Project
Authors), Atkinson Hyperlegible Next (The Atkinson Hyperlegible Next Project
Authors), Space Grotesk (The Space Grotesk Project Authors), IBM Plex Sans
(IBM Corp., Reserved Font Name "Plex"; embedded unchanged), Manrope (The
Manrope Project Authors), VT323 (The VT323 Project Authors).
`src/melodee/dependencies.py` fetches each with its `OFL.txt` into ignored
`build/deps/fonts/` against pinned SHA-256 digests. Release zips include
each `OFL.txt`. Melodee Pixel is Melodee's own (`src/melodee/ui/fonts/`,
`tools/gen_pixel_font.py`).

## Native core and Rhythmer additions

The native sampler, sequencer and effects originate in Melodee by Kerem Kiliç (Ellic Studio). Rhythmer also contains separately licensed ported instruments and FM-1-derived musical generators and Break performance code; the native-core description does not describe those additions.

- FM-1 Rhythmer GEN: FM1 Open/M-VAVE contributors, JIANT and jazzy rhythmer; GPL-3.0-only with retained notices in `fm1-gen/`.
- Break performance: Amenity, a port of Presethead's Jungle Choppa, via FM-1 Rhythmer; GPL-3.0-only with the upstream linking permission. See `fm1-break/NOTICE.md` and `LICENSE.md`. No third-party break recording is bundled.
- The Rhythmer port collection retains each source author, licence, selected source revision and local changes in its `SOURCE.json`, `VENDORED.md` and corresponding source. These instruments are additions by Rhythmer, not bundled instruments supplied by Melodee.

## Additional source-pinned instruments

- Amino: klaine/amino, MIT; Adventure Kid waveforms CC0 and dependent credits retained in the original THIRD_PARTY_NOTICES.md. Original 324 presets.
- Fizzik: filliformes/fizzik-move, MIT. Original 31 presets.
- Weird Dreams: filliformes/weird-dreams-move; MIT grant in its README, retaining the Daniele Filaretti / WeirdDrums attribution. Its original 64 eight-voice banks are paired into 32 sixteen-pad kits.
- KrautDrums: filliformes/krautdrums-move, MIT; all 16 original voices form one sixteen-pad kit. External rhythm launcher notes are not exposed as sounds.

Exact revisions, complete grants and adaptation sources accompany the ports. No trademark endorsement by the original authors or hardware manufacturers is implied.
