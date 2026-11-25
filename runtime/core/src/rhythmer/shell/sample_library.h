// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/spsc_queue.h"
#include "rhythmer/engine/bounce.h"
#include "rhythmer/shell/expansion_install.h"
#include "rhythmer/engine/auto_chop.h"
#include "rhythmer/engine/onsets.h"
#include "rhythmer/engine/recall_ring.h"
#include "rhythmer/engine/sample_data.h"
#include "rhythmer/engine/sfz.h"
#include "rhythmer/model/looper.h"
#include "rhythmer/model/sampler.h"
#include "rhythmer/shell/load_pool.h"
#include "rhythmer/shell/stem_writer.h"
#include "rhythmer/shell/sample_cache.h"
#include "rhythmer/shell/stems.h"
#include "rhythmer/shell/stream_holds.h"
#include "rhythmer/shell/zone_shapes.h"
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct Hardware;
class Platform;

// The samples on the drives and the ones loaded, run on the service thread:
// it finds the card's Samples folder, Rhythmer/Samples (or $RHYTHMER_SAMPLES on a host), lists
// the folder the browser is in, reads the files asked for, works out their
// waveform and hits, hands the audio to the engine host, and writes the
// files Rhythmer makes (Save Sample, Trim Sample, Extract, takes). The MIDI
// and screen threads read what it found through view() under its own short
// lock; the audio thread never touches it.
//
// Two drives, as the MPC's browser has (B2): the card, starting in its
// Samples folder, and the internal one, read only: the factory library's
// categories (/content/Samples) and the stock app's own samples
// (/data/UserContent/Samples) side by side. Rhythmer writes only to the card.
namespace rhythmer {

class EngineHost;

enum class Drive : uint8_t { Card, Internal, Count };
inline const char* drive_name(Drive d) { return d == Drive::Internal ? "INTERNAL" : "CARD"; }

struct SampleFile {
    std::string path, name; // name: the stem in capitals, or the folder's
    uint64_t bytes = 0;
    bool folder = false;
    bool bank = false;      // a kit bank file (".mldb", model/project_file.h), listed with the WAV files
    bool instrument = false; // a multisampled instrument (".sfz", ".xpm", ".exs"), loaded onto a pad as a WAV file is
    bool kit = false;        // an MPC drum program (".xpm", ".xty"): Push loads it onto the track as Load Kit does
    bool expansion = false;  // an MPC expansion (".xpn"): Push installs it onto the card
    bool presets = false;    // a SoundFont (".sf2", roadmap P3): listed as a folder, its presets inside it
    bool midi = false;       // a Standard MIDI File (".mid", roadmap P4): Push lays its notes on the track's pattern
    bool starred = false;    // on STARRED (shell/sample_lists.h)
    uint8_t list = 0;        // the row that opens STARRED (1) or RECENT (2); its path is that list's folder name
    std::string where {};    // in a list: the folder its file is in; on a list's row: how many it holds
};
inline std::vector<SampleFile> sample_browser_files(std::vector<SampleFile> files, bool break_only) {
    if (break_only) files.erase(std::remove_if(files.begin(), files.end(), [](const SampleFile& f) {
        return f.bank || f.instrument || f.kit || f.expansion || f.presets || f.midi;
    }), files.end());
    return files;
}
struct LoadedSample {
    std::string name, path, error;
    uint32_t frames = 0;
    bool streamed = false;   // a long sample read from the card as it plays (SampleData::stream; `path` is the file asked
                             // for, which for an MP3, FLAC, Ogg or AIFF is not the WAV it plays from): its overview and
                             // hits come a moment after it plays (`shaped`)
    bool shaped = false;
    Overview overview {};
    bool stereo = false;                 // its sides differ: the pages draw each side's waveform
    std::array<Overview, 2> sides {};    // left, right (stereo only)
    std::array<Hit, max_hits> hits {};
    unsigned hit_count = 0;
    BeatGrid beats;          // its own tempo and beats, for Chop's Beats type
    AutoRead autos;          // what Chop's Auto type reads of its sound (engine/auto_chop.h)
    int8_t root = root_unread; // its note (SampleRef::root): a pitch class or root_none; root_unread until it is found
    bool ready = false;
    // A multisampled instrument: no frames of its own, so nothing to trim or chop.
    bool instrument = false;
    unsigned zones = 0, parts = 0, parts_ready = 0, parts_failed = 0, left = 0;
    uint8_t low_key = 127, high_key = 0;
    std::string ignored;     // what it asked for and is not played, named
    std::array<uint16_t, 128> map {}; // for each key, the velocities it has (a bit for each 8)
    // Round 3 "instrument C": each zone as Stage draws it, how far each of its files got, its velocity layers and the
    // longest round robin.
    std::vector<ZoneShape> shapes;
    std::vector<PartState> part_state;
    unsigned layers = 1, rounds = 0;
    std::array<int8_t, 4> stages {{-1, -1, -1, -1}}; // the pad's attack, decay, sustain, release, -1: none named
    int8_t velocity = 100;   // the pad's Velocity Sense
};

// A multisampled instrument's zones as the library loads them (".sfz", an MPC
// keygroup program, a pad of an MPC program or project, ".exs"), its samples
// found on the card. `error` names why where it gives nothing.
SfzRead read_instrument(const std::string& path, std::string* error);
// Where a zone's file of that instrument is on the card, as the library loads it (any case); empty when missing.
std::string instrument_zone_file(const std::string& instrument, const std::string& sample);

// A file Rhythmer makes from a slot's audio, on the service thread.
struct SampleJob {
    enum class Kind : uint8_t { Save, Derive };
    Kind kind = Kind::Save;
    uint16_t source = 0;  // the slot the audio comes from
    uint16_t target = 0;  // Derive: the slot the new file goes into
    uint32_t from = 0, to = 0; // frames; to 0: the end
    bool edges = false;   // Extract: 2 ms fades where the slice was cut inside the source (shell/chop.h)
    char name[40] {};     // the file's name, without .wav; a free one is found
    char folder[16] {};   // under the card's Samples ("" itself)
};

class SampleLibrary {
public:
    explicit SampleLibrary(EngineHost& host);
    ~SampleLibrary();

    // MIDI thread: ask for `path` in project slot `slot`; ask for a fresh list.
    // `find_note`: look for its note (LoadedSample::root) too; a slot that keeps one already does not.
    bool load(unsigned slot, const std::string& path, bool find_note = true);
    // MIDI thread: slots asked for by load() not yet read and offered to the audio thread, and instruments still
    // landing (a project's loading).
    unsigned loads_waiting() const {
        unsigned left = asked.load(std::memory_order_relaxed) - answered.load(std::memory_order_acquire);
        return left + landings_open.load(std::memory_order_relaxed);
    }
    // MIDI thread: how a slot's load stands (the Loading sheet), read without the lock. `settled`: its file is in, or
    // could not be read; an instrument's once each of its files has landed or failed.
    struct SlotLoad { bool settled = false, instrument = false; unsigned parts = 0, parts_in = 0; };
    SlotLoad slot_load(unsigned slot) const;
    // True for a path the library loads as a multisampled instrument (an .sfz, a keygroup program, an .sf2 preset...).
    static bool instrument_path(const std::string& path);
    // MIDI thread: whether slot `slot` may hold anything: a file was asked for it, or a sample made here was put in
    // it, since it was last emptied. A project switched to asks nothing of the slots it leaves empty that hold nothing.
    bool held(unsigned slot) const { return slot < sample_slots && slot_held[slot].load(std::memory_order_relaxed); }
    // The slot a load last began work for; -1 while nothing loads.
    int loading_now() const { return now_loading.load(std::memory_order_relaxed); }
    void rescan() { scan_wanted.store(true); }
    // Service thread: a take or a render is writing to the card (SD card access waits for it).
    bool busy() const;
    // Service thread: SD card access. Instruments are read from the card, so before it is
    // lent they let go of it (false if they could not in time), and come back after.
    bool release_instruments();
    void restore_instruments();
    bool instruments_away() const { return !away.empty(); }
    // MIDI thread: the browser. A folder to list (empty: the drive's start),
    // up one folder, or the other drive.
    void browse(Drive drive, const std::string& folder);
    void browse_up();
    // MIDI thread: a file to hear (preview_slot), empty for none; ready()
    // counts the previews that arrived, so the MIDI thread knows when to play one.
    void preview(const std::string& path);
    void preview_service(bool read_request=true); // service thread: bounded decode pieces; no Hardware dependency
    unsigned previews() const { return preview_count.load(std::memory_order_acquire); }
    std::string previewed() const;
    struct PreviewInfo { std::string path; double seconds=0; bool complete=false; };
    PreviewInfo preview_info() const;
    // MIDI thread: write a file; done() hands back what came of it (a notice).
    bool write(const SampleJob& job) { return jobs.push(job); }
    struct Done { char text[32] {}; int16_t failed = -1; }; // failed: the Derive target slot that got nothing
    bool done(Done& out) { return finished.pop(out); }
    // MIDI thread: kit bank files. Save Bank writes `bytes` as Samples/Banks/<name>.mldb (a free
    // name; done() tells how it went); a bank file to read, whose bytes take_bank() hands back.
    void write_bank(const std::string& name, std::vector<uint8_t> bytes);
    void read_bank(const std::string& path);
    struct BankBytes { std::string path, error; std::vector<uint8_t> bytes; };
    bool take_bank(BankBytes& out);
    // MIDI thread: a file or a folder to delete from the card: a folder with all in it, an MPC 3 track, program or
    // project with the data folder that is its own ("<name>_[TrackData]"). done() tells how it went, and the folder
    // in view is listed again. removals(): how many have been done (the plug-in picker lists again too).
    void remove(const std::string& path);
    unsigned removals() const { return removal_count.load(std::memory_order_acquire); }
    // MIDI thread: the browser's STARRED and RECENT (shell/sample_lists.h). star(): Shift + B3, starred or no
    // longer; used(): a file loaded from the browser, first on RECENT; unlist(): Erase in a list, off it, the
    // file kept. Done on the service thread, which keeps them on the card and lists the folder in view again.
    void star(const std::string& path) { list_change(ListChange::Star, path); }
    void used(const std::string& path) { list_change(ListChange::Use, path); }
    void unlist(const std::string& path) { list_change(ListChange::Unlist, path); }
    // MIDI thread: an MPC expansion (.xpn) to install onto the card (shell/expansion_install.h);
    // done() says when it starts and ends, and the browser then shows its folder.
    void install(const std::string& path);
    // Changes each time the list or a loaded sample changes.
    unsigned generation() const { return changes.load(std::memory_order_acquire); }
    // Freeze plan F2a, any thread: each slot's sample now (nullptr: none), for a print host to play, held until
    // release_samples(): one the audio thread lets go of meanwhile is freed only after that.
    std::array<const SampleData*, sample_slots> hold_samples();
    void release_samples();

    // Service thread, every few milliseconds: files, loads, finished takes, recalls and renders.
    void service(Hardware& hardware);

    // MIDI thread (parity phase 6, shell/sample_capture.cpp). Audio Recall:
    // the ring's last 25 s (from the first sound) into slot `slot`, as
    // Samples/Recorded/RECALL n.wav. A render of the mix the engine host is
    // about to play: into slot `slot` (Resample, Samples/Recorded/RESAMPLE
    // n.wav), or straight into Rhythmer/Mixdowns/<name>.wav on the card (the
    // song). `frames`: how long it should run, to make room up front.
    bool recall(unsigned slot, const RecallOptions& options) { return recalls.push({uint16_t(slot), options}); }
    // Roadmap P4: a mixdown's file as 32-bit float (bits 32) or integer PCM (24, 16: dithered); stems beside it (each
    // track and bus the engine host's StemRing takes, as Rhythmer/Stems/<name>/NN <stem>.wav, the mix as Mix.wav), and
    // its pad stems (StemRing::pads()): pad stem n as Rhythmer/Stems/<name>/<pad_paths[n]>.wav, the kit's pads in a
    // folder of their own. `more`: a later pass of the song for the pad stems the first could not take, into the last
    // stems' folder, no mix; `pass` of `passes`. A silent pad stem's file is not kept.
    struct Render {
        bool to_file = false; uint16_t slot = 0; uint32_t frames = 0; char name[40] {};
        uint8_t bits = 32;
        bool stems = false, more = false;
        uint8_t pass = 1, passes = 1;
        std::array<std::array<char, 24>, StemRing::stem_count> stem_names {};
        std::array<std::array<char, 72>, StemRing::pad_most> pad_paths {};
        std::array<uint8_t, StemRing::pad_most> pad_tracks {}; // each pad stem's track (its delay: StemRing::pad_delay)
    };
    bool render(const Render& job) { return renders.push(job); }
    StemRing* stem_ring(); // MIDI thread: made the first time stems are wanted, kept after
    // Stems: 1 once a pass's files are all written and closed, -1 when it failed or was stopped, 0 meanwhile (a
    // render popped). The MIDI thread starts the next pass on 1.
    int stems_result() const { return stems_done_.load(std::memory_order_acquire); }
    std::string card() const; // the card's top folder; empty without one

    // Stems (shell/stems.h, shell/sample_library_stems.cpp). MIDI thread: slot `source`'s sample separated into its
    // drums, bass, music and vocals, as Samples/Stems/<name> DRUMS.wav, <name> BASS.wav, <name> MUSIC.wav and
    // <name> VOCALS.wav (the same free <name> for the four). take_stems() hands back the files written, or why
    // there are none ("stopped": it was called off). One job at a time; stop_stems() and stems_progress() from any thread.
    struct StemsRequest { uint16_t source = 0; char name[24] {}; bool best = false; };
    bool separate(const StemsRequest& job) { bool expected=false;if(!stems_inflight.compare_exchange_strong(expected,true))return false;stems_cancel_requested.store(false);if(stems_wanted.push(job))return true;stems_inflight.store(false);return false; }
    void stop_stems() { stems_cancel_requested.store(true);separator.cancel(); }
    StemsProgress stems_progress() const { return separator.progress(); }
    bool take_stems(StemsResult& out);
    // Where the separating program is: the stems folder of the card folder the app was copied from (a release's
    // autorun/rhythmer, or where a boot menu keeps it), else autorun/rhythmer/stems, autorun/sample-base/stems or
    // sample-base/stems at the card's top (shell/stems_find.h); or $RHYTHMER_STEMS_ENGINE. Empty when there is none.
    // Its model is the file spleeter-4stems.f16 beside it, or $RHYTHMER_STEMS_MODEL.
    static std::string stems_program(const std::string& card_top);

    // The Looper (docs/LOOPER.md, shell/looper_files.cpp). Commit to pad: loop `loop` into slot `slot`, as
    // Samples/Looper/L<n> Loop m.wav, once the audio thread lets it be read. A project's loops: written as
    // <dir>/Loop N.wav for the loops in `mask` (the others' files go), or read back into the Looper.
    struct LoopCommit { uint16_t slot = 0; uint8_t loop = 0; };
    bool commit_loop(const LoopCommit& job) { return commits.push(job); }
    struct LoopFiles { bool save = true; std::string dir; uint8_t mask = 0; std::array<LoopTrack, loop_tracks> loops {}; };
    void loop_files(LoopFiles job);
    bool loops_busy() const;
    unsigned loop_save_errors() const {return loop_save_failures.load();} // loop files still being written (leaving waits)

    // Any thread but audio: a copy of what is known of the browser. The loaded samples are read a slot at a time
    // (slot(), measure()): each is several kilobytes with its waveform and hits.
    struct View {
        std::string root;  // the card's Samples folder (Rhythmer/Samples), where Rhythmer writes; empty: no card
        Drive drive = Drive::Card;
        std::string folder; // the folder listed; empty: the internal drive's top
        bool top = true;    // nothing above it to go back to
        std::vector<SampleFile> files; // folders first, then WAV files, each by name
        int came_from = -1; // after going up: the folder it came out of
    };
    void view(View& out) const;
    LoadedSample slot(unsigned n) const;
    // What the project keeps of slot `n`'s sample once it is loaded: its length, whether it streams and, an
    // instrument's, its zones.
    struct Measure { uint32_t frames = 0; bool streamed = false; std::vector<ZoneShape> shapes; };
    Measure measure(unsigned n) const;
    // Slot n's note (LoadedSample::root) once the file at `path` is read there, else root_unread.
    int8_t root_of(unsigned n, const char* path) const;
    // MIDI thread: where the pads on slot `n`'s long sample start, end and loop (44.1 kHz frames), for its chunks
    // there to be held, so a hit sounds at once (shell/stream_holds.h). Those before are let go. Kept for the slot
    // across reloads (SD card access) until asked again.
    void hold(unsigned n, std::vector<HeldRange> ranges);
    bool break_ready(unsigned n) const { return n < sample_slots && break_prepared[n].load(std::memory_order_acquire) > 0; }
    bool break_failed(unsigned n) const { return n < sample_slots && break_prepared[n].load(std::memory_order_acquire) < 0; }
    // Round 3 "instrument C": the zone of slot `n`'s instrument a press of `key` at `velocity` reaches (-1 none),
    // and zone `zone`'s file as its regions name it ("" none). Under the lock.
    int zone_at(unsigned n, int key, int velocity) const;
    std::string zone_file(unsigned n, unsigned zone) const;
    // Pads (round 2, pads B): slot `n`'s waveform between two frames (to 0: its end) in `columns` peaks
    // 0..255, from the overview it keeps; false when the slot holds no audio.
    bool thumbnail(unsigned n, uint32_t from, uint32_t to, uint8_t* out, unsigned columns) const;
    // MIDI thread: the loudest sample of slot `n` between two frames (0..1+), for
    // Normalize; -1 when it is not loaded. Reads the audio under the lock.
    float peak(unsigned n, uint32_t from, uint32_t to) const;
    // MIDI thread: `frame` of slot `n` pulled onto the nearest zero crossing (shell/chop.h's
    // Zero Snap); the frame itself when the slot is not loaded. Reads the audio under the lock.
    uint32_t zero_crossing(unsigned n, uint32_t frame) const;
    // The same going one way (the wheel's fine edit, shell/fine_edit.cpp): the first crossing at or past `frame`.
    uint32_t zero_crossing_toward(unsigned n, uint32_t frame, int way) const;
    // Trim and Chop zoomed in (shell/zoom_view.h): slot `n`'s peaks between two frames in `columns` columns
    // 0..255, read from its audio (the overview is too coarse that close); false when it holds no audio.
    // With `sides` (two arrays of `columns`), the left and right apart as well.
    bool detail(unsigned n, uint32_t from, uint32_t to, uint8_t* out, unsigned columns, uint8_t* const* sides = nullptr) const;
    // A long sample's frames around a frame, read from its file (zero_crossing() on one): `part` holds them at its
    // rate, `own` is the frame as its own, `first` the first's place; `read` false when it is not one or it failed.
    struct Around { SampleData part; uint32_t own = 0, first = 0; bool read = false; };
    Around around(unsigned n, uint32_t frame, uint32_t back, uint32_t ahead) const;
    std::vector<SampleFile> files() const;
    std::string root() const;
    std::string folder() const;
    Drive drive() const;
    int came_from() const;
    bool top() const;

    // Where each drive starts; tests and hosts may point them elsewhere.
    static std::vector<std::string> internal_tops();

private:
    std::array<std::atomic<int8_t>, sample_slots> break_prepared {};
    struct Request { uint16_t slot = 0; char path[sizeof(SampleRef::path)] {}; bool find_note = true; };
    void scan();
    void make(const SampleJob& job);
    void hear(const std::string& path);
    // The browser's preview read a piece a pass while its start plays (hear()): the file, the sample it fills and
    // the frames of a piece; `path` the file asked for (a program's preview is another file).
    struct Hearing;
    std::unique_ptr<Hearing> hearing;
    bool hear_piece();   // the next piece; false once it is all there (or cut short) and the file let go
    void hear_more();    // a pass: the next piece, unless another file, or none, is wanted now
    void stop_hearing(); // what is there plays to its end; nothing more is read
    void finish(const std::string& text, int failed = -1);
    std::string free_name(const std::string& folder, const std::string& name, const char* extension = ".wav") const;
    void bank_files(); // the bank writes and reads asked for
    void install_service(); // an expansion asked for: opened, then a file of it each pass
    std::unique_ptr<ExpansionInstall> installing;
    std::vector<std::string> installs_wanted; // under `lock`
    void cancel_landing(unsigned slot);
    void hear_instrument(const std::string& path);
    bool reap();       // what the audio thread has let go of; true if anything went
    // A sample handed to the audio thread for slot `slot` (nullptr: none); false while its queue is full. A sample is
    // handed over at its own rate, as it was read or recorded: a voice converts as it reads (engine/sampler.cpp).
    bool offer(unsigned slot, SampleData* data);
    // An instrument whose files are landing (sample_library_instruments.cpp). The pool's threads read it and write
    // only the part and the zones of the file each was given; the rest is the service thread's.
    struct Landing {
        uint16_t slot = 0;
        std::string folder;
        std::string sf2;                        // roadmap P3: an .sf2's preset, its sounds taken from this file
        SfzRead read;
        std::shared_ptr<Multisample> multi;
        std::vector<std::string> names;         // each part's file, as its regions name it
        std::vector<std::vector<uint16_t>> zones; // the zones of each part
        unsigned done = 0;                      // service thread: its files landed or failed so far
        std::atomic<bool> cancelled {false};    // its slot was asked for again, or it was let go: what is left is skipped
    };
    std::vector<std::shared_ptr<Landing>> landings;
    // Loads (sample_library.cpp, sample_library_instruments.cpp). Each file asked for is read on one of the pool's
    // threads (a sample read, converted and analysed; an instrument's text read; one of its files found, laid out
    // if it must be, mapped and pinned), several side by side. What they made comes back here, and the service
    // thread hands it to the audio thread in the order it arrives, as that thread's queue has room. Each load of a
    // slot has a serial; one still on its way when the slot is asked for again is dropped when it arrives.
    struct Handover { // a sample read (or none: the slot emptied), waiting for the audio thread's queue
        uint16_t slot = 0;
        uint32_t serial = 0;
        bool clear = false;
        bool refused = false; // not read: it would have left memory short (shell/sample_memory.h)
        std::unique_ptr<SampleData> data;
        LoadedSample loaded;
    };
    // A long sample's waveform and hits, read from its file after it plays (stream_shape()).
    struct ShapeRead {
        uint16_t slot = 0;
        uint32_t serial = 0;
        Overview overview {};
        std::array<Overview, 2> sides {};
        bool stereo = false;
        std::array<Hit, max_hits> hits {};
        unsigned hit_count = 0;
        BeatGrid beats;
        AutoRead autos;
        int8_t root = root_none;
        std::vector<float> peaks; // Shown::peaks
    };
    std::vector<ShapeRead> shapes_read; // under loaded_lock
    void make_streamed(const SampleJob& job, const SampleData& source); // Save Sample, Trim, Extract on a long sample
    void stream_shape(const std::string& path, ShapeRead& out, bool find_note); // a pool thread
    // A pool thread: a long MP3, FLAC, Ogg or AIFF as a PCM WAV in the card's cache, to be read from the card as it
    // plays; "" where it is read into memory instead, or (`error`) not at all.
    std::string card_copy(const std::string& path, const SampleCache& copies, std::string& error, bool& refused);
    std::mutex copying; // one such WAV written at a time, and never while the cache is tidied
    std::atomic<bool> shapes_stopped {false}; // SD card access: a shape being read stops where it is
    // Held chunks of the long samples (hold()): what the MIDI thread asked (under `lock`), what is known (service
    // thread, kept across reloads) and the slots still to bring in line, a few chunks a pass.
    std::vector<std::pair<uint16_t, std::vector<HeldRange>>> holds_wanted;
    std::vector<std::vector<HeldRange>> holds_known;
    std::vector<uint16_t> holds_due;
    void holds_service();
    struct InstrumentRead { Request request; uint32_t serial = 0; SfzRead read; std::string error; };
    struct PartLanded { std::shared_ptr<Landing> landing; unsigned index = 0; bool landed = false, laid_out = false; std::string why; };
    std::mutex loaded_lock;
    std::vector<Handover> files_loaded;              // under loaded_lock: from the pool
    std::vector<InstrumentRead> instruments_read;    // under loaded_lock
    std::vector<PartLanded> parts_landed;            // under loaded_lock
    std::deque<Handover> handovers;                  // service thread: in the order they are handed on
    std::vector<InstrumentRead> instruments_waiting; // service thread: read, their zones not yet with the audio thread
    std::chrono::steady_clock::time_point handover_since; // when the first of `handovers` was first refused
    bool handover_refused = false;
    std::array<uint32_t, sample_slots> slot_serial {};             // service thread
    std::array<std::atomic<uint32_t>, sample_slots> slot_state {}; // a SlotLoad packed (slot_load())
    std::array<std::atomic<bool>, sample_slots> slot_held {};      // held()
    std::atomic<int> now_loading {-1};
    void set_slot_state(unsigned slot, bool settled, bool instrument = false, unsigned parts = 0, unsigned parts_in = 0);
    void dispatch(const Request& request);     // a file asked for: its load begun on the pool
    void take_loaded();                        // what the pool has made since the last pass, handed on
    bool hand_over(Handover& h);               // false: the audio thread's queue is full, again next pass
    bool begin_landing(InstrumentRead& read);  // an instrument read: its zones to the audio thread, its files to the pool (false as above)
    PartLanded land_part(const std::shared_ptr<Landing>& landing, unsigned index, const SampleCache& cache); // a pool thread
    static constexpr uint64_t long_layout_bytes = uint64_t(48) << 20;
    std::mutex long_layout; // an instrument's long file is decoded whole to be laid out: one of those at a time
    SampleCache cache;
    bool cache_tidied = false;
    std::vector<std::pair<uint16_t, std::string>> away; // the instruments put down for SD card access

    EngineHost& host;
    SpscQueue<16, Request> requests; // MIDI thread produces, service thread consumes
    std::atomic<unsigned> asked {0}, answered {0}; // requests taken by load(), and read since (loads_waiting())
    std::atomic<unsigned> landings_open {0};        // instruments still landing, a part a pass
    SpscQueue<8, SampleJob> jobs;    // the same
    SpscQueue<8, Done> finished;     // service thread produces, MIDI thread consumes
    std::atomic<bool> scan_wanted {true};
    std::atomic<unsigned> changes {0};
    std::atomic<unsigned> preview_count {0};
    std::atomic<unsigned> take_count {0};
    void save_take();
    // A take drained into Samples/Recorded/TAKE n.wav as it is recorded (with a card): the file, how far it has got,
    // whether its file is open or failed, and whether this take was looked at already.
    void take_service();
    WavStream take_file;
    std::string take_path;
    uint32_t take_drained = 0;
    bool take_open = false, take_failed = false, take_seen = false;
    // Recall and renders (shell/sample_capture.cpp).
    struct RecallJob { uint16_t slot = 0; RecallOptions options; };
    SpscQueue<4, RecallJob> recalls;
    SpscQueue<4, Render> renders;
    void save_recall(const RecallJob& job);
    void drain_render();
    // Stems: the job asked for, the one running, and what came of the last (under `lock`).
    std::atomic<bool> stems_inflight{false},stems_cancel_requested{false};
    SpscQueue<2, StemsRequest> stems_wanted;
    StemSeparator separator;
    std::vector<StemsResult> stems_done;
    void stems_service(const Platform& platform);
    // A sample made here, `data`'s left and right at `rate` Hz (a take, a recall or a render at the device's): written
    // so, as Samples/<folder>/<stem> n.wav when there is a card, kept as written at that rate (keep_capture()),
    // offered to `slot` and listed; `what` names it in a failure.
    void land(unsigned slot, std::unique_ptr<SampleData> data, unsigned rate, const char* folder, const char* stem, const char* what);
    // A file this thread has just written, into slot `slot` as a file asked for (dispatch()): a long one is then read
    // from the card as it plays. The MIDI thread puts it on its pad once it is in (Context::files_landed()).
    void load_written(unsigned slot, const std::string& path);
    bool render_written = false; // a render into a slot going straight into its file on the card (render_path)
    bool rendering = false;
    Render render_job;
    unsigned render_rate = 44100; // the render's frames, the device's rate when it began
    // The Looper's files: a commit waiting for its loop, file jobs in turn (shell/looper_files.cpp).
    SpscQueue<4, LoopCommit> commits;
    struct Waiting { bool on = false; LoopCommit job; unsigned passes = 0; };
    Waiting committing;
    mutable std::mutex loop_lock;
    std::vector<LoopFiles> loop_jobs;
    LoopFiles loop_job;
    bool loop_job_on = false;
    unsigned loop_at = 0, loop_passes = 0;
    std::atomic<bool> loop_busy {false};
    std::atomic<unsigned> loop_save_failures {0};
    void loop_service(); // a step of the commit and the file jobs, never waiting
    std::unique_ptr<SampleData> render_data;
    WavStream render_file;
    std::unique_ptr<StemRing> stems_ring;                       // roadmap P4
    std::unique_ptr<StemWriter> stem_writer;                    // the stems' files, written on a thread of its own
    std::string stems_folder;                                   // the last stems' folder: a later pass writes there
    std::vector<std::string> stem_paths;                        // each file the writer has, by the ring's rows, then the mix
    bool stems_open = false, stems_delayed = false, stems_closing = false, stems_cancelled = false, stems_failed = false;
    std::atomic<int> stems_done_ {0};
    std::vector<float> stem_chunk;
    bool open_stems(const std::string& folder);
    void drain_stems();
    void close_stems(); // once the writer is done: the files closed, silent pad stems gone, said
    std::string render_path;
    std::vector<float> render_chunk;
    std::vector<std::unique_ptr<SampleData>> owned; // service thread: every sample the audio thread may hold
    std::array<const SampleData*, sample_slots> data_of {}; // under `lock`: what each slot holds now, for peak()
    unsigned print_holds = 0;                                // under `lock`: hold_samples() not released yet
    std::vector<std::unique_ptr<SampleData>> held_back;      // service thread: let go of by the audio thread while held
    // Under `lock`: each slot's sample as the pages see it and, an instrument's, its files (zone_file()). Kept only
    // for the slots that hold or held one: none reads as an empty LoadedSample.
    // A long sample's peaks (stream_shape()): the loudest of each peak_block of its own frames, left and right in
    // turn, for Normalize and a zoomed waveform wider than a block a column without reading the card.
    static constexpr uint32_t peak_block = 1024;
    struct Shown { LoadedSample sample; std::vector<std::string> parts; std::vector<float> peaks; };
    // detail() on a long sample, under `lock`: from its peaks where a column is a block or more, else from its
    // frames in memory where the pager holds them (the zoomed view is held: shell/stream_holds.h).
    bool detail_streamed(unsigned n, const SampleData& data, uint32_t from, uint32_t to, uint8_t* out, unsigned columns, uint8_t* const* sides) const;
    std::array<std::unique_ptr<Shown>, sample_slots> slot_shown;
    const LoadedSample& shown_at(unsigned slot) const;
    Shown& show(unsigned slot);
    std::string root_seen, card_top;
    unsigned ticks = 0;
    mutable std::mutex lock;
    View known;
    // Under `lock`: what the MIDI thread asked of the browser.
    struct Wanted { bool pending = false, up = false; Drive drive = Drive::Card; std::string folder; } wanted;
    double preview_seconds=0;bool preview_complete=false;
    std::string preview_wanted, preview_heard; // under `lock`
    std::vector<std::pair<std::string, std::vector<uint8_t>>> bank_writes; // under `lock`: name, bytes
    std::vector<std::string> bank_reads;  // under `lock`: paths
    std::vector<std::string> removals_wanted; // under `lock`: paths to delete
    std::atomic<unsigned> removal_count {0};
    void remove_service();
    // STARRED and RECENT: what the MIDI thread asked, under `lock`; the lists themselves, the service thread's.
    struct ListChange { enum Kind : uint8_t { Star, Use, Unlist } kind = Star; std::string path; };
    void list_change(ListChange::Kind kind, const std::string& path);
    std::vector<ListChange> list_changes;
    std::vector<std::string> starred_paths, recent_paths;
    std::string list_parent; // the folder a list was opened from: B1 goes back to it
    void lists_read();       // the card's Rhythmer/Starred.txt and Recent.txt, when a card comes
    void lists_write(bool starred, bool recent);
    void lists_service();    // the changes asked for, kept on the card
    std::vector<BankBytes> banks_read;    // under `lock`: what came of them
    bool preview_pending = false;
    // A load timed for the journal, from the first file asked for until nothing is left to read or hand on: how
    // many files, instruments and instrument files it was, and how many of those had to be laid out first.
    struct LoadLog {
        bool open = false;
        std::chrono::steady_clock::time_point since;
        unsigned files = 0, instruments = 0, parts = 0, laid_out = 0, failed = 0;
        unsigned refused = 0; // samples not read for want of memory: the first is told (a notice), the rest counted
    } load_log;
    // What the pool's threads spent on each step of it, added up over the threads (microseconds): samples read and
    // converted, analysed (waveform and hits), instruments' texts read, their files landed.
    struct LoadTimes { std::atomic<uint64_t> read {0}, analyse {0}, parse {0}, land {0}; } load_times;
    void log_load(); // closes it and writes its line once everything is in
    // Last, so its threads end before anything they use goes.
    LoadPool pool {3};
};

} // namespace rhythmer
