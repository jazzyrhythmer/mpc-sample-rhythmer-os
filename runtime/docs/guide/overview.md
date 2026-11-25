# Overview

[Contents](../GUIDE.md) · Next: [Coming from the stock MPC Sample](coming-from-the-stock-mpc-sample.md)

Rhythmer is a groovebox app for the MPC Sample. It runs from the microSD card
in place of the stock app. It changes nothing on the MPC itself.

A Rhythmer **project** has:

- **16 tracks**, one for each pad. A new project has none: it opens on
  track 1 with the track types (see [Make a new track](tracks.md#make-a-new-track)).
  A track is one of these types:
  - **Kit** (SAMPLER): 128 pads in 8 banks (A–H), each pad playing a sample
    or a sampled instrument.
  - **Keygroup** (KEYGROUP): one sampled instrument, or one sample, played
    across the pads and keys.
  - **Plug-in** (PLUGIN): a VST3, LV2, CLAP or VST2 instrument from the card.
  - **MIDI**: notes, CCs and program changes sent to an external instrument.
  - **Audio**: a recording of the input in each pattern.
- **128 patterns per track**, in 8 banks (A–H) of 16. A pattern holds notes
  and knob movements (Motion), up to 128 bars long. Each track plays one
  pattern at a time and can change pattern on its own.
- **128 sequences**. Sequence 3 is pattern 3 of every track. Launch a sequence to
  change all tracks at once.
- **A song**: an arrangement of pattern clips on a timeline, one lane per
  track.
- **A mixer**: a level, pan and four sends per track, four buses (A–D) and
  the master. On a kit, each pad also has its own four sends.
- **Effects**: a six-slot chain on every track, bus and the master. A slot
  holds a built-in effect or a plug-in effect.
- **The Looper**: four audio loops (L1–L4) that follow the tempo.
- **Prints**: a track can be frozen. Its patterns are rendered to audio on
  the card and play from there, which saves CPU (see
  [Freeze a track](tracks.md#freeze-a-track)).

Around the project:

- **The Menu**: hold the encoder on any page. It lists the page's jobs and
  the 16 places, and opens the lessons (see [The Menu](basics.md#the-menu)).
- **Basic and Full**: Rhythmer starts on Full, which shows every page in this
  guide. Basic shows the stock app's pages plus tracks, plug-ins, the mixer
  and Song (see [Basic and Full](settings.md#basic-and-full)).
- **Plug-ins** go in `Rhythmer/Plugins` on the card. The plug-in picker
  can also load one from any folder on the card (see
  [Plug-ins](plug-ins.md)).
- **The card**: everything Rhythmer makes is in the `Rhythmer` folder, such
  as projects in `Rhythmer/Projects` and samples in `Rhythmer/Samples`. The
  only other folder Rhythmer needs at the card's top is `autorun` (see
  [Files on the card](projects-and-files.md#files-on-the-card)).
- **The web page**: connect the MPC to a computer with USB-C and open
  **http://rhythmer.local** while Rhythmer runs. Its tabs are **Device**
  (updates and plug-in zips), **Files** (the card's files), **Quickstart**
  and **Manual** (this guide).
- **Updates**: a release zip copied to the card, or sent from the web page,
  updates Rhythmer (see [Update Rhythmer](projects-and-files.md#update-rhythmer)).

---

[Contents](../GUIDE.md) · Next: [Coming from the stock MPC Sample](coming-from-the-stock-mpc-sample.md)
