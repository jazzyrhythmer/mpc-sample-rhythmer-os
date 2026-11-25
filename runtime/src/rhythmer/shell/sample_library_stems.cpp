// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Stems for the sample library (shell/stems.h), on the service thread: a
// slot's sample copied to a job of the separator, the four files it writes
// named and placed in the card's Samples/Stems, and what came of it handed
// to the MIDI thread, which loads the files as it loads any other.
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/shell/sample_memory.h"
#include "rhythmer/shell/stems_find.h"
#include "drivers/platform.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace rhythmer {
namespace {
namespace fs = std::filesystem;

// Memory left for everything else while a job runs: a job that would leave less is refused, not started and ended
// by the kernel half way.
constexpr uint64_t stems_headroom = uint64_t(192) << 20;

// A name as a file's: no character a card's file system refuses, no space or dot at its end.
std::string file_name(const char* name) {
    std::string clean;
    for (const char* c = name; *c; ++c)
        clean += (*c == '/' || *c == '\\' || *c == ':' || *c == '"' || *c == '*' || *c == '?' || *c == '<' || *c == '>' || *c == '|') ? '-' : *c;
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
    return clean.empty() ? std::string("SAMPLE") : clean;
}
} // namespace

std::string SampleLibrary::stems_program(const std::string& card_top) {
    if (const char* named = std::getenv("RHYTHMER_STEMS_ENGINE"); named && *named) return named;
    // The card folder the app was copied from, which sd-worker.sh writes beside its copy in RAM, else the fixed
    // folders (shell/stems_find.h).
    std::error_code error;
    const auto exe = fs::read_symlink("/proc/self/exe", error);
    return find_stems_program(card_top, error ? std::string() : release_sd_folder(exe.parent_path()));
}

bool SampleLibrary::take_stems(StemsResult& out) {
    std::lock_guard<std::mutex> guard(lock);
    if (stems_done.empty()) return false;
    out = std::move(stems_done.front());
    stems_done.erase(stems_done.begin());stems_inflight.store(false);
    return true;
}

void SampleLibrary::stems_service(const Platform& platform) {
    StemsResult ended;
    if (separator.take(ended)) {
        if (ended.error.empty()) scan_wanted.store(true); // the files show in the browser
        std::lock_guard<std::mutex> guard(lock);
        stems_done.push_back(std::move(ended));
    }
    StemsRequest request;
    if (!stems_wanted.pop(request)) return;
    auto refuse = [&](const char* why) {
        StemsResult none;
        none.error = why;
        std::lock_guard<std::mutex> guard(lock);
        stems_done.push_back(std::move(none));
    };
    if(stems_cancel_requested.load()){refuse("stopped");return;}
    if (separator.busy()) { refuse("busy"); return; }
    std::string root, top;
    {
        std::lock_guard<std::mutex> guard(lock);
        root = known.root;
        top = card_top;
    }
    if (root.empty()) { refuse("no card"); return; }
    const SampleData* source = data_of[request.source % sample_slots]; // only this thread writes it
    if (!source || !source->frames || source->multi || source->growth) { refuse("not loaded"); return; }
    const uint32_t frames = source->model_frames(); // 44.1 kHz frames: what the program takes, and the stems' length
    if (frames < stems_least_frames) { refuse("under a second"); return; }
    StemsJob job;
    job.engine = stems_program(top);
    std::error_code error;
    if (job.engine.empty() || !fs::is_regular_file(job.engine, error)) { refuse("no program on the card"); return; }
    const char* model = std::getenv("RHYTHMER_STEMS_MODEL"); // beside the program
    job.model = model && *model ? std::string(model) : (fs::path(job.engine).parent_path() / "spleeter-4stems.f16").string();
    if (!fs::is_regular_file(job.model, error)) { refuse("no model on the card"); return; }
    job.best = request.best;
    const uint64_t needed = stems_memory(frames, job.best) + stems_headroom;
    uint64_t free = platform.memory().available_kb * 1024; // 0: a host, which does not say
    // Short of it: what the pager keeps of files it has read is given back first (engine/stream_pager.h).
    if (free && free < needed && stream_cache_give_back()) free = memory_free();
    if (free && free < needed) { // with BEST off it would fit: so says the notice
        refuse(job.best && free >= stems_memory(frames, false) + stems_headroom ? "not enough memory, BEST off fits" : "not enough memory");
        return;
    }
    // The four files share one free name: "LOOP DRUMS", "LOOP BASS", "LOOP MUSIC", "LOOP VOCALS", else "LOOP 2 DRUMS" ...
    const std::string folder = root + "/Stems", base = file_name(request.name);
    fs::create_directories(folder, error);
    bool found = false;
    for (unsigned n = 1; n < 1000 && !found; ++n) {
        const std::string name = base + (n > 1 ? " " + std::to_string(n) : std::string());
        found = true;
        for (unsigned stem = 0; stem < stem_count; ++stem) {
            job.paths[stem] = folder + "/" + name + " " + stem_name(stem) + ".wav";
            found = found && !fs::exists(job.paths[stem], error);
        }
    }
    if (!found) { refuse("no free name"); return; }
    const char* work = std::getenv("RHYTHMER_STEMS_WORK");
    job.work = work && *work ? work : "/run/rhythmer-stems";
    // Its frames at 44.1 kHz, as the program takes them: the stems come out at the same length, to the frame. A sample
    // at another rate is converted for the job alone; the conversion goes with it. A long sample read from the card
    // (not in memory) is read from its file for the job.
    SampleData whole;
    const SampleData* from = source;
    if (source->stream) {
        const std::string path = source->stream->source ? source->stream->source->path : std::string(); // the file it plays from
        if (path.empty() || !read_wav(path, whole, stream_most_seconds).empty()) { refuse("cannot read it"); return; }
        from = &whole;
    }
    if (from->rate == 44100) {
        job.left.assign(from->left.begin(), from->left.begin() + std::min<uint32_t>(frames, from->frames));
        job.right.assign(from->right.begin(), from->right.begin() + std::min<uint32_t>(frames, from->frames));
    } else {
        resample(from->left.data(), from->right.data(), from->frames, from->rate, 44100, job.left, job.right);
    }
    job.left.resize(frames, 0.0f); job.right.resize(frames, 0.0f);
    if (!separator.start(std::move(job))) refuse("busy");
    else if(stems_cancel_requested.load())separator.cancel();
}

} // namespace rhythmer
