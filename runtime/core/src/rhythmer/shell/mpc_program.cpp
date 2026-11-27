// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/mpc_program.h"
#include "rhythmer/engine/dsp.h"
#include "rhythmer/engine/engine_info.h"
#include "rhythmer/model/scales.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace rhythmer {
namespace fs = std::filesystem;
namespace {
constexpr size_t most_text = 32u << 20;

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

bool read_bytes(const std::string& path, std::string& out, size_t limit) {
    std::error_code error;
    auto bytes = fs::file_size(path, error);
    if (error || bytes > most_text) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.resize(std::min<size_t>(bytes, limit));
    file.read(out.data(), std::streamsize(out.size()));
    out.resize(size_t(file.gcount()));
    return true;
}

// A file as a program names it, beside the program: exactly or, on a card written elsewhere, whatever the case.
fs::path found_beside(const fs::path& folder, const std::string& relative) {
    fs::path at = folder;
    std::stringstream parts(relative);
    std::string part;
    std::error_code error;
    while (std::getline(parts, part, '/')) {
        if (part.empty() || part == "." || part == "..") continue; // never out of the program's folder
        if (fs::exists(at / part, error)) { at /= part; continue; }
        bool found = false;
        const auto wanted = lowered(part);
        for (auto it = fs::directory_iterator(at, error); !error && it != fs::directory_iterator(); it.increment(error))
            if (lowered(it->path().filename().string()) == wanted) { at = it->path(); found = true; break; }
        if (!found) return {};
    }
    return at;
}

// JUCE keeps an element's text as text-node children: a leaf is one with only those.
bool has_elements(const juce::XmlElement& element) {
    for (auto* child : element.getChildIterator())
        if (!child->isTextElement()) return true;
    return false;
}

KeygroupTags leaves_of(const juce::XmlElement& element) {
    KeygroupTags tags;
    for (auto* child : element.getChildIterator())
        if (!child->isTextElement() && !has_elements(*child)) tags[child->getTagName().toStdString()] = child->getAllSubText().toStdString();
    return tags;
}

// The project's sample slots, each path once; -1 when full.
int slot_for(Project& project, const std::string& path, const std::string& name) {
    if (path.size() >= sizeof(SampleRef::path)) return -1;
    int free = -1;
    for (unsigned n = 0; n < sample_slots; ++n) {
        const auto& ref = project.samples[n];
        if (ref.used() && path == ref.path) return int(n);
        if (free < 0 && !ref.used()) free = int(n);
    }
    if (free < 0) return -1;
    auto& ref = project.samples.edit(size_t(free));
    std::snprintf(ref.path, sizeof(ref.path), "%s", path.c_str());
    std::snprintf(ref.name, sizeof(ref.name), "%s", name.c_str());
    return free;
}
// An MPC program writes all 128 of its pads or keygroups, four layers each, some two hundred elements a pad, whether
// a layer has a sample or not: 1.2 MB of text for a keygroup of forty files, and a third of a second for the XML
// reader on the MPC. Nothing reads a layer that names no sample, nor a pad without one (keygroup_layers()), so
// those are left out of the text before it is parsed.
bool names_sample(const std::string& text, size_t from, size_t to) {
    for (const char* tag : {"<SampleName>", "<SampleFile>"}) {
        const size_t length = std::strlen(tag);
        for (size_t at = text.find(tag, from); at != std::string::npos && at < to; at = text.find(tag, at + length)) {
            size_t end = text.find('<', at + length);
            if (end == std::string::npos || end > to) end = to;
            if (text.find_first_not_of(" \t\r\n", at + length) < end) return true;
        }
    }
    return false;
}

// `text` without its elements from `open` to `close` that name no sample.
std::string sampled_only(const std::string& text, const char* open, const char* close) {
    std::string out;
    out.reserve(text.size());
    const size_t close_length = std::strlen(close);
    size_t at = 0;
    for (;;) {
        const size_t begin = text.find(open, at);
        if (begin == std::string::npos) break;
        size_t end = text.find(close, begin);
        if (end == std::string::npos) break;
        end += close_length;
        out.append(text, at, begin - at);
        if (names_sample(text, begin, end)) out.append(text, begin, end - begin);
        at = end;
    }
    out.append(text, at, std::string::npos);
    return out;
}
} // namespace

std::string parse_mpc_program(const std::string& whole, KeygroupProgram& program) {
    size_t first = whole.find_first_not_of(" \t\r\n\xef\xbb\xbf");
    if (first == std::string::npos || whole[first] != '<') return "Not an MPC program";
    const std::string text = sampled_only(sampled_only(whole, "<Layer ", "</Layer>"), "<Instrument ", "</Instrument>");
    auto xml = juce::XmlDocument::parse(juce::String::fromUTF8(text.data(), int(text.size())));
    if (!xml || !xml->hasTagName("MPCVObject")) return "Unreadable";
    auto* found = xml->getChildByName("Program");
    if (!found) return "Unreadable";
    program = KeygroupProgram {};
    program.type = found->getStringAttribute("type").toStdString();
    program.tags = leaves_of(*found);
    if (auto* instruments = found->getChildByName("Instruments"))
        for (auto* element : instruments->getChildWithTagNameIterator("Instrument")) {
            KeygroupInstrument instrument;
            instrument.number = element->getIntAttribute("number");
            instrument.tags = leaves_of(*element);
            if (auto* layers = element->getChildByName("Layers"))
                for (auto* layer : layers->getChildWithTagNameIterator("Layer")) instrument.layers.push_back(leaves_of(*layer));
            program.instruments.push_back(std::move(instrument));
        }
    return {};
}

std::string read_program(const std::string& path, const std::string& bytes, KeygroupProgram& program) {
    if (bytes.size() >= 2 && uint8_t(bytes[0]) == 0x1f && uint8_t(bytes[1]) == 0x8b) // the MPC 3's: gzip around JSON
        return read_mpc_json_program(std::vector<uint8_t>(bytes.begin(), bytes.end()), path, program);
    return parse_mpc_program(bytes, program);
}

bool is_mpc_program(const std::string& path) {
    auto kind = lowered(fs::path(path).extension().string());
    return kind == ".xpm" || kind == ".xty";
}

bool is_drum_program(const std::string& path) {
    if (!is_mpc_program(path)) return false;
    std::string head;
    if (!read_bytes(path, head, 16384)) return false;
    if (head.size() >= 2 && uint8_t(head[0]) == 0x1f && uint8_t(head[1]) == 0x8b) { // JSON: read whole (an .xty is a few hundred KB)
        std::string whole;
        KeygroupProgram program;
        return read_bytes(path, whole, 8u << 20) && read_program(path, whole, program).empty() && program.type == "Drum";
    }
    return head.find("<Program type=\"Drum\"") != std::string::npos;
}

std::string program_preview(const std::string& path) {
    const fs::path where(path);
    // Expansions name it after the program file ("Kit.xpm.mp3", "Kit.xpm.wav"); some after its name alone.
    for (const auto& name : {where.filename().string(), where.stem().string()})
        for (const char* kind : {".mp3", ".wav"}) {
            auto found = found_beside(where.parent_path(), "[Previews]/" + name + kind);
            if (!found.empty()) return found.string();
        }
    return {};
}

std::string import_mpc_program(const std::vector<uint8_t>& file, const std::string& path, Project& project, MpcImport& report) {
    KeygroupProgram program;
    auto error = read_program(path, std::string(file.begin(), file.end()), program);
    if (!error.empty()) return error;
    if (program.type != "Drum") return "Not a drum program";
    const fs::path where(path), folder = where.parent_path();
    project = Project {};
    std::snprintf(project.name, sizeof(project.name), "%s", where.stem().string().c_str());
    project.root = 0;
    project.scale = chromatic_scale;
    auto& track = project.tracks[0];
    load_sound(track, EngineKind::Sampler, 0);
    auto named = program.tags.count("ProgramName") ? program.tags["ProgramName"] : where.stem().string();
    track.set_name(named.c_str());
    auto number = [&](const char* tag, double fallback) {
        auto found = program.tags.find(tag);
        return found == program.tags.end() ? fallback : std::atof(found->second.c_str());
    };
    track.mix.level = mpc_level(number("Volume", 0.7079));
    track.mix.pan = mpc_pan(number("Pan", 0.5));
    for (unsigned b = 0; b < bus_count; ++b) // the program's own Send 1-4: the whole kit's
        track.mix.sends[b] = uint8_t(std::clamp(std::lround(number(("Send" + std::to_string(b + 1)).c_str(), 0) * 100), 0l, 100l));
    std::array<uint32_t, 128> colours {}; // <ProgramPads-v2.10> (an .xty's or MPC 3 program's: ProgramPads)
    for (const auto& [tag, text] : program.tags)
        if (tag.rfind("ProgramPads", 0) == 0) { colours = mpc_pad_colours(text); break; }
    for (const auto& instrument : program.instruments) {
        const auto layers = keygroup_layers(instrument);
        if (layers.empty()) continue;
        if (instrument.number < 1 || instrument.number > int(sampler_pads)) { ++report.over_limit; continue; }
        const auto sample = keygroup_sample(*layers.front());
        int slot = -1;
        double to_model = 1.0;
        if (layers.size() == 1) { // its WAV, as any pad's
            auto found = found_beside(folder, sample);
            report.missing += found.empty();
            auto at = found.empty() ? (folder / sample) : found;
            slot = slot_for(project, at.string(), at.stem().string());
            if (unsigned rate = mpc_wav_rate(at.string())) to_model = double(dsp::model_rate) / rate;
        } else { // its layers, as an instrument read from the program
            slot = slot_for(project, path + "#" + std::to_string(instrument.number), fs::path(sample).stem().string());
        }
        if (slot < 0) { ++report.over_limit; continue; }
        auto& pad = track.pads[size_t(instrument.number - 1)];
        keygroup_pad(instrument, to_model, pad);
        pad.sample = uint16_t(slot);
        pad.colour = pad.kit_colour = colours[size_t(instrument.number - 1) % colours.size()];
        ++report.pads;
    }
    if (!report.pads) return "Nothing to import";
    report.tracks = 1;
    return {};
}

unsigned layers_from_programs(Project& project) {
    unsigned moved = 0;
    for (auto& track : project.tracks)
        for (auto& pad : track.pads) {
            if (!pad.used()) continue;
            const unsigned own = pad.sample % sample_slots;
            std::string file, which, error;
            if (!split_program_pad(project.samples[own].path, file, which)) continue;
            auto pads = program_pads(file, error);
            auto found = pads.find(which);
            if (found == pads.end()) continue;
            const auto layers = keygroup_layers(found->second);
            if (layers.size() < 2 || layers.size() > pad_layers) continue;
            std::array<std::string, pad_layers> paths;
            bool all = true;
            for (size_t n = 0; n < layers.size() && all; ++n) {
                auto at = found_beside(fs::path(file).parent_path(), keygroup_sample(*layers[n]));
                all = !at.empty() && at.string().size() < sizeof(SampleRef::path);
                if (all) paths[n] = at.string();
            }
            if (!all) continue;
            // The slots it would need: files not in the project yet, each once; its instrument's slot comes free
            // when no other pad plays it.
            unsigned sharing = 0, free = 0, wanted = 0;
            for (const auto& t : project.tracks) for (const auto& p : t.pads) sharing += p.used() && p.plays(uint16_t(own));
            for (const auto& ref : project.samples) free += !ref.used();
            if (sharing == 1) ++free;
            for (size_t n = 0; n < layers.size(); ++n) {
                bool known = std::any_of(project.samples.begin(), project.samples.end(), [&](const SampleRef& r) { return r.used() && paths[n] == r.path; });
                bool earlier = std::find(paths.begin(), paths.begin() + long(n), paths[n]) != paths.begin() + long(n);
                wanted += !known && !earlier;
            }
            if (wanted > free) continue;
            std::array<uint16_t, pad_layers> slots {};
            std::array<double, pad_layers> to_model {};
            to_model.fill(1.0);
            PadSound made = pad;
            if (!keygroup_pad_layers(found->second, slots, to_model, made)) continue; // a note-off pad: as it was
            if (sharing == 1) project.samples.edit(own) = SampleRef {};
            for (size_t n = 0; n < layers.size(); ++n) {
                slots[n] = uint16_t(slot_for(project, paths[n], fs::path(paths[n]).stem().string()));
                if (unsigned rate = mpc_wav_rate(paths[n])) to_model[n] = double(dsp::model_rate) / rate;
            }
            made = pad;
            keygroup_pad_layers(found->second, slots, to_model, made);
            pad = made;
            ++moved;
        }
    return moved;
}

bool split_program_pad(const std::string& path, std::string& file, std::string& which) {
    auto mark = path.rfind('#');
    if (mark == std::string::npos || mark + 1 >= path.size()) return false;
    file = path.substr(0, mark);
    which = path.substr(mark + 1);
    auto kind = lowered(fs::path(file).extension().string());
    return kind == ".xpm" || kind == ".xpj" || kind == ".xty";
}

std::map<std::string, KeygroupInstrument> program_pads(const std::string& file, std::string& error) {
    // The pads of a kit load one after another: the file is read once for all of them.
    static std::mutex lock;
    static std::string cached_path;
    static int64_t cached_stamp = -1;
    static std::map<std::string, KeygroupInstrument> cached;
    std::error_code fs_error;
    auto stamp = int64_t(fs::last_write_time(file, fs_error).time_since_epoch().count()) ^ int64_t(fs::file_size(file, fs_error));
    if (fs_error) { error = "Files missing"; return {}; }
    std::lock_guard<std::mutex> guard(lock);
    if (file == cached_path && stamp == cached_stamp) return cached;
    std::map<std::string, KeygroupInstrument> pads;
    std::string bytes;
    if (!read_bytes(file, bytes, most_text)) { error = "cannot read it"; return {}; }
    if (lowered(fs::path(file).extension().string()) == ".xpj") {
        pads = mpc_project_pads(std::vector<uint8_t>(bytes.begin(), bytes.end()), file);
    } else {
        KeygroupProgram program;
        error = read_program(file, bytes, program);
        if (!error.empty()) return {};
        for (auto& instrument : program.instruments)
            if (keygroup_layers(instrument).size() > 1) pads[std::to_string(instrument.number)] = std::move(instrument);
    }
    cached_path = file;
    cached_stamp = stamp;
    cached = pads;
    return pads;
}

} // namespace rhythmer
