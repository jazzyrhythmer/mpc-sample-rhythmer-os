// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// A Drum Rack preset's JSON read by juce_core's reader (shell/drum_rack.h); the rest is drum_rack.cpp's, which
// the core's own tests build without JUCE.
#include "rhythmer/shell/drum_rack.h"
#include "rhythmer/engine/dsp.h"
#include "rhythmer/shell/sample_decode.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace rhythmer {
namespace {

namespace fs = std::filesystem;

double number(const juce::var& value, double fallback) {
    return value.isDouble() || value.isInt() || value.isInt64() ? static_cast<double>(value) : fallback;
}

// The Drum Rack a preset holds: the device itself, or the first of an Instrument Rack's first chain, as the Move
// saves a drum track ("instrumentRack" > chains[0] > devices[0] "drumRack"). Void when there is none.
juce::var drum_rack_of(const juce::var& device, int depth = 0) {
    if (depth > 3 || !device.isObject()) return {};
    const auto kind = device["kind"].toString();
    if (kind == "drumRack") return device;
    if (kind != "instrumentRack") return {};
    const auto* chains = device["chains"].getArray();
    if (!chains || chains->isEmpty()) return {};
    const auto* devices = chains->getReference(0)["devices"].getArray();
    if (!devices || devices->isEmpty()) return {};
    return drum_rack_of(devices->getReference(0), depth + 1);
}

// A sound file's length at 44.1 kHz, from its header; 0 when it cannot be read.
uint32_t model_frames_of(const std::string& path) {
    AudioReader reader;
    if (!reader.open(path, 3600).empty() || !reader.rate()) return 0;
    return uint32_t(std::llround(double(reader.frames()) * dsp::model_rate / reader.rate()));
}

} // namespace

std::string parse_drum_rack(const std::string& text, const std::string& preset_path, const AbletonLibraries& libraries,
                            DrumRack& out) {
    out = DrumRack {};
    juce::var preset;
    if (juce::JSON::parse(juce::String::fromUTF8(text.data(), int(text.size())), preset).failed() || !preset.isObject())
        return "NOT A DRUM KIT";
    const auto rack = drum_rack_of(preset);
    const auto* chains = rack["chains"].getArray();
    if (!chains) return "NOT A DRUM KIT";
    out.name = preset["name"].toString().toStdString();
    if (out.name.empty()) out.name = fs::path(preset_path).stem().string();
    const auto folder = fs::path(preset_path).parent_path().string();
    for (const auto& chain : *chains) {
        const auto* devices = chain["devices"].getArray();
        if (!devices || devices->isEmpty()) continue;
        const auto& cell = devices->getReference(0);
        if (cell["kind"].toString() != "drumCell") continue; // a pad that plays something else (a rack, a synth)
        const auto& p = cell["parameters"];
        const auto& zone = chain["drumZoneSettings"];
        const auto& mixer = chain["mixer"];
        DrumRackPad pad;
        pad.note = int(number(zone["receivingNote"], -1));
        pad.choke = std::clamp(int(number(zone["chokeGroup"], 0)), 0, 16);
        pad.uri = cell["deviceData"]["sampleUri"].toString().toStdString();
        pad.file = ableton_sample_path(pad.uri, folder, libraries);
        const double gain = number(p["Voice_Gain"], 1.0);
        pad.volume_db = number(p["Volume"], 0.0) + (gain > 0 ? 20.0 * std::log10(gain) : -INFINITY) + number(mixer["volume"], 0.0);
        pad.pan = number(p["Pan"], 0.0) + number(mixer["pan"], 0.0);
        pad.transpose = int(std::lround(number(p["Voice_Transpose"], 0.0)));
        pad.detune = number(p["Voice_Detune"], 0.0);
        pad.start = std::clamp(number(p["Voice_PlaybackStart"], 0.0), 0.0, 1.0);
        pad.length = std::clamp(number(p["Voice_PlaybackLength"], 1.0), 0.0, 1.0);
        pad.velocity_to_volume = std::clamp(number(p["Voice_VelocityToVolume"], 0.35), 0.0, 1.0);
        pad.gate = p["Voice_Envelope_Mode"].toString() == "A-S-R";
        if (!p["Voice_Filter_On"].isBool() || bool(p["Voice_Filter_On"])) pad.filter = p["Voice_Filter_Type"].toString().toStdString();
        pad.cutoff_hz = number(p["Voice_Filter_Frequency"], 22000.0);
        pad.resonance = number(p["Voice_Filter_Resonance"], 0.0);
        out.pads.push_back(pad);
    }
    return out.pads.empty() ? "NOT A DRUM KIT" : std::string();
}

std::string import_drum_rack_preset(const std::string& path, const AbletonLibraries& libraries, Project& project,
                                    unsigned& missing) {
    missing = 0;
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.empty()) return "UNREADABLE";
    DrumRack rack;
    if (auto error = parse_drum_rack(text, path, libraries, rack); !error.empty()) return error;
    return import_drum_rack(rack, project, missing, model_frames_of);
}

} // namespace rhythmer
