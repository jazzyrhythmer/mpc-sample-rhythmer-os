// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// A few threads for work that reads the card and converts audio (the sample
// library's loads): files are read and converted side by side, on the cores
// the audio leaves free, and the thread that asked keeps answering. Work is
// begun in the order posted. Below the screen's and the service thread's
// priority; never the audio thread's business.
namespace rhythmer {

class LoadPool {
public:
    explicit LoadPool(unsigned threads);
    ~LoadPool(); // what runs is waited for; what still waits is dropped
    LoadPool(const LoadPool&) = delete;
    LoadPool& operator=(const LoadPool&) = delete;
    void post(std::function<void()> work);
    unsigned pending() const;          // posted and not done yet
    bool wait_idle(unsigned most_ms);  // true once nothing waits or runs
private:
    void run();
    mutable std::mutex lock;
    std::condition_variable wake, rest;
    std::deque<std::function<void()>> waiting;
    unsigned running = 0;
    bool stopping = false;
    std::vector<std::thread> threads;
};

} // namespace rhythmer
