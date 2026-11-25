// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/engine/arp.h"
#include <algorithm>
#include <array>
#include <cstdint>

// Note Repeat, as on the MPC: every pad held plays again on each step of the
// rate, together, each as hard as it was struck (or, with Settings > PADS >
// Repeat velocity on Pressure, as hard as it is held). Apart from the arp: it
// shares only its clock (engine/arp.h's ticks and rates). Audio thread only;
// no allocation.
namespace rhythmer {

struct NoteRepeat {
    static constexpr unsigned gate = 60;   // percent of a step a repeat lasts
    bool on = false;
    unsigned rate = arp_default_rate;      // engine/arp.h
    bool pressure_velocity = false;        // a held pad's pressure is its repeats' velocity
    std::array<int, 16> keys {};           // as pressed: a kit pad's key with its semitones and 16 Levels cutoff (arp_named)
    std::array<int, 16> velocity {};
    std::array<float, 16> pressure {};     // each held key's pad pressure, 0..1
    unsigned count = 0;

    void press(int key, int vel) {
        for (unsigned n = 0; n < count; ++n) if (arp_named(keys[n]) == arp_named(key)) return;
        if (count == keys.size()) return;
        keys[count] = key; velocity[count] = vel; pressure[count] = 0; ++count;
    }
    void release(int key) {
        unsigned w = 0;
        for (unsigned n = 0; n < count; ++n)
            if (arp_named(keys[n]) != arp_named(key)) { keys[w] = keys[n]; velocity[w] = velocity[n]; pressure[w] = pressure[n]; ++w; }
        count = w;
    }
    void clear() { count = 0; }
    void press_harder(int key, float value) { for (unsigned n = 0; n < count; ++n) if (arp_named(keys[n]) == arp_named(key)) pressure[n] = value; }
    // How hard held key n plays on this step.
    int velocity_of(unsigned n) const {
        float p = pressure_velocity ? pressure[n] : 0.0f;
        return p > 0 ? std::clamp(int(p * 127.0f + 0.5f), 1, 127) : velocity[n];
    }
};

} // namespace rhythmer
