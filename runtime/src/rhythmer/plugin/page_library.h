// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <array>

// Rhythmer's own page files (docs/PLUGIN_PAGES.md), for plug-ins whose makers ship none: the rack reads them after
// the card's Rhythmer/Plugin Pages folder and the plug-in's own package (plugin/plugin_rack.cpp).
namespace rhythmer {

inline constexpr std::array<const char*, 1> page_library {{
    // Neural Amp Modeler, the official plug-in built for the MPC (plugin/nam.h): Model, Cab and Output first, the
    // tone stack next, its switches with shorter names, the rest after in its own order.
    R"json({"melodee-pages": 1, "plugin": {"name": "NeuralAmpModeler"},
      "pages": [
        {"name": "AMP", "params": ["Model", "Cab", "Output"]},
        {"name": "TONE", "params": ["Bass", "Middle", "Treble"]},
        {"name": "INPUT", "params": ["Input", "Threshold", {"name": "NoiseGateActive", "label": "Gate"}]},
        {"name": "SWITCHES", "params": [{"name": "ToneStack", "label": "Tone"}, {"name": "IRToggle", "label": "Cab on"},
                                        {"name": "OutputMode", "label": "Out mode"}]},
        {"name": "MORE", "params": [{"name": "CalibrateInput", "label": "Calibrate"},
                                    {"name": "InputCalibrationLevel", "label": "Cal level"}], "rest": true}]})json",
}};

} // namespace rhythmer
