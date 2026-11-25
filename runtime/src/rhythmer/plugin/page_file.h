// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/plugin/drum_map.h"
#include "rhythmer/plugin/plugin_pages.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A page file (docs/PLUGIN_PAGES.md): JSON naming a plug-in and the pages Rhythmer shows for it, from the card's
// Rhythmer/Plugin Pages folder, the plug-in's own package or Rhythmer's library (plugin/page_library.h). The rack reads
// it at load (plugin/plugin_rack.cpp); a file that does not read or match is passed over for the next source.
// Plain C++, no JUCE: the host tests read it too.
namespace rhythmer {

// A parameter as a file names it: by its name as the plug-in gives it, or by the format's own ID (JUCE's
// getParameterID(): the CLAP param id, VST3 ParamID, VST2 index, LV2 symbol); a label for its card where the
// plug-in's name is long.
struct PageFileParam {
    std::string name, id, label;
};
struct PageFilePage {
    std::string name;
    std::vector<PageFileParam> params;
    bool rest = false;              // then every parameter no page of the file names, in the plug-in's order
    PagePicture picture = PagePicture::None;
    std::vector<PageFileParam> parts; // the picture's parameters in the kind's order (plugin_pages.h PagePictureSlots)
};
// A skin (docs/PLUGIN_PAGES.md, Skins): the maker's art for its pages, drawn by Rhythmer from images and data, no
// plug-in code. A page's controls in a box of the area (pixels, the area's top left 0, 0): a knob or switch is a
// filmstrip (frames stacked top to bottom, the frame picked by the value), a slider a track image and a cap image, a
// meter a filmstrip for a measured level ("of": "in" or "out"), a value its parameter's text, a label text, an image a
// picture, a curve the page's picture ("of": "filter", "envelope", "eq") and a scope the plug-in's sound, in the
// maker's colour. Images are PNG files, paths relative to the page file's folder.
enum class SkinKind : uint8_t { Image, Knob, Switch, Slider, Meter, Value, Label, Curve, Scope };
struct SkinControlSpec {
    SkinKind kind = SkinKind::Image;
    PageFileParam param;            // knob, switch, slider, value
    std::string image, cap;         // the filmstrip or picture; a slider's cap
    unsigned frames = 1;            // a filmstrip's frames
    int x = 0, y = 0, w = 0, h = 0; // its box; 0: the image's (a filmstrip's frame's) size
    std::string text, of;           // a label's text; what a meter, curve shows
    uint32_t colour = 0;            // 0xRRGGBB; `coloured` false: Rhythmer's
    bool coloured = false;
    int text_size = 11;             // a label's or value's text, 11 px and up
    int align = 0;                  // a label's or value's: -1 left, 0 centre, 1 right
    bool horizontal = false;        // a slider across, not up
};
struct SkinPageSpec {
    std::string background;         // a PNG the area's size
    std::vector<SkinControlSpec> controls;
};
struct SkinSpec {
    bool present = false;
    std::string font;               // a TTF or OTF for its labels and values ("" Rhythmer's)
    std::vector<std::pair<std::string, SkinPageSpec>> pages; // by page name, the area under B1-B3 (320 x 218)
    bool has_strip = false;
    SkinPageSpec strip;             // an effect's strip under the chain row (320 x 92)
};

struct PageFile {
    // Which plug-in: any of its CLAP id, VST2 unique ID (four letters, or a number), LV2 URI, or its name as Rhythmer
    // shows it (with its vendor, where given). VST3 plug-ins go by name.
    std::string clap, vst2, lv2, name, vendor;
    std::vector<PageFilePage> pages;
    SkinSpec skin;
};
// What the rack knows of a loaded plug-in, to match a file against.
struct PluginIdentity {
    std::string clap, lv2, name, vendor;
    int32_t vst2 = 0;               // its unique ID; 0 none
};

namespace page_file_detail {
using drum_map_detail::Reader;
// A parameter: "Name", or {"name": "Name", "label": "SHORT"}, or {"id": "1234"}.
inline bool param(Reader& r, PageFileParam& out) {
    if (r.peek('"')) return r.string(&out.name);
    if (!r.peek('{')) return false;
    return r.object([&](const std::string& key) {
        if ((key == "name" || key == "label" || key == "id") && r.peek('"'))
            return r.string(key == "name" ? &out.name : key == "label" ? &out.label : &out.id);
        if (key == "id") { long v = 0; if (!r.number(&v)) return false; out.id = std::to_string(v); return true; }
        return r.skip();
    });
}
inline bool hex_colour(Reader& r, uint32_t& out) {
    std::string text;
    if (!r.peek('"') || !r.string(&text)) return false;
    if (!text.empty() && text[0] == '#') text.erase(0, 1);
    if (text.size() != 6) return false;
    out = 0;
    for (char c : text) {
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0) return false;
        out = out << 4 | uint32_t(v);
    }
    return true;
}
template <class Each> bool array(Reader& r, Each each);
// One skin page: {"background": "a.png", "controls": [...]}. A control's "label" object is a label of its own.
inline bool skin_page(Reader& r, SkinPageSpec& page);
template <class Each> bool array(Reader& r, Each each) {
    if (!r.peek('[')) return r.skip();
    if (++r.depth > 32) return false;
    r.eat('[');
    if (!r.eat(']')) {
        do { if (!each()) return false; } while (r.eat(','));
        if (!r.eat(']')) return false;
    }
    --r.depth;
    return true;
}
inline bool skin_control(Reader& r, std::vector<SkinControlSpec>& out) {
    SkinControlSpec c;
    std::vector<SkinControlSpec> labels;
    std::string kind;
    long n = 0;
    bool sized = false;
    long size = 0;
    bool ok = r.object([&](const std::string& key) {
        if (key == "kind" && r.peek('"')) return r.string(&kind);
        if (key == "param") return param(r, c.param) || r.skip();
        if ((key == "image" || key == "strip") && r.peek('"')) return r.string(&c.image);
        if (key == "cap" && r.peek('"')) return r.string(&c.cap);
        if ((key == "text" || key == "of") && r.peek('"')) return r.string(key == "text" ? &c.text : &c.of);
        if (key == "colour" || key == "color") { if (hex_colour(r, c.colour)) { c.coloured = true; return true; } return r.skip(); }
        if (key == "align" && r.peek('"')) {
            std::string a;
            if (!r.string(&a)) return false;
            c.align = a == "left" ? -1 : a == "right" ? 1 : 0;
            return true;
        }
        if (key == "orientation" && r.peek('"')) { std::string o; if (!r.string(&o)) return false; c.horizontal = o == "horizontal"; return true; }
        if (key == "label" && r.peek('{')) {
            SkinControlSpec label;
            label.kind = SkinKind::Label;
            long v = 0;
            bool read = r.object([&](const std::string& k) {
                if (k == "text" && r.peek('"')) return r.string(&label.text);
                if ((k == "colour" || k == "color")) { if (hex_colour(r, label.colour)) { label.coloured = true; return true; } return r.skip(); }
                if (k == "x" && r.number(&v)) { label.x = int(v); return true; }
                if (k == "y" && r.number(&v)) { label.y = int(v); return true; }
                if (k == "w" && r.number(&v)) { label.w = int(v); return true; }
                if (k == "size" && r.number(&v)) { label.text_size = int(std::max(11L, v)); return true; }
                if (k == "align" && r.peek('"')) { std::string a; if (!r.string(&a)) return false; label.align = a == "left" ? -1 : a == "right" ? 1 : 0; return true; }
                return r.skip();
            });
            if (read) labels.push_back(label);
            return read;
        }
        if (key == "frames" && r.number(&n)) { c.frames = unsigned(std::clamp(n, 1L, 512L)); return true; }
        if (key == "size" && r.number(&size)) { sized = true; return true; }
        if ((key == "x" || key == "y" || key == "w" || key == "h") && r.number(&n)) {
            (key == "x" ? c.x : key == "y" ? c.y : key == "w" ? c.w : c.h) = int(std::clamp(n, -2000L, 2000L));
            return true;
        }
        return r.skip();
    });
    if (!ok) return false;
    static const std::pair<const char*, SkinKind> kinds[] {{"image", SkinKind::Image}, {"knob", SkinKind::Knob}, {"switch", SkinKind::Switch},
        {"slider", SkinKind::Slider}, {"meter", SkinKind::Meter}, {"value", SkinKind::Value}, {"label", SkinKind::Label},
        {"curve", SkinKind::Curve}, {"scope", SkinKind::Scope}};
    bool known = false;
    for (const auto& [name, k] : kinds) if (kind == name) { c.kind = k; known = true; }
    if (sized) { // a knob's square; a label's or value's text
        if (c.kind == SkinKind::Value || c.kind == SkinKind::Label) c.text_size = int(std::max(11L, size));
        else c.w = c.h = int(std::clamp(size, 1L, 2000L));
    }
    if (known) out.push_back(c); // an unknown kind (a later version's, "canvas"): skipped
    for (auto& label : labels) out.push_back(label);
    return true;
}
inline bool skin_page(Reader& r, SkinPageSpec& page) {
    return r.object([&](const std::string& key) {
        if (key == "background" && r.peek('"')) return r.string(&page.background);
        if (key == "controls") return array(r, [&] { return r.peek('{') ? skin_control(r, page.controls) : r.skip(); });
        return r.skip();
    });
}
inline bool skin(Reader& r, SkinSpec& out) {
    out.present = true;
    return r.object([&](const std::string& key) {
        if (key == "font" && r.peek('"')) return r.string(&out.font);
        if (key == "strip" && r.peek('{')) { out.has_strip = true; return skin_page(r, out.strip); }
        if (key != "pages" || !r.peek('{')) return r.skip();
        return r.object([&](const std::string& name) {
            if (!r.peek('{')) return r.skip();
            SkinPageSpec page;
            if (!skin_page(r, page)) return false;
            out.pages.push_back({name, std::move(page)});
            return true;
        });
    });
}
} // namespace page_file_detail

// Reads `json` into `out`; false with `why` (a few words) when it is not a page file Rhythmer can use.
inline bool read_page_file(std::string_view json, PageFile& out, std::string& why) {
    using namespace page_file_detail;
    out = {};
    Reader r {json};
    long version = 0;
    bool ok = r.object([&](const std::string& key) {
        if (key == "melodee-pages") return r.number(&version) || r.skip();
        if (key == "plugin") {
            return r.object([&](const std::string& k) {
                if (k == "vst2" && !r.peek('"')) { long v = 0; if (!r.number(&v)) return r.skip(); out.vst2 = std::to_string(v); return true; }
                std::string* field = k == "clap" ? &out.clap : k == "vst2" ? &out.vst2 : k == "lv2" ? &out.lv2
                                   : k == "name" ? &out.name : k == "vendor" ? &out.vendor : nullptr;
                return field && r.peek('"') ? r.string(field) : r.skip();
            });
        }
        if (key == "skin" && r.peek('{')) return skin(r, out.skin);
        if (key != "pages") return r.skip();
        return array(r, [&] {
            if (!r.peek('{')) return r.skip();
            PageFilePage page;
            bool read = r.object([&](const std::string& k) {
                if (k == "name" && r.peek('"')) return r.string(&page.name);
                if (k == "rest") { page.rest = r.word("true"); return page.rest || r.skip(); }
                if (k == "params")
                    return array(r, [&] { PageFileParam p; if (!param(r, p)) return r.skip(); page.params.push_back(p); return true; });
                if (k != "picture" || !r.peek('{')) return r.skip();
                std::string kind;
                std::vector<std::pair<std::string, PageFileParam>> named; // the picture's parts by their role
                std::vector<PageFileParam> listed;                          // value: "params"
                bool pic = r.object([&](const std::string& part) {
                    if (part == "kind" && r.peek('"')) return r.string(&kind);
                    if (part == "params")
                        return array(r, [&] { PageFileParam p; if (!param(r, p)) return r.skip(); listed.push_back(p); return true; });
                    if (part == "bands")
                        return array(r, [&] {
                            if (!r.peek('{')) return r.skip();
                            PageFileParam band[3];
                            bool b = r.object([&](const std::string& role) {
                                int n = role == "freq" ? 0 : role == "gain" ? 1 : role == "q" ? 2 : -1;
                                return n >= 0 ? param(r, band[n]) || r.skip() : r.skip();
                            });
                            for (auto& p : band) listed.push_back(p);
                            return b;
                        });
                    PageFileParam p;
                    if (!param(r, p)) return r.skip();
                    named.push_back({part, p});
                    return true;
                });
                if (!pic) return false;
                auto role = [&](const char* name) {
                    for (auto& [k2, p] : named) if (k2 == name) return p;
                    return PageFileParam {};
                };
                if (kind == "envelope") { page.picture = PagePicture::Envelope; page.parts = {role("attack"), role("decay"), role("sustain"), role("release")}; }
                else if (kind == "filter") { page.picture = PagePicture::Filter; page.parts = {role("cutoff"), role("resonance")}; }
                else if (kind == "value") { page.picture = PagePicture::Value; listed.resize(std::min<size_t>(listed.size(), 3)); page.parts = listed; }
                else if (kind == "eq") { page.picture = PagePicture::Eq; listed.resize(std::min<size_t>(listed.size(), PagePictureSlots::most)); page.parts = listed; }
                else if (kind == "scope") page.picture = PagePicture::Scope;
                return true; // an unknown kind: no picture
            });
            if (!read) return false;
            out.pages.push_back(std::move(page));
            return true;
        });
    });
    if (!ok) { why = "not JSON Rhythmer can read"; return false; }
    if (version < 1) { why = "no \"melodee-pages\": 1"; return false; }
    if (version > 1) { why = "version " + std::to_string(version) + " is newer than this Rhythmer"; return false; }
    if (out.clap.empty() && out.vst2.empty() && out.lv2.empty() && out.name.empty()) { why = "names no plug-in"; return false; }
    if (out.pages.empty()) { why = "has no pages"; return false; }
    return true;
}

// A VST2 unique ID as files write it: its four letters ("NAMp"), else the number.
inline std::string vst2_code(int32_t id) {
    std::string code;
    for (int shift = 24; shift >= 0; shift -= 8) {
        char c = char((uint32_t(id) >> shift) & 0xff);
        if (c < 32 || c > 126) return std::to_string(id);
        code.push_back(c);
    }
    return code;
}

// Whether `file` is for the plug-in `who`: its CLAP id, VST2 ID, LV2 URI, or else its name (and vendor, where the
// file gives one).
inline bool page_file_matches(const PageFile& file, const PluginIdentity& who) {
    if (!file.clap.empty() && file.clap == who.clap) return true;
    if (!file.vst2.empty() && who.vst2 && (file.vst2 == vst2_code(who.vst2) || file.vst2 == std::to_string(who.vst2))) return true;
    if (!file.lv2.empty() && file.lv2 == who.lv2) return true;
    if (file.name.empty() || file.name != who.name) return false;
    return file.vendor.empty() || file.vendor == who.vendor;
}

// The file's pages as parameter indices (plugin_pages.h): `shown` the plug-in's parameters any page may show, `names`
// and `ids` every parameter's name and ID by index. A parameter the plug-in does not have is left out and said in
// `problems` (one line each); a page left empty is dropped by lay_out().
// The parameter `p` names, among `shown` (by ID when it gives one, else by name): its index, -1 none.
inline int find_param(const PageFileParam& p, const std::vector<int>& shown, const std::vector<std::string>& names,
                      const std::vector<std::string>& ids) {
    for (int s : shown) {
        size_t n = size_t(s);
        if (!p.id.empty() ? n < ids.size() && ids[n] == p.id : !p.name.empty() && n < names.size() && names[n] == p.name) return s;
    }
    return -1;
}

inline std::vector<PluginPage> resolve_page_file(const PageFile& file, const std::vector<int>& shown, const std::vector<std::string>& names,
                                                 const std::vector<std::string>& ids, std::vector<std::string>& problems) {
    auto lookup = [&](const PageFileParam& p) { return find_param(p, shown, names, ids); };
    auto missing = [&](const PageFileParam& p) {
        if (p.id.empty() && p.name.empty()) return;
        std::string line = "no parameter " + (p.id.empty() ? "\"" + p.name + "\"" : "with id " + p.id);
        if (std::find(problems.begin(), problems.end(), line) == problems.end()) problems.push_back(line);
    };
    std::vector<int> named;
    for (const auto& page : file.pages)
        for (const auto& p : page.params) {
            int i = lookup(p);
            if (i >= 0) named.push_back(i); else missing(p);
        }
    std::vector<PluginPage> out;
    for (const auto& page : file.pages) {
        PluginPage made;
        made.name = page.name;
        for (const auto& p : page.params)
            if (int i = lookup(p); i >= 0) { made.params.push_back(i); made.labels.push_back(p.label); }
        if (page.rest)
            for (int s : shown)
                if (std::find(named.begin(), named.end(), s) == named.end()) { made.params.push_back(s); made.labels.push_back(""); }
        made.picture = page.picture;
        for (const auto& part : page.parts) {
            int i = lookup(part);
            if (i < 0) missing(part);
            made.picture_params.push_back(i);
        }
        out.push_back(std::move(made));
    }
    return out;
}

} // namespace rhythmer
