// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/stem_writer.h"
#include <algorithm>
#include <cmath>

namespace rhythmer {

StemWriter::~StemWriter() {
    {
        std::lock_guard<std::mutex> guard(lock);
        quit = true;
    }
    wake.notify_all();
    if (thread.joinable()) thread.join();
}

bool StemWriter::open(const std::vector<std::string>& paths, unsigned rate) {
    streams.clear();
    pending.assign(paths.size(), {});
    peaks.assign(paths.size(), 0.0f);
    over.store(false); write_failed.store(false);
    for (const auto& path : paths) {
        auto stream = std::make_unique<WavStream>();
        if (!stream->open(path, rate, 32)) { streams.clear(); return false; }
        streams.push_back(std::move(stream));
    }
    if (!thread.joinable()) thread = std::thread([this] { run(); });
    return true;
}

bool StemWriter::add(unsigned n, const float* interleaved, unsigned frames) {
    if (n >= streams.size()) return false;
    auto& into = pending[n];
    if (into.empty()) into.reserve(size_t(chunk_frames) * 2);
    float loud = peaks[n];
    for (size_t i = 0; i < size_t(frames) * 2; ++i) loud = std::max(loud, std::fabs(interleaved[i]));
    peaks[n] = loud;
    into.insert(into.end(), interleaved, interleaved + size_t(frames) * 2);
    if (into.size() >= size_t(chunk_frames) * 2) push(n);
    return !over.load(std::memory_order_relaxed);
}

void StemWriter::add_silence(unsigned n, unsigned frames) {
    if (n >= streams.size()) return;
    pending[n].insert(pending[n].end(), size_t(frames) * 2, 0.0f);
    if (pending[n].size() >= size_t(chunk_frames) * 2) push(n);
}

// File n's frames so far onto the queue; past most_bytes they are dropped and the files fail.
void StemWriter::push(unsigned n) {
    Chunk chunk;
    chunk.file = n;
    chunk.frames.swap(pending[n]);
    if (chunk.frames.empty()) return;
    const size_t bytes = chunk.frames.size() * sizeof(float);
    {
        std::lock_guard<std::mutex> guard(lock);
        if (queued + bytes > most_bytes) { over.store(true); return; }
        queued += bytes;
        queue.push_back(std::move(chunk));
    }
    wake.notify_all();
}

void StemWriter::flush() {
    for (unsigned n = 0; n < pending.size(); ++n) push(n);
}

bool StemWriter::done() const {
    std::lock_guard<std::mutex> guard(lock);
    return queue.empty() && !writing;
}

size_t StemWriter::queued_bytes() const {
    std::lock_guard<std::mutex> guard(lock);
    return queued;
}

bool StemWriter::close(std::vector<float>* loudest) {
    bool ok = !over.load() && !write_failed.load();
    for (auto& stream : streams) ok = stream->close() && ok;
    if (loudest) *loudest = peaks;
    streams.clear();
    pending.clear();
    return ok;
}

void StemWriter::run() {
    std::unique_lock<std::mutex> guard(lock);
    for (;;) {
        wake.wait(guard, [this] { return quit || !queue.empty(); });
        if (quit) return;
        Chunk chunk = std::move(queue.front());
        queue.pop_front();
        writing = true;
        guard.unlock();
        const size_t bytes = chunk.frames.size() * sizeof(float);
        if (!streams[chunk.file]->add(chunk.frames.data(), unsigned(chunk.frames.size() / 2))) write_failed.store(true);
        guard.lock();
        queued -= bytes;
        writing = false;
    }
}

} // namespace rhythmer
