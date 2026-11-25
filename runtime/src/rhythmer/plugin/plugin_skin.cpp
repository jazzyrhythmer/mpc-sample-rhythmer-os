// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/plugin/plugin_skin.h"
#include <fstream>
#include <map>

namespace rhythmer {
namespace fs = std::filesystem;

namespace {
// A path the file names, inside its folder only: no absolute paths, no "..".
bool inside(const std::string& path) {
    if (path.empty() || path[0] == '/') return false;
    for (const auto& part : fs::path(path)) if (part == "..") return false;
    return true;
}
} // namespace

std::shared_ptr<const PluginSkin> load_skin(const SkinSpec& spec, const fs::path& folder, const std::vector<int>& shown,
                                            const std::vector<std::string>& names, const std::vector<std::string>& ids,
                                            std::vector<std::string>& problems) {
    if (!spec.present || folder.empty()) return nullptr;
    auto skin = std::make_shared<PluginSkin>();
    std::map<std::string, juce::Image> loaded; // a file used twice is decoded once
    bool over = false;
    auto image = [&](const std::string& path) -> juce::Image {
        if (path.empty()) return {};
        if (auto it = loaded.find(path); it != loaded.end()) return it->second;
        juce::Image picture;
        if (!inside(path)) problems.push_back("image \"" + path + "\" is outside the page file's folder");
        else {
            picture = juce::ImageFileFormat::loadFrom(juce::File(juce::String((folder / path).string())));
            if (!picture.isValid()) problems.push_back("no image \"" + path + "\"");
        }
        if (picture.isValid()) {
            skin->bytes += size_t(picture.getWidth()) * size_t(picture.getHeight()) * 4;
            if (skin->bytes > skin_most_bytes && !over) { over = true; problems.push_back("images over 4 MB"); }
        }
        loaded[path] = picture;
        return picture;
    };
    auto page = [&](const SkinPageSpec& from) {
        PluginSkin::Page out;
        out.background = image(from.background);
        for (const auto& c : from.controls) {
            PluginSkin::Control control;
            control.kind = c.kind;
            if (c.kind == SkinKind::Knob || c.kind == SkinKind::Switch || c.kind == SkinKind::Slider || c.kind == SkinKind::Value) {
                control.param = find_param(c.param, shown, names, ids);
                if (control.param < 0) {
                    problems.push_back("skin: no parameter " + (c.param.id.empty() ? "\"" + c.param.name + "\"" : "with id " + c.param.id));
                    continue;
                }
            }
            control.image = image(c.image);
            control.cap = image(c.cap);
            control.frames = std::max(1u, c.frames);
            int w = c.w, h = c.h;
            if (control.image.isValid()) { // a filmstrip's frame, or the picture
                if (!w) w = control.image.getWidth();
                if (!h) h = control.image.getHeight() / int(control.frames);
            }
            if (c.kind == SkinKind::Value || c.kind == SkinKind::Label) { if (!w) w = 100; if (!h) h = c.text_size + 4; }
            control.box = {c.x, c.y, w, h};
            control.text = juce::String::fromUTF8(c.text.c_str());
            control.of = c.of;
            control.coloured = c.coloured;
            control.colour = juce::Colour(0xff000000u | c.colour);
            control.text_size = std::max(11, c.text_size);
            control.align = c.align;
            control.horizontal = c.horizontal;
            bool drawable = control.image.isValid() || c.kind == SkinKind::Value || c.kind == SkinKind::Label
                         || c.kind == SkinKind::Curve || c.kind == SkinKind::Scope;
            if (drawable && !control.box.isEmpty()) out.controls.push_back(std::move(control));
        }
        return out;
    };
    for (const auto& [name, from] : spec.pages) skin->pages.push_back({name, page(from)});
    if (spec.has_strip) { skin->has_strip = true; skin->strip = page(spec.strip); }
    if (!spec.font.empty()) {
        std::error_code e;
        fs::path at = folder / spec.font;
        if (!inside(spec.font) || !fs::is_regular_file(at, e) || fs::file_size(at, e) > (size_t(4) << 20)) problems.push_back("no font \"" + spec.font + "\"");
        else {
            std::ifstream in(at, std::ios::binary);
            std::string data((std::istreambuf_iterator<char>(in)), {});
            skin->font = juce::Typeface::createSystemTypefaceFor(data.data(), data.size());
            if (!skin->font) problems.push_back("font \"" + spec.font + "\" does not read");
        }
    }
    if (over) return nullptr;
    if (skin->pages.empty() && !skin->has_strip) return nullptr;
    return skin;
}

} // namespace rhythmer
