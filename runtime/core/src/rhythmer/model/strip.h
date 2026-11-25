// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/limits.h"
#include <array>
#include <cstdint>

// The mixer's strip (rhythmer-sample docs/MIXER_PLAN.md): the same fixed processing on every channel of the mix
// (tracks, submixes, buses, the input and the master), after the channel's effect chain: trim, a nine-band EQ, a
// compressor and width. As it comes, a strip changes nothing, so a project from before it plays as it did.
namespace rhythmer {

constexpr unsigned eq_bands = 9;
// The bands' centres in Hz, an octave apart.
constexpr std::array<uint16_t, eq_bands> eq_band_hz {63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
constexpr int eq_range = 120;       // a band's gain, tenths of a dB: -12..12 dB
constexpr int trim_range = 240;     // tenths of a dB: -24..24 dB
constexpr unsigned width_max = 200; // percent: 0 mono, 100 as it is, 200 twice as wide

// The compressor's attack and release, as one choice.
enum class CompSpeed : uint8_t { Fast, Medium, Slow, Auto, Count };
constexpr unsigned comp_amount_max = 48;                  // dB below full scale
constexpr unsigned comp_ratio_min = 15, comp_ratio_max = 200; // tenths: 1.5:1 .. 20:1
constexpr int comp_gain_min = -120, comp_gain_max = 240;  // tenths of a dB

struct Strip {
    int16_t trim = 0;                    // tenths of a dB, before the bands
    uint8_t width = 100;                 // percent
    bool eq_on = true;                   // off: the bands are skipped, their gains kept
    std::array<int16_t, eq_bands> eq {}; // each band's gain, tenths of a dB
    bool comp_on = true;                 // off: the compressor is skipped, its values kept
    uint8_t comp_amount = 0;             // where it starts, dB below full scale; 0: it passes the sound as it is
    uint8_t comp_ratio = 40;             // tenths
    uint8_t comp_speed = uint8_t(CompSpeed::Medium);
    uint8_t comp_key = 0;                // what it hears: 0 its own channel, else fx_key_track(t), fx_key_bus(b) or fx_key_sub(s) (project.h)
    uint8_t comp_mix = 100;              // percent of the compressed sound against the dry
    int16_t comp_gain = 0;               // after it, tenths of a dB

    bool eq_flat() const { for (auto g : eq) if (g) return false; return true; }
    bool comp_idle() const { return comp_amount == 0; }
    // As it comes: the file leaves it out.
    bool flat() const {
        const Strip none;
        return trim == none.trim && width == none.width && eq_on == none.eq_on && eq == none.eq && comp_on == none.comp_on
            && comp_amount == none.comp_amount && comp_ratio == none.comp_ratio && comp_speed == none.comp_speed
            && comp_key == none.comp_key && comp_mix == none.comp_mix && comp_gain == none.comp_gain;
    }
};

// A strip's values one at a time, as a command, a knob or (later) a Motion lane sets them.
enum class StripPart : uint8_t {
    Trim, Width, EqOn,
    Eq0, Eq1, Eq2, Eq3, Eq4, Eq5, Eq6, Eq7, Eq8, // the bands, 63 Hz .. 16 kHz
    CompOn, Amount, Ratio, Speed, Key, Mix, Gain,
    Count
};
constexpr StripPart eq_part(unsigned band) { return StripPart(unsigned(StripPart::Eq0) + band % eq_bands); }

inline int strip_get(const Strip& s, StripPart part) {
    const unsigned p = unsigned(part);
    if (p >= unsigned(StripPart::Eq0) && p <= unsigned(StripPart::Eq8)) return s.eq[p - unsigned(StripPart::Eq0)];
    switch (part) {
    case StripPart::Trim: return s.trim;
    case StripPart::Width: return s.width;
    case StripPart::EqOn: return s.eq_on;
    case StripPart::CompOn: return s.comp_on;
    case StripPart::Amount: return s.comp_amount;
    case StripPart::Ratio: return s.comp_ratio;
    case StripPart::Speed: return s.comp_speed;
    case StripPart::Key: return s.comp_key;
    case StripPart::Mix: return s.comp_mix;
    case StripPart::Gain: return s.comp_gain;
    default: return 0;
    }
}

constexpr int strip_key_most = 28; // the compressor's key: 0 its own channel .. fx_key_count - 1 (model/project.h)
// `value` into its range first.
inline void strip_set(Strip& s, StripPart part, int value) {
    auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    const unsigned p = unsigned(part);
    if (p >= unsigned(StripPart::Eq0) && p <= unsigned(StripPart::Eq8)) { s.eq[p - unsigned(StripPart::Eq0)] = int16_t(clamp(value, -eq_range, eq_range)); return; }
    switch (part) {
    case StripPart::Trim: s.trim = int16_t(clamp(value, -trim_range, trim_range)); break;
    case StripPart::Width: s.width = uint8_t(clamp(value, 0, int(width_max))); break;
    case StripPart::EqOn: s.eq_on = value != 0; break;
    case StripPart::CompOn: s.comp_on = value != 0; break;
    case StripPart::Amount: s.comp_amount = uint8_t(clamp(value, 0, int(comp_amount_max))); break;
    case StripPart::Ratio: s.comp_ratio = uint8_t(clamp(value, int(comp_ratio_min), int(comp_ratio_max))); break;
    case StripPart::Speed: s.comp_speed = uint8_t(clamp(value, 0, int(CompSpeed::Count) - 1)); break;
    case StripPart::Key: s.comp_key = uint8_t(clamp(value, 0, strip_key_most)); break;
    case StripPart::Mix: s.comp_mix = uint8_t(clamp(value, 0, 100)); break;
    case StripPart::Gain: s.comp_gain = int16_t(clamp(value, comp_gain_min, comp_gain_max)); break;
    default: break;
    }
}

// Motion (docs/MIXER_PLAN.md core step 6; decision 7: tracks' strips only): a lane for each value of a track's strip
// but its two switches and its key, after the mix's lanes (model/limits.h).
constexpr StripPart strip_lane_parts[] {StripPart::Trim, StripPart::Width, StripPart::Eq0, StripPart::Eq1, StripPart::Eq2,
                                        StripPart::Eq3, StripPart::Eq4, StripPart::Eq5, StripPart::Eq6, StripPart::Eq7,
                                        StripPart::Eq8, StripPart::Amount, StripPart::Ratio, StripPart::Speed, StripPart::Mix,
                                        StripPart::Gain};
constexpr unsigned motion_strip_base = motion_mix_end;
constexpr unsigned motion_strip_count = sizeof(strip_lane_parts) / sizeof(strip_lane_parts[0]);
static_assert(motion_strip_base + motion_strip_count <= motion_bend, "the strip's lanes fit below the pitch bend's");
constexpr bool is_strip_lane(unsigned param) { return param >= motion_strip_base && param < motion_strip_base + motion_strip_count; }
constexpr StripPart strip_lane_part(unsigned param) { return strip_lane_parts[(param - motion_strip_base) % motion_strip_count]; }
// The lane of `part`, or -1 (a switch, the key).
constexpr int motion_strip(StripPart part) {
    for (unsigned n = 0; n < motion_strip_count; ++n) if (strip_lane_parts[n] == part) return int(motion_strip_base + n);
    return -1;
}

} // namespace rhythmer
