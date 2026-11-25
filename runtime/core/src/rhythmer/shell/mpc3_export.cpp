// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The MPC 3 export (shell/mpc3_export.h). The template is a project MPC 3
// saved (mpc3_template.json), cut down by tools/mpc3_template.py to one of each
// thing: a drum track and an empty pad, a keygroup track and an empty keygroup,
// the Submix, Return and Out tracks, a sequence, a clip and a note event. Each
// export reads it afresh and fills copies. Numbers keep the kind the template
// gives them: MPC 3 reads 1.0 and 1 differently, and JUCE writes a double as 1.0.
//
// A kit pad stays a pad of its Drum track when the MPC's pad can play it: a WAV
// file and its layers, or an instrument whose zones each cover every key (a pad
// of an MPC program or project, velocity layers), at most eight. An instrument
// of key zones, and a WAV pad its patterns play at other pitches (16 Levels,
// Keys), become a Keygroup track of their own, the pad's notes going with them.
#include "rhythmer/shell/mpc3_export.h"
#include "rhythmer/shell/file_copy.h"
#include "rhythmer/model/scales.h"
#include "rhythmer/model/scene_links.h"
#include "rhythmer/engine/dsp.h"
#include "rhythmer/engine/engine_info.h"
#include "rhythmer/engine/keygroup.h"
#include "rhythmer/engine/midi_engine.h"
#include "rhythmer/engine/sfz.h"
#include "rhythmer/model/sampler.h"
#include "rhythmer/model/song_flatten.h"
#include "rhythmer/shell/mpc_program.h"
#include "rhythmer/shell/mpc_wav_slices.h"
#include "rhythmer/shell/sample_library.h"
#include "rhythmer/shell/xpm_write.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <vector>

asm(R"(
    .section .rodata
    .balign 16
    .global rhythmer_mpc3_template_start
rhythmer_mpc3_template_start:
    .incbin "src/rhythmer/shell/mpc3_template.json"
    .global rhythmer_mpc3_template_end
rhythmer_mpc3_template_end:
    .previous
)");

extern "C" const unsigned char rhythmer_mpc3_template_start[], rhythmer_mpc3_template_end[];

namespace rhythmer {
namespace fs = std::filesystem;
namespace {

constexpr unsigned mpc_ticks_per_tick = 960 / ticks_per_quarter; // the MPC counts 960 a quarter
constexpr unsigned mpc_pads = 128;
constexpr unsigned mpc_layers = 8;  // a pad's or a keygroup's
constexpr int played_root = 60;     // an instrument pad's key with no pitch (engine/sampler.cpp zones_on)

juce::var read_template() {
    return juce::JSON::parse(juce::String::fromUTF8(reinterpret_cast<const char*>(rhythmer_mpc3_template_start),
                                                    int(rhythmer_mpc3_template_end - rhythmer_mpc3_template_start)));
}

// A property of an object, to change in place (objects and arrays inside are shared, not copied).
juce::var& field(const juce::var& object, const char* key) {
    static juce::var none;
    auto* dynamic = object.getDynamicObject();
    auto* value = dynamic ? dynamic->getProperties().getVarPointer(key) : nullptr;
    if (!value) { none = juce::var(); return none; }
    return *value;
}
void put(const juce::var& object, const char* key, juce::var value) {
    if (auto* dynamic = object.getDynamicObject()) dynamic->setProperty(key, std::move(value));
}
juce::var real(double value) { return juce::var(value); } // written with a decimal point, as MPC 3 has it
juce::var whole(int value) { return juce::var(value); }
juce::var array_of(std::vector<juce::var> items) {
    juce::Array<juce::var> out;
    for (auto& item : items) out.add(std::move(item));
    return juce::var(out);
}
juce::var object() { return juce::var(new juce::DynamicObject()); }
juce::var entry(juce::var key, juce::var value) {
    auto made = object();
    put(made, "key", std::move(key));
    put(made, "value", std::move(value));
    return made;
}

// Rhythmer's 0..127 level (100 is 0 dB) as an MPC Volume: the inverse of mpc_level() (engine/keygroup.cpp).
double mpc_volume(unsigned level) {
    if (!level) return 0.0;
    double db = level >= 100 ? (level - 100) / 27.0 * level_top_db : 37 * std::log10(level / 100.0);
    return std::pow(10.0, (db - level_top_db) / 40);
}
double mpc_pan_of(int pan) { return 0.5 + pan / 100.0; }
double fraction(unsigned percent) { return std::clamp(percent, 0u, 100u) / 100.0; }

// The pad's filter as the MPC's filter table (the inverse of mpc_filter()).
int mpc_filter_type(PadFilter filter) {
    switch (filter) {
    case PadFilter::Lpf2: return 2;
    case PadFilter::Lpf4: return 3;
    case PadFilter::Hpf2: return 7;
    case PadFilter::Hpf4: return 8;
    case PadFilter::Bpf2: return 11;
    case PadFilter::Bpf4: return 12;
    case PadFilter::Classic: return 29;
    default: return 0;
    }
}

struct Wav {
    unsigned rate = 0, channels = 0, bits = 0, format = 0; // format: 1 PCM, 3 float, 0xFFFE extensible
    uint64_t frames = 0;
    std::streamoff data = 0; // where its samples start
};
Wav wav_of(const std::string& path) {
    Wav out;
    std::ifstream file(path, std::ios::binary);
    char head[12];
    if (!file.read(head, sizeof(head)) || std::memcmp(head, "RIFF", 4) || std::memcmp(head + 8, "WAVE", 4)) return out;
    unsigned align = 0;
    char chunk[8];
    while (file.read(chunk, sizeof(chunk))) {
        const uint32_t size = juce::ByteOrder::littleEndianInt(chunk + 4);
        if (!std::memcmp(chunk, "fmt ", 4) && size >= 16) {
            char format[16];
            if (!file.read(format, sizeof(format))) return out;
            out.format = juce::ByteOrder::littleEndianShort(format);
            out.channels = juce::ByteOrder::littleEndianShort(format + 2);
            out.rate = juce::ByteOrder::littleEndianInt(format + 4);
            align = juce::ByteOrder::littleEndianShort(format + 12);
            out.bits = juce::ByteOrder::littleEndianShort(format + 14);
            file.seekg(std::streamoff(size - 16) + (size & 1), std::ios::cur);
            continue;
        }
        if (!std::memcmp(chunk, "data", 4)) {
            if (align) out.frames = size / align;
            out.data = file.tellg();
            return out;
        }
        file.seekg(std::streamoff(size) + (size & 1), std::ios::cur);
    }
    return out;
}

// A print's first two channels as a stereo 32-bit float WAV (a print of a kit with pad sends carries a pair a bus
// after them, model/print.h; the MPC plays two). False when it is not a float WAV of more channels.
bool stereo_print(const std::string& source, const std::string& target) {
    const Wav in = wav_of(source);
    const bool floats = in.format == 3 || in.format == 0xFFFE;
    if (!floats || in.bits != 32 || in.channels <= 2 || !in.data) return false;
    std::ifstream from(source, std::ios::binary);
    std::ofstream to(target, std::ios::binary | std::ios::trunc);
    if (!from || !to) return false;
    from.seekg(in.data);
    auto word = [&](uint32_t v, int bytes) { for (int b = 0; b < bytes; ++b) to.put(char((v >> (8 * b)) & 0xff)); };
    const uint32_t data_bytes = uint32_t(in.frames * 2 * 4);
    to.write("RIFF", 4); word(36 + data_bytes, 4); to.write("WAVEfmt ", 8);
    word(16, 4); word(3, 2); word(2, 2); word(in.rate, 4); word(in.rate * 8, 4); word(8, 2); word(32, 2);
    to.write("data", 4); word(data_bytes, 4);
    std::vector<char> frame(size_t(in.channels) * 4);
    for (uint64_t f = 0; f < in.frames && from.read(frame.data(), std::streamsize(frame.size())); ++f) to.write(frame.data(), 8);
    return bool(to);
}

bool wav_named(const std::string& path) {
    auto kind = fs::path(path).extension().string();
    for (auto& c : kind) c = char(std::tolower(static_cast<unsigned char>(c)));
    return kind == ".wav";
}
bool is_wav(const SampleRef& ref) { return ref.used() && !ref.instrument() && wav_named(ref.path); }

// The project's WAV files, each copied once into the data folder under a name of its own.
class Files {
public:
    // `in_place`: the files are in the data folder already, under their own names (a track file written beside a
    // program's folder): nothing is copied.
    explicit Files(fs::path f, bool in_place = false) : folder(std::move(f)), in_place(in_place) {}
    // The copy's name in the data folder for the file at `source` (named after `stem` when given; a print of more
    // than two channels made stereo); empty when the file is not there, so nothing names it (MPC 3 stops on opening
    // a project that names a sample it cannot find).
    const std::string& name(const std::string& source, Mpc3Export& report, const std::string& own_stem = {}) {
        auto found = names.find(source);
        if (found != names.end()) return found->second;
        std::error_code missing_error;
        if (!fs::exists(source, missing_error)) { ++report.missing; return names[source] = std::string(); }
        if (in_place) {
            // Under the name the folder has it by: a program names "X.WAV" and a card finds "X.wav" for it, where an
            // MPC's own drive would not.
            std::string own = fs::path(source).filename().string();
            if (listed.empty()) {
                std::error_code list_error;
                for (const auto& entry : fs::directory_iterator(folder, list_error)) {
                    std::string name = entry.path().filename().string(), key = name;
                    for (auto& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
                    listed[key] = name;
                }
            }
            std::string key = own;
            for (auto& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (auto there = listed.find(key); there != listed.end()) own = there->second;
            taken.insert(own);
            infos[own] = wav_of(source);
            ++report.samples;
            order.push_back(own);
            return names[source] = own;
        }
        auto stem = own_stem.empty() ? fs::path(source).stem().string() : own_stem;
        std::string file = stem + ".wav";
        for (unsigned n = 2; taken.count(file); ++n) file = stem + " " + std::to_string(n) + ".wav";
        taken.insert(file);
        if (!stereo_print(source, (folder / file).string()) && !copy_card_file(source, folder / file)) {
            ++report.missing;
            return names[source] = std::string();
        }
        infos[file] = wav_of((folder / file).string());
        ++report.samples;
        order.push_back(file);
        return names[source] = file;
    }
    const Wav& info(const std::string& file) { return infos[file]; }
    // The copy's name for a file already copied; empty when it was not (or is not there).
    std::string copied(const std::string& source) const {
        auto found = names.find(source);
        return found == names.end() ? std::string() : found->second;
    }
    // The MPC's entry for a file: the project's and a program's samples lists.
    static juce::var sample_entry(const std::string& file) {
        return juce::JSON::parse(juce::String("{\"version\": 1, \"name\": ") + juce::JSON::toString(juce::String(fs::path(file).stem().string())) +
                                 ", \"path\": " + juce::JSON::toString(juce::String(file)) +
                                 ", \"loadImpl\": 0, \"metadata\": {\"tempo\": 0.0, \"rootNote\": 60, \"tune\": 0.0, \"key\": \"\"}}");
    }
    const std::vector<std::string>& all() const { return order; }
private:
    fs::path folder;
    bool in_place = false;
    std::map<std::string, std::string> listed; // in place: the folder's files by their names in lower case
    std::map<std::string, std::string> names;
    std::map<std::string, Wav> infos;
    std::set<std::string> taken;
    std::vector<std::string> order;
};

// One layer as the MPC plays it: a region of a WAV file (its own frames), the velocities that play it, its tune
// over what the pad or keygroup gives, its level and pan, and the key it sounds at its own pitch.
struct Layer {
    std::string source;              // the file on the card
    int64_t start = 0, end = -1;     // frames of the file; end -1: its end
    int64_t loop_start = -1;         // -1: the start
    bool loop = false, reverse = false;
    uint8_t low_velocity = 1, high_velocity = 127;
    int cents = 0;                   // over the pad's or keygroup's tune
    double gain_db = 0, pan = 0;     // pan -1..1
    int root = played_root;          // MIDI
};

// A keygroup: keys and the layers they play.
struct Keygroup {
    uint8_t low_key = 0, high_key = 127;
    bool one_shot = false;
    int attack = 0, release = 0;      // its zones' envelope over the pad's, in the pad's steps (keygroups over the
                                      // same keys with a release each: a fall of two stages, engine/release_fit.h)
    int velocity = 0;                 // its zones' Velocity over the pad's, percent
    bool mono = false;                // on the mono side of a split: Mono, in the side's mute group
    std::vector<Layer> layers;
};

// A Rhythmer frame (44.1 kHz) as a frame of a file at `rate`.
int64_t file_frame(uint32_t at, unsigned rate) { return std::llround(at * double(rate ? rate : dsp::model_rate) / dsp::model_rate); }

// A WAV pad's layers: its own region and its layers', in the file's frames.
std::vector<Layer> wav_layers(const Project& project, const PadSound& pad, Mpc3Export& report) {
    std::vector<Layer> out;
    for (unsigned l = 0; l < pad_layers; ++l) {
        const auto sound = pad.layer(l);
        if (!sound.used()) continue;
        const auto& ref = project.samples[sound.sample % sample_slots];
        if (!is_wav(ref)) { ++report.skipped_pads; continue; }
        const unsigned rate = wav_of(ref.path).rate;
        Layer layer;
        layer.source = ref.path;
        layer.start = file_frame(sound.start, rate);
        layer.end = sound.end ? file_frame(sound.end, rate) : -1;
        layer.loop = pad.loop;
        layer.loop_start = file_frame(pad.loop_lock ? sound.start : pad.loop_start, rate);
        layer.reverse = pad.reverse;
        layer.low_velocity = l ? sound.low : 0; // the MPC's first layer starts at 0, as it saves one
        layer.high_velocity = sound.high;
        layer.cents = l ? sound.pitch * 100 + sound.fine : 0;
        layer.gain_db = (l ? sound.gain : pad.gain) / 100.0;
        layer.pan = l ? sound.pan / 50.0 : 0;
        out.push_back(std::move(layer));
    }
    return out;
}

// An instrument's zones as layers: the first of each round robin, no release samples, WAV files only.
std::vector<std::pair<SampleZone, Layer>> instrument_layers(const std::string& path, const SfzRead& read, Mpc3Export& report) {
    std::vector<std::pair<SampleZone, Layer>> out;
    for (const auto& region : read.regions) {
        const auto& zone = region.zone;
        if (zone.on_release || zone.sequence > 1) continue;
        std::string source = fs::path(region.sample).is_absolute() ? region.sample : instrument_zone_file(path, region.sample);
        if (source.empty() || !wav_named(source)) { ++report.skipped_pads; continue; }
        Layer layer;
        layer.source = source;
        layer.start = std::max<int64_t>(region.offset, 0);
        layer.end = region.end;
        layer.loop = zone.loop != ZoneLoop::Off;
        layer.loop_start = layer.loop ? int64_t(zone.loop_start) : -1;
        layer.low_velocity = zone.low_velocity;
        layer.high_velocity = zone.high_velocity;
        layer.cents = zone.tune;
        layer.gain_db = zone.gain;
        layer.pan = zone.pan;
        layer.root = zone.root;
        out.emplace_back(zone, std::move(layer));
    }
    return out;
}

// A layer written into the MPC's layer object.
void fill_layer(const juce::var& layer, const Layer& from, const std::string& file, const Wav& wav, bool first, bool keyed) {
    put(layer, "active", true);
    put(layer, "sampleName", juce::String(fs::path(file).stem().string()));
    put(layer, "sampleFile", juce::String(file));
    put(layer, "direction", whole(from.reverse ? 1 : 0));
    put(layer, "loopMode", whole(from.loop ? 1 : 0));
    if (keyed) put(layer, "rootNote", whole(std::clamp(from.root, 0, 127) + 1)); // one past the MIDI note, as the MPC writes it
    put(layer, "velocityStart", whole(first ? 0 : from.low_velocity));
    put(layer, "velocityEnd", whole(from.high_velocity));
    const int semis = int(std::lround(from.cents / 100.0));
    put(layer, "coarseTune", whole(std::clamp(semis, -36, 36)));
    put(layer, "fineTune", whole(std::clamp(from.cents - semis * 100, -99, 99)));
    put(layer, "pan", real(std::clamp(0.5 + from.pan / 2, 0.0, 1.0)));
    if (from.gain_db != 0) put(field(layer, "volume"), "gainCoefficient", real(std::pow(10.0, from.gain_db / 20)));
    const int64_t end = from.end >= 0 ? from.end : int64_t(wav.frames);
    const auto& slice = field(layer, "sliceInfo");
    put(slice, "Start", whole(int(from.start)));
    put(slice, "End", whole(int(end)));
    put(slice, "LoopStart", whole(int(from.loop_start >= 0 ? from.loop_start : from.start)));
    put(slice, "LoopMode", whole(from.loop ? 1 : 0));
}

// Writes the layers into an instrument's layer list (at most eight); how many went in.
unsigned fill_layers(const juce::var& instrument, const std::vector<Layer>& layers, bool keyed, Files& files, std::vector<std::string>& used,
                     Mpc3Export& report) {
    const auto* list = field(instrument, "layersv").getArray();
    if (!list) return 0;
    unsigned written = 0;
    for (const auto& layer : layers) {
        if (written == unsigned(list->size()) || written == mpc_layers) { ++report.skipped_pads; continue; }
        const auto& file = files.name(layer.source, report);
        if (file.empty()) continue; // not on the card: left out
        fill_layer(list->getReference(int(written)), layer, file, files.info(file), written == 0, keyed);
        if (std::find(used.begin(), used.end(), file) == used.end()) used.push_back(file);
        ++written;
    }
    return written;
}

// The pad's sound as the MPC's pad or keygroup has it: level, pan, tune, envelope, filter.
void fill_sound(const juce::var& instrument, const PadSound& pad, bool muted, int coarse, bool one_shot) {
    put(instrument, "coarseTune", whole(std::clamp(coarse, -36, 36)));
    put(instrument, "fineTune", whole(pad.fine));
    put(instrument, "monophonic", pad.mono);
    put(instrument, "polyphony", whole(pad.mono ? 1 : 0)); // MPC 3 makes a pad of polyphony 1 mono again; poly is 0, as it saves one
    put(instrument, "whichMuteGroup", whole(pad.group));
    put(instrument, "loopLock", pad.loop_lock);
    const auto& mix = field(instrument, "mixable");
    put(mix, "volume", real(mpc_volume(pad.level)));
    put(mix, "pan", real(mpc_pan_of(pad.pan)));
    put(mix, "mute", muted);
    const auto& synth = field(instrument, "synthSection");
    put(synth, "velocitySensitivity", real(fraction(pad.velocity_sense)));
    const auto& amp = field(synth, "ampEnvelope");
    put(field(amp, "Attack"), "value0", real(fraction(pad.attack)));
    put(field(amp, "Decay"), "value0", real(fraction(pad.decay)));
    put(field(amp, "Sustain"), "value0", real(fraction(pad.sustain)));
    put(field(amp, "Release"), "value0", real(fraction(pad.release)));
    put(field(amp, "DecayFromEnd"), "value0", !pad.decay_from_start);
    put(field(amp, "OneShot"), "value0", one_shot);
    const auto& filter = field(field(synth, "filterData"), "value0");
    put(filter, "filterType", whole(mpc_filter_type(pad.filter)));
    put(filter, "filterCutoff", real(fraction(pad.cutoff)));
    put(filter, "filterResonance", real(fraction(pad.resonance)));
    put(filter, "filterEnvelopeAmount", real(fraction(pad.filter_amount)));
    const auto& filter_env = field(synth, "filterEnvelope");
    put(field(filter_env, "Attack"), "value0", real(fraction(pad.filter_attack)));
    put(field(filter_env, "Decay"), "value0", real(fraction(pad.filter_decay)));
    put(field(filter_env, "Sustain"), "value0", real(fraction(pad.filter_sustain)));
    put(field(filter_env, "Release"), "value0", real(fraction(pad.filter_release)));
}

// The pad's warp as the MPC's: Warp on, locked to the sequence's tempo (Rhythmer's Seq) or stretched by a percentage,
// from the tempo the region was played at (its own, else what its # Beats over its length make, else the project's).
void fill_warp(const juce::var& instrument, const PadSound& pad, const Layer& region, const Wav& wav, double project_bpm) {
    put(instrument, "warpEnable", pad.warped());
    if (!pad.warped()) return;
    put(instrument, "bpmLock", pad.warp == warp_seq);
    put(instrument, "stretchPercentage", whole(pad.warp >= warp_min ? pad.warp : 100));
    double bpm = project_bpm;
    const int64_t end = region.end >= 0 ? region.end : int64_t(wav.frames);
    if (pad.own_tempo) bpm = pad.own_tempo / 10.0;
    else if (pad.beats && wav.rate && end > region.start) bpm = pad.beats * 60.0 * wav.rate / double(end - region.start);
    put(instrument, "tempo", real(bpm));
}

// Every "track" a Q-Link or pad assignment targets that is not one of `names` pointed at `first`, as MPC 3 does
// itself on loading when the name is gone; with no names, every one.
void retarget_tracks(const juce::var& node, const std::set<std::string>& names, const juce::String& first) {
    if (auto* object = node.getDynamicObject()) {
        bool gone = false;
        for (const auto& property : object->getProperties()) {
            if (property.name.toString() == "track" && property.value.isString()) {
                const auto target = property.value.toString().toStdString();
                gone = !target.empty() && !names.count(target); // an empty name targets no track
            } else
                retarget_tracks(property.value, names, first);
        }
        if (gone) object->setProperty("track", first);
    } else if (const auto* list = node.getArray()) {
        for (const auto& item : *list) retarget_tracks(item, names, first);
    }
}

// A name for each written track: its own, else the fallback; never one another track or a bus has.
std::string unique_name(const std::string& own, const std::string& fallback, std::set<std::string>& taken) {
    std::string base = own.empty() ? fallback : own;
    std::string name = base;
    for (unsigned n = 2; taken.count(name); ++n) name = base + " " + std::to_string(n);
    taken.insert(name);
    return name;
}

// What a written track plays of Rhythmer's: a kit track's pads (`pad` -1) or one pad as a keygroup track.
struct Written {
    unsigned index = 0;    // Rhythmer's track
    int pad = -1;          // a keygroup track's pad; -1 the kit, or an Audio track of prints (`audio`)
    bool shifted = false;  // a keygroup track's notes: 60 + the pad's pitch + the note's (an instrument), else 60 + the note's
    std::string name;
    juce::var track;
    bool audio = false;    // the track's prints (or `takes`)
    bool takes = false;    // an AUDIO track's: its patterns' takes (freeze plan F3)
    bool keyed = false;    // a KEYGROUP track's: its notes MIDI notes (60 + the tune's semitones on an instrument)
};


// A pad's sounds as MPC 3 gets them: a drum pad's layers, or keygroups. An instrument's program keeps its Mono and
// Portamento (engine/sfz.h SfzRead) on the keygroup track it becomes.
struct PadPlan {
    std::vector<Layer> drum;
    std::vector<Keygroup> keys;
    bool instrument = false, mono = false, legato = false, tied = false;
    bool mono_all = false; // Mono on every key (else on one side of a split: its keygroups carry it)
    double glide = 0;
};

// An instrument's zones as keygroups, by their keys in the order they come (128 at most).
void plan_keygroups(std::vector<std::pair<SampleZone, Layer>>& zones, const SfzRead& read, PadPlan& plan, Mpc3Export& report) {
    plan.instrument = true;
    plan.mono = read.mono; plan.legato = read.legato; plan.tied = read.tied; plan.glide = read.glide;
    plan.mono_all = read.mono && read.mono_low == 0 && read.mono_high == 127;
    for (auto& [zone, layer] : zones) {
        auto same = std::find_if(plan.keys.begin(), plan.keys.end(), [&](const Keygroup& k) {
            return k.low_key == zone.low_key && k.high_key == zone.high_key && k.attack == zone.attack && k.release == zone.release
                && k.velocity == zone.velocity;
        });
        if (same == plan.keys.end()) {
            if (plan.keys.size() == mpc_pads) { ++report.skipped_pads; continue; }
            Keygroup made;
            made.low_key = zone.low_key;
            made.high_key = zone.high_key;
            made.one_shot = zone.one_shot;
            made.attack = zone.attack; made.release = zone.release;
            made.velocity = zone.velocity;
            made.mono = read.mono && !plan.mono_all && zone.low_key >= read.mono_low && zone.high_key <= read.mono_high;
            plan.keys.push_back(made);
            same = plan.keys.end() - 1;
        }
        same->layers.push_back(std::move(layer));
    }
}

// The MPC's samples list of a program or a project.
juce::var samples_of(const std::vector<std::string>& used) {
    std::vector<juce::var> list;
    for (const auto& file : used) list.push_back(Files::sample_entry(file));
    return array_of(std::move(list));
}

// A pad's keygroups as MPC 3's Keygroup track `track_name`, a copy of the template's (its mix and mute are the
// caller's); void when none of them has a file. `pad`: the pad they play on (level, tune, envelope, filter).
juce::var keygroup_track(const juce::var& doc, const PadPlan& plan, const PadSound& pad, const std::string& track_name, double tempo,
                         Files& files, Mpc3Export& report) {
    juce::var made = field(doc, "keygroupTrack").clone();
    put(made, "name", juce::String(track_name));
    const auto& program = field(made, "program");
    put(program, "name", juce::String(track_name));
    juce::Array<juce::var> instruments;
    std::vector<std::string> used;
    unsigned groups = 0;
    for (unsigned k = 0; k < mpc_pads; ++k) {
        juce::var instrument = field(doc, "keygroupPad").clone();
        if (k < plan.keys.size()) {
            const auto& group = plan.keys[k];
            if (fill_layers(instrument, group.layers, true, files, used, report)) {
                // An instrument's notes carry the pad's pitch in their keys; a WAV pad's keygroup carries it in its tune.
                fill_sound(instrument, pad, false, plan.instrument ? 0 : pad.pitch, group.one_shot);
                put(instrument, "lowNote", whole(group.low_key));
                put(instrument, "highNote", whole(group.high_key));
                put(instrument, "triggerMode", whole(group.one_shot ? 0 : 2));
                if (plan.instrument) {
                    // An instrument's attack and release as the MPC's 0..1 for the seconds Rhythmer plays them
                    // in (its zones' own stages over the pad's): what read_keygroup() made of them, back.
                    const auto& amp = field(field(instrument, "synthSection"), "ampEnvelope");
                    const float attack = time_seconds(std::clamp(pad.attack + group.attack, 0, 100)) * 0.3f;
                    const float release = time_seconds(std::clamp(pad.release + group.release, 0, 100)) * 0.5f + 0.003f;
                    put(field(amp, "Attack"), "value0", real(xpm_envelope_value(attack)));
                    put(field(amp, "Release"), "value0", real(xpm_envelope_value(release)));
                    // Its own Velocity (a split's sides follow velocity each their own way).
                    if (group.velocity)
                        put(field(instrument, "synthSection"), "velocitySensitivity", real(fraction(uint8_t(std::clamp(int(pad.velocity_sense) + group.velocity, 0, 100)))));
                    // A split's mono side: MPC 3's program is mono or poly as a whole, so each of the side's
                    // keygroups is Mono and they share a mute group: a note there cuts the last.
                    if (group.mono) {
                        put(instrument, "monophonic", true);
                        put(instrument, "polyphony", whole(1));
                        put(instrument, "whichMuteGroup", whole(1));
                    }
                }
                if (!plan.instrument) {
                    const auto& first = group.layers.front();
                    fill_warp(instrument, pad, first, files.info(files.name(first.source, report)), tempo);
                }
                ++groups;
            }
        }
        instruments.add(instrument);
    }
    if (!groups) return juce::var();
    put(field(program, "drum"), "instruments", juce::var(instruments));
    put(field(program, "keygroup"), "numKeygroups", whole(int(groups)));
    if (plan.mono && plan.mono_all) { // MPC 3's Mono and Portamento, as the program read had them
        const auto& voices = field(program, "drum");
        put(voices, "monophonic", true);
        put(voices, "portamentoTime", real(mpc_portamento_value(plan.glide)));
        put(voices, "portamentoLegato", plan.legato);
        put(voices, "monoRetrigger", !plan.tied); // Mono Retrig: a key pressed while one is held starts the note again
    }
    put(program, "samples", samples_of(used));
    retarget_tracks(made, {}, juce::String(track_name));
    return made;
}

// The template's five header lines (its kind `kind`: a project's, a track's), then {"data": ...}, gzipped, as `out`.
std::string write_document(const juce::var& doc, const char* kind, const juce::var& data, const fs::path& out) {
    juce::String text;
    if (const auto* header = field(doc, "header").getArray())
        for (int n = 0; n < header->size(); ++n) text << (n == 2 ? juce::String(kind) : header->getReference(n).toString()) << "\n";
    auto root = object();
    put(root, "data", data);
    text << juce::JSON::toString(root);
    fs::path temporary = out;
    temporary += ".part";
    {
        juce::FileOutputStream file(juce::File(temporary.string()));
        if (!file.openedOk()) return "Cannot write " + out.string();
        file.setPosition(0);
        file.truncate();
        juce::GZIPCompressorOutputStream packed(file, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        const auto utf8 = text.toUTF8();
        if (!packed.write(utf8.getAddress(), utf8.sizeInBytes() - 1)) return "Cannot write " + out.string();
        packed.flush();
    }
    std::error_code error;
    fs::rename(temporary, out, error);
    if (error) return "Cannot write " + out.string();
    return {};
}

} // namespace

std::string export_mpc3_project(const Project& project, const PatternBank& patterns, const std::string& folder,
                                const std::string& name, Mpc3Export& report, const PrintTable* prints, const std::string& prints_dir,
                                const std::string& audio_dir) {
    report = Mpc3Export {};
    report.mixer = mixer_left_out(project, true); // strips, a submix's name, sends and chain, bus mutes: not carried
    const juce::var doc = read_template();
    const auto& data = field(doc, "data");
    if (!data.isObject() || !field(doc, "keygroupTrack").isObject()) return "MPC 3 template unreadable";
    const fs::path where(folder);
    const fs::path data_folder = where / (name + "_[ProjectData]");
    std::error_code error;
    fs::create_directories(data_folder, error);
    if (error) return "Cannot make " + data_folder.string();

    Files files(data_folder);
    std::set<std::string> taken;
    std::vector<std::pair<std::string, juce::var>> buses; // name, track
    if (const auto* list = field(doc, "buses").getArray())
        for (const auto& bus : *list) {
            buses.emplace_back(bus["name"].toString().toStdString(), bus.clone());
            taken.insert(buses.back().first);
        }
    const auto& pad_notes = field(field(field(doc, "drumTrack"), "program"), "padNoteMap")["noteForPad"];
    auto program_mix = [](const juce::var& program, const Mix& mix) {
        const auto& mixable = field(program, "mixable");
        put(mixable, "volume", real(mpc_volume(mix.level)));
        put(mixable, "pan", real(mpc_pan_of(mix.pan)));
        put(mixable, "mute", mix.mute);
        put(mixable, "solo", mix.solo);
    };

    std::vector<Written> written;
    for (unsigned t = 0; t < track_count; ++t) {
        const auto& track = project.tracks[t];
        if (!track.used()) continue;
        if (track.engine == EngineKind::Audio) { // freeze plan F3: an Audio track of MPC 3's own, its takes in its program
            juce::var made = field(doc, "audioTrack").clone();
            char fallback[16];
            std::snprintf(fallback, sizeof(fallback), "Audio %03u", t + 1);
            const std::string track_name = unique_name(track.name, fallback, taken);
            put(made, "name", juce::String(track_name));
            put(made, "mute", track.mix.mute);
            const auto& program = field(made, "program");
            put(program, "name", juce::String(track_name));
            program_mix(program, track.mix);
            std::vector<std::string> used;
            for (const auto& pattern : patterns.at[t]) {
                if (!pattern.audio.used() || audio_dir.empty()) continue;
                const auto& file = files.name((fs::path(audio_dir) / audio_take_name(pattern.audio.take)).string(), report);
                if (!file.empty() && std::find(used.begin(), used.end(), file) == used.end()) used.push_back(file);
            }
            put(program, "samples", samples_of(used));
            retarget_tracks(made, {}, juce::String(track_name));
            Written w;
            w.index = t;
            w.name = track_name;
            w.track = made;
            w.audio = w.takes = true;
            written.push_back(std::move(w));
            ++report.audio;
            continue;
        }
        if (track.engine == EngineKind::Plugin && prints && !prints_dir.empty()) { // its prints, as an Audio track
            bool any = false;
            for (const auto& r : prints->records) any = any || (r.track == t && !r.file.empty());
            if (any) {
                juce::var made = field(doc, "audioTrack").clone();
                char fallback[16];
                std::snprintf(fallback, sizeof(fallback), "Audio %03u", t + 1);
                const std::string track_name = unique_name(track.name, fallback, taken);
                put(made, "name", juce::String(track_name));
                put(made, "mute", track.mix.mute);
                const auto& program = field(made, "program");
                put(program, "name", juce::String(track_name));
                // A print is made at print_mix()'s level and pan; the track plays it at its own relative to those.
                const Mix at = print_mix(track);
                Mix relative = track.mix;
                const double db = volume_db(track.mix.level) - volume_db(at.level);
                relative.level = 100;
                program_mix(program, relative);
                put(field(program, "mixable"), "volume", real(std::isfinite(db) ? std::pow(10.0, (db - level_top_db) / 40) : 0.0));
                put(field(program, "mixable"), "pan", real(std::clamp(mpc_pan_of(track.mix.pan - at.pan), 0.0, 1.0)));
                std::vector<std::string> used;
                for (const auto& r : prints->records)
                    if (r.track == t && !r.file.empty()) {
                        char stem[64];
                        std::snprintf(stem, sizeof(stem), "%s %s", track_name.c_str(), print_pattern_name(r.pattern).c_str());
                        const auto& file = files.name((fs::path(prints_dir) / r.file).string(), report, stem);
                        if (!file.empty() && std::find(used.begin(), used.end(), file) == used.end()) used.push_back(file);
                    }
                put(program, "samples", samples_of(used));
                retarget_tracks(made, {}, juce::String(track_name));
                Written w;
                w.index = t;
                w.name = track_name;
                w.track = made;
                w.audio = true;
                written.push_back(std::move(w));
                ++report.audio;
                continue;
            }
        }
        if (track.engine == EngineKind::Midi) { // a MIDI track: MPC 3's own, on its channel, with its bank and program
            using namespace midi_track;
            juce::var made = field(doc, "midiTrack").clone();
            char fallback[16];
            std::snprintf(fallback, sizeof(fallback), "MIDI %03u", t + 1);
            const std::string track_name = unique_name(track.name, fallback, taken);
            put(made, "name", juce::String(track_name));
            put(made, "mute", track.mix.mute);
            put(field(made, "program"), "name", juce::String(track_name));
            // The port stays MPC 3's own choice (<none> until picked there): Rhythmer's ports are not its ports.
            // Channels count from 0 there (MPC 3 shows a stored 4 as channel 5).
            put(field(made, "midiOutputRoute"), "outputChannel", whole(std::clamp<int>(track.params[Channel], 1, 16) - 1));
            const auto& sound = field(made, "midiBankAndProgramNumber");
            const int msb = track.params[BankMsb], lsb = track.params[BankLsb], program = track.params[Program]; // 0 Off, else n + 1
            put(sound, "midiBankEnable", msb != 0 || lsb != 0);
            put(sound, "midiBankMsb", whole(std::max(msb - 1, 0)));
            put(sound, "midiBankLsb", whole(std::max(lsb - 1, 0)));
            put(sound, "midiProgramNumberEnable", program != 0);
            put(sound, "midiProgramNumber", whole(std::max(program - 1, 0)));
            retarget_tracks(made, {}, juce::String(track_name));
            Written w;
            w.index = t;
            w.name = track_name;
            w.track = made;
            w.keyed = true; // its notes are MIDI notes
            written.push_back(std::move(w));
            ++report.midi;
            continue;
        }
        if (!track.sampled()) { ++report.skipped_tracks; continue; }
        // A KEYGROUP track (model/project.h): its one sound, pad 1, a keygroup whatever it is, its notes MIDI notes.
        const bool keyed = track.engine == EngineKind::Keygroup;

        // Which pads the kit plays and which become keygroup tracks.
        std::array<bool, sampler_pads> pitched {};
        std::array<bool, sampler_pads> noted {};
        if (keyed) pitched[0] = noted[0] = true;
        else
            for (const auto& pattern : patterns.at[t])
                for (const auto& e : pattern.events)
                    if (e.key >= sampler_first_note && e.key < sampler_first_note + sampler_pads) {
                        noted[e.key - sampler_first_note] = true;
                        pitched[e.key - sampler_first_note] = pitched[e.key - sampler_first_note] || e.pitch;
                    }
        std::array<PadPlan, sampler_pads> plans;
        for (unsigned n = 0; n < (keyed ? 1u : sampler_pads); ++n) {
            const auto& pad = track.pads[n];
            if (!pad.used()) continue;
            const auto& ref = project.samples[pad.sample % sample_slots];
            auto& plan = plans[n];
            if (!ref.instrument()) {
                auto layers = wav_layers(project, pad, report);
                if (layers.empty()) continue;
                if (!pitched[n]) { plan.drum = std::move(layers); continue; }
                Keygroup all; // played at other pitches: one keygroup over every key, its own pitch at 60
                all.one_shot = pad.play == PadPlay::OneShot;
                all.layers = std::move(layers);
                if (keyed) for (auto& layer : all.layers) layer.root = keyed_root(project, track); // a KEYGROUP track: its note

                plan.keys.push_back(std::move(all));
                continue;
            }
            std::string why, file, which;
            const auto read = read_instrument(ref.path, &why);
            auto zones = instrument_layers(ref.path, read, report);
            if (zones.empty()) { ++report.skipped_pads; continue; }
            const bool every_key = std::all_of(zones.begin(), zones.end(), [](const auto& z) { return z.first.low_key == 0 && z.first.high_key == 127; });
            if (!keyed && (split_program_pad(ref.path, file, which) || every_key) && zones.size() <= mpc_layers && !pitched[n]) {
                for (auto& [zone, layer] : zones) { // a pad's layers: each at the pitch the pad's key plays it
                    if (zone.keytrack) layer.cents += (played_root - zone.root) * zone.keytrack;
                    layer.root = played_root;
                    plan.drum.push_back(std::move(layer));
                }
                continue;
            }
            plan_keygroups(zones, read, plan, report);
        }

        // The kit: its pads, unless all of them went to keygroup tracks and no note is left for it.
        bool kit_wanted = false;
        for (unsigned n = 0; n < sampler_pads; ++n) kit_wanted = kit_wanted || !plans[n].drum.empty() || (noted[n] && plans[n].keys.empty());
        if (kit_wanted) {
            juce::var made = field(doc, "drumTrack").clone();
            char fallback[16];
            std::snprintf(fallback, sizeof(fallback), "Drum %03u", unsigned(written.size() + 1));
            const std::string track_name = unique_name(track.name, fallback, taken);
            put(made, "name", juce::String(track_name));
            put(made, "mute", track.mix.mute);
            const auto& program = field(made, "program");
            put(program, "name", juce::String(track_name));
            program_mix(program, track.mix);
            juce::Array<juce::var> instruments;
            std::vector<std::string> used;
            for (unsigned n = 0; n < mpc_pads; ++n) {
                juce::var instrument = field(doc, "drumPad").clone();
                // Into its program, as MPC 3's own kits' pads (destination 0; the template's pads, from an imported
                // MPC Sample project, say 1, which MPC 3 reads as Submix 1, past the track's own channel).
                const auto& route = field(field(instrument, "mixable"), "audioRoute");
                put(route, "destination", whole(0));
                if (n < sampler_pads && !plans[n].drum.empty() && fill_layers(instrument, plans[n].drum, false, files, used, report)) {
                    const auto& pad = track.pads[n];
                    if (pad.out) { // sent to a submix: MPC 3's pad output (destination 1, the submix's index)
                        put(route, "destination", whole(1));
                        put(route, "audioRouteSubIndex", whole(int((pad.out - 1u) % sub_count)));
                    }
                    fill_sound(instrument, pad, track.pad_muted(n), pad.pitch, pad.play == PadPlay::OneShot);
                    put(instrument, "triggerMode", whole(pad.play == PadPlay::OneShot ? 0 : 1));
                    const auto& first = plans[n].drum.front();
                    fill_warp(instrument, pad, first, files.info(files.name(first.source, report)), project.tempo / 10.0);
                    ++report.pads;
                }
                instruments.add(instrument);
            }
            put(field(program, "drum"), "instruments", juce::var(instruments));
            put(program, "samples", samples_of(used));
            retarget_tracks(made, {}, juce::String(track_name));
            written.push_back({t, -1, false, track_name, made, false});
        }

        // Each keygroup pad as a Keygroup track.
        for (unsigned n = 0; n < sampler_pads; ++n) {
            const auto& plan = plans[n];
            if (plan.keys.empty()) continue;
            const auto& pad = track.pads[n];
            char label[4], fallback[48];
            pad_label(n, label);
            std::snprintf(fallback, sizeof(fallback), "%s %s", track.name[0] ? track.name : "Keys", label);
            const std::string own = keyed ? std::string(track.name) : std::string(project.samples[pad.sample % sample_slots].name);
            const std::string track_name = unique_name(own, fallback, taken);
            juce::var made = keygroup_track(doc, plan, pad, track_name, project.tempo / 10.0, files, report);
            if (made.isVoid()) continue;
            put(made, "mute", track.mix.mute || track.pad_muted(n));
            program_mix(field(made, "program"), track.mix);
            Written w {t, int(n), plan.instrument, track_name, made};
            w.keyed = keyed; // by name: given by position it went to `takes`, the field added before it
            written.push_back(std::move(w));
            ++report.keygroups;
        }
    }
    if (written.empty()) return "Nothing to export";
    for (const auto& w : written) report.tracks += w.pad < 0 && !w.audio && project.tracks[w.index].engine != EngineKind::Midi;

    // The mixer (rhythmer-sample docs/MIXER_PLAN.md): a track sent to a submix goes to MPC 3's (its audioRoute
    // destination 1, the submix's index; 2 is Out 1/2), and each submix a track plays into gets its level, pan, mute
    // and solo, on its bus track and in the mixer's list, as MPC 3 keeps both.
    std::array<bool, sub_count> sub_used {};
    for (const auto& w : written) { // a kit's pads sent to submixes (put on their instruments above)
        const auto& track = project.tracks[w.index];
        if (w.pad >= 0 || w.audio || track.engine != EngineKind::Sampler) continue;
        for (const auto& pad : track.pads) if (pad.used() && pad.out) sub_used[(pad.out - 1u) % sub_count] = true;
    }
    for (const auto& w : written) {
        const auto& track = project.tracks[w.index];
        if (!track.mix.out || track.engine == EngineKind::Midi) continue;
        const unsigned s = (track.mix.out - 1u) % sub_count;
        const auto& route = field(field(field(w.track, "program"), "mixable"), "audioRoute");
        put(route, "destination", whole(1));
        put(route, "audioRouteSubIndex", whole(int(s)));
        sub_used[s] = true;
    }
    const auto* submix_list = field(field(data, "mixer"), "submixes").getArray();
    for (unsigned s = 0; s < sub_count; ++s) {
        if (!sub_used[s]) continue;
        const auto& mix = project.subs[s].mix;
        const std::string bus_name = "Submix " + std::to_string(s + 1);
        for (auto& b : buses)
            if (b.first == bus_name) { put(b.second, "mute", mix.mute); program_mix(field(b.second, "program"), mix); }
        if (submix_list && int(s) < submix_list->size()) program_mix((*submix_list)[int(s)], mix);
    }

    // Every name, as MPC 3 keys its clips: sorted, the buses among them.
    std::vector<std::string> names;
    for (const auto& w : written) names.push_back(w.name);
    for (const auto& b : buses) names.push_back(b.first);
    std::sort(names.begin(), names.end());

    // The pads' notes: pad n plays the MPC's note for pad n.
    auto note_of_pad = [&](unsigned pad) {
        const auto& v = pad_notes[juce::Identifier("value" + juce::String(pad))];
        return v.isVoid() ? int((sampler_first_note + pad) % 128) : int(v);
    };
    // Which pads went to keygroup tracks, by Rhythmer's track: their notes leave the kit.
    std::map<unsigned, std::set<int>> moved;
    for (const auto& w : written) if (w.pad >= 0 && !w.audio) moved[w.index].insert(w.pad);

    // A sequence: each written track's pattern (a track's numbered pattern, or its lane of the song laid out) and, for
    // an Audio track, where its prints play. Every clip is the sequence's length, its pattern written out again each
    // time it loops: MPC 3 plays a track's clip once through the sequence.
    const TimeSignature signature = project.time_signature();
    const int beat_pulses = int(3840 / signature.unit);
    const int bar_pulses = beat_pulses * signature.beats;
    struct Placement { int at = 0, length = 0; const PrintRecord* record = nullptr; const AudioRegion* take = nullptr; };
    auto make_sequence = [&](int key, const std::string& title, unsigned tempo, int pulses,
                             const std::function<const Pattern&(const Written&)>& pattern_of,
                             const std::function<std::vector<Placement>(const Written&)>& prints_of) {
        const int bars = std::max(1, (pulses + bar_pulses - 1) / bar_pulses);
        juce::var sequence = field(doc, "sequence").clone();
        put(sequence, "name", juce::String(title));
        put(sequence, "bpm", real(tempo / 10.0));
        put(sequence, "lengthBars", whole(bars));
        put(sequence, "loopStartBar", whole(0));
        put(sequence, "loopEndBar", whole(bars));
        put(sequence, "lengthPulses", whole(pulses)); // a pattern of part of a bar keeps its length; the bars round up
        put(sequence, "loopStartPulses", whole(0));
        put(sequence, "loopEndPulses", whole(pulses));
        auto signature_list = [&] {
            auto one = object();
            put(one, "beatsPerBar", whole(signature.beats));
            put(one, "beatLength", whole(beat_pulses));
            put(one, "barStart", whole(0));
            auto list = object();
            put(list, "timeSignatures", array_of({one}));
            return list;
        };
        put(sequence, "timeSignatureTrack", signature_list());

        std::vector<juce::var> clips;
        for (const auto& track_name : names) {
            juce::var clip = field(doc, "clip").clone();
            put(clip, "name", juce::String(track_name));
            put(clip, "timeSignatureList", signature_list());
            const int clip_pulses = pulses;
            juce::Array<juce::var> events;
            auto own = std::find_if(written.begin(), written.end(), [&](const Written& w) { return w.name == track_name; });
            if (own != written.end() && own->audio) { // its prints: the clip spans the sequence, as MPC 3 saves an audio clip
                for (const auto& place : prints_of(*own)) {
                    if (const auto* take = place.take) { // an audio track's take, as recorded: its part of the file, level, fades
                        const auto& file = files.name((fs::path(audio_dir) / audio_take_name(take->take)).string(), report);
                        if (file.empty()) continue;
                        const auto& wav = files.info(file);
                        const double k = double(wav.rate ? wav.rate : 44100) / 44100.0; // the take counts 44.1 kHz frames
                        juce::var event = field(doc, "audioEvent").clone();
                        put(event, "time", whole(place.at));
                        put(event, "muted", false);
                        const auto& audio = field(event, "audio");
                        const double volume = double(audio["volume"]) * std::pow(10.0, take->gain / 200.0);
                        put(audio, "sample", Files::sample_entry(file));
                        put(audio, "name", juce::String(fs::path(file).stem().string()));
                        put(audio, "tempo", real(take->tempo / 10.0));
                        put(audio, "volume", real(volume));
                        put(audio, "reverse", take->reverse);
                        put(audio, "fadeIn", whole(int(std::llround(take->fade_in * k))));
                        put(audio, "fadeOut", whole(int(std::llround(take->fade_out * k))));
                        put(audio, "sliceStart", whole(int(std::llround(take->offset * k))));
                        const bool to_end = take->offset + take->frames >= take->take_frames && wav.frames; // its file's own end, exact
                        put(audio, "sliceEnd", whole(to_end ? int(wav.frames) : int(std::llround((uint64_t(take->offset) + take->frames) * k))));
                        put(audio, "warpEnable", take->warp); // MPC 3's own Warp at the tempo it was recorded at
                        put(audio, "coarseTune", whole(take->semi));
                        put(audio, "fineTune", whole(take->fine));
                        put(audio, "length", whole(place.length));
                        events.add(event);
                        continue;
                    }
                    const auto* record = place.record;
                    const auto& file = files.name((fs::path(prints_dir) / record->file).string(), report);
                    if (file.empty()) continue;
                    const auto& wav = files.info(file);
                    const unsigned rate = wav.rate ? wav.rate : record->rate;
                    // One pass of the pattern (Loop and Cut prints are that long; a Ring print's tail is cut where it loops).
                    const int64_t pass = std::llround(record->pass * double(rate) / dsp::model_rate);
                    const int64_t end = std::min<int64_t>(int64_t(wav.frames ? wav.frames : record->frames), pass > 0 ? pass : INT64_MAX);
                    juce::var event = field(doc, "audioEvent").clone();
                    put(event, "time", whole(place.at));
                    put(event, "muted", false);
                    const auto& audio = field(event, "audio");
                    put(audio, "sample", Files::sample_entry(file));
                    put(audio, "name", juce::String(fs::path(file).stem().string()));
                    put(audio, "tempo", real(tempo / 10.0));
                    put(audio, "sliceStart", whole(0));
                    put(audio, "sliceEnd", whole(int(end)));
                    put(audio, "warpEnable", false); // printed at the pattern's tempo
                    put(audio, "length", whole(place.length));
                    events.add(event);
                }
            } else if (own != written.end()) {
                const auto& pattern = pattern_of(*own);
                const auto& pads = project.tracks[own->index].pads;
                // A pattern shorter than the sequence plays again until it ends, as Rhythmer loops each track's pattern.
                const int pass_pulses = int(std::max<uint32_t>(pattern.length, 1) * mpc_ticks_per_tick);
                for (int round = 0; round < pulses; round += pass_pulses)
                for (const auto& e : pattern.events) {
                    if (e.tick >= pattern.length) continue;
                    if (!own->keyed && (e.key < sampler_first_note || e.key >= sampler_first_note + mpc_pads)) continue;
                    if (round + int(e.tick * mpc_ticks_per_tick) >= pulses) break;
                    const int pad = e.key - int(sampler_first_note);
                    int note_key;
                    if (own->keyed) { // the note itself; an instrument's tune moves its keys, a WAV's is in its keygroup
                        note_key = std::clamp(int(e.key) + (own->shifted ? pads[0].pitch : 0), 0, 127);
                    } else if (own->pad < 0) {
                        if (moved[own->index].count(pad)) continue;
                        note_key = note_of_pad(unsigned(pad));
                    } else {
                        if (pad != own->pad) continue;
                        note_key = std::clamp(played_root + (own->shifted ? pads[size_t(pad)].pitch : 0) + e.pitch, 0, 127);
                    }
                    juce::var event = field(doc, "noteEvent").clone();
                    put(event, "time", whole(round + int(e.tick * mpc_ticks_per_tick)));
                    put(event, "muted", false);
                    const auto& note = field(event, "note");
                    put(note, "note", whole(note_key));
                    put(note, "velocity", real(std::clamp<int>(e.velocity, 1, 127) / 127.0));
                    put(note, "length", whole(int(std::max<unsigned>(e.length, 1) * mpc_ticks_per_tick)));
                    put(note, "probability", whole(std::clamp<int>(e.chance, 0, 100)));
                    put(note, "ratchet", whole(int(e.hits())));
                    events.add(event);
                    ++report.notes;
                }
            }
            put(clip, "endPulses", whole(clip_pulses));
            put(clip, "loopStartPulses", whole(0));
            put(clip, "loopEndPulses", whole(clip_pulses));
            put(field(clip, "eventList"), "events", juce::var(events));
            clips.push_back(entry(juce::String(track_name), clip));
        }
        put(sequence, "trackClipMaps", array_of({array_of(std::move(clips))}));
        return entry(whole(key), sequence);
    };
    // What an Audio track plays of `pattern`: an audio track's take, or a plug-in track's print.
    auto has_audio = [&](const Written& w, unsigned pattern) {
        if (w.takes) return patterns.at[w.index][pattern % pattern_count].audio.used();
        return w.audio && prints && prints->find(w.index, pattern);
    };
    // A print (or take) of `pattern` from `from` to `to` (pulses), again each time the pattern loops.
    auto loops_of = [&](const Written& w, unsigned pattern, int from, int to, std::vector<Placement>& out) {
        const PrintRecord* record = nullptr;
        const AudioRegion* take = nullptr;
        if (w.takes) {
            take = &patterns.at[w.index][pattern % pattern_count].audio;
            if (!take->used()) return false;
        } else {
            record = prints ? prints->find(w.index, pattern) : nullptr;
            if (!record || record->file.empty()) return false;
        }
        const int pass_pulses = int(std::max<uint32_t>(patterns.at[w.index][pattern % pattern_count].length, 1) * mpc_ticks_per_tick);
        for (int at = from; at < to; at += pass_pulses) {
            int length = std::min(pass_pulses, to - at);
            if (take) // a take shorter than the pattern ends where it does
                length = std::min<int>(length, int(frames_to_ticks(take->frames, take->tempo) * mpc_ticks_per_tick));
            out.push_back({at, length, record, take});
        }
        return true;
    };

    // Sequences: all 128, the highest first, as MPC 3 saves them. Each writes the pattern it keeps for a track, nothing
    // where it keeps the track off, and pattern n where it keeps nothing for the track (model/scene_links.h).
    static const Pattern silent {};
    auto kept_of = [&](const Written& w, unsigned n) { return export_pattern(project, w.index, n); };
    std::vector<juce::var> numbered(pattern_count);
    for (int p = int(pattern_count) - 1; p >= 0; --p) {
        uint32_t longest = 0;
        bool any = false;
        for (const auto& w : written) {
            const int kept = kept_of(w, unsigned(p));
            if (kept < 0) continue;
            const auto& pattern = patterns.at[w.index][size_t(kept)];
            longest = std::max(longest, pattern.length);
            any = any || !pattern.events.empty() || has_audio(w, unsigned(kept));
        }
        report.sequences += any;
        const int pulses = int(std::max<uint32_t>(longest, 1) * mpc_ticks_per_tick);
        char title[24];
        std::snprintf(title, sizeof(title), "Sequence %02d", p + 1);
        const unsigned tempo = project.seq_tempo && project.pattern_tempo[size_t(p)] ? project.pattern_tempo[size_t(p)] : project.tempo;
        numbered[size_t(p)] = make_sequence(p, title, tempo, pulses,
            [&](const Written& w) -> const Pattern& {
                const int kept = kept_of(w, unsigned(p));
                return kept < 0 ? silent : patterns.at[w.index][size_t(kept)];
            },
            [&](const Written& w) {
                std::vector<Placement> out;
                const int kept = kept_of(w, unsigned(p));
                if (kept >= 0 && loops_of(w, unsigned(kept), 0, pulses, out)) ++report.regions;
                return out;
            });
    }

    // The song, laid out as one sequence in the first pattern number nothing uses (as Song -> new pattern does,
    // shell/song_export.cpp): each track's lane its clips' patterns end to end, each Audio track its clips' prints.
    const Song& song = project.song;
    int song_key = -1;
    const unsigned song_bars = song.length();
    if (song_bars) {
        for (unsigned p = 0; p < pattern_count && song_key < 0; ++p) {
            bool free = true;
            for (const auto& w : written) free = free && patterns.at[w.index][p].empty() && !has_audio(w, p);
            for (unsigned n = 0; n < song.count && free; ++n) free = song.clips[n].pattern != p;
            if (free) song_key = int(p);
        }
        if (song_key < 0) report.song_left_out = true;
    }
    if (song_key >= 0) {
        const uint32_t bar_ticks = project.bar_ticks();
        std::map<unsigned, Pattern> lanes;
        bool full = false;
        for (const auto& w : written)
            if (!w.audio && !lanes.count(w.index)) flatten_lane(song, w.index, patterns.at[w.index], bar_ticks, song_bars, lanes[w.index], full);
        report.song_bars = song_bars;
        report.song_full = full;
        report.song_tempo_left_out = !song.tempo.empty();
        numbered[size_t(song_key)] = make_sequence(song_key, "Song", project.tempo, int(song_bars * bar_ticks * mpc_ticks_per_tick),
            [&](const Written& w) -> const Pattern& { return lanes[w.index]; },
            [&](const Written& w) {
                std::vector<Placement> out;
                for (unsigned n = 0; n < song.count; ++n) {
                    const auto& clip = song.clips[n];
                    if (clip.track != w.index || clip.muted) continue;
                    loops_of(w, clip.pattern, int(clip.bar * bar_ticks * mpc_ticks_per_tick), int(clip.end() * bar_ticks * mpc_ticks_per_tick), out);
                }
                return out;
            });
    }
    juce::Array<juce::var> sequences;
    for (int p = int(pattern_count) - 1; p >= 0; --p) sequences.add(numbered[size_t(p)]);

    // The project: its tracks, the buses after them, sequences, samples and tempo. Each track as long as the sequence
    // playing (as MPC 3's own keygroup tracks and buses are): the drum and audio tracks it saved were two bars long
    // and looped their first two bars in a longer sequence.
    juce::Array<juce::var> tracks;
    for (const auto& w : written) {
        put(w.track, "length", whole(0));
        put(w.track, "lengthFollowsSequenceLength", true);
        tracks.add(w.track);
    }
    for (const auto& b : buses) tracks.add(b.second);
    put(data, "tracks", juce::var(tracks));
    put(data, "sequences", juce::var(sequences));
    // Each copied file's chop, in the WAV as the MPC keeps it ("atem" and "slic"), so Chop on the MPC finds the slices.
    for (unsigned n = 0; n < sample_slots; ++n) {
        if (!is_wav(project.samples[n]) || !project.chops[n].used()) continue;
        const auto file = files.copied(project.samples[n].path);
        if (!file.empty() && write_mpc_slices((data_folder / file).string(), project.chops[n], false)) ++report.chops;
    }
    put(data, "samples", samples_of(files.all()));
    put(data, "masterTempoEnabled", !project.seq_tempo);
    put(data, "masterTempo", real(project.tempo / 10.0));
    // The song is what plays first when there is one, else the pattern the first track plays.
    put(data, "currentSequence", whole(song_key >= 0 ? song_key : int(project.tracks[written.front().index].pattern % pattern_count)));
    put(data, "currentTrackIndex", whole(0));
    std::vector<juce::var> transport;
    for (const auto& track_name : names) {
        auto start = object();
        put(start, "key", whole(0));
        put(start, "value", whole(0));
        transport.push_back(entry(juce::String(track_name), array_of({start})));
    }
    put(field(data, "clipPlayerData"), "trackClipTransportMap", array_of(std::move(transport)));
    put(field(data, "info"), "title", juce::String(name));
    retarget_tracks(data, std::set<std::string>(names.begin(), names.end()), juce::String(written.front().name));

    return write_document(doc, "SerialisableProjectData", data, where / (name + ".xpj"));
}

std::string write_mpc3_track(const std::string& program, const std::string& file) {
    const juce::var doc = read_template();
    if (!field(doc, "keygroupTrack").isObject() || !field(doc, "keygroupPad").isObject()) return "MPC 3 template unreadable";
    const fs::path out(file), data = fs::path(program).parent_path();
    if (data.filename().string() != out.stem().string() + "_[TrackData]" || data.parent_path() != out.parent_path())
        return "The program is not in " + out.stem().string() + "_[TrackData]";
    Mpc3Export report;
    std::string why;
    const SfzRead read = read_instrument(program, &why);
    auto zones = instrument_layers(program, read, report);
    if (zones.empty()) return why.empty() ? std::string("No keygroups") : why;
    PadPlan plan;
    plan_keygroups(zones, read, plan, report);
    // The pad as a Load lands the program (shell/sample_files.cpp): poly, the envelope most of its zones have.
    PadSound pad;
    pad.mono = false;
    const auto& e = read.envelope;
    if (e.attack) pad.attack = uint8_t(*e.attack > 0 ? sfz_attack_step(*e.attack) : 0);
    if (e.decay) pad.decay = uint8_t(sfz_decay_step(*e.decay));
    if (e.sustain) pad.sustain = uint8_t(sfz_percent_step(*e.sustain));
    if (e.release) pad.release = uint8_t(sfz_release_step(*e.release));
    pad.velocity_sense = uint8_t(std::clamp(read.velocity, 0.0, 100.0)); // its first keygroup's (the rest are over it)
    Files files(data, true);
    const juce::var made = keygroup_track(doc, plan, pad, out.stem().string(), 120.0, files, report);
    if (made.isVoid()) return "No keygroups";
    return write_document(doc, "SerialisableTrackData", made, out);
}

} // namespace rhythmer
