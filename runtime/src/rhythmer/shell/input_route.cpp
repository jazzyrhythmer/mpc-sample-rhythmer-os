// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/input_route.h"
#include "drivers/mixer.h"
#include "runtime/app.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace rhythmer {

void InputRoute::service(Hardware& hardware, bool plugged) {
    uint16_t want_now = wanted.load(std::memory_order_relaxed);
    auto source = InputSource((want_now & 0x7f) % unsigned(InputSource::Count));
    bool mute = want_now & 0x80;
    unsigned loops = want_now >> 8 & 7u;
    int key = int(source) | int(loops) << 8;
    if (key != routed) {
        bool usb_wanted = from_usb(source) || (loops & need_usb);
        // A loop or MIDI track on the rear inputs or the mic routes the ADC to it (the rear inputs first).
        const char* adc = adc_route(source, loops);
        // USB-C first off (the audio thread stops taking blocks), then the ADC route, then USB-C on.
        if (!usb_wanted) { usb_live.store(false, std::memory_order_release); usb.stop(); }
        if (hardware.mixer.available() && !hardware.mixer.input(adc)) hardware.mixer.input("off");
        if (usb_wanted) {
            if (usb.state() == UsbStreamState::Stopped) usb.start("auto");
            usb_live.store(true, std::memory_order_release);
        }
        routed = key;
    }
    // The speaker: off with headphones or line out in, and while the mic is heard through it.
    int on = !plugged && !mute ? 1 : 0;
    plugged_now.store(plugged, std::memory_order_relaxed);
    quieted.store(!plugged && mute, std::memory_order_relaxed);
    if (on != speaker.load(std::memory_order_relaxed) && hardware.mixer.available() && hardware.mixer.speaker(on != 0))
        speaker.store(on, std::memory_order_relaxed);
}

const float* InputRoute::usb_block(unsigned frames) {
    if (!usb_live.load(std::memory_order_acquire)) return nullptr;
    frames = std::min(frames, 128u);
    if (!usb.pull(block, frames)) std::fill(block, block + 2 * frames, 0.0f); // silent until enough is buffered
    // The loop probe: the first frame 18 dB over the loudest of the 20 ms before (and over -46 dB).
    float before = 0;
    for (float p : recent) before = std::max(before, p);
    const float onset = std::max(0.005f, before * 8);
    unsigned first = frames;
    float peak = 0;
    for (unsigned f = 0; f < frames; ++f) {
        float a = std::max(std::fabs(block[2 * f]), std::fabs(block[2 * f + 1]));
        if (a > onset && first == frames) first = f;
        peak = std::max(peak, a);
    }
    recent[recent_at++ % recent.size()] = peak;
    floor_now.store(before, std::memory_order_relaxed);
    if (first < frames) {
        int64_t sent = sent_ns.load(std::memory_order_relaxed);
        int64_t now = int64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        int64_t at = now + int64_t(double(first) * 1e9 / std::max(1u, rate_now.load(std::memory_order_relaxed)));
        if (sent && sent != loop_from && at > sent && at - sent < 500000000) {
            loop_from = sent;
            unsigned us = unsigned((at - sent) / 1000);
            unsigned n = loops.load(std::memory_order_relaxed);
            loop_last.store(us, std::memory_order_relaxed);
            loop_sum.fetch_add(us, std::memory_order_relaxed);
            if (!n || us < loop_least.load(std::memory_order_relaxed)) loop_least.store(us, std::memory_order_relaxed);
            if (us > loop_most.load(std::memory_order_relaxed)) loop_most.store(us, std::memory_order_relaxed);
            loops.store(n + 1, std::memory_order_relaxed);
        }
    }
    return block;
}

std::string InputRoute::loop_json() const {
    unsigned n = loops.load(std::memory_order_relaxed);
    return "{\"loops\":" + std::to_string(n) + ",\"last_us\":" + std::to_string(loop_last.load(std::memory_order_relaxed)) +
           ",\"avg_us\":" + std::to_string(n ? loop_sum.load(std::memory_order_relaxed) / n : 0) +
           ",\"least_us\":" + std::to_string(loop_least.load(std::memory_order_relaxed)) +
           ",\"most_us\":" + std::to_string(loop_most.load(std::memory_order_relaxed)) +
           ",\"floor_db\":" + std::to_string(int(std::lround(20 * std::log10(std::max(1e-6f, floor_now.load(std::memory_order_relaxed)))))) + "}";
}

} // namespace rhythmer
