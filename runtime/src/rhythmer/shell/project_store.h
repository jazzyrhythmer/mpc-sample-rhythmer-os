// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/sound_file.h"
#include "surface/midi.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct Hardware;
struct StorageSpace;

// Projects as files on the card, run on the service thread: Rhythmer/Projects
// on the first card ($RHYTHMER_PROJECTS on a host), in folders. The MIDI
// thread asks (list a folder, save these bytes, load, duplicate, delete, new
// folder, open at start) through a queue; loaded files come back through
// another, and the listing is read under a short lock. On the first pass it
// loads the project set to open at start or, with none set, the one last
// opened or saved, so a power-on picks up where the last session left off.
namespace rhythmer {

struct ProjectEntry {
    enum class Kind : uint8_t { Folder, Project } kind = Kind::Project;
    std::string name, path;
    unsigned tracks = 0, tempo = 0, projects = 0; // a project's tracks and tempo (tenths); a folder's projects
    int64_t modified = 0;
    bool mpc() const { return kind == Kind::Project && path.size() > 4 && !path.compare(path.size() - 4, 4, ".xpj"); } // the MPC's own (shell/mpc_project.h)
};

// The MPC's internal drive in the Project list (B2): the stock app's own projects, its autosave and the factory ones,
// read only. Its folders start with ':', which no folder on the card can hold.
constexpr const char* internal_folder = ":internal";
inline bool on_internal(const std::string& folder) { return folder.compare(0, 9, internal_folder) == 0; }
// The card's own folders above Rhythmer/Projects (BACK at the top of the list), read only as the stock folder is:
// ":card" is the card's top, ":card/<folder>..." below it. Rhythmer/Projects and MPC-Sample/Projects, reached
// from there, are the list's top and its MPC Sample folder.
constexpr const char* card_folder = ":card";
inline bool on_card_top(const std::string& folder) { return folder.compare(0, 5, card_folder) == 0 && (folder.size() == 5 || folder[5] == '/'); }
// A folder on the card ("" the list's top) as the list names it: under Rhythmer/Projects relative, the stock
// projects as "MPC Sample...", anything else ":card/...". `card` is the card's path, `root` Rhythmer/Projects.
std::string card_folder_of(const std::string& card, const std::string& root, const std::string& dir);

struct LoadedProject {
    std::string path, error;
    std::vector<uint8_t> bytes;
    bool imported = false; // from an MPC project: the bytes are a Rhythmer project, with no file of its own yet
    std::string note;      // what the import did, or why it could not
    // Freeze plan F3: an import's audio takes, copied beside the projects as "<audio_from without .mldp> Audio" (a
    // hidden folder) until the project is first saved: Save copies them in as a Save As would ("" none).
    std::string audio_from;
    bool recovery = false;
    std::string origin = {}, media = {};
};

class ProjectStore {
public:
    ProjectStore();
    ~ProjectStore();

    // MIDI thread.
    void list(const std::string& folder);   // relative to the root; "" is the root
    // `from`: the file it was saved as before (Save As, freeze plan F2d): its Prints folder copied beside the new one.
    bool save(const std::string& path, std::vector<uint8_t> bytes, const std::string& from = "", bool recovery = false, uint64_t ticket = 0, const std::string& media = "");
    struct SaveResult { uint64_t ticket; std::string path, error; bool recovery; std::string moved_from = {}; };
    std::unique_ptr<SaveResult> take_save_result();
    std::string save_directory(const std::string& folder) const;
    std::string recovery_directory() const;
    void dismiss_recovery();
    void load(const std::string& path);
    // Any thread: how many loads have been asked for. The app reads the card's folders ahead before it serves each
    // (shell/card_warm.h), so a project's plug-ins and samples never meet folders that have gone from memory.
    unsigned loads_asked() const { return load_count.load(std::memory_order_acquire); }
    void duplicate(const std::string& path);
    void remove(const std::string& path);
    void new_folder();
    void new_folder_at(const std::string& folder);
    void open_at_start(const std::string& path);
    void opened(const std::string& path);   // the project now open, from its file: the one to resume at the next start
    void save_settings(const std::string& text); // Rhythmer's settings (shell/settings.h), kept with the start project
    void rename(const std::string& path, const std::string& name); // a project, in its folder
    void save_sound(const std::string& name, std::vector<uint8_t> bytes); // Rhythmer/Sounds/<name>.mlds
    void append_usage(const std::string& file, const std::string& text); // Rhythmer/Usage/<file> (shell/usage_log.h)
    void move(const std::string& path, const std::string& folder); // a project into a folder ("" the top), its name kept (" 2" on a clash)
    // The project (`bytes` as save_project() makes them, its file `path` for its prints and takes, "" unsaved) as an
    // MPC 3 project, Rhythmer/MPC 3/<name>.xpj on the card (shell/mpc3_export.h), or as an Ableton Live Set,
    // Rhythmer/Ableton/<name> Project/<name>.als (shell/live_export.h); " 2" on a clash. On a thread of its own:
    // copying its samples can take a while. One export at a time. What came of it comes back through take_note().
    void export_mpc3(const std::string& path, std::vector<uint8_t> bytes, const std::string& name);
    void export_live(const std::string& path, std::vector<uint8_t> bytes, const std::string& name);
    std::unique_ptr<std::string> take_note(); // MIDI thread: an export's outcome, if one is waiting
    bool exporting() const { return export_busy.load(std::memory_order_acquire); }
    // A project path in the folder in view for a typed name: the name itself,
    // or with " 2", " 3"... when a project has it already.
    std::string named_path(const std::string& name) const;
    std::unique_ptr<LoadedProject> take_loaded(); // a loaded project, if one is waiting
    // A free name in the folder in view: "Project 3", or "<base> copy".
    std::string free_path(const std::string& base) const;

    // Any thread, before the threads start too: where projects live, $RHYTHMER_PROJECTS or Rhythmer/Projects on
    // the first card among `storage`'s mounts (empty with none), and the settings file beside them,
    // Rhythmer/settings: its start= and last= lines, and the rest, which Settings::read() takes.
    struct SettingsFile { std::string start, last, settings, recovery; };
    static std::string projects_root(const std::vector<StorageSpace>& storage);
    static SettingsFile read_settings_file(const std::string& root);

    // Service thread.
    void service(Hardware& hardware);
    // Service thread: the card came back from a computer (SD card access): look for it and list it again.
    void rescan() { root.clear(); ticks = 0; }

    // Any thread but audio.
    struct View {
        std::string recovery_offer;
        std::string root, folder, start; // folder relative to root; start: the project opened at start
        std::vector<ProjectEntry> entries;
        uint64_t free_bytes = 0;
        unsigned saves = 0;              // finished saves, so a leave can wait for one
        std::string message;             // the last thing that went wrong, if anything
        std::string settings;            // the settings file's lines but the start one
        bool settings_read = false;      // the card's settings file has been read (or found missing)
        std::vector<UserSound> sounds;   // Rhythmer/Sounds, by name
        std::vector<std::string> folders; // every folder under the root, relative ("Live/Set 1"), by name
        std::string moved_from, moved_to; // the last move, so an open project can follow its file
    };
    void view(View& out) const;
    unsigned generation() const { return changes.load(std::memory_order_acquire); }
    unsigned saves() const { return saved.load(std::memory_order_acquire); }

private:
    friend struct ProjectSavingChecks; // deterministic service-queue regression harness
    struct Request {
        enum class Kind : uint8_t { List, Save, Load, Duplicate, Remove, NewFolder, OpenAtStart, Opened, Settings, Rename, SaveSound, Move, ExportMpc3, DismissRecovery, Usage, ExportLive } kind;
        std::string path;
        std::vector<uint8_t> bytes;
        std::string from = {};
        bool recovery = false;
        uint64_t ticket = 0;
        std::string media = {}; // Save: the project's file before (Save As), whose prints come along; ExportMpc3: its file
    };
    void push(Request* request);
    void run(Request& request);
    void scan();
    void mount_projects(const std::string& found);
    void publish_message(const std::string& message);

    MidiQueue<32, Request*> requests;           // MIDI thread produces
    MidiQueue<32, SaveResult*> save_results;
    MidiQueue<4, LoadedProject*> loaded;        // service thread produces
    MidiQueue<4, std::string*> notes;           // the export thread produces
    std::thread exporter;                       // service thread starts and joins it
    std::atomic<bool> export_busy {false};
    void start_export(Request& request);        // service thread
    std::string root, folder, start, settings;  // service thread
    std::string active_recovery;
    std::string last;                           // service thread: the project last opened or saved
    void remember(const std::string& path);     // service thread: `last`, written to the card when it changes
    void write_settings(std::error_code& error);
    void scan_sounds();
    bool started = false;
    unsigned ticks = 0;
    // Service thread: what scan() read out of each project file, by its path, kept while the file's time and size
    // stand, so a scan does not parse every project of the folder again.
    struct Scanned { int64_t modified = 0; uint64_t bytes = 0; unsigned tracks = 0, tempo = 0; bool ok = false; };
    std::unordered_map<std::string, Scanned> scanned;
    mutable std::mutex lock;
    View known;
    std::atomic<unsigned> changes {0}, saved {0};
    std::atomic<unsigned> load_count {0};
};

// The Project list's rows, the one count its list, K1's range and the shell agree on: SD card access at the
// top of every folder, then the folder's entries (New project and Save are tiles before the list).
inline unsigned project_rows(size_t entries) { return unsigned(entries) + 1; }

} // namespace rhythmer
