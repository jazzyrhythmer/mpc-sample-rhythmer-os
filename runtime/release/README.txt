Rhythmer {version} for the Akai MPC Sample
==========================================

Rhythmer is a 16-track groovebox that runs on the MPC Sample in place of the
stock app. Each track can hold a kit of samples, a sampled instrument
(keygroup), a VST3, LV2, CLAP or VST2 instrument, a MIDI track or an audio
track. Patterns, sequences and a song, a looper, a mixer and effect chains
that take plug-in effects.

Rhythmer does not reflash the stock MPC firmware. It starts from the microSD card.
If it fails to start, or crashes, the stock MPC app comes back. Back up the
card before you install it.


Requirements
------------
- MPC Sample on firmware 1.3.0.12, 1.4.0.7 or 1.4.0.8. On any other
  firmware Rhythmer does not start and the stock app keeps running.
- On firmware 1.4: Developer Mode on, in the stock app's Settings on the
  DEV ENHANCEMENTS (BETA) page.
- A microSD card for the MPC (the one you already use is fine).


Install
-------
1. Back up the card. If it already has a folder named "autorun" at its top
   level (MPC Sample Plugins uses one, for example), move it somewhere
   safe: Rhythmer's replaces it.
2. Unzip the release. Copy "autorun" and "autorun.sh" to the card root.
   Merge the included "Rhythmer/Plugins" files into the same folder on the
   card; this installs the bundled instruments and all 69 kits. Preserve
   your projects, samples and user sounds. Do not edit or
   rename anything inside them: the MPC refuses an autorun whose files
   changed.
3. Put the card in the MPC and switch it on. The stock app starts first.
   On firmware 1.4 it asks whether to run the card's autorun script:
   press Yes. A few seconds later Rhythmer takes over the screen.

On firmware 1.4, Screen Mirror is not available and Rhythmer runs at
44.1 kHz only.

Use the Default project template for Rhythmer Kit / K-WASUB 909 on track 1.
Empty Project keeps the tracks empty. Basic provides the main instrument,
sequencer, mixer and Song pages. For every page, press SHIFT + Pad 8 (Settings) and turn
K1 (FEATURES) to Full. Hold the encoder for the Menu; its GO page (B3)
has LEARN on pad 16, four short lessons.

For a new manual save or SAVE AS, choose the folder and project name.
SAVE then updates that manual file. Rhythmer/Projects is the suggested folder.
Separate recovery checkpoints are under Rhythmer/Autosave, one per project;
they do not create numbered manual projects. Put samples in Rhythmer/Samples.

Plug-ins: put VST3, LV2 and CLAP plug-ins built for 64-bit ARM Linux, VST2
plug-ins built for 64-bit or 32-bit ARM Linux, and Open MPC plug-ins, in
the folder "Rhythmer/Plugins" on the card (make a folder named "Plugins"
inside the "Rhythmer" folder). Earlier versions read a "Plugins" folder at
the top level of the card: move its contents into "Rhythmer/Plugins";
saved projects find them there. Or install a plug-in's zip without moving
the card: on http://rhythmer.local, Device, drop it on Plug-ins and press
Install. Optional Melodee synth plug-ins,
Melodee Poly, Melodee PD and Melodee Chip, are free downloads:
https://github.com/keremimo/mpc-vst-melodee-poly/releases/latest
https://github.com/keremimo/mpc-vst-melodee-pd/releases/latest
https://github.com/keremimo/mpc-vst-melodee-chip/releases/latest

New to Rhythmer? The quickstart, with pictures of each screen:
http://rhythmer.local/docs/manual/FIRST_GROOVE_020_EN
The user guide, with every page and control:
http://rhythmer.local/docs/manual/MANUAL_020_EN


Update
------
1. For the app updater, use the matching signed UPDATE zip, not the full
   SD package. Use the full SD package when new engines or the firmware
   1.4 root autorun.sh also need installing. Back up an existing boot manager
   and follow its installation process rather than replacing it blindly.
2. Copy the zip to the top level of the card (Project > SD card access,
   or a card reader). Or, with the MPC connected to a computer by USB-C
   and Rhythmer running, open http://rhythmer.local (or
   http://192.168.155.1), choose the zip under Update and press Send.
3. When Rhythmer asks "Update to <version>?", press B3 (INSTALL). Rhythmer
   makes a separate recovery checkpoint, installs the new version on the
   card and restarts. Your manually saved project file is not overwritten.
Only official, signed releases are offered. If the update fails, the card
is not changed. The previous version stays in autorun/rhythmer.prev.


Back to the stock app
---------------------
- Press Power briefly. On the Exit screen, press B3 (MPC APP): Rhythmer
  follows its save/recovery flow and the stock app runs until the next time
  the MPC is switched on.
- To stop Rhythmer starting at all, delete the "autorun" folder and the
  "autorun.sh" file from the card (and put back any autorun folder you
  moved away).


Reporting problems
------------------
Say what you pressed, what you saw and the version (Settings, ABOUT,
press B3 for the credits, or the line below). After a crash, add the
report Rhythmer wrote to the card's "Rhythmer/Crashes" folder. If Rhythmer
did not start at all, check that the firmware is 1.3.0.12, 1.4.0.7 or 1.4.0.8
(with Developer Mode on) and that the autorun folder and autorun.sh sit
at the top level of the card.
Issues: https://github.com/jazzyrhythmer/mpc-sample-rhythmer-os/issues


Version and source
------------------
Rhythmer {version}, source commit {commit}
Source code: {source}
RHYTHMER OS by Jazzy Rhythmer.
Built on Melodee by Kerem Kiliç (Ellic Studio): https://github.com/keremimo/melodee-sample
Runtime: https://github.com/keremimo/sample-base
See the bundled third-party notices and source for the original engine authors.

Rhythmer is free software under the GNU Affero General Public License,
version 3 (licenses/LICENSE-AGPL-3.0.txt, in this folder). It uses JUCE
under the AGPLv3 and embeds the Rubik typeface and the other screen fonts
under the SIL Open Font License 1.1; see licenses/. Stems runs a separate
program, stems/rhythmer-stems, built on SpleeterRT (GPL 3.0) and OpenBLAS
(BSD), with Deezer's Spleeter model (MIT) beside it; see licenses/stems/.
Not affiliated with or endorsed by inMusic or Akai Professional.
