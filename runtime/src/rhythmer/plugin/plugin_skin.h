// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/plugin/page_file.h"
#include <juce_graphics/juce_graphics.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// A plug-in's skin as loaded (docs/PLUGIN_PAGES.md, Skins; plugin/page_file.h SkinSpec): its images decoded, its font
// made, its controls bound to parameter indices. Loaded on the rack's thread at the plug-in's load and never changed
// after, so the screen draws it from the panel's shared pointer (plugin/plugin_panel.h) with no lock.
namespace rhythmer {

// An effect's skin strip in a track's FX chain: everything under the chain's slots, 320 pixels wide.
constexpr int skin_strip_rows = 144;

struct PluginSkin {
    struct Control {
        SkinKind kind = SkinKind::Image;
        int param = -1;                 // its parameter's index (knob, switch, slider, value), -1 none
        juce::Image image, cap;         // the filmstrip or picture; a slider's cap
        unsigned frames = 1;
        juce::Rectangle<int> box;       // in the area
        juce::String text;
        std::string of;                 // a meter's "in" / "out", a curve's "filter" / "envelope" / "eq"
        juce::Colour colour;
        bool coloured = false;
        int text_size = 11, align = 0;
        bool horizontal = false;
    };
    struct Page {
        juce::Image background;
        std::vector<Control> controls;
    };
    juce::Typeface::Ptr font;           // null: Rhythmer's
    std::vector<std::pair<std::string, Page>> pages; // by page name: the area under B1-B3
    bool has_strip = false;
    Page strip;                         // an effect's strip under the chain row, to the bottom (skin_strip_rows)
    size_t bytes = 0;                   // its images, decoded

    const Page* page(const std::string& name) const {
        for (const auto& [n, p] : pages) if (n == name) return &p;
        return nullptr;
    }
};

constexpr size_t skin_most_bytes = size_t(4) << 20; // a skin's images together, decoded (RGBA)

// `spec`'s images and font from `folder` (the page file's), its controls bound among `shown` by `names` and `ids`.
// Null when it has nothing to draw, or its images come to more than skin_most_bytes; what is missing or too big goes
// to `problems` a line each (an image not found, a parameter it lacks: that control is left out). Rack's thread.
std::shared_ptr<const PluginSkin> load_skin(const SkinSpec& spec, const std::filesystem::path& folder, const std::vector<int>& shown,
                                            const std::vector<std::string>& names, const std::vector<std::string>& ids,
                                            std::vector<std::string>& problems);

} // namespace rhythmer
