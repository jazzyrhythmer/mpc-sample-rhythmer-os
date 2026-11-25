// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/sample_data.h"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rhythmer {

// Stems to the card on a thread of their own (roadmap P4: a mixdown's stems, its pad stems and the mix beside them),
// each file a big chunk at a time. The service thread adds each file's frames as they come; they wait in memory until
// a chunk is full, then join the queue, which the thread writes in turn. Few big writes keep the card near the speed
// it writes one file at (21 MB/s on the MPC's), where small ones to many files spread at once are slow. A card slower
// than the stems lets the queue grow, up to most_bytes, and the song plays on; past that the files fail.
class StemWriter {
public:
    static constexpr unsigned chunk_frames = 65536;          // 512 kB of stereo float a file at a time
    static constexpr size_t most_bytes = size_t(640) << 20;  // the queue's most (the MPC has about 1.5 GB free)
    ~StemWriter();
    // Service thread: the files, each made new as stereo 32-bit float at `rate` Hz; false when one could not be made
    // (none are kept then). Only while nothing is being written (done()).
    bool open(const std::vector<std::string>& paths, unsigned rate);
    unsigned files() const { return unsigned(streams.size()); }
    // Service thread: `frames` interleaved stereo frames onto file `n`; false once the queue has run past most_bytes.
    bool add(unsigned n, const float* interleaved, unsigned frames);
    void add_silence(unsigned n, unsigned frames);
    // Service thread: what is left becomes the queue's last chunks; done() once the thread has written them all.
    void flush();
    bool done() const;
    // Service thread, once done(): the files closed, each one's loudest sample (0: silent) in `loudest`; false when a
    // write failed or the queue ran over.
    bool close(std::vector<float>* loudest = nullptr);
    size_t queued_bytes() const;
private:
    struct Chunk { unsigned file = 0; std::vector<float> frames; };
    std::vector<std::unique_ptr<WavStream>> streams;
    std::vector<std::vector<float>> pending;  // each file's frames not yet a chunk
    std::vector<float> peaks;
    std::deque<Chunk> queue;
    size_t queued = 0;                        // bytes in the queue and being written
    bool writing = false, quit = false;
    std::atomic<bool> over {false}, write_failed {false};
    mutable std::mutex lock;
    std::condition_variable wake;
    std::thread thread;
    void push(unsigned n);
    void run();
};

} // namespace rhythmer
