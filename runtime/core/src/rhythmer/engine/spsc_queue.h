// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <array>
#include <atomic>

namespace rhythmer {

// One producer thread and one consumer thread, never waiting and never
// allocating: the queue between the engine's threads (commands into the
// audio thread, what it is done with back out). A full queue refuses the
// event and remembers it overflowed. sample-base's MidiQueue (surface/midi.h)
// as it is, so the core needs no hardware runtime.
template <unsigned Size, class Event> class SpscQueue {
    static_assert(Size >= 2, "A queue needs a spare slot");
public:
    bool push(const Event& event) {
        auto w = write.load(std::memory_order_relaxed);
        auto next = (w + 1) % Size;
        if (next == read.load(std::memory_order_acquire)) {
            overflow.store(true, std::memory_order_release);
            return false;
        }
        events[w] = event;
        write.store(next, std::memory_order_release);
        return true;
    }
    bool pop(Event& event) {
        auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire)) return false;
        event = events[r];
        read.store((r + 1) % Size, std::memory_order_release);
        return true;
    }
    bool take_overflow() { return overflow.exchange(false, std::memory_order_acq_rel); }
private:
    std::array<Event, Size> events {};
    std::atomic<unsigned> read {0}, write {0};
    std::atomic<bool> overflow {false};
};

} // namespace rhythmer
