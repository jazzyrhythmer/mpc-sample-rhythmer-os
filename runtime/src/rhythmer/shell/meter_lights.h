// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The level meters right of the screen (surface/leds.h: five LEDs a strip,
// green, green, green, yellow, red from the bottom): a level as the LEDs lit,
// as the stock MPC Sample lights them (AC50LedVuMeter in MPC Sample 1.3.0):
// the same steps for the mix and the input, red only at full scale, and the
// levels read every 50 ms. The engine's levels rise at once and fall 60 dB a
// second (EngineHost::out_level), as stock's do; no LED is held.
#pragma once
#include "surface/leds.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace rhythmer {

using MeterSteps = std::array<float, surface_meter_segments>;
// dBFS above which each LED lights, bottom to top (stock's -24.1, -12.1, -6.1 and -3.1, and the red past -0.1).
constexpr MeterSteps meter_steps_db {-24.1f, -12.1f, -6.1f, -3.1f, -0.1f};
// How often the LEDs read the levels, ms: stock's timer.
constexpr uint32_t meter_read_ms = 50;

// The strip's LEDs for `level` (linear, 1 = full scale): a bar up to it.
inline uint8_t meter_lit(float level, const MeterSteps& steps = meter_steps_db) {
    if (!(level > 0)) return 0;
    float db = 20 * std::log10(level);
    uint8_t bits = 0;
    for (unsigned n = 0; n < surface_meter_segments && db > steps[n]; ++n) bits |= uint8_t(1u << n);
    return bits;
}

// The LEDs as last read: a read every meter_read_ms, and at once when the source changes (mix to input or back).
struct MeterReads {
    std::array<uint8_t, surface_meter_lights> lit {};
    // `level(side)` gives side 0 (left) or 1 (right) of the source, linear.
    template <class Level> const std::array<uint8_t, surface_meter_lights>& at(uint32_t now, bool input, Level&& level) {
        if (!read || input != from_input || int32_t(now - next) >= 0) {
            for (unsigned side = 0; side < surface_meter_lights; ++side) lit[side] = meter_lit(level(side));
            read = true; from_input = input; next = now + meter_read_ms;
        }
        return lit;
    }

private:
    uint32_t next = 0;
    bool read = false, from_input = false;
};

} // namespace rhythmer
