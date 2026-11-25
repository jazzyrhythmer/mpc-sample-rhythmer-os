// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Ableton's Drum Rack presets onto a kit (shell/drum_rack.h): where their samples are, and each pad's values in
// the kit's terms. The preset's JSON is read in drum_rack_preset.cpp.
#include "rhythmer/shell/drum_rack.h"
#include "rhythmer/engine/engine_info.h"
#include "rhythmer/model/scales.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace rhythmer {
namespace {

namespace fs = std::filesystem;

bool starts(const std::string& text, const char* head) { return text.rfind(head, 0) == 0; }

// %XX undone, as a URI's path keeps a space ("%20") or any byte past ASCII.
std::string unescaped(const std::string& text) {
    auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    std::string out;
    for (size_t n = 0; n < text.size(); ++n) {
        if (text[n] == '%' && n + 2 < text.size() && hex(text[n + 1]) >= 0 && hex(text[n + 2]) >= 0) {
            out += char(hex(text[n + 1]) * 16 + hex(text[n + 2]));
            n += 2;
        } else out += text[n];
    }
    return out;
}

// A level as the MPC shows it (model/sampler.h volume_db()) for `db`: -74..0 dB 1..100, 0..+6 dB 100..127.
uint8_t level_of_db(double db) {
    if (std::isnan(db)) return 100;
    if (db <= level_floor_db) return std::isinf(db) ? 0 : 1; // a Gain of 0: silent
    if (db <= 0) return uint8_t(std::clamp<long>(std::lround(100.0 * std::pow(10.0, db / 37.0)), 1, 100));
    return uint8_t(std::clamp<long>(std::lround(100.0 + db * 27.0 / level_top_db), 100, 127));
}

// The project's slot for a file, each path once; -1 when full.
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

} // namespace

std::string ableton_sample_path(const std::string& uri, const std::string& preset_folder, const AbletonLibraries& libraries) {
    if (uri.empty()) return {};
    auto under = [](const std::string& root, const std::string& rest) {
        return root.empty() ? std::string() : (fs::path(root) / fs::path(unescaped(rest)).relative_path()).string();
    };
    static constexpr char core[] = "ableton:/packs/abl-core-library/", user[] = "ableton:/user-library/";
    if (starts(uri, core)) return under(libraries.core, uri.substr(sizeof core - 1));
    if (starts(uri, user)) return under(libraries.user, uri.substr(sizeof user - 1));
    if (starts(uri, "ableton:")) return {}; // another pack's
    if (starts(uri, "file://")) return unescaped(uri.substr(7));
    const auto path = fs::path(unescaped(uri));
    return path.is_absolute() ? path.string() : (fs::path(preset_folder) / path).string();
}

bool is_drum_rack_preset(const std::string& path) {
    auto extension = fs::path(path).extension().string();
    for (auto& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (extension != ".json") return false;
    std::ifstream in(path, std::ios::binary);
    std::string head(4096, '\0');
    in.read(head.data(), std::streamsize(head.size()));
    head.resize(size_t(in.gcount()));
    // A Track Preset ("devicePreset.json" schema) whose rack holds a Drum Rack, which comes near the top.
    return head.find("devicePreset") != std::string::npos && head.find("\"drumRack\"") != std::string::npos;
}

int drum_rack_pad(int note) {
    const int pad = note - int(sampler_first_note);
    return pad >= 0 && pad < int(sampler_pads) ? pad : -1;
}

void drum_rack_pad_sound(const DrumRackPad& from, uint32_t frames, PadSound& pad) {
    // The region: Start and Length are of the whole sample.
    const double end = std::min(1.0, from.start + from.length);
    pad.start = frames ? uint32_t(std::llround(from.start * frames)) : 0;
    pad.end = frames && end < 1.0 ? std::max(pad.start + 1, uint32_t(std::llround(end * frames))) : 0;
    pad.level = level_of_db(from.volume_db);
    pad.pan = int8_t(std::clamp<long>(std::lround(from.pan * 50.0), -50, 50));
    pad.pitch = int8_t(std::clamp(from.transpose, -48, 48));
    pad.fine = int8_t(std::clamp<long>(std::lround(from.detune * 100.0), -50, 50));
    pad.group = uint8_t(std::clamp(from.choke, 0, 16));
    pad.velocity_sense = uint8_t(std::clamp<long>(std::lround(from.velocity_to_volume * 100.0), 0, 100));
    pad.play = from.gate ? PadPlay::Gate : PadPlay::OneShot;
    // A filter that cuts: the pad's cutoff 0..100 is 20 Hz up 9.97 octaves (engine/sampler.cpp), as the Hz are.
    // "Lowpass" is the Drum Sampler's steeper one beside "Lowpass 12dB"; its Peak has no pad filter to go to.
    pad.filter = PadFilter::Off;
    if ((from.filter == "Lowpass" || from.filter == "Lowpass 12dB") && from.cutoff_hz < 20000.0)
        pad.filter = from.filter == "Lowpass" ? PadFilter::Lpf4 : PadFilter::Lpf2;
    else if (from.filter == "Highpass" && from.cutoff_hz > 31.0) // 30 Hz, its lowest: open
        pad.filter = PadFilter::Hpf4;
    if (pad.filter == PadFilter::Off) return;
    pad.cutoff = uint8_t(std::clamp<long>(std::lround(std::log2(std::max(from.cutoff_hz, 20.0) / 20.0) * 100.0 / 9.97), 0, 100));
    pad.resonance = uint8_t(std::clamp<long>(std::lround(from.resonance * 100.0), 0, 100));
}

std::string import_drum_rack(const DrumRack& rack, Project& project, unsigned& missing,
                             const std::function<uint32_t(const std::string&)>& frames_of) {
    missing = 0;
    project = Project {};
    std::snprintf(project.name, sizeof(project.name), "%s", rack.name.c_str());
    project.root = 0;
    project.scale = chromatic_scale;
    auto& track = project.tracks[0];
    load_sound(track, EngineKind::Sampler, 0);
    track.set_name(rack.name.c_str());
    unsigned pads = 0;
    for (const auto& from : rack.pads) {
        const int at = drum_rack_pad(from.note);
        if (at < 0 || from.uri.empty()) continue; // off the kit, or a pad with no sample
        std::error_code error;
        if (from.file.empty() || !fs::is_regular_file(from.file, error)) { ++missing; continue; }
        const int slot = slot_for(project, from.file, fs::path(from.file).stem().string());
        if (slot < 0) continue;
        auto& pad = track.pads[size_t(at)];
        // The region needs the sample's length; a whole one does not.
        drum_rack_pad_sound(from, from.start > 0 || from.length < 1 ? frames_of(from.file) : 0, pad);
        pad.sample = uint16_t(slot);
        ++pads;
    }
    return pads ? std::string() : "NO SAMPLES";
}

} // namespace rhythmer
