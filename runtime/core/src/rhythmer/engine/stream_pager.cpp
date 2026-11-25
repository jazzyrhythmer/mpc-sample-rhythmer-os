// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/engine/stream_pager.h"
#include "rhythmer/engine/sample_data.h"
#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <list>
#include <map>
#include <pthread.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#if defined(__linux__)
#include <ctime>
#include <linux/futex.h>
#include <sys/syscall.h>
#endif

namespace rhythmer {
namespace {
bool real_pin(const void* address, size_t bytes, bool lock) { return (lock ? mlock(address, bytes) : munlock(address, bytes)) == 0; }
uint32_t chunk_count(uint32_t frames) { return frames ? (frames - 1) / stream_chunk + 1 : 0; }
size_t chunk_bytes(uint32_t frames, uint32_t chunk) { return std::min<uint32_t>(stream_chunk, frames - chunk * stream_chunk) * sizeof(float); }
} // namespace

namespace {
// A side's room in a part read in place: whole chunks, so each chunk of each side is whole pages to let go of.
size_t side_bytes(uint32_t frames) { return (size_t(frames) * sizeof(float) + stream_chunk_bytes - 1) / stream_chunk_bytes * stream_chunk_bytes; }
// What a chunk of a part read in place takes in memory: both sides.
uint64_t kept_bytes(const ZonePart& part, uint32_t chunk) { return uint64_t(chunk_bytes(part.total, chunk)) * (part.right != part.left ? 2 : 1); }

// The chunks read in place and in memory, of every part and every pager (the live one's and a print's). `idle`: the
// ones nothing counts on, the longest so first; `at`: where each is in it. `open`: the sources whose file is open,
// the first opened first. A source's lock is taken before `lock`, never after it but by a try: trim() and
// close_one() pass over a source that is being read.
struct Idle { ZonePart* part; uint32_t chunk; };
struct StreamCache {
    std::mutex lock;
    std::list<Idle> idle;
    std::map<std::pair<const ZonePart*, uint32_t>, std::list<Idle>::iterator> at;
    uint64_t bytes = 0, budget = stream_cache_bytes;
    std::vector<ZoneSource*> open;
};
StreamCache& cache() { static StreamCache c; return c; }

// The same chunk given back: its pages go, and read as silence until it is filled again.
void empty_chunk(ZonePart& part, uint32_t chunk) {
    const uint32_t first = chunk * stream_chunk;
    madvise(const_cast<float*>(part.left) + first, stream_chunk_bytes, MADV_DONTNEED);
    if (part.right != part.left) madvise(const_cast<float*>(part.right) + first, stream_chunk_bytes, MADV_DONTNEED);
}

// Under the part's source's lock. A chunk nothing counts on any more: the latest of the idle.
void now_idle(ZonePart& part, uint32_t chunk) {
    StreamCache& c = cache();
    std::lock_guard<std::mutex> guard(c.lock);
    c.at[{&part, chunk}] = c.idle.insert(c.idle.end(), {&part, chunk});
}
// Under the part's source's lock. An idle chunk counted on again.
void now_wanted(ZonePart& part, uint32_t chunk) {
    StreamCache& c = cache();
    std::lock_guard<std::mutex> guard(c.lock);
    auto found = c.at.find({&part, chunk});
    if (found == c.at.end()) return;
    c.idle.erase(found->second);
    c.at.erase(found);
}
// Under the part's source's lock. A chunk read into memory.
void now_kept(ZonePart& part, uint32_t chunk) {
    StreamCache& c = cache();
    std::lock_guard<std::mutex> guard(c.lock);
    c.bytes += kept_bytes(part, chunk);
}
// With no source's lock held. Past the budget (`all`: past nothing), the idle chunks given back, the longest idle
// first; one whose part is being read or counted now is passed over.
void trim(bool all = false) {
    StreamCache& c = cache();
    std::lock_guard<std::mutex> guard(c.lock);
    for (auto idle = c.idle.begin(); c.bytes > (all ? 0 : c.budget) && idle != c.idle.end();) {
        ZonePart& part = *idle->part;
        const uint32_t chunk = idle->chunk;
        std::unique_lock<std::mutex> own(part.source->lock, std::try_to_lock);
        if (!own.owns_lock()) { ++idle; continue; }
        empty_chunk(part, chunk);
        part.source->kept[chunk] = 0;
        c.bytes -= std::min(c.bytes, kept_bytes(part, chunk));
        c.at.erase({&part, chunk});
        idle = c.idle.erase(idle);
    }
}
// A part's mapping is going: its chunks are in memory no more.
void forget(ZonePart& part) {
    StreamCache& c = cache();
    std::lock_guard<std::mutex> guard(c.lock);
    for (auto idle = c.idle.begin(); idle != c.idle.end();) {
        if (idle->part != &part) { ++idle; continue; }
        c.at.erase({&part, idle->chunk});
        idle = c.idle.erase(idle);
    }
    for (uint32_t chunk = 0; chunk < part.source->kept.size(); ++chunk)
        if (part.source->kept[chunk]) c.bytes -= std::min(c.bytes, kept_bytes(part, chunk));
}

// Under the source's lock. Its file, opened at its first read and kept open: a read then needs no look through the
// card's folders, which the card may have to be read for. With stream_files_open open, the one opened first is closed.
int file_of(ZoneSource& source) {
    if (source.file >= 0) return source.file;
    source.file = open(source.path.c_str(), O_RDONLY | O_CLOEXEC);
    if (source.file < 0) return -1; // the card is gone
    StreamCache& c = cache();
    std::lock_guard<std::mutex> guard(c.lock);
    c.open.push_back(&source);
    for (size_t n = 0; c.open.size() > stream_files_open && n + 1 < c.open.size(); ++n) {
        ZoneSource& other = *c.open[n];
        std::unique_lock<std::mutex> own(other.lock, std::try_to_lock);
        if (!own.owns_lock()) continue; // being read now: the next
        close(other.file);
        other.file = -1;
        c.open.erase(c.open.begin() + std::ptrdiff_t(n));
        break;
    }
    return source.file;
}
// Under the source's lock (or at its end). Its file closed: a read failed, or the source is going.
void close_file(ZoneSource& source) {
    StreamCache& c = cache();
    std::lock_guard<std::mutex> guard(c.lock); // file_of() may be closing it for another's
    if (source.file < 0) return;
    close(source.file);
    source.file = -1;
    c.open.erase(std::remove(c.open.begin(), c.open.end(), &source), c.open.end());
}

// A chunk of a part read in place, from its file into the part's memory as floats. False when the file gives less.
bool fill_chunk(ZonePart& part, uint32_t chunk) {
    ZoneSource& source = *part.source;
    const uint32_t first = chunk * stream_chunk, frames = std::min<uint32_t>(stream_chunk, part.total - first);
    WavLayout layout;
    layout.channels = source.channels; layout.bits = source.bits; layout.floating = source.floating;
    const size_t block = layout.block();
    static thread_local std::vector<uint8_t> bytes; // the pager's thread, and a landing thread's hold()
    bytes.resize(size_t(frames) * block);
    const int file = file_of(source);
    if (file < 0) return false;
    size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t got = pread(file, bytes.data() + done, bytes.size() - done, off_t(source.data_at + uint64_t(first) * block + done));
        if (got <= 0) break; // the file is shorter than it said, or the card is gone
        done += size_t(got);
    }
    if (done < bytes.size()) { close_file(source); return false; } // opened again at the next read: a card put back in
    float* left = const_cast<float*>(part.left) + first;
    wav_frames(layout, bytes.data(), frames, left, part.right != part.left ? const_cast<float*>(part.right) + first : nullptr);
    return true;
}
} // namespace

ZonePart::~ZonePart() {
    if (source) forget(*this);
    if (mapping) munmap(mapping, mapping_bytes);
}
ZoneSource::~ZoneSource() { close_file(*this); }

uint64_t stream_cache_held() {
    StreamCache& c = cache();
    std::lock_guard<std::mutex> guard(c.lock);
    return c.bytes;
}
void stream_cache_budget(uint64_t bytes) {
    { StreamCache& c = cache(); std::lock_guard<std::mutex> guard(c.lock); c.budget = bytes; }
    trim();
}
uint64_t stream_cache_give_back() {
    const uint64_t was = stream_cache_held();
    trim(true);
    const uint64_t now = stream_cache_held();
    return was > now ? was - now : 0;
}

bool read_frames(const ZonePart& part, uint32_t from, uint32_t frames, float* left, float* right) {
    if (!part.source || from >= part.total) return false;
    frames = std::min(frames, part.total - from);
    const ZoneSource& source = *part.source;
    WavLayout layout;
    layout.channels = source.channels; layout.bits = source.bits; layout.floating = source.floating;
    const size_t block = layout.block();
    std::vector<uint8_t> bytes(size_t(frames) * block);
    const int file = open(source.path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file < 0) return false;
    size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t got = pread(file, bytes.data() + done, bytes.size() - done, off_t(source.data_at + uint64_t(from) * block + done));
        if (got <= 0) break;
        done += size_t(got);
    }
    close(file);
    if (done < bytes.size()) return false;
    wav_frames(layout, bytes.data(), frames, left, right);
    return true;
}

bool read_in_place(const std::string& path, const WavLayout& layout, ZonePart& part) {
    if (!layout.frames || layout.channels < 1 || layout.channels > 2) return false;
    if (access(path.c_str(), R_OK) != 0) return false;
    auto source = std::make_unique<ZoneSource>();
    source->path = path;
    source->data_at = layout.data_at; source->channels = uint8_t(layout.channels); source->bits = uint8_t(layout.bits);
    source->floating = layout.floating;
    // Room for every frame, none of it memory yet: a page is had only once a chunk is filled.
    const size_t side = side_bytes(layout.frames), bytes = side * layout.channels;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_NORESERVE)
    flags |= MAP_NORESERVE;
#endif
    void* start = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (start == MAP_FAILED) return false;
    part.mapping = start; part.mapping_bytes = bytes;
    part.left = static_cast<const float*>(start);
    part.right = layout.channels > 1 ? part.left + side / sizeof(float) : part.left;
    part.pins.assign((layout.frames - 1) / stream_chunk + 1, 0);
    source->kept.assign(part.pins.size(), 0);
    part.rate = float(layout.rate);
    part.total = layout.frames;
    part.source = std::move(source);
    return true;
}

StreamPager::StreamPager(Pin pin) : pin_fn(pin ? pin : real_pin) {}
StreamPager::~StreamPager() { stop(); }

void StreamPager::start() {
    if (running.exchange(true)) return;
    thread = std::thread([this] { run(); });
}

void StreamPager::stop() {
    if (!running.exchange(false)) return;
    kick();
    if (thread.joinable()) thread.join();
}

void StreamPager::kick() {
    kicks.fetch_add(1, std::memory_order_seq_cst);
#if defined(__linux__)
    if (resting.load(std::memory_order_seq_cst))
        syscall(SYS_futex, reinterpret_cast<uint32_t*>(&kicks), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
#endif
}

bool StreamPager::following() const {
    for (const auto& w : work) if (w.part && !w.dead) return true;
    return false;
}

void StreamPager::run() {
    // Below the audio thread (30), above whatever else would keep it from the card.
    sched_param priority {};
    priority.sched_priority = 20;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority); // best effort
#if defined(__linux__) && defined(SYS_ioprio_set)
    // Its reads go to the card before any other thread's (a load, a save, a folder listed): the real-time I/O class,
    // its first level, for this thread. Best effort: the card's scheduler has to know classes (bfq does).
    syscall(SYS_ioprio_set, 1 /* this thread */, 0, (1 /* real-time */ << 13) | 0);
#endif
    while (running.load(std::memory_order_relaxed)) {
        const uint32_t seen = kicks.load(std::memory_order_seq_cst); // before the look: a kick after it is not slept through
        if (pump()) continue;
#if defined(__linux__)
        if (!following()) {
            // No stream open: nothing moves until a message comes. The kernel compares the word with `seen` before
            // it sleeps; the limit is only for a wake that went missing, never needed in the ordinary run.
            timespec limit {0, 250 * 1000 * 1000};
            resting.store(1, std::memory_order_seq_cst);
            if (running.load(std::memory_order_relaxed))
                syscall(SYS_futex, reinterpret_cast<uint32_t*>(&kicks), FUTEX_WAIT_PRIVATE, seen, &limit, nullptr, 0);
            resting.store(0, std::memory_order_seq_cst);
            continue;
        }
#else
        (void)seen;
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int StreamPager::open(ZonePart& part, uint32_t frame, bool reverse, float speed, uint32_t safe) {
    Sending held(sending);
    for (unsigned n = 0; n < stream_count; ++n) {
        auto& s = slots[n];
        if (s.used) continue;
        s.used = true;
        s.closing = false;
        s.dead.store(false, std::memory_order_relaxed);
        s.reverse = reverse;
        s.head = head_of(frame, safe, reverse, part.total);
        s.at.store(frame, std::memory_order_relaxed);
        Message m;
        m.kind = Message::Open; m.stream = int16_t(n); m.reverse = reverse; m.part = &part; m.frame = frame; m.speed = speed;
        m.serial = ++s.serial;
        if (!messages.push(m)) { s.used = false; return -1; }
        kick();
        return int(n);
    }
    return -1;
}

void StreamPager::restart(int stream, uint32_t frame, uint32_t safe) {
    Sending held(sending);
    auto& s = slots[size_t(stream)];
    s.head = head_of(frame, safe, s.reverse, UINT32_MAX);
    s.at.store(frame, std::memory_order_relaxed);
    Message m;
    m.kind = Message::Restart; m.stream = int16_t(stream); m.frame = frame; m.reverse = s.reverse;
    m.serial = ++s.serial;
    messages.push(m); // on overflow the head still plays, and a gap follows
    kick();
}

uint32_t StreamPager::head_of(uint32_t frame, uint32_t safe, bool reverse, uint32_t total) {
    if (!reverse) return uint32_t(std::min<uint64_t>(uint64_t(frame) + safe, total));
    return safe ? (frame + 1 > safe ? frame + 1 - safe : 0) : UINT32_MAX; // reversed: from here down, held; none: nothing
}

uint32_t StreamPager::limit(int stream) const {
    const auto& s = slots[size_t(stream)];
    uint64_t ready = s.ready.load(std::memory_order_acquire);
    if (uint32_t(ready >> 32) == s.serial) return s.reverse ? std::min(uint32_t(ready), s.head) : std::max(uint32_t(ready), s.head);
    return s.head;
}

void StreamPager::close(int stream) {
    Sending held(sending);
    auto& s = slots[size_t(stream)];
    Message m;
    m.kind = Message::Close; m.stream = int16_t(stream);
    if (messages.push(m)) { s.used = false; s.closing = false; }
    else s.closing = true;
    kick();
}

void StreamPager::block() { // close() takes the lock for each
    for (unsigned n = 0; n < stream_count; ++n) if (slots[n].closing) close(int(n));
}

void StreamPager::flush() {
    if (!running.load(std::memory_order_relaxed)) { pump(); return; }
    uint64_t asked = flush_asked.fetch_add(1, std::memory_order_acq_rel) + 1;
    kick();
    for (unsigned n = 0; n < 2000 && flush_done.load(std::memory_order_acquire) < asked; ++n) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

bool StreamPager::pin_chunk(ZonePart& part, uint32_t chunk) {
    if (part.source) { // read in place: the first to want a chunk not in memory fills it from the file
        {
            std::lock_guard<std::mutex> guard(part.source->lock);
            if (part.pins[chunk]) { ++part.pins[chunk]; return true; }
            if (part.source->kept[chunk]) { // read before and still here: nothing to read
                now_wanted(part, chunk);
                part.pins[chunk] = 1;
                return true;
            }
            if (!fill_chunk(part, chunk)) return false;
            part.source->kept[chunk] = 1;
            now_kept(part, chunk);
            part.pins[chunk] = 1;
        }
        trim(); // room for it: the chunks longest unused go
        return true;
    }
    if (part.pins[chunk]) { ++part.pins[chunk]; return true; }
    size_t bytes = chunk_bytes(part.total, chunk);
    if (!pin_fn(part.left + chunk * stream_chunk, bytes, true)) return false;
    if (part.right != part.left && !pin_fn(part.right + chunk * stream_chunk, bytes, true)) {
        pin_fn(part.left + chunk * stream_chunk, bytes, false);
        return false;
    }
    part.pins[chunk] = 1;
    return true;
}

void StreamPager::unpin_chunk(ZonePart& part, uint32_t chunk) {
    if (part.source) { // read in place: it stays in memory, the first to go when the memory is wanted
        {
            std::lock_guard<std::mutex> guard(part.source->lock);
            if (--part.pins[chunk]) return;
            now_idle(part, chunk);
        }
        trim();
        return;
    }
    if (--part.pins[chunk]) return;
    size_t bytes = chunk_bytes(part.total, chunk);
    pin_fn(part.left + chunk * stream_chunk, bytes, false);
    if (part.right != part.left) pin_fn(part.right + chunk * stream_chunk, bytes, false);
}

bool StreamPager::hold(ZonePart& part, uint32_t frame, uint32_t frames) {
    const uint32_t total = part.total;
    if (!total || frame >= total) return true;
    uint32_t last = std::min(frame + std::max(frames, 1u), total) - 1;
    for (uint32_t chunk = frame / stream_chunk; chunk <= last / stream_chunk; ++chunk)
        if (!pin_chunk(part, chunk)) return false;
    return true;
}

void StreamPager::release(Work& w) {
    if (w.part) for (uint32_t c = w.lo; c < w.hi; ++c) unpin_chunk(*w.part, c);
    w.lo = w.hi = 0;
}

void StreamPager::begin(Work& w, const Message& m) {
    release(w);
    if (m.part) w.part = m.part;
    if (m.kind == Message::Open) w.speed = m.speed;
    w.serial = m.serial;
    w.reverse = m.reverse;
    w.dead = false;
    uint32_t chunk = m.frame / stream_chunk;
    w.lo = w.hi = w.reverse ? chunk + 1 : chunk;
}

// One more chunk ahead of the voice if it wants one, and what it has left behind let go.
bool StreamPager::step(unsigned n) {
    auto& w = work[n];
    if (!w.part || w.dead) return false;
    auto& slot = slots[n];
    const uint32_t frames = w.part->total, chunks = chunk_count(frames);
    const uint32_t at = slot.at.load(std::memory_order_relaxed);
    const uint32_t ahead = uint32_t(stream_chunk * std::max(1.0f, w.speed) * 2);
    auto publish = [&](uint32_t limit) { slot.ready.store(uint64_t(w.serial) << 32 | limit, std::memory_order_release); };
    // A voice that went on while its reads were late (SamplerEngine::pass()) is past everything pinned for it: what
    // lies between is let go unread, and the reading goes on from where the voice is.
    const uint32_t here = std::min(at, frames ? frames - 1 : 0) / stream_chunk;
    if (!w.reverse ? here > w.hi : here + 1 < w.lo) {
        release(w);
        w.lo = w.hi = w.reverse ? here + 1 : here;
    }
    if (!w.reverse) {
        uint32_t want = std::min<uint64_t>(chunks, (uint64_t(at) + ahead) / stream_chunk + 1);
        if (w.hi < want) {
            if (!pin_chunk(*w.part, w.hi)) { w.dead = true; slot.dead.store(true, std::memory_order_relaxed); return false; }
            ++w.hi;
            publish(std::min<uint64_t>(uint64_t(w.hi) * stream_chunk, frames));
            return true;
        }
        uint32_t chunk = at / stream_chunk, keep = chunk ? chunk - 1 : 0; // a chunk behind stays, for a stretch's taps
        while (w.lo < keep && w.lo < w.hi) unpin_chunk(*w.part, w.lo++);
    } else {
        uint32_t low = at > ahead ? (at - ahead) / stream_chunk : 0;
        if (w.lo > low) {
            if (!pin_chunk(*w.part, w.lo - 1)) { w.dead = true; slot.dead.store(true, std::memory_order_relaxed); return false; }
            --w.lo;
            publish(w.lo * stream_chunk);
            return true;
        }
        uint32_t keep = at / stream_chunk + 2;
        while (w.hi > keep && w.hi > w.lo) unpin_chunk(*w.part, --w.hi);
    }
    return false;
}

bool StreamPager::pump() {
    bool did = false;
    uint64_t asked = flush_asked.load(std::memory_order_acquire);
    Message m;
    while (messages.pop(m)) {
        did = true;
        auto& w = work[size_t(m.stream)];
        if (m.kind == Message::Close) { release(w); w.part = nullptr; }
        else begin(w, m);
    }
    flush_done.store(asked, std::memory_order_release);
    for (unsigned n = 0; n < stream_count; ++n) did |= step(n);
    return did;
}

} // namespace rhythmer
