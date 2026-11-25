// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// The picture a page draws above its row in hand (docs/PLUGIN_PAGES_PLAN.md, pick 4 a): found from its parameters'
// names, an envelope (attack, decay, sustain, release of one prefix: "Amp Attack" ... "Amp Release") or a filter
// (a cutoff with a resonance); or named by a page file (plugin/page_file.h), those two and large values (up to
// three), an EQ (bands of frequency, gain, Q) or the plug-in's sound (scope). Its parts in the kind's order:
// `params[n]` the parameter (-1 none), `slots[n]` where it is in the layout's rows (-1 not on the page).
enum class PagePicture : uint8_t { None, Envelope, Filter, Value, Eq, Scope };
struct PagePictureSlots {
    static constexpr unsigned most = 12; // an EQ's four bands of three
    PagePicture kind = PagePicture::None;
    int slots[most] {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int params[most] {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};

// A plug-in's pages (docs/PLUGIN_PAGES_PLAN.md): named groups of its parameters, laid out for K1-K3 in rows of
// three. The rack takes them from the first source that has them: the plug-in's CLAP remote-controls pages, an
// Open MPC skin's Q-Link pages, its parameter groups (VST3 units, LV2 port groups, CLAP modules), else its list.
// Plain C++, no JUCE: the host tests read it too.
namespace rhythmer {

struct PluginPage {
    PluginPage() = default;
    PluginPage(std::string page, std::vector<int> list) : name(std::move(page)), params(std::move(list)) {}
    std::string name;      // "" none: the plug-in's one list
    std::vector<int> params; // parameter indices, in order
    std::vector<std::string> labels; // a page file's names for their cards, by `params` ("" or missing: the plug-in's)
    PagePicture picture = PagePicture::None; // a page file's picture; None: found from the names
    std::vector<int> picture_params; // its parts' parameters, in the kind's order (-1 none)
};

// Pages as the cards take them: each page's parameters in rows of three, its last row filled with -1 (no
// parameter), so a row never holds two pages' parameters. `slots[row * 3 + k]` is what K(k + 1) turns on that row.
struct PluginLayout {
    std::vector<int> slots;
    std::vector<std::string> names;   // each page's name
    std::vector<uint16_t> first_rows; // each page's first row
    std::vector<std::string> labels;  // by slot: a page file's name for its card ("" the plug-in's)
    std::vector<std::vector<PagePictureSlots>> pictures; // each page's pictures (a page file's, else find_pictures())
    std::vector<bool> named;          // by page: its picture is a page file's, not looked for
    unsigned rows() const { return unsigned(slots.size() / 3); }
    unsigned pages() const { return unsigned(names.size()); }
    int at(size_t slot) const { return slot < slots.size() ? slots[slot] : -1; }
    // The page row `row` is on (0 with none).
    unsigned page_of(unsigned row) const {
        unsigned page = 0;
        for (unsigned p = 1; p < first_rows.size() && first_rows[p] <= row; ++p) page = p;
        return page;
    }
    // How many rows page `page` has.
    unsigned rows_of(unsigned page) const {
        if (page >= first_rows.size()) return 0;
        unsigned end = page + 1 < first_rows.size() ? first_rows[page + 1] : rows();
        return end - first_rows[page];
    }
};

// Lays out `pages`: within a page a parameter is taken once (it may be on more than one page); a page left empty
// is dropped.
inline PluginLayout lay_out(const std::vector<PluginPage>& pages) {
    PluginLayout out;
    for (const auto& page : pages) {
        std::vector<int> own;
        std::vector<std::string> labels;
        for (size_t n = 0; n < page.params.size(); ++n) {
            int p = page.params[n];
            if (p < 0 || std::find(own.begin(), own.end(), p) != own.end()) continue;
            own.push_back(p);
            labels.push_back(n < page.labels.size() ? page.labels[n] : std::string());
        }
        if (own.empty()) continue;
        size_t first = out.slots.size();
        out.names.push_back(page.name);
        out.first_rows.push_back(uint16_t(out.rows()));
        out.slots.insert(out.slots.end(), own.begin(), own.end());
        out.labels.insert(out.labels.end(), labels.begin(), labels.end());
        while (out.slots.size() % 3) { out.slots.push_back(-1); out.labels.emplace_back(); }
        out.pictures.emplace_back();
        out.named.push_back(page.picture != PagePicture::None);
        if (page.picture == PagePicture::None) continue;
        PagePictureSlots picture;
        picture.kind = page.picture;
        for (size_t n = 0; n < page.picture_params.size() && n < PagePictureSlots::most; ++n) {
            int p = page.picture_params[n];
            picture.params[n] = p;
            auto at = std::find(out.slots.begin() + long(first), out.slots.end(), p);
            picture.slots[n] = p >= 0 && at != out.slots.end() ? int(at - out.slots.begin()) : -1;
        }
        out.pictures.back().push_back(picture);
    }
    return out;
}

// Each page keeps only the parameters in `allowed` (the ones shown at all: not hidden, not the bypass).
inline void keep_only(std::vector<PluginPage>& pages, const std::vector<int>& allowed) {
    for (auto& page : pages) {
        std::vector<int> params;
        std::vector<std::string> labels;
        for (size_t n = 0; n < page.params.size(); ++n) {
            if (std::find(allowed.begin(), allowed.end(), page.params[n]) == allowed.end()) continue;
            params.push_back(page.params[n]);
            labels.push_back(n < page.labels.size() ? page.labels[n] : std::string());
        }
        page.params = std::move(params);
        page.labels = std::move(labels);
    }
}

// Pages from parameter groups (VST3 units, LV2 port groups, CLAP modules): `groups[i]` is the group of `params[i]`
// as a path, "/" between levels ("" none). With any group: parameters outside every group on a first page MAIN,
// then a page per group in the order the groups first appear, named by the group's last level (its whole path, a
// space between levels, where two groups end the same). With none: one page "" of them all.
inline std::vector<PluginPage> group_pages(const std::vector<int>& params, const std::vector<std::string>& groups) {
    std::vector<std::string> paths;
    for (size_t i = 0; i < params.size(); ++i) {
        const std::string& g = i < groups.size() ? groups[i] : std::string();
        if (!g.empty() && std::find(paths.begin(), paths.end(), g) == paths.end()) paths.push_back(g);
    }
    if (paths.empty()) return {PluginPage {"", params}};
    auto last = [](const std::string& path) {
        auto cut = path.find_last_of('/');
        return cut == std::string::npos ? path : path.substr(cut + 1);
    };
    std::vector<PluginPage> pages(paths.size() + 1);
    pages[0].name = "MAIN";
    for (size_t g = 0; g < paths.size(); ++g) {
        std::string leaf = last(paths[g]);
        bool shared = std::count_if(paths.begin(), paths.end(), [&](const std::string& p) { return last(p) == leaf; }) > 1;
        if (!shared) { pages[g + 1].name = leaf; continue; }
        std::string whole = paths[g];
        std::replace(whole.begin(), whole.end(), '/', ' ');
        pages[g + 1].name = whole;
    }
    for (size_t i = 0; i < params.size(); ++i) {
        const std::string& g = i < groups.size() ? groups[i] : std::string();
        size_t page = g.empty() ? 0 : size_t(std::find(paths.begin(), paths.end(), g) - paths.begin()) + 1;
        pages[page].params.push_back(params[i]);
    }
    if (pages[0].params.empty()) pages.erase(pages.begin());
    return pages;
}

// The pages a plug-in gets (the plan's pick 2): its own pages (`own`: remote-controls or Q-Link pages, already
// kept to `shown`) first, then, where it has parameter groups, the group pages (group_pages()) of the parameters
// its own pages leave out, so nothing is lost (Surge XT's remote-controls pages hold 36 of its 775). A plug-in
// without groups shows only its own pages. With no own pages: group_pages() of them all.
inline std::vector<PluginPage> pages_with_groups(std::vector<PluginPage> own, const std::vector<int>& shown,
                                                 const std::vector<std::string>& groups) {
    own.erase(std::remove_if(own.begin(), own.end(), [](const PluginPage& p) { return p.params.empty(); }), own.end());
    if (own.empty()) return group_pages(shown, groups);
    bool grouped = std::any_of(groups.begin(), groups.end(), [](const std::string& g) { return !g.empty(); });
    if (!grouped) return own;
    std::vector<int> rest;
    std::vector<std::string> rest_groups;
    for (size_t i = 0; i < shown.size(); ++i) {
        bool taken = std::any_of(own.begin(), own.end(), [&](const PluginPage& p) {
            return std::find(p.params.begin(), p.params.end(), shown[i]) != p.params.end();
        });
        if (taken) continue;
        rest.push_back(shown[i]);
        rest_groups.push_back(i < groups.size() ? groups[i] : std::string());
    }
    if (rest.empty()) return own;
    auto more = group_pages(rest, rest_groups);
    if (more.size() == 1 && more[0].name.empty()) more[0].name = "MAIN"; // the rest all outside every group
    own.insert(own.end(), more.begin(), more.end());
    return own;
}

namespace plugin_pages_detail {
inline std::string lower(std::string s) {
    for (auto& c : s) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return s;
}
// `name` with the first `word` taken out ("amp attack" less "attack": "amp "); `found` whether it held the word.
inline std::string without(const std::string& name, const std::string& word, bool& found) {
    auto at = name.find(word);
    found = at != std::string::npos;
    return found ? name.substr(0, at) + name.substr(at + word.size()) : name;
}
} // namespace plugin_pages_detail

// The pictures each page of `layout` can draw, from `names` (the plug-in's parameter names, by parameter index): an
// envelope for each attack whose page also holds decay, sustain and release of the same prefix, then a filter for
// each cutoff whose page holds a resonance ("resonance", "reso", "emphasis"; the same prefix first). A page of
// several (Melodee Poly's FILTER tab: its filter, both envelopes) draws the one its row in hand turns most of
// (picture_for()).
inline void find_pictures(PluginLayout& layout, const std::vector<std::string>& names) {
    using namespace plugin_pages_detail;
    layout.pictures.resize(layout.pages());
    layout.named.resize(layout.pages(), false);
    for (unsigned page = 0; page < layout.pages(); ++page) {
        if (layout.named[page]) continue; // a page file's own picture
        layout.pictures[page].clear();
        size_t from = size_t(layout.first_rows[page]) * 3, to = from + size_t(layout.rows_of(page)) * 3;
        auto name_at = [&](size_t slot) {
            int p = layout.at(slot);
            return p >= 0 && size_t(p) < names.size() ? lower(names[size_t(p)]) : std::string();
        };
        auto& found_here = layout.pictures[page];
        for (size_t a = from; a < to; ++a) {
            bool found = false;
            std::string prefix = without(name_at(a), "attack", found);
            if (!found) continue;
            int slots[4] {int(a), -1, -1, -1};
            const char* words[3] {"decay", "sustain", "release"};
            for (int w = 0; w < 3; ++w)
                for (size_t b = from; b < to && slots[w + 1] < 0; ++b) {
                    bool has = false;
                    if (without(name_at(b), words[w], has) == prefix && has) slots[w + 1] = int(b);
                }
            if (slots[1] >= 0 && slots[2] >= 0 && slots[3] >= 0) {
                PagePictureSlots picture;
                picture.kind = PagePicture::Envelope;
                std::copy(slots, slots + 4, picture.slots);
                for (int n = 0; n < 4; ++n) picture.params[n] = layout.at(size_t(slots[n]));
                found_here.push_back(picture);
            }
        }
        for (size_t c = from; c < to; ++c) {
            bool found = false;
            std::string prefix = without(name_at(c), "cutoff", found);
            if (!found) continue;
            int same = -1, any = -1; // a resonance of the cutoff's prefix, else the first
            for (size_t r = from; r < to; ++r)
                for (const char* word : {"resonance", "reso", "emphasis"}) {
                    bool has = false;
                    std::string rest = without(name_at(r), word, has);
                    if (!has) continue;
                    if (rest == prefix && same < 0) same = int(r);
                    if (any < 0) any = int(r);
                    break;
                }
            int best = same >= 0 ? same : any;
            if (best >= 0) {
                PagePictureSlots picture;
                picture.kind = PagePicture::Filter;
                picture.slots[0] = int(c);
                picture.slots[1] = best;
                picture.params[0] = layout.at(c);
                picture.params[1] = layout.at(size_t(best));
                found_here.push_back(picture);
            }
        }
        std::stable_partition(found_here.begin(), found_here.end(), [](const PagePictureSlots& p) { return p.kind == PagePicture::Filter; });
    }
}

// The picture page `page` draws with row `row` in hand: of its pictures, the one with most parts on that row; of
// those tied, the one with a part nearest it, then a filter before an envelope (Key Track's row on Melodee Poly's
// FILTER tab: the filter); none when it has none.
inline PagePictureSlots picture_for(const PluginLayout& layout, unsigned page, unsigned row) {
    if (page >= layout.pictures.size() || layout.pictures[page].empty()) return {};
    const auto& all = layout.pictures[page];
    size_t best = 0;
    int most = -1, nearest = 1 << 30;
    for (size_t n = 0; n < all.size(); ++n) {
        int on_row = 0, near = 1 << 30;
        for (int slot : all[n].slots) {
            if (slot < 0) continue;
            int away = std::abs(int(unsigned(slot) / 3) - int(row));
            on_row += away == 0;
            near = std::min(near, away);
        }
        if (on_row > most || (on_row == most && near < nearest)) { most = on_row; nearest = near; best = n; }
    }
    return all[best];
}

// A value text as a plug-in writes it ("12.5 ms", "1.2 s", "350ms", "1.20 sec", "2 kHz", "440 Hz"): its number
// in seconds or in hertz; false when the text names no such unit.
inline bool value_in(const std::string& text, bool hertz, double& out) {
    std::string t = plugin_pages_detail::lower(text);
    size_t at = 0;
    while (at < t.size() && t[at] == ' ') ++at;
    size_t start = at;
    if (at < t.size() && (t[at] == '-' || t[at] == '+')) ++at;
    bool digits = false;
    while (at < t.size() && ((t[at] >= '0' && t[at] <= '9') || t[at] == '.' || t[at] == ',')) { digits |= t[at] != '.' && t[at] != ','; ++at; }
    if (!digits) return false;
    std::string number = t.substr(start, at - start);
    for (auto& c : number) if (c == ',') c = '.';
    double v = std::atof(number.c_str());
    while (at < t.size() && t[at] == ' ') ++at;
    std::string unit = t.substr(at);
    while (!unit.empty() && (unit.back() == ' ' || unit.back() == '.')) unit.pop_back();
    if (hertz) {
        if (unit == "hz") { out = v; return true; }
        if (unit == "khz" || unit == "k") { out = v * 1000; return true; }
        return false;
    }
    if (unit == "ms" || unit == "msec") { out = v / 1000; return true; }
    if (unit == "s" || unit == "sec" || unit == "secs" || unit == "seconds") { out = v; return true; }
    if (unit == "min") { out = v * 60; return true; }
    return false;
}

// A gain as a plug-in writes it ("-3.5 dB", "+6dB"): its number of decibels; false without the unit.
inline bool decibels_in(const std::string& text, double& out) {
    std::string t = plugin_pages_detail::lower(text);
    auto at = t.find("db");
    if (at == std::string::npos) return false;
    std::string number = t.substr(0, at);
    for (auto& c : number) if (c == ',') c = '.';
    size_t start = number.find_first_of("+-.0123456789");
    if (start == std::string::npos || number.find_first_of("0123456789", start) == std::string::npos) return false;
    out = std::atof(number.c_str() + start);
    return true;
}

} // namespace rhythmer
