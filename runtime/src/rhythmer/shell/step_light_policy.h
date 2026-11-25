// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
namespace rhythmer {
// On a monochrome theme, colour cannot distinguish a filled step from the
// playhead. Reserve full brightness for the moving/held step. Keep the native
// coloured-theme velocity grammar, pattern timing and MIDI unchanged.
inline bool monochrome_step_colour(uint32_t colour) {
    return ((colour >> 16) & 255) == ((colour >> 8) & 255) && ((colour >> 8) & 255) == (colour & 255);
}
inline float step_rest_brightness(uint32_t colour, unsigned velocity) {
    if (!velocity) return .25f;
    if (monochrome_step_colour(colour)) return velocity >= 120 ? .65f : velocity >= 90 ? .4f : .25f;
    return velocity >= 120 ? 1.f : velocity >= 90 ? .65f : .4f;
}
}
