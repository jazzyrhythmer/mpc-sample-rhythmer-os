// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/ui/paint.h"
#include "rhythmer/ui/font_data.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace rhythmer::ui {
namespace {
unsigned face_in_use = 0; // Settings > SCREEN > Font (use_face())
bool tight = false;       // an outline face's letters closed up a little, for a string a hair too wide (Paint::narrow_for)

// One typeface per weight, cloned from a face's variable wght axis; each face
// loaded on first use.
const std::array<juce::Typeface::Ptr, 4>& typefaces() {
    // Never freed: JUCE's own glyph caches, torn down at exit after this, still point at the typefaces.
    static auto& loaded = *new std::array<std::array<juce::Typeface::Ptr, 4>, face_data.size()>;
    auto& out = loaded[face_in_use];
    if (!out[0]) {
        const auto& data = face_data[face_in_use];
        auto base = juce::Typeface::createSystemTypefaceFor(data.start, size_t(data.end - data.start));
        if (!base) throw std::runtime_error(std::string("Cannot load the embedded font ") + faces[face_in_use].name);
        const float weights[] {400, 500, 600, 700};
        for (size_t n = 0; n < out.size(); ++n) {
            juce::FontVariableSetting setting {"wght", weights[n]};
            auto face = base->cloneWithVariableSettings(juce::Span<const juce::FontVariableSetting>(&setting, 1));
            out[n] = face ? face : base;
        }
    }
    return out;
}

bool pixel_face() { return faces[face_in_use].em > 0; }

// A pixel face's size: the whole multiple of its grid whose capitals come
// nearest Rubik's (0.7 em) at `size`, never under 1x.
float pixel_size(float size) {
    const auto& f = faces[face_in_use];
    return std::max(1.0f, std::round(0.7f * size / f.cap)) * f.em;
}

juce::Font make_font(float size, Weight weight, bool caps) {
    // JUCE's kerning factor is a share of the font height (ascent + descent,
    // about 1.185 em for Rubik); capitals get type::caps_tracking. A pixel
    // face keeps its own one-pixel spacing, which tracking would smear.
    if (pixel_face()) return juce::Font(juce::FontOptions(typefaces()[size_t(weight)]).withPointHeight(pixel_size(size)));
    auto options = juce::FontOptions(typefaces()[size_t(weight)]).withPointHeight(size * faces[face_in_use].scale);
    float tracking = (caps ? type::caps_tracking : 0) - (tight ? 0.04f : 0);
    if (tracking != 0) options = options.withKerningFactor(tracking / 1.185f);
    return juce::Font(options);
}

// `value` with each character the face in use has no glyph for put as plain
// text it has (most faces lack the arrow; the pixel faces some signs).
juce::String shown(const juce::String& value) {
    bool plain = true;
    for (auto c : value) if (c > 0x7e) { plain = false; break; }
    if (plain) return value;
    const auto& face = typefaces()[0];
    juce::String out;
    for (auto c : value) {
        if (c <= 0x7e || face->getNominalGlyphForCodepoint(c)) { out << juce::String::charToString(c); continue; }
        switch (c) {
        case 0x2192: out << "->"; break;
        case 0x2265: out << ">="; break;
        case 0x2264: out << "<="; break;
        case 0x00BD: out << "1/2"; break;
        case 0x00BE: out << "3/4"; break;
        case 0x2212: case 0x2013: case 0x2014: out << "-"; break;
        case 0x00D7: out << "x"; break;
        case 0x203A: out << ">"; break;
        case 0x2039: out << "<"; break;
        case 0x00B7: out << "."; break;
        default: out << juce::String::charToString(c); break;
        }
    }
    return out;
}

// Text costs most of a frame: JUCE shapes each string with HarfBuzz, and its
// own layout cache compares whole Fonts (names, features, variations) on every
// lookup. These caches key on plain values instead. They hold fonts, string
// widths and justified layouts, are used from one thread at a time (the screen
// thread, or a host tool) and are dropped whole when they grow past `most`,
// since a page reuses the same few hundred strings frame after frame.
struct Style {
    int size_hundredths; Weight weight; bool caps; unsigned face; bool tight;
    bool operator==(const Style& o) const { return size_hundredths == o.size_hundredths && weight == o.weight && caps == o.caps && face == o.face && tight == o.tight; }
    size_t hash() const { return size_t(size_hundredths) * 31u + size_t(weight) * 7u + size_t(caps) + size_t(face) * 1009u + size_t(tight) * 7919u; }
};
Style style_of(float size, Weight weight, bool caps) { return {int(std::lround(size * 100)), weight, caps, face_in_use, tight}; }

struct FontHash { size_t operator()(const Style& s) const { return s.hash(); } };

struct WidthKey {
    juce::String text; Style style;
    bool operator==(const WidthKey& o) const { return style == o.style && text == o.text; }
};
struct WidthHash { size_t operator()(const WidthKey& k) const { return size_t(k.text.hashCode64()) ^ (k.style.hash() << 1); } };

struct LayoutKey {
    juce::String text; Style style; float w, h; int justify;
    bool operator==(const LayoutKey& o) const {
        return style == o.style && w == o.w && h == o.h && justify == o.justify && text == o.text;
    }
};
struct LayoutHash {
    size_t operator()(const LayoutKey& k) const {
        return size_t(k.text.hashCode64()) ^ (k.style.hash() << 1) ^ (std::hash<float>()(k.w) << 3)
             ^ (std::hash<float>()(k.h) << 5) ^ (size_t(k.justify) << 7);
    }
};

std::vector<TextIssue>* audit = nullptr;

template <typename Map> void bound(Map& map, size_t most) { if (map.size() >= most) map.clear(); }

const juce::Font& cached_font(float size, Weight weight, bool caps) {
    static std::unordered_map<Style, juce::Font, FontHash> fonts;
    auto key = style_of(size, weight, caps);
    auto found = fonts.find(key);
    if (found != fonts.end()) return found->second;
    bound(fonts, 256);
    return fonts.emplace(key, make_font(size, weight, caps)).first->second;
}

float cached_width(const juce::String& value, float size, Weight weight, bool caps) {
    static std::unordered_map<WidthKey, float, WidthHash> widths;
    WidthKey key {value, style_of(size, weight, caps)};
    auto found = widths.find(key);
    if (found != widths.end()) return found->second;
    bound(widths, 4096);
    float w = juce::GlyphArrangement::getStringWidth(cached_font(size, weight, caps), value);
    widths.emplace(std::move(key), w);
    return w;
}

// The layout juce::Graphics::drawText() builds, laid out once per string,
// style and box size.
const juce::GlyphArrangement& cached_layout(const juce::String& value, float size, Weight weight, bool caps,
                                            float w, float h, juce::Justification justify) {
    static std::unordered_map<LayoutKey, juce::GlyphArrangement, LayoutHash> layouts;
    LayoutKey key {value, style_of(size, weight, caps), w, h, justify.getFlags()};
    auto found = layouts.find(key);
    if (found != layouts.end()) return found->second;
    bound(layouts, 2048);
    juce::GlyphArrangement arrangement;
    arrangement.addCurtailedLineOfText(cached_font(size, weight, caps), value, 0, 0, w, false);
    arrangement.justifyGlyphs(0, arrangement.getNumGlyphs(), 0, 0, w, h, justify);
    return layouts.emplace(std::move(key), std::move(arrangement)).first->second;
}
} // namespace

void use_face(unsigned face) { face_in_use = face % face_choices; }
unsigned face_count() { return face_choices; }
const char* face_name(unsigned face) { return faces[face % faces.size()].name; }

juce::Font font(float size, Weight weight, bool caps) { return cached_font(std::max(size, type::floor), weight, caps); }

void audit_text(std::vector<TextIssue>* sink) { audit = sink; }
namespace { ControlsSeen* seen_controls = nullptr; }
void audit_controls(ControlsSeen* sink) { seen_controls = sink; }
ControlsSeen* controls_sink() { return seen_controls; }
void report_text_issue(const TextIssue& issue) { if (audit) audit->push_back(issue); }

float Paint::width(const juce::String& asked, const Text& style) const {
    auto value = shown(asked);
    return cached_width(value, std::max(style.size, type::floor), style.weight, style.caps);
}

// When `value` is wider than `w` in the face in use: a pixel face's narrow
// companion (it cannot shrink between its multiples), else an outline face
// other than Rubik a little tighter. Undone by the caller (restore_face).
unsigned Paint::narrow_for(const juce::String& value, const Text& style, float w) const {
    unsigned was = face_in_use;
    tight = false;
    if (width(value, style) <= w + 0.5f) return was;
    int narrow = faces[face_in_use].narrow;
    if (narrow >= 0) face_in_use = unsigned(narrow);
    else if (face_in_use != 0 && faces[face_in_use].em == 0) tight = true;
    return was;
}

Paint::Text Paint::fit(const juce::String& value, Text style, float w, float smallest) const {
    smallest = std::max(smallest, type::floor);
    style.size = std::max(style.size, type::floor);
    while (style.size > smallest && width(value, style) > w) style.size = std::max(smallest, style.size - 0.5f);
    return style;
}

juce::StringArray Paint::wrap(const juce::String& value, Text& style, float w, int most) const {
    juce::StringArray words;
    words.addTokens(value, " ", "");
    words.removeEmptyStrings();
    style.size = std::max(style.size, type::floor);
    for (;;) {
        juce::StringArray lines;
        for (const auto& word : words) {
            if (lines.isEmpty()) { lines.add(word); continue; }
            auto joined = lines[lines.size() - 1] + " " + word;
            if (width(joined, style) <= w || lines.size() >= most) lines.set(lines.size() - 1, joined);
            else lines.add(word);
        }
        bool fits = true;
        for (const auto& line : lines) fits = fits && width(line, style) <= w;
        if (fits || style.size <= type::floor) return lines;
        style.size = std::max(type::floor, style.size - 0.5f);
    }
}

float Paint::text(const juce::String& original, float x, float y, float w, float h, const Text& asked,
                  juce::Justification justify) {
    auto value = shown(original);
    Text style = asked;
    style.size = std::max(style.size, type::floor);
    struct Back { unsigned face; ~Back() { face_in_use = face; tight = false; } } back {narrow_for(value, style, w)};
    if (audit && value.isNotEmpty()) {
        float needed = width(value, style);
        // Under the floor, wider than its box, or an ellipsis standing in for what does not fit: all fail the check.
        bool ellipsis = value.contains(juce::String::fromUTF8("\xe2\x80\xa6")) || value.contains("...");
        if (asked.size < type::floor || needed > w + 0.5f || ellipsis) audit->push_back({value, asked.size, needed, w});
    }
    // Colour and font stay set on g, as drawText() callers expect.
    g.setColour(style.colour);
    g.setFont(cached_font(style.size, style.weight, style.caps));
    if (value.isEmpty()) return 0;
    juce::Rectangle<float> area(x, y, w, h);
    if (g.clipRegionIntersects(area.getSmallestIntegerContainer()))
    {
        const auto& layout = cached_layout(value, style.size, style.weight, style.caps, w, h, justify);
        if (pixel_face() && layout.getNumGlyphs() > 0) { // the first glyph's corner and baseline on whole pixels, so every square is sharp
            const auto& first = layout.getGlyph(0);
            float gx = x + first.getLeft(), gy = y + first.getBaselineY();
            x += std::round(gx) - gx;
            y += std::round(gy) - gy;
        }
        layout.draw(g, juce::AffineTransform::translation(x, y));
    }
    return width(value, style);
}

void Paint::fill(float x, float y, float w, float h, juce::Colour c, float radius) {
    g.setColour(c);
    (void)radius;
    g.fillRect(juce::Rectangle<float>(std::round(x), std::round(y), std::round(w), std::round(h)));
}

void Paint::ring(float x, float y, float w, float h, juce::Colour c, float radius, float thickness) {
    g.setColour(c);
    float half = thickness / 2;
    (void)radius;
    g.drawRect(juce::Rectangle<float>(x + half, y + half, w - thickness, h - thickness), thickness);
}

void Paint::circle(float cx, float cy, float r, juce::Colour c) {
    g.setColour(c);
    if(r<=3) g.fillRect(juce::Rectangle<float>(std::round(cx-r),std::round(cy-r),2*r,2*r));
    else g.fillEllipse(cx-r,cy-r,2*r,2*r);
}

void Paint::dim_zone(int y, int h, float amount) { fill(0, float(y), float(zone::width), float(h), colour::bg.withAlpha(amount)); }

} // namespace rhythmer::ui
