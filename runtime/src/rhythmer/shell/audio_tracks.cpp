// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/audio_tracks.h"
#include "rhythmer/engine/engine_host.h"
#include "rhythmer/engine/stream_pager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

namespace rhythmer {
namespace fs = std::filesystem;

namespace {
constexpr unsigned longest_take_seconds = 6 * 3600; // what a take's file is read to (WavReader stops there)
constexpr unsigned take_bits = 24;                  // as the MPC records its audio tracks
// A take's number in a file name, a take still being written (".part") too.
unsigned take_in(std::string name) {
    const std::string part = ".part";
    if (name.size() > part.size() && !name.compare(name.size() - part.size(), part.size(), part)) name.resize(name.size() - part.size());
    return audio_take_of(name);
}
} // namespace

AudioTracks::AudioTracks(EngineHost& h) : host(h) {
    thread = std::thread([this] { run(); });
}

AudioTracks::~AudioTracks() {
    {
        std::lock_guard<std::mutex> held(lock);
        stopping = true;
    }
    wake.notify_all();
    if (thread.joinable()) thread.join();
    if (taking) { take_file.close(); std::error_code e; fs::remove(take_part, e); }
    for (auto& m : made) trash.push_back(m.pack);
    made.clear();
    if (pager) pager->flush();
    for (auto* pack : trash) delete pack;
}

void AudioTracks::build(AudioPackJob job) {
    std::lock_guard<std::mutex> held(lock);
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&](const AudioPackJob& j) { return j.track == job.track; }), jobs.end());
    jobs.push_back(std::move(job));
    wake.notify_all();
}

void AudioTracks::prune_later(std::string folder, std::vector<unsigned> kept) {
    std::lock_guard<std::mutex> held(lock);
    prunes.emplace_back(std::move(folder), std::move(kept));
    wake.notify_all();
}

bool AudioTracks::take_made(AudioPackMade& out) {
    std::lock_guard<std::mutex> held(lock);
    if (made.empty()) return false;
    out = made.front();
    made.pop_front();
    return true;
}

void AudioTracks::discard(AudioPack* pack) {
    if (!pack) return;
    std::lock_guard<std::mutex> held(lock);
    trash.push_back(pack);
    wake.notify_all();
}

bool AudioTracks::record(const AudioTakeJob& job) {
    std::lock_guard<std::mutex> held(lock);
    if (take_wanted || busy.load()) return false;
    take_job = job;
    take_wanted = true;
    busy.store(true);
    wake.notify_all();
    return true;
}

bool AudioTracks::take_recorded(AudioTakeDone& out) {
    std::lock_guard<std::mutex> held(lock);
    if (recorded.empty()) return false;
    out = std::move(recorded.front());
    recorded.pop_front();
    return true;
}

unsigned AudioTracks::next_take(const std::string& folder, unsigned at_least) {
    unsigned next = std::max(at_least, 1u);
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(folder, error)) {
        unsigned take = take_in(entry.path().filename().string());
        if (take) next = std::max(next, take + 1);
    }
    return next;
}

namespace {
bool write_peaks(const std::string& path, const std::vector<uint8_t>& peaks) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(peaks.data()), std::streamsize(peaks.size()));
    return bool(out);
}
} // namespace

std::vector<uint8_t> AudioTracks::peaks_of(const std::string& folder, unsigned take) {
    const std::string path = (fs::path(folder) / audio_peaks_name(take)).string();
    std::vector<uint8_t> peaks;
    {
        std::ifstream in(path, std::ios::binary);
        if (in) { peaks.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); if (!peaks.empty()) return peaks; }
    }
    WavReader reader; // none kept (a take from elsewhere): read once through
    if (!reader.open((fs::path(folder) / audio_take_name(take)).string(), longest_take_seconds).empty() || !reader.rate()) return peaks;
    const double per = double(reader.rate()) * take_peak_frames / 44100.0; // the take's frames a column
    std::vector<float> l(16384), r(16384);
    uint64_t at = 0;
    for (uint32_t n; (n = reader.read(l.data(), r.data(), 16384)) > 0; at += n)
        for (uint32_t f = 0; f < n; ++f) {
            size_t column = size_t(double(at + f) / per);
            if (column >= peaks.size()) peaks.resize(column + 1, 0);
            uint8_t v = uint8_t(std::min(1.0f, std::max(std::fabs(l[f]), std::fabs(r[f]))) * 255);
            peaks[column] = std::max(peaks[column], v);
        }
    write_peaks(path, peaks);
    return peaks;
}

unsigned AudioTracks::prune(const std::string& folder, const std::vector<unsigned>& kept) {
    unsigned removed = 0;
    std::error_code error;
    std::vector<fs::path> going;
    for (const auto& entry : fs::directory_iterator(folder, error)) {
        std::string name = entry.path().filename().string();
        const std::string overview = ".peaks"; // a take's overview goes with it
        if (name.size() > overview.size() && !name.compare(name.size() - overview.size(), overview.size(), overview))
            name = name.substr(0, name.size() - overview.size()) + ".wav";
        unsigned take = audio_take_of(name);
        if (take && std::find(kept.begin(), kept.end(), take) == kept.end()) going.push_back(entry.path());
    }
    for (const auto& path : going) { std::error_code e; if (fs::remove(path, e)) ++removed; }
    return removed;
}

void AudioTracks::run() {
    for (;;) {
        AudioPackJob job;
        bool have = false;
        std::vector<AudioPack*> going;
        std::deque<std::pair<std::string, std::vector<unsigned>>> pruning;
        {
            std::unique_lock<std::mutex> held(lock);
            // Every 25 ms while a take is written, often enough to keep up with it (the engine host's ring holds 10 s
            // at the highest rate). A take asked for wakes the thread once, to open its file; take_wanted stays set
            // until the take ends, so once `taking` it must not end the wait, or the loop spins a core for the whole
            // take. With no take, ten looks a second still find a ring the engine ended on its own (drain()).
            wake.wait_for(held, std::chrono::milliseconds(taking ? 25 : 100),
                          [&] { return stopping || !jobs.empty() || !trash.empty() || (take_wanted && !taking) || !prunes.empty(); });
            if (stopping) return;
            if (!jobs.empty()) { job = std::move(jobs.front()); jobs.pop_front(); have = true; }
            going.swap(trash);
            pruning.swap(prunes);
        }
        for (const auto& [folder, kept] : pruning) prune(folder, kept);
        if (!going.empty()) {
            if (pager) pager->flush(); // the pager has seen every close the audio thread sent: none of their pins is taken after
            for (auto* pack : going) delete pack; // their takes' parts go with them
        }
        drain();
        if (have) { make(job); drain(); }
    }
}

// Each pattern's take: its file read in place (once for every pattern playing it), its frames counted at the take's
// own rate, and a second from where it starts pinned so it plays at once there.
void AudioTracks::make(const AudioPackJob& job) {
    AudioPackMade out;
    out.track = job.track; out.serial = job.serial;
    auto pack = std::make_unique<AudioPack>();
    pack->track = uint8_t(job.track);
    std::map<unsigned, ZonePart*> parts;
    for (const auto& [pattern, r] : job.takes) {
        if (!r.used()) continue;
        auto found = parts.find(r.take);
        ZonePart* part = nullptr;
        if (found != parts.end()) {
            part = found->second;
        } else {
            const std::string path = (fs::path(job.folder) / audio_take_name(r.take)).string();
            WavReader reader;
            auto owned = std::make_unique<ZonePart>();
            if (reader.open(path, longest_take_seconds).empty() && read_in_place(path, reader.layout(), *owned)) {
                owned->frames.store(owned->total);
                part = owned.get();
                pack->takes.push_back(std::move(owned));
            }
            parts[r.take] = part;
            if (part) out.peaks[r.take] = peaks_of(job.folder, r.take);
        }
        if (!part || !part->total) { ++out.missing; continue; }
        const double k = double(part->rate) / 44100.0; // the take counts 44.1 kHz frames; its file has its own
        auto own = [&](uint32_t frames) { return uint32_t(std::llround(double(frames) * k)); };
        const uint32_t offset = std::min(own(r.offset), part->total - 1);
        const uint32_t frames = std::min(own(r.frames), part->total - offset);
        if (!frames) continue;
        RegionClip& c = pack->add(pattern);
        c.take = part;
        c.offset = offset;
        c.frames = frames;
        c.fade_in = std::min(own(r.fade_in), frames);
        c.fade_out = std::min(own(r.fade_out), frames);
        c.gain = std::pow(10.0f, float(r.gain) / 200.0f);
        c.reverse = r.reverse;
        c.tempo = r.tempo; c.warp = r.warp; c.mode = r.mode;
        c.pitch = std::pow(2.0f, float(r.semi * 100 + r.fine) / 1200.0f);
        const uint32_t second = std::min(frames, uint32_t(part->rate));
        if (pager) {
            if (c.reverse) pager->hold(*part, offset + frames - second, second);
            else if (pager->hold(*part, offset, second)) c.held = second;
        }
    }
    out.pack = pack.release();
    std::lock_guard<std::mutex> held(lock);
    made.push_back(std::move(out));
}

// The take: its file opened when it is asked for, the engine host's frames written as they come, and once the take
// is over and every frame is in, the file closed and named. A take stopped before it began leaves no file.
void AudioTracks::drain() {
    auto& ring = host.audio_take();
    if (!taking) {
        bool wanted = false;
        {
            std::lock_guard<std::mutex> held(lock);
            wanted = take_wanted;
        }
        if (!wanted) {
            auto state = ring.state();
            if (state == Bounce::State::Done || state == Bounce::State::Cancelled || state == Bounce::State::Failed) {
                float drop[2 * 1024];
                while (ring.pop(drop, 1024)) {} // nobody asked for it
                ring.release();
            }
            return;
        }
        std::error_code error;
        fs::create_directories(take_job.folder, error);
        take_job.take = next_take(take_job.folder, take_job.take);
        take_part = (fs::path(take_job.folder) / audio_take_name(take_job.take)).string() + ".part";
        take_rate = ring.rate();
        take_peaks.clear(); take_peak_at = 0;
        taking = true;
        if (!take_file.open(take_part, take_rate, take_bits)) {
            AudioTakeDone done;
            done.job = take_job; done.error = "cannot write to the card";
            taking = false;
            std::lock_guard<std::mutex> held(lock);
            take_wanted = false;
            recorded.push_back(std::move(done));
            busy.store(false);
            return;
        }
    }
    chunk.resize(2 * 16384);
    const double per = double(take_rate) * take_peak_frames / 44100.0; // the take's frames an overview column
    for (unsigned n; (n = ring.pop(chunk.data(), 16384)) > 0;) {
        take_file.add(chunk.data(), n);
        for (unsigned f = 0; f < n; ++f, ++take_peak_at) {
            size_t column = size_t(double(take_peak_at) / per);
            if (column >= take_peaks.size()) take_peaks.resize(column + 1, 0);
            uint8_t v = uint8_t(std::min(1.0f, std::max(std::fabs(chunk[2 * f]), std::fabs(chunk[2 * f + 1]))) * 255);
            take_peaks[column] = std::max(take_peaks[column], v);
        }
    }
    auto state = ring.state();
    bool ended = state == Bounce::State::Done || state == Bounce::State::Cancelled || state == Bounce::State::Failed;
    if (!ended || !ring.drained()) return; // a take stopped before its first bar ends Cancelled, with nothing in it
    AudioTakeDone done;
    done.job = take_job;
    done.pattern = host.audio_take_pattern();
    done.rate = take_rate;
    done.frames = take_file.frames();
    bool whole = take_file.close();
    ring.release();
    std::error_code error;
    const std::string final_path = take_part.substr(0, take_part.size() - 5);
    if (state == Bounce::State::Failed) done.error = "the card could not keep up";
    else if (!whole) done.error = "cannot write to the card";
    else if (!done.frames) done.error = "nothing recorded";
    if (done.error.empty()) {
        fs::rename(take_part, final_path, error);
        write_peaks((fs::path(take_job.folder) / audio_peaks_name(take_job.take)).string(), take_peaks);
        done.peaks = std::move(take_peaks);
    }
    if (!done.error.empty() || error) {
        fs::remove(take_part, error);
        if (done.error.empty()) done.error = "cannot name the take";
    }
    taking = false;
    std::lock_guard<std::mutex> held(lock);
    take_wanted = false;
    recorded.push_back(std::move(done));
    busy.store(false);
}

} // namespace rhythmer
