// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/load_pool.h"
#include <chrono>
#if defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace rhythmer {

LoadPool::LoadPool(unsigned count) {
    for (unsigned n = 0; n < count; ++n) threads.emplace_back([this] { run(); });
}

LoadPool::~LoadPool() {
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
        waiting.clear();
    }
    wake.notify_all();
    for (auto& t : threads) if (t.joinable()) t.join();
}

void LoadPool::post(std::function<void()> work) {
    {
        std::lock_guard<std::mutex> guard(lock);
        if (stopping) return;
        waiting.push_back(std::move(work));
    }
    wake.notify_one();
}

unsigned LoadPool::pending() const {
    std::lock_guard<std::mutex> guard(lock);
    return unsigned(waiting.size()) + running;
}

bool LoadPool::wait_idle(unsigned most_ms) {
    std::unique_lock<std::mutex> held(lock);
    return rest.wait_for(held, std::chrono::milliseconds(most_ms), [this] { return waiting.empty() && !running; });
}

void LoadPool::run() {
#if defined(__linux__)
    setpriority(PRIO_PROCESS, id_t(syscall(SYS_gettid)), 5); // the screen and the controls come first
#endif
    std::unique_lock<std::mutex> held(lock);
    for (;;) {
        wake.wait(held, [this] { return stopping || !waiting.empty(); });
        if (stopping) return;
        auto work = std::move(waiting.front());
        waiting.pop_front();
        ++running;
        held.unlock();
        work();
        work = nullptr; // what it held is let go here, not under the lock
        held.lock();
        --running;
        if (waiting.empty() && !running) rest.notify_all();
    }
}

} // namespace rhythmer
