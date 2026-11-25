// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "drivers/usb_audio.h"
#include "rhythmer/model/input_config.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>

struct Hardware;

// The hardware behind Input Config's source (parity phase 6), and the
// Looper's loops that take a hardware input of their own, on the service
// thread: the ADC route (the mic or the rear line inputs, never both; a loop's
// pick wins over Input Config's), USB-C audio from a computer (opened only
// while a USB source is chosen, by Input Config or a loop; nothing streams
// until the computer plays) and the built-in
// speaker, which plays while neither headphones nor line out is plugged in
// and, as on the MPC, goes quiet while the mic is monitored. The source is
// always routed, so Audio Recall's ring always hears it.
//
// The MIDI thread says what it wants (want()); the audio thread takes the
// USB-C block (usb_block()); the service thread does the rest (service()).
namespace rhythmer {

class InputRoute {
public:
    InputRoute() = default;
    InputRoute(const InputRoute&) = delete;
    InputRoute& operator=(const InputRoute&) = delete;

    // What the loops' own hardware inputs need (loop_needs()).
    static constexpr unsigned need_line = 1, need_mic = 2, need_usb = 4;
    // The ADC's route: the rear inputs when a loop or MIDI track needs them, else the mic when one does, else what
    // the source needs ("mic", "line" or "off").
    static const char* adc_route(InputSource source, unsigned needs) {
        return needs & need_line ? "line" : needs & need_mic ? "mic" : input_route(source);
    }
    // Whether the source is on the ADC side the route does not take (the mic while a loop or MIDI track has the rear
    // inputs, or the other way): it is heard as silence and shown IN USE.
    static bool adc_taken(InputSource source, unsigned needs) {
        if (source == InputSource::Mic) return (needs & need_line) != 0;
        return from_rear(source) && !(needs & need_line) && (needs & need_mic);
    }
    // MIDI thread: the source, whether monitoring should quiet the speaker, and what the loops need.
    void want(InputSource source, bool mute_speaker, unsigned loops = 0) {
        wanted.store(uint16_t(unsigned(source) | (mute_speaker ? 0x80u : 0u) | (loops & 7u) << 8), std::memory_order_relaxed);
    }
    // Service thread, every pass (and on a jack change): routes, USB-C, the speaker.
    void service(Hardware& hardware, bool plugged);
    // Audio thread: this block from USB-C (interleaved stereo, `frames` <= 128),
    // or nullptr while no USB source is chosen.
    const float* usb_block(unsigned frames);
    // Audio thread: the device's rate, which USB-C in is converted to.
    void set_device_rate(unsigned rate) { usb.set_device_rate(rate); rate_now.store(rate, std::memory_order_relaxed); }
    // The round trip of a computer's instrument played from here: the MIDI thread marks each Note On played by hand
    // as it is sent (CLOCK_MONOTONIC ns); usb_block() takes the first frame of USB-C in 18 dB over the 20 ms before,
    // within half a second of one, as its sound coming back. loop_json(): the last, average, least and most, in us, to
    // the frame the engine takes it at (the output's latency comes on top), and the level before (dBFS).
    void note_sent(int64_t ns) { sent_ns.store(ns, std::memory_order_relaxed); }
    std::string loop_json() const;

    // Any thread.
    bool speaker_on() const { return speaker.load(std::memory_order_relaxed) == 1; }
    bool speaker_quieted() const { return quieted.load(std::memory_order_relaxed); } // by the monitored mic
    bool jack_out() const { return plugged_now.load(std::memory_order_relaxed); }   // headphones or line out in
    UsbStreamState usb_state() const { return usb.state(); }
    std::string usb_json() const { return usb.json(); } // USB-C in's counters: locked or trimmed, pitch, fill

private:
    UsbAudioInput usb;
    std::atomic<uint16_t> wanted {uint16_t(InputSource::Rear)};
    std::atomic<bool> usb_live {false}, quieted {false}, plugged_now {false};
    std::atomic<int> speaker {-1}; // as last set: 1 on, 0 off, -1 not touched
    int routed = -1;               // the InputSource and loop needs routed, -1 none yet
    float block[2 * 128] {};
    std::atomic<int64_t> sent_ns {0};
    std::atomic<unsigned> rate_now {48000};
    int64_t loop_from = 0;              // audio thread: the Note On last matched
    std::array<float, 8> recent {};     // audio thread: the last blocks' peaks
    unsigned recent_at = 0;
    std::atomic<float> floor_now {0};   // the loudest of them, for loop_json()
    std::atomic<unsigned> loops {0}, loop_last {0}, loop_least {0}, loop_most {0};
    std::atomic<uint64_t> loop_sum {0};
};

} // namespace rhythmer
