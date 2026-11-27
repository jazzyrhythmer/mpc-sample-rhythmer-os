// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/mpc_project.h"
#include "rhythmer/shell/mpc_wav_slices.h"
#include "rhythmer/engine/engine_info.h"
#include "rhythmer/engine/dsp.h"
#include "rhythmer/engine/sample_data.h"
#include "rhythmer/model/scales.h"
#include "rhythmer/model/scene_links.h"
#include "rhythmer/model/song_steps.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace rhythmer {
namespace fs = std::filesystem;
namespace {

constexpr ssize_t largest_document = 256 * 1024 * 1024;
constexpr unsigned mpc_ticks_per_tick = 960 / ticks_per_quarter; // the MPC counts 960 a quarter, as the sequencer
static_assert(960 % ticks_per_quarter == 0, "the MPC's pulses are whole ticks");
constexpr int note_event = 3;                                    // an event's "type"
constexpr int mpc_pads = 128;

// The file unpacked. It can be several gzip members one after another: the MPC Sample's factory kits and demos
// (/content/Projects) are the header in one member and the JSON in the next. A member's last four bytes are its
// unpacked size, which marks where the next member starts.
juce::MemoryBlock unpack(const std::vector<uint8_t>& file) {
    juce::MemoryBlock text;
    size_t start = 0;
    while (start + 18 <= file.size() && ssize_t(text.getSize()) < largest_document) { // 18: a gzip member's least
        juce::MemoryBlock member;
        {
            juce::MemoryInputStream raw(file.data() + start, file.size() - start, false);
            juce::GZIPDecompressorInputStream inflated(&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
            inflated.readIntoMemoryBlock(member, largest_document - ssize_t(text.getSize()));
        }
        if (member.getSize() == 0) break;
        text.append(member.getData(), member.getSize());
        const uint32_t size = uint32_t(member.getSize());
        size_t next = 0;
        for (size_t k = start + 18; k + 2 < file.size() && !next; ++k)
            if (file[k] == 0x1f && file[k + 1] == 0x8b && file[k + 2] == 8
                && (uint32_t(file[k - 4]) | uint32_t(file[k - 3]) << 8 | uint32_t(file[k - 2]) << 16 | uint32_t(file[k - 1]) << 24) == size)
                next = k;
        if (!next) break;
        start = next;
    }
    return text;
}

// The document's "data": unpacked, its header checked, parsed. Void on failure.
juce::var read_document(const std::vector<uint8_t>& file) {
    const juce::MemoryBlock text = unpack(file);
    const char* begin = static_cast<const char*>(text.getData());
    const char* end = begin + text.getSize();
    std::array<std::string, 5> header;
    const char* at = begin;
    for (auto& line : header) {
        const char* stop = std::find(at, end, '\n');
        if (stop == end) return {};
        line.assign(at, stop);
        at = stop + 1;
    }
    // SerialisableProjectData (.xpj), SerialisableTrackData (.xty), and the MPC 3's programs alike
    if (header[0] != "ACVS" || header[2].rfind("Serialisable", 0) != 0 || header[3] != "json") return {};
    return juce::JSON::parse(juce::String::fromUTF8(at, int(end - at)))["data"];
}

double number(const juce::var& object, const char* key, double fallback = 0) {
    const auto& value = object[key];
    return value.isVoid() ? fallback : double(value);
}
int integer(const juce::var& object, const char* key, int fallback = 0) { return int(number(object, key, fallback)); }
bool flag(const juce::var& object, const char* key, bool fallback = false) {
    const auto& value = object[key];
    return value.isVoid() ? fallback : bool(value);
}
// A song step's value: the MPC writes "item.sequenceIndex" and "item.repeats" (the names in the stock app); a plain
// key, or one inside "item", is read too.
int step_value(const juce::var& step, const char* key, int fallback) {
    const auto name = std::string("item.") + key;
    for (const auto* value : {&step[name.c_str()], &step[key], &step["item"][key]})
        if (!value->isVoid()) return int(*value);
    return fallback;
}

// A sequence's name as its scene's: what the Name sheet can type (shell/name_entry.h), cut to the scene's length at
// the last whole word that fits. A default name ("Sequence 01", or none) leaves the scene unnamed.
void scene_name(const juce::String& text, Scene& scene) {
    static constexpr const char* typed = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.()&'";
    const auto name = text.trim();
    if (name.isEmpty() || name == "(unnamed)") return;
    if (name.startsWithIgnoreCase("Sequence") && name.substring(8).trim().containsOnly("0123456789")) return;
    std::string kept;
    for (auto c : name.toStdString())
        if (c && std::strchr(typed, c) && !(c == ' ' && (kept.empty() || kept.back() == ' '))) kept += c;
    const size_t room = sizeof(scene.name) - 1;
    if (kept.size() > room) {
        const size_t space = kept.rfind(' ', room); // a space at `room` ends a whole word that fits
        kept.resize(space != std::string::npos && space > 0 ? space : room);
    }
    while (!kept.empty() && kept.back() == ' ') kept.pop_back();
    if (!kept.empty()) scene.set_name(kept.c_str());
}

uint8_t percent(double value) { return uint8_t(std::clamp(std::lround(value * 100), 0l, 100l)); }
uint16_t tenths(double bpm) { return uint16_t(std::clamp(std::lround(bpm * 10), 300l, 3000l)); }

uint8_t level_of(double value) { return mpc_level(value); }
int8_t pan_of(double value) { return mpc_pan(value); }
PadFilter filter_of(int type) { return mpc_filter(type); }

unsigned wav_rate(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    char head[12];
    if (!file.read(head, sizeof(head)) || std::memcmp(head, "RIFF", 4) || std::memcmp(head + 8, "WAVE", 4)) return 0;
    char chunk[8];
    while (file.read(chunk, sizeof(chunk))) {
        uint32_t size = juce::ByteOrder::littleEndianInt(chunk + 4);
        if (!std::memcmp(chunk, "fmt ", 4)) {
            char format[8];
            return file.read(format, sizeof(format)) ? juce::ByteOrder::littleEndianInt(format + 4) : 0;
        }
        file.seekg(std::streamoff(size) + (size & 1), std::ios::cur);
    }
    return 0;
}

// The project's sample slots, each file once.
class Samples {
public:
    Samples(Project& p, fs::path f) : project(p), folder(std::move(f)) {}
    // A slot for the file, -1 when it does not fit; `missing` is set when it is not there.
    int slot(const juce::String& file, bool& missing) {
        auto name = fs::path(file.toStdString()).filename(); // the name alone: never out of the data folder
        auto path = (folder / name).string();
        std::error_code error;
        missing = !fs::exists(path, error);
        if (path.size() >= sizeof(SampleRef::path)) return -1;
        const int n = free_or_same(path);
        if (n < 0 || project.samples[size_t(n)].used()) return n;
        auto& ref = project.samples.edit(size_t(n));
        std::strncpy(ref.path, path.c_str(), sizeof(ref.path) - 1);
        std::strncpy(ref.name, name.stem().string().c_str(), sizeof(ref.name) - 1);
        auto rate = wav_rate(path);
        rates[size_t(n)] = rate ? double(dsp::model_rate) / rate : 1.0;
        return n;
    }
    // A slot for a pad's layers, played as an instrument: the path names the pad.
    int instrument(const std::string& path, const std::string& name) {
        if (path.size() >= sizeof(SampleRef::path)) return -1;
        const int n = free_or_same(path);
        if (n < 0 || project.samples[size_t(n)].used()) return n;
        auto& ref = project.samples.edit(size_t(n));
        std::snprintf(ref.path, sizeof(ref.path), "%s", path.c_str());
        std::snprintf(ref.name, sizeof(ref.name), "%s", name.c_str());
        return n;
    }
    double to_model(unsigned slot) const { return rates[slot % sample_slots]; }
private:
    // The slot holding `path`, else the first free one (the slots are filled from the first on); -1: none.
    int free_or_same(const std::string& path) const {
        for (unsigned n = 0; n < sample_slots; ++n) {
            const auto& ref = project.samples[n];
            if (!ref.used() || path == ref.path) return int(n);
        }
        return -1;
    }
    Project& project;
    fs::path folder;
    std::array<double, sample_slots> rates {};
};

const juce::var nothing; // what the lookups below hand back when there is no such thing

// A program's ProgramPads (mpc_pad_colours()): a flag or a pad is {"value0": ...}, as the MPC writes them.
std::array<uint32_t, 128> pad_colours_of(const juce::var& pads) {
    std::array<uint32_t, 128> out {};
    if (!pads.isObject()) return out;
    auto colour = [](const juce::var& v) { return full_colour(uint32_t(juce::int64(v)) & 0xFFFFFF); };
    if (flag(pads["PadsFollowTrackColour"], "value0")) return out; // the track's: Rhythmer's own
    if (flag(pads["Universal"], "value0")) {
        if (!pads["universalPad"].isVoid()) out.fill(colour(pads["universalPad"]));
        return out;
    }
    const auto& each = pads["pads"];
    for (unsigned n = 0; n < out.size(); ++n) {
        const auto& v = each[juce::Identifier("value" + juce::String(n))];
        if (!v.isVoid()) out[n] = colour(v);
    }
    return out;
}

const juce::var& sounding_layer(const juce::var& instrument) {
    const auto* layers = instrument["layersv"].getArray();
    if (layers)
        for (const auto& layer : *layers)
            if (flag(layer, "active", true) && layer["sampleFile"].toString().isNotEmpty()) return layer;
    return nothing;
}

unsigned sounding_layers(const juce::var& instrument) {
    unsigned count = 0;
    if (const auto* layers = instrument["layersv"].getArray())
        for (const auto& layer : *layers) count += flag(layer, "active", true) && !flag(layer, "mute") && layer["sampleFile"].toString().isNotEmpty();
    return count;
}

// A layer as a drum program's <Layer> (engine/keygroup.h), its file in the project's data folder.
KeygroupTags layer_tags(const juce::var& layer, const std::string& data_folder) {
    const auto& slice = layer["sliceInfo"];
    const auto& volume = layer["volume"];
    auto text = [](double value) { return std::to_string(value); };
    KeygroupTags tags;
    tags["Active"] = flag(layer, "active", true) ? "True" : "False";
    tags["Mute"] = flag(layer, "mute") ? "True" : "False";
    tags["SampleName"] = data_folder + "/" + fs::path(layer["sampleFile"].toString().toStdString()).filename().string();
    tags["VelStart"] = text(integer(layer, "velocityStart"));
    tags["VelEnd"] = text(integer(layer, "velocityEnd", 127));
    tags["TuneCoarse"] = text(integer(layer, "coarseTune"));
    tags["TuneFine"] = text(integer(layer, "fineTune"));
    tags["Volume"] = text(volume.isObject() ? number(volume, "gainCoefficient", 1) : number(layer, "volume", 1));
    tags["Pan"] = text(number(layer, "pan", 0.5));
    tags["SliceStart"] = text(number(slice, "Start"));
    tags["SliceEnd"] = text(number(slice, "End"));
    tags["SliceLoop"] = text(flag(layer, "layerLoopModeOverridesSliceLoopMode", true) ? integer(layer, "loopMode") : integer(slice, "LoopMode"));
    tags["SliceLoopStart"] = text(number(slice, "LoopStart"));
    tags["Direction"] = text(integer(layer, "direction"));
    tags["RootNote"] = text(integer(layer, "rootNote", 61)); // one past the MIDI note, as in an .xpm
    tags["KeyTrack"] = flag(layer, "keyTrackEnable") ? "True" : "False";
    tags["Pitch"] = text(number(layer, "pitch"));
    return tags;
}

// An instrument (a drum pad or a keygroup) as a program's <Instrument>, its layers' files in `data_folder`.
KeygroupInstrument instrument_tags(const juce::var& instrument, int number_of, const std::string& data_folder) {
    const auto& synth = instrument["synthSection"];
    const auto& amp = synth["ampEnvelope"];
    const auto& filter = synth["filterData"]["value0"];
    const auto& filter_env = synth["filterEnvelope"];
    const auto& mix = instrument["mixable"];
    auto text = [](double value) { return std::to_string(value); };
    auto truth = [](bool value) { return std::string(value ? "True" : "False"); };
    KeygroupInstrument made;
    made.number = number_of;
    auto& t = made.tags;
    t["LowNote"] = text(integer(instrument, "lowNote"));
    t["HighNote"] = text(integer(instrument, "highNote", 127));
    t["ZonePlay"] = text(integer(instrument, "zonePlayTime", 1));
    t["TriggerMode"] = text(integer(instrument, "triggerMode", 2));
    t["IgnoreBaseNote"] = truth(flag(instrument, "ignoreBaseNote"));
    t["Volume"] = text(number(mix, "volume", 0.7079));
    t["Pan"] = text(number(mix, "pan", 0.5));
    t["Mute"] = truth(flag(mix, "mute"));
    t["TuneCoarse"] = text(integer(instrument, "coarseTune"));
    t["TuneFine"] = text(integer(instrument, "fineTune"));
    t["Mono"] = truth(flag(instrument, "monophonic"));
    t["MuteGroup"] = text(integer(instrument, "whichMuteGroup"));
    t["VolumeAttack"] = text(number(amp["Attack"], "value0"));
    t["VolumeHold"] = text(number(amp["Hold"], "value0"));
    t["VolumeDecay"] = text(number(amp["Decay"], "value0"));
    t["VolumeSustain"] = text(number(amp["Sustain"], "value0", 1));
    t["VolumeRelease"] = text(number(amp["Release"], "value0"));
    t["VolumeDecayType"] = truth(flag(amp["DecayFromEnd"], "value0", true));
    t["VelocitySensitivity"] = text(number(synth, "velocitySensitivity", 1));
    t["FilterType"] = text(integer(filter, "filterType", 29));
    t["Cutoff"] = text(number(filter, "filterCutoff", 1));
    t["Resonance"] = text(number(filter, "filterResonance"));
    t["FilterEnvAmt"] = text(number(filter, "filterEnvelopeAmount"));
    t["FilterAttack"] = text(number(filter_env["Attack"], "value0"));
    t["FilterDecay"] = text(number(filter_env["Decay"], "value0"));
    t["FilterSustain"] = text(number(filter_env["Sustain"], "value0"));
    t["FilterRelease"] = text(number(filter_env["Release"], "value0"));
    if (const auto* layers = instrument["layersv"].getArray())
        for (const auto& layer : *layers)
            if (layer["sampleFile"].toString().isNotEmpty()) made.layers.push_back(layer_tags(layer, data_folder));
    return made;
}

void read_pad(const juce::var& instrument, const juce::var& layer, unsigned slot, double to_model, PadSound& pad, bool layered = false) {
    const auto frame = [&](double at) { return uint32_t(std::max(std::lround(at * to_model), 0l)); };
    const auto& slice = layer["sliceInfo"];
    const auto& synth = instrument["synthSection"];
    const auto& amp = synth["ampEnvelope"];
    const auto& mix = instrument["mixable"];
    pad.sample = uint16_t(slot);
    pad.start = frame(number(slice, "Start"));
    pad.end = frame(number(slice, "End"));
    pad.pitch = int8_t(std::clamp(integer(instrument, "coarseTune") + integer(layer, "coarseTune"), -48, 48));
    pad.fine = int8_t(std::clamp(integer(instrument, "fineTune") + integer(layer, "fineTune"), -99, 99));
    pad.level = level_of(number(mix, "volume", 0.7079));
    pad.pan = pan_of(number(mix, "pan", 0.5));
    pad.play = flag(amp["OneShot"], "value0", true) ? PadPlay::OneShot : PadPlay::Gate;
    pad.reverse = integer(layer, "direction") == 1;
    pad.group = uint8_t(std::clamp(integer(instrument, "whichMuteGroup"), 0, 16));
    pad.mono = flag(instrument, "monophonic");
    pad.attack = percent(number(amp["Attack"], "value0"));
    pad.decay = percent(number(amp["Decay"], "value0"));
    pad.sustain = percent(number(amp["Sustain"], "value0", 1));
    pad.release = percent(number(amp["Release"], "value0"));
    pad.decay_from_start = !flag(amp["DecayFromEnd"], "value0", true);
    pad.velocity_sense = percent(number(synth, "velocitySensitivity", 1));
    pad.loop = (flag(layer, "layerLoopModeOverridesSliceLoopMode", true) ? integer(layer, "loopMode") : integer(slice, "LoopMode")) != 0;
    pad.loop_lock = flag(instrument, "loopLock", true);
    pad.loop_start = frame(number(slice, "LoopStart"));
    const auto& filter = synth["filterData"]["value0"];
    pad.cutoff = percent(number(filter, "filterCutoff", 1));
    pad.resonance = percent(number(filter, "filterResonance"));
    pad.filter_amount = percent(number(filter, "filterEnvelopeAmount"));
    pad.filter = filter_of(integer(filter, "filterType", 29));
    if (pad.filter == PadFilter::Classic && pad.cutoff == 100 && !pad.resonance && !pad.filter_amount) pad.filter = PadFilter::Off; // wide open: nothing to hear
    if (pad.filter_amount) {
        const auto& env = synth["filterEnvelope"];
        pad.filter_attack = percent(number(env["Attack"], "value0"));
        pad.filter_decay = percent(number(env["Decay"], "value0"));
        pad.filter_sustain = percent(number(env["Sustain"], "value0"));
        pad.filter_release = percent(number(env["Release"], "value0"));
    }
    if (layered) { // the layers' own tune, region and loop are their zones' (engine/keygroup.h)
        pad.start = pad.end = pad.loop_start = 0;
        pad.loop = false;
        pad.loop_lock = true;
        pad.reverse = false;
        pad.pitch = int8_t(std::clamp(integer(instrument, "coarseTune"), -48, 48));
        pad.fine = int8_t(std::clamp(integer(instrument, "fineTune"), -99, 99));
        return;
    }
    if (flag(instrument, "warpEnable")) {
        pad.warp = flag(instrument, "bpmLock", true) ? warp_seq : uint8_t(std::clamp(integer(instrument, "stretchPercentage", 100), int(warp_min), int(warp_max)));
        pad.own_tempo = tenths(number(instrument, "tempo", 120));
    }
}

// A track's clip in a sequence, by the track's name; void when it has none.
const juce::var& clip_of(const juce::var& sequence, const juce::String& track) {
    const auto* maps = sequence["trackClipMaps"].getArray();
    if (maps)
        for (const auto& map : *maps)
            if (const auto* clips = map.getArray())
                for (const auto& clip : *clips)
                    if (clip["key"].toString() == track) return clip["value"];
    return nothing;
}

bool has_notes(const juce::var& clip) {
    if (const auto* events = clip["eventList"]["events"].getArray())
        for (const auto& event : *events)
            if (integer(event, "type") == note_event) return true;
    return false;
}

// A kit track's clip. A note played on Chop's pads (the stock MPC Sample's Chop pad play) is its pad's note with the
// slice in its Sample Slice modifier (the sixteenth): a slice hit (mpc_slice_hit()).
unsigned read_notes(const juce::var& clip, const std::array<int, 128>& pad_of_note, bool slices_relative, Pattern& pattern) {
    unsigned added = 0;
    const auto* events = clip["eventList"]["events"].getArray();
    if (!events) return 0;
    for (const auto& event : *events) {
        if (integer(event, "type") != note_event || flag(event, "muted")) continue;
        const auto& note = event["note"];
        int key = integer(note, "note", -1);
        if (key < 0 || key > 127 || pad_of_note[size_t(key)] < 0) continue;
        uint32_t tick = uint32_t(std::lround(number(event, "time") / mpc_ticks_per_tick));
        if (tick >= pattern.length) continue;
        Event e;
        e.tick = tick;
        e.length = note_ticks(uint64_t(std::max(1l, std::lround(number(note, "length") / mpc_ticks_per_tick))));
        e.key = uint8_t(sampler_first_note + unsigned(pad_of_note[size_t(key)]));
        e.velocity = uint8_t(std::clamp(std::lround(number(note, "velocity", 1) * 127), 1l, 127l));
        e.chance = uint8_t(std::clamp(integer(note, "probability", 100), 0, 100));
        e.slice = mpc_slice_hit(flag(note, "modifierActiveState15"), number(note, "modifierValue15"), slices_relative);
        added += pattern.add(e);
    }
    return added;
}

// A keygroup track's clip: its notes as MIDI notes, as a KEYGROUP track keeps them.
unsigned read_keys(const juce::var& clip, Pattern& pattern) {
    unsigned added = 0;
    const auto* events = clip["eventList"]["events"].getArray();
    if (!events) return 0;
    for (const auto& event : *events) {
        if (integer(event, "type") != note_event || flag(event, "muted")) continue;
        const auto& note = event["note"];
        int key = integer(note, "note", -1);
        if (key < 0 || key > 127) continue;
        uint32_t tick = uint32_t(std::lround(number(event, "time") / mpc_ticks_per_tick));
        if (tick >= pattern.length) continue;
        Event e;
        e.tick = tick;
        e.length = note_ticks(uint64_t(std::max(1l, std::lround(number(note, "length") / mpc_ticks_per_tick))));
        e.key = uint8_t(key);
        e.velocity = uint8_t(std::clamp(std::lround(number(note, "velocity", 1) * 127), 1l, 127l));
        e.chance = uint8_t(std::clamp(integer(note, "probability", 100), 0, 100));
        added += pattern.add(e);
    }
    return added;
}

} // namespace

bool is_mpc_project(const std::string& path) { return fs::path(path).extension() == ".xpj"; }

std::array<uint32_t, 128> mpc_pad_colours(const std::string& json) {
    auto root = juce::JSON::parse(juce::String::fromUTF8(json.data(), int(json.size())));
    if (auto* object = root.getDynamicObject()) // an .xpm's: the pads inside {"ProgramPads-v2.10": {...}}
        for (const auto& property : object->getProperties())
            if (property.name.toString().startsWith("ProgramPads")) return pad_colours_of(property.value);
    return pad_colours_of(root);
}

unsigned mpc_wav_rate(const std::string& path) { return wav_rate(path); }

std::map<std::string, KeygroupInstrument> mpc_project_pads(const std::vector<uint8_t>& file, const std::string& path) {
    std::map<std::string, KeygroupInstrument> pads;
    const auto data = read_document(file);
    const auto* tracks = data["tracks"].getArray();
    if (!tracks) return pads;
    const fs::path where(path);
    const std::string data_folder = where.stem().string() + "_[ProjectData]";
    for (int t = 0; t < tracks->size(); ++t) {
        const auto* instruments = tracks->getReference(t)["program"]["drum"]["instruments"].getArray();
        if (!instruments) continue;
        for (int n = 0; n < std::min(instruments->size(), mpc_pads); ++n) {
            const auto& instrument = instruments->getReference(n);
            if (sounding_layers(instrument) < 2) continue;
            pads[std::to_string(t) + "." + std::to_string(n + 1)] = instrument_tags(instrument, n + 1, data_folder);
        }
    }
    return pads;
}

namespace {
// An MPC 3 program in JSON as the .xpm reader's tree, its layers' files in `data_folder` (relative to the file).
std::string program_tree(const juce::var& program, const std::string& data_folder, KeygroupProgram& out) {
    const auto* instruments = program["drum"]["instruments"].getArray();
    if (!instruments) return "Not an MPC program";
    out = KeygroupProgram {};
    out.type = integer(program, "type") == 1 ? "Keygroup" : "Drum";
    out.tags["ProgramName"] = program["name"].toString().toStdString();
    out.tags["Volume"] = std::to_string(number(program["mixable"], "volume", 0.7079));
    out.tags["Pan"] = std::to_string(number(program["mixable"], "pan", 0.5));
    if (program["programPads"].isObject()) // its pad colours, as an .xpm keeps them (mpc_pad_colours())
        out.tags["ProgramPads"] = juce::JSON::toString(program["programPads"], true).toStdString();
    if (program["keygroup"].isObject()) {
        out.tags["KeygroupNumKeygroups"] = std::to_string(integer(program["keygroup"], "numKeygroups", 128));
        out.tags["KeygroupTranspose"] = std::to_string(integer(program["keygroup"], "transpose"));
    }
    // Mono and Portamento, an .xpm's program tags (engine/keygroup.cpp read_keygroup()).
    const auto& voices = program["drum"];
    out.tags["Mono"] = flag(voices, "monophonic") ? "True" : "False";
    out.tags["PortamentoTime"] = std::to_string(number(voices, "portamentoTime", 0));
    out.tags["PortamentoLegato"] = flag(voices, "portamentoLegato") ? "True" : "False";
    // Mono Retrig (MPC 3.9): a key pressed while one is held starts the note again. A project saved before it has none.
    if (!voices["monoRetrigger"].isVoid()) out.tags["MonoRetrigger"] = flag(voices, "monoRetrigger") ? "True" : "False";
    for (int n = 0; n < std::min(instruments->size(), mpc_pads); ++n)
        out.instruments.push_back(instrument_tags(instruments->getReference(n), n + 1, data_folder));
    return {};
}
} // namespace

std::string read_mpc_json_program(const std::vector<uint8_t>& file, const std::string& path, KeygroupProgram& out) {
    const auto data = read_document(file);
    if (data.isVoid()) return "Unreadable";
    const auto& program = data["program"].isObject() ? data["program"] : data;
    // Its samples: in "<name>_[TrackData]" (an .xty) or "<name>_[ProgramData]" beside it, else beside it.
    const fs::path where(path);
    std::string data_folder;
    std::error_code error;
    for (const char* tail : {"_[TrackData]", "_[ProgramData]"})
        if (data_folder.empty() && fs::is_directory(where.parent_path() / (where.stem().string() + tail), error)) data_folder = where.stem().string() + tail;
    return program_tree(program, data_folder, out);
}

std::string mpc_project_keygroup(const std::vector<uint8_t>& file, const std::string& path, unsigned track, KeygroupProgram& out) {
    const auto data = read_document(file);
    const auto* tracks = data["tracks"].getArray();
    if (!tracks || track >= unsigned(tracks->size())) return "Track not found";
    const auto& program = tracks->getReference(int(track))["program"];
    if (integer(program, "type") != 1) return "Not a keygroup";
    return program_tree(program, fs::path(path).stem().string() + "_[ProjectData]", out);
}

std::string import_mpc_project(const std::vector<uint8_t>& file, const std::string& path, Project& project, PatternBank& patterns, MpcImport& report) {
    const auto data = read_document(file);
    const auto* tracks = data["tracks"].getArray();
    if (!tracks) return "Not an MPC Sample project";

    project = Project {};
    patterns.clear();
    const fs::path where(path);
    std::snprintf(project.name, sizeof(project.name), "%s", where.stem().string().c_str());
    project.root = 0;
    project.scale = chromatic_scale;

    // Sequences by their number (the file's order is not): the ones Rhythmer has patterns for.
    std::map<unsigned, const juce::var*> sequences;
    if (const auto* list = data["sequences"].getArray())
        for (const auto& entry : *list) {
            unsigned key = unsigned(integer(entry, "key"));
            if (key < pattern_count) sequences[key] = &entry["value"];
            else ++report.over_limit;
        }

    // Notes' Sample Slice modifiers: the slice itself (Absolute, as the MPC Sample saves it) or an offset (Relative).
    const bool slices_relative = data["modifierPlaybackProperties"]["Sample Slice"].toString() == "Relative";

    Samples samples(project, where.parent_path() / (where.stem().string() + "_[ProjectData]"));
    unsigned used = 0;
    int raw_index = -1;
    std::map<std::string, unsigned> take_of; // an Audio track's file: its take number
    const fs::path data_folder = where.parent_path() / (where.stem().string() + "_[ProjectData]");
    for (const auto& source : *tracks) {
        ++raw_index;
        const auto& program = source["program"];
        if (integer(program, "type") == 6) { // an MPC 3 Audio track (freeze plan F3): an AUDIO track
            const auto name = source["name"].toString();
            std::map<unsigned, const juce::var*> firsts; // each sequence's region from its start
            unsigned regions = 0, others = 0;
            auto same = [](const juce::var& a, const juce::var& b) { // the take again (as a pattern looping in a longer sequence)
                return a["sample"]["path"].toString() == b["sample"]["path"].toString() && number(a, "sliceStart") == number(b, "sliceStart")
                    && number(a, "sliceEnd") == number(b, "sliceEnd");
            };
            for (const auto& [n, sequence] : sequences) {
                const auto* events = clip_of(*sequence, name)["eventList"]["events"].getArray();
                if (!events) continue;
                for (const auto& event : *events)
                    if (integer(event, "type") == 4 && integer(event, "time") <= 10 && !firsts.count(n)) firsts[n] = &event;
                for (const auto& event : *events) {
                    if (integer(event, "type") != 4) continue;
                    ++regions;
                    if (firsts.count(n) && firsts[n] != &event && !same((*firsts[n])["audio"], event["audio"])) ++others;
                    if (!firsts.count(n)) ++others;
                }
            }
            if (!regions) continue;
            if (used == track_count) { ++report.over_limit; continue; }
            const unsigned index = used++;
            auto& track = project.tracks[index];
            track = Track {};
            track.engine = EngineKind::Audio;
            track.set_name(name.toRawUTF8());
            const auto& mix = program["mixable"];
            track.mix.level = level_of(number(mix, "volume", 0.7079));
            track.mix.pan = pan_of(number(mix, "pan", 0.5));
            track.mix.mute = flag(mix, "mute") || flag(source, "mute");
            track.mix.solo = flag(mix, "solo");
            for (const auto& [n, sequence] : sequences)
                patterns.at[index][n].length = uint32_t(std::clamp<int64_t>(int64_t(number(*sequence, "lengthPulses", 3840) / mpc_ticks_per_tick), 1, project.max_length()));
            for (const auto& [n, event] : firsts) {
                const auto& audio = (*event)["audio"];
                const std::string file = audio["sample"]["path"].toString().toStdString();
                const fs::path wav = data_folder / file;
                WavReader reader;
                if (file.empty() || !reader.open(wav.string(), 6 * 3600).empty() || !reader.rate()) { ++report.missing; continue; }
                auto known = take_of.find(wav.string());
                unsigned take = known != take_of.end() ? known->second : unsigned(take_of.size() + 1);
                if (known == take_of.end()) { take_of[wav.string()] = take; report.takes.emplace_back(take, wav.string()); }
                const double k = 44100.0 / double(reader.rate()); // the file's frames counted at 44.1 kHz, as a take's are
                auto model = [&](double frames) { return uint32_t(std::max(0.0, std::round(frames * k))); };
                const double total = double(reader.frames());
                const double start = std::clamp(number(audio, "sliceStart", 0), 0.0, total), end = std::clamp(number(audio, "sliceEnd", total), start, total);
                AudioRegion r;
                r.take = uint16_t(take);
                r.offset = model(start);
                r.frames = model(end - start);
                r.take_frames = std::max(model(total), r.offset + r.frames);
                r.tempo = tenths(number(audio, "tempo", 120));
                const double volume = number(audio, "volume", 0.5011872);
                r.gain = int16_t(volume > 0 ? std::clamp<long>(std::lround(200 * std::log10(volume / 0.5011872)), region_gain_min, region_gain_max) : region_gain_min);
                r.reverse = flag(audio, "reverse");
                r.fade_in = std::min(model(number(audio, "fadeIn", 0)), r.frames);
                r.fade_out = std::min(model(number(audio, "fadeOut", 0)), r.frames);
                r.warp = flag(audio, "warpEnable");
                r.semi = int8_t(std::clamp(integer(audio, "coarseTune", 0), -24, 24));
                r.fine = int8_t(std::clamp(integer(audio, "fineTune", 0), -50, 50));
                if (!r.used()) continue;
                patterns.at[index][n].audio = r;
                ++report.regions;
            }
            report.regions_left_out += others;
            continue;
        }
        const auto* instruments = program["drum"]["instruments"].getArray();
        if (!instruments) continue; // submixes and outputs are tracks in the file too
        const auto name = source["name"].toString();
        if (integer(program, "type") == 1) { // an MPC 3 keygroup track: a KEYGROUP track playing it ("<file>#k<track>")
            const juce::var* first = nullptr;
            for (const auto& instrument : *instruments) if (!first && !sounding_layer(instrument).isVoid()) first = &instrument;
            if (!first) continue;
            if (used == track_count) { ++report.over_limit; continue; }
            int slot = samples.instrument(path + "#k" + std::to_string(raw_index), name.toStdString().substr(0, sizeof(SampleRef::name) - 1));
            if (slot < 0) { ++report.over_limit; continue; }
            const unsigned index = used++;
            auto& track = project.tracks[index];
            load_sound(track, EngineKind::Keygroup, 0);
            track.set_name(name.toRawUTF8());
            const auto& mix = program["mixable"];
            track.mix.level = level_of(number(mix, "volume", 0.7079));
            track.mix.pan = pan_of(number(mix, "pan", 0.5));
            track.mix.mute = flag(mix, "mute") || flag(source, "mute");
            track.mix.solo = flag(mix, "solo");
            // its sound: the first keygroup's envelope, filter and level; its zones' own tune and regions are theirs
            read_pad(*first, sounding_layer(*first), unsigned(slot), 1.0, track.pads[0], true);
            track.pads[0].play = PadPlay::Gate;
            track.pads[0].mono = flag(program["drum"], "monophonic");
            track.last_pad = 0;
            ++report.pads;
            for (const auto& [n, sequence] : sequences) {
                auto& pattern = patterns.at[index][n];
                pattern.length = uint32_t(std::clamp<int64_t>(int64_t(number(*sequence, "lengthPulses", 3840) / mpc_ticks_per_tick), 1, project.max_length()));
                pattern.free = false; // the sequence's length, notes or not
                report.notes += read_keys(clip_of(*sequence, name), pattern);
            }
            continue;
        }
        bool wanted = false;
        for (const auto& instrument : *instruments) wanted = wanted || !sounding_layer(instrument).isVoid();
        for (const auto& [n, sequence] : sequences) wanted = wanted || has_notes(clip_of(*sequence, name));
        if (!wanted) continue;
        if (used == track_count) { ++report.over_limit; continue; }

        const unsigned index = used++;
        auto& track = project.tracks[index];
        load_sound(track, EngineKind::Sampler, 0);
        track.set_name(name.toRawUTF8());
        const auto& mix = program["mixable"];
        track.mix.level = level_of(number(mix, "volume", 0.7079));
        track.mix.pan = pan_of(number(mix, "pan", 0.5));
        track.mix.mute = flag(mix, "mute") || flag(source, "mute");
        track.mix.solo = flag(mix, "solo");
        const auto colours = pad_colours_of(program["programPads"]);

        for (int n = 0; n < std::min(instruments->size(), mpc_pads); ++n) {
            const auto& instrument = instruments->getReference(n);
            const auto& layer = sounding_layer(instrument);
            if (layer.isVoid()) continue;
            bool missing = false;
            const bool layered = sounding_layers(instrument) > 1;
            int slot = layered ? samples.instrument(path + "#" + std::to_string(raw_index) + "." + std::to_string(n + 1),
                                                    layer["sampleName"].toString().toStdString().substr(0, sizeof(SampleRef::name) - 1))
                               : samples.slot(layer["sampleFile"].toString(), missing);
            if (slot < 0) { ++report.over_limit; continue; }
            report.missing += missing;
            read_pad(instrument, layer, unsigned(slot), layered ? 1.0 : samples.to_model(unsigned(slot)), track.pads[size_t(n)], layered);
            track.pads[size_t(n)].colour = track.pads[size_t(n)].kit_colour = colours[size_t(n)];
            ++report.pads;
        }

        std::array<int, 128> pad_of_note;
        pad_of_note.fill(-1);
        const auto& pad_notes = program["padNoteMap"]["noteForPad"];
        for (int pad = mpc_pads - 1; pad >= 0; --pad) { // the lowest pad of a note wins
            auto key = juce::String("value") + juce::String(pad);
            int note = pad_notes[key.toRawUTF8()].isVoid() ? 36 + pad : int(pad_notes[key.toRawUTF8()]);
            if (note >= 0 && note < 128) pad_of_note[size_t(note)] = pad;
        }
        for (const auto& [n, sequence] : sequences) {
            auto& pattern = patterns.at[index][n];
            pattern.length = uint32_t(std::clamp<int64_t>(int64_t(number(*sequence, "lengthPulses", 3840) / mpc_ticks_per_tick), 1, project.max_length()));
            pattern.free = false; // the sequence's length, notes or not
            report.notes += read_notes(clip_of(*sequence, name), pad_of_note, slices_relative, pattern);
        }
    }
    if (!used) return "Nothing to import";
    report.tracks = used;
    // Each file's chop, as the MPC keeps it in the WAV ("atem"): Chop on a pad of the whole file finds its slices.
    for (unsigned n = 0; n < sample_slots; ++n)
        if (project.samples[n].used() && !project.samples[n].instrument()) project.chops.edit(n) = read_mpc_slices(project.samples[n].path);
    report.sequences = unsigned(sequences.size());

    // The tempo: the project's own when it is on, else each sequence's.
    const bool master = flag(data, "masterTempoEnabled");
    const unsigned current = unsigned(integer(data, "currentSequence"));
    project.seq_tempo = !master && !sequences.empty();
    project.tempo = tenths(number(data, "masterTempo", 120));
    for (const auto& [n, sequence] : sequences) project.pattern_tempo[n] = tenths(number(*sequence, "bpm", 120));
    if (!master && sequences.count(current)) project.tempo = project.pattern_tempo[current];
    if (sequences.count(current))
        for (unsigned t = 0; t < used; ++t) project.tracks[t].pattern = uint8_t(current);
    // Each sequence's own name, as Sequences shows it.
    for (const auto& [n, sequence] : sequences) scene_name((*sequence)["name"].toString(), project.scenes[n]);
    // Each sequence keeps its own number on every track, as the MPC plays it (model/scene_links.h).
    keep_columns(project, patterns);
    if (sequences.count(current)) project.scene = uint8_t(current % pattern_count);

    // One time signature for every pattern, as in Rhythmer: the first sequence's.
    if (!sequences.empty()) {
        const auto* signatures = (*sequences.begin()->second)["timeSignatureTrack"]["timeSignatures"].getArray();
        if (signatures && !signatures->isEmpty()) {
            const auto& first = signatures->getReference(0);
            int beat = integer(first, "beatLength", 960), unit = beat > 0 ? 3840 / beat : 4, beats = integer(first, "beatsPerBar", 4);
            for (unsigned n = 0; n < time_signature_count; ++n)
                if (time_signatures[n].beats == beats && time_signatures[n].unit == unit) project.signature = uint8_t(n);
        }
    }

    // The song: the first of the file's songs with steps (the MPC Sample's Song page edits one), laid out as Song's
    // clips, a marker a step, and the tempo lane for sequences of their own tempo (model/song_steps.h).
    const juce::var* song = nullptr;
    if (const auto* songs = data["songs"].getArray())
        for (const auto& candidate : *songs) {
            const auto* items = candidate["items"].getArray();
            if (!items || items->isEmpty()) continue;
            if (song) ++report.songs_left_out;
            else song = &candidate;
        }
    if (song) {
        std::vector<SongStep> steps;
        const unsigned bar = std::max(project.bar_ticks(), 1u);
        for (const auto& item : *(*song)["items"].getArray()) {
            const int index = step_value(item, "sequenceIndex", -1);
            if (index < 0 || !sequences.count(unsigned(index))) { ++report.steps_left_out; continue; } // not in the file
            const auto ticks = std::clamp<int64_t>(int64_t(number(*sequences[unsigned(index)], "lengthPulses", 3840) / mpc_ticks_per_tick), 1, project.max_length());
            SongStep step;
            step.sequence = uint8_t(index);
            step.repeats = uint16_t(std::clamp(step_value(item, "repeats", 1), 1, int(song_bars)));
            step.bars = uint16_t((uint64_t(ticks) + bar - 1) / bar);
            steps.push_back(step);
        }
        const unsigned left = song_from_steps(project, patterns, steps, !master && !flag(*song, "ignoreTempo"));
        report.song_steps = unsigned(steps.size()) - left;
        report.steps_left_out += left;
    }
    return {};
}

} // namespace rhythmer
