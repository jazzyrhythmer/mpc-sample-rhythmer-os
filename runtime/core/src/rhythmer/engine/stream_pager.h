// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/spsc_queue.h"
#include "rhythmer/engine/multisample.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

// What keeps a multisampled voice off the card's time. A zone's frames are
// mapped from the card and read as memory, which is a page fault, and so a
// wait on the SD card, for any page not in RAM: the audio thread must never
// do that. This thread pins the chunks ahead of each voice (mlock: a wait
// here, not there) and publishes how far is safe; the audio thread reads
// only below it. mlock answers ENOMEM where a read would be SIGBUS, so a card
// pulled out stops a voice instead of the app.
//
// A PCM WAV needs no copy laid out to be read so (read_in_place()): where a
// mapped part's chunk is pinned, its chunk is read from the file and turned
// to floats in memory. The voice reads floats below the published limit
// either way.
//
// A chunk read from a file stays in memory once nothing counts on it: a pad
// hit again, a loop come round and a voice that follows another through the
// same sample read none of it from the card twice. All such chunks together
// take up to stream_cache_bytes; past that the chunks nothing counts on are
// given back, the one longest unused first. The file stays open between
// reads (up to stream_files_open of them, the one opened first closed for the
// next), and the pager's thread reads the card ahead of every other reader
// (the real-time I/O class).
namespace rhythmer {

constexpr uint64_t stream_cache_bytes = uint64_t(200) << 20;
constexpr unsigned stream_files_open = 256;
// The chunks read in place and in memory now, in bytes, and how much they may take (tests set less).
uint64_t stream_cache_held();
void stream_cache_budget(uint64_t bytes);
// Every such chunk nothing counts on given back now, for a load that needs the memory: the bytes that went.
uint64_t stream_cache_give_back();

struct WavLayout; // engine/sample_data.h

// A part read in place from the PCM WAV at `path`, as `layout` says it lies there: no copy of it is laid out on the
// card. Room is set aside for its frames as floats at the file's own rate, none of it memory until a chunk is
// wanted: the pager then reads that chunk of the file and turns it to floats (a tenth of a millisecond) where for a
// mapped part it pins pages, and the chunk stays until the memory is wanted for another. False when the file cannot
// be read; the part is then as it was.
bool read_in_place(const std::string& path, const WavLayout& layout, ZonePart& part);
// `frames` frames of such a part from `from` on, straight from its file into `left` and `right` (a mono file's on
// both), its memory not touched: a few frames looked at by a thread that is not the audio's. False when the file
// gives less.
bool read_frames(const ZonePart& part, uint32_t from, uint32_t frames, float* left, float* right);

class StreamPager {
public:
    static constexpr unsigned stream_count = 96;
    using Pin = bool (*)(const void* address, size_t bytes, bool lock);
    explicit StreamPager(Pin pin = nullptr);
    ~StreamPager();
    StreamPager(const StreamPager&) = delete;
    StreamPager& operator=(const StreamPager&) = delete;
    void start();
    void stop();

    // Audio thread, or its render workers (engine/render_pool.h): open(),
    // restart(), close() and block() take a short spin lock, as kit tracks may
    // render on several cores at once; a stream's own calls (place(), limit(),
    // dead()) are its voice's alone. open() gives a stream, or -1 with none free.
    // `safe`: frames from `frame` on (reversed: down to it) that are held already (hold()), read at once; past them
    // the pager's word is waited for (a zone's start and loop are held a chunk; freeze plan F2b: a print's start and
    // bar lines; a long kit sample's pads' starts, ends and loop points).
    int open(ZonePart& part, uint32_t frame, bool reverse, float speed, uint32_t safe = stream_chunk);
    void restart(int stream, uint32_t frame, uint32_t safe = stream_chunk); // a loop came round to here
    void place(int stream, uint32_t frame) { slots[size_t(stream)].at.store(frame, std::memory_order_relaxed); }
    // Reading forwards: frames below this are safe. Reversed: frames at or above it.
    uint32_t limit(int stream) const;
    bool dead(int stream) const { return slots[size_t(stream)].dead.load(std::memory_order_relaxed); }
    void close(int stream);
    void block();                             // closes the queue had no room for
    void underrun() { under.fetch_add(1, std::memory_order_relaxed); }

    // Before a part's mapping goes (any thread but audio and the pager's):
    // returns once the pager has seen everything the audio thread sent.
    void flush();
    unsigned underruns() const { return under.load(std::memory_order_relaxed); }

    // The landing thread, before the part is published: frames that stay in
    // memory for as long as the part does. False when they could not be pinned.
    bool hold(ZonePart& part, uint32_t frame, uint32_t frames);
    // A part read in place, any thread but audio and the pager's (its chunks are counted under its source's lock):
    // one chunk held (its count raised, read from the file when it is not in memory), or let go (at 0 it stays in
    // memory, the first to go when the memory is wanted).
    bool hold_chunk(ZonePart& part, uint32_t chunk) { return chunk < part.pins.size() && pin_chunk(part, chunk); }
    void let_go(ZonePart& part, uint32_t chunk) { if (chunk < part.pins.size()) unpin_chunk(part, chunk); }

    bool pump(); // one pass: messages, then a chunk for each stream that wants one; true if it did anything

private:
    struct Slot {
        std::atomic<uint32_t> at {0};
        std::atomic<uint64_t> ready {0}; // serial << 32 | limit
        std::atomic<bool> dead {false};
        uint32_t serial = 0, head = 0;   // audio thread's
        bool reverse = false;
        bool used = false, closing = false;
    };
    struct Work {
        ZonePart* part = nullptr;
        uint32_t serial = 0;
        bool reverse = false, dead = false;
        float speed = 1;
        uint32_t lo = 0, hi = 0;          // the chunks this stream has pinned: [lo, hi)
    };
    struct Message {
        enum Kind : uint8_t { Open, Restart, Close } kind = Open;
        int16_t stream = 0;
        bool reverse = false;
        ZonePart* part = nullptr;
        uint32_t frame = 0, serial = 0;
        float speed = 1;
    };
    static uint32_t head_of(uint32_t frame, uint32_t safe, bool reverse, uint32_t total);
    void begin(Work& work, const Message& message);
    void release(Work& work);
    bool step(unsigned stream);
    bool pin_chunk(ZonePart& part, uint32_t chunk);
    void unpin_chunk(ZonePart& part, uint32_t chunk);
    void run();

    Pin pin_fn;
    std::array<Slot, stream_count> slots {};
    std::array<Work, stream_count> work {};
    SpscQueue<4096, Message> messages;   // one producer: the holder of `sending`
    std::atomic_flag sending = ATOMIC_FLAG_INIT;
    struct Sending { // the producer side for the length of a call
        std::atomic_flag& flag;
        explicit Sending(std::atomic_flag& f) : flag(f) { while (flag.test_and_set(std::memory_order_acquire)) {} }
        ~Sending() { flag.clear(std::memory_order_release); }
    };
    std::atomic<unsigned> under {0};
    std::atomic<uint64_t> flush_asked {0}, flush_done {0};
    std::atomic<bool> running {false};
    std::thread thread;
    // The thread's rest. With a stream open it looks every millisecond, as
    // it always did: a voice moves on by itself and its reads must stay ahead. With none it has nothing to follow,
    // and sleeps until kick() says a message waits (a stream opened, restarted or closed, a flush, stop()).
    // kick() is for any thread and never blocks: it wakes the thread only while it rests.
    std::atomic<uint32_t> kicks {0}, resting {0};
    void kick();
    bool following() const; // the pager's thread: some stream has reads to keep ahead
};

} // namespace rhythmer
