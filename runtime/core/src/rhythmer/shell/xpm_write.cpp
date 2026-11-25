// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/xpm_write.h"
#include "rhythmer/engine/keygroup.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace rhythmer {
namespace fs = std::filesystem;

namespace {
std::string escaped(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}
std::string number(double value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.6f", value);
    return text;
}
// Lines of tags at one indent.
struct Tags {
    std::string& out;
    int indent;
    void operator()(const char* name, const std::string& value) {
        out += std::string(size_t(indent), ' ') + "<" + name + ">" + value + "</" + name + ">\n";
    }
    void operator()(const char* name, int value) { (*this)(name, std::to_string(value)); }
    void operator()(const char* name, double value) { (*this)(name, number(value)); }
    void flag(const char* name, bool value) { (*this)(name, std::string(value ? "True" : "False")); }
};

// The program's pads (MPC 2.10 on): a keygroup program's sixteen pads all play the universal pad.
std::string program_pads() {
    std::string json = "{\n    \"ProgramPads-v2.10\": {\n        \"Universal\": {\n            \"value0\": true\n        },\n"
                       "        \"Type\": {\n            \"value0\": 2\n        },\n        \"universalPad\": 8947848,\n"
                       "        \"pads\": {\n";
    for (unsigned n = 0; n < 128; ++n) json += "            \"value" + std::to_string(n) + "\": 0" + (n < 127 ? ",\n" : "\n");
    json += "        },\n        \"UnusedPads\": {\n            \"value0\": 0\n        }\n    }\n}";
    return escaped(json);
}

void layer_text(std::string& s, unsigned number_, const XpmLayer* l, uint8_t root, double gain) {
    s += "            <Layer number=\"" + std::to_string(number_) + "\">\n";
    Tags t {s, 14};
    const int last = l && l->frames ? int(l->frames) - 1 : 0; // the slice's last frame, as the MPC keeps it
    const bool loop = l && l->looped();
    t.flag("Active", true);
    t("Volume", gain);
    t("Pan", 0.5);
    t("Pitch", 0.0);
    t("TuneCoarse", 0);
    t("TuneFine", 0);
    t("VelStart", l ? int(l->low_velocity) : 0);
    t("VelEnd", l ? int(l->high_velocity) : 127);
    t("SampleStart", 0);
    t("SampleEnd", 0);
    t("LoopStart", loop ? int(l->loop_start) : 0);
    t("LoopEnd", loop ? int(l->loop_end) : 0);
    t("LoopCrossfadeLength", 0);
    t("LoopTune", 0);
    t("RootNote", int(root) + 1); // one past the MIDI note, as the MPC writes it
    t.flag("KeyTrack", false);
    t("SampleName", l ? escaped(l->sample) : std::string());
    t("PitchRandom", 0.0);
    t("VolumeRandom", 0.0);
    t("PanRandom", 0.0);
    t("OffsetRandom", 0.0);
    t("SampleFile", std::string());
    t("SliceIndex", 128);
    t("Direction", 0);
    t("Offset", 0);
    t("SliceStart", 0);
    t("SliceEnd", loop ? int(l->loop_end) : last); // a loop runs to the slice's end and back
    t("SliceLoopStart", loop ? int(l->loop_start) : 0);
    t("SliceLoop", loop ? 1 : 0);
    t("SliceLoopCrossFadeLength", 0);
    t("SliceTailPosition", 0.5);
    t("SliceTailLength", 0.0);
    s += "            </Layer>\n";
}

void instrument_text(std::string& s, unsigned number_, const XpmKeygroup* k, double release, bool layered, bool mono_side, unsigned most) {
    s += "      <Instrument number=\"" + std::to_string(number_) + "\">\n";
    Tags t {s, 8};
    t.flag("CueBusEnable", false);
    for (const char* send : {"Send1", "Send2", "Send3", "Send4"}) t(send, 0.0);
    t("Volume", 0.707946); // 0 dB on the MPC's curve
    t.flag("Mute", false);
    t.flag("Solo", false);
    t("Pan", 0.5);
    t("AutomationFilter", 1);
    t("FilterKeytrack", 0.0);
    t("FilterType", 0); // off: the plug-in's sound as it was sampled
    t("Cutoff", 1.0);
    t("Resonance", 0.0);
    t("FilterEnvAmt", 0.0);
    t("AfterTouchToFilter", 0.0);
    t("VelocityToFilter", 0.0);
    t("VelocityToFilterEnvelope", 0.0);
    t("VelocityToStart", 0.0);
    t("VelocityToFilterAttack", 0.0);
    t("FilterAttack", 0.0);
    t("FilterDecay", 0.047244);
    t("FilterSustain", 1.0);
    t("FilterRelease", 0.0);
    t("FilterAttackCurve", 0.375);
    t("FilterDecayCurve", 0.375);
    t("FilterReleaseCurve", 0.375);
    t("FilterHold", 0.0);
    t.flag("FilterDecayType", true);
    t.flag("FilterADEnvelope", false);
    t("VolumeHold", 0.0);
    t.flag("VolumeDecayType", true);
    t.flag("VolumeADEnvelope", false);
    t("VolumeAttack", 0.0);
    t("VolumeDecay", 0.0);
    t("VolumeSustain", 1.0);
    t("VolumeRelease", xpm_envelope_value(k && k->release >= 0 ? k->release : release));
    t("VolumeAttackCurve", 0.375);
    t("VolumeDecayCurve", 0.375);
    t("VolumeReleaseCurve", 0.375);
    t("PitchHold", 0.0);
    t.flag("PitchDecayType", true);
    t.flag("PitchADEnvelope", false);
    t("PitchAttack", 0.0);
    t("PitchDecay", 0.047244);
    t("PitchSustain", 1.0);
    t("PitchRelease", 0.0);
    t("PitchAttackCurve", 0.375);
    t("PitchDecayCurve", 0.375);
    t("PitchReleaseCurve", 0.375);
    t("PitchEnvAmount", 0.5);
    t("VelocityToPitch", 0.0);
    t("VelocityToVolumeAttack", 0.0);
    // As the plug-in's level followed velocity; not heard: fully, or with velocity layers not at all (the layers
    // hold the dynamics).
    t("VelocitySensitivity", k && k->velocity >= 0 ? std::clamp(k->velocity, 0.0, 1.0) : layered ? 0.0 : 1.0);
    t("VelocityToPan", 0.0);
    t("RandomizationScale", 1.0);
    for (const char* random : {"AttackRandom", "DecayRandom", "CutoffRandom", "ResonanceRandom"}) t(random, 0.0);
    t("TuneCoarse", 0);
    t("TuneFine", 0);
    t.flag("Mono", mono_side); // a split's mono side: each keygroup Mono, all in one mute group
    t("Polyphony", mono_side ? 1 : 0);
    // An unused instrument, past the program's count, sits on one key as the MPC leaves them.
    const uint8_t root = k && !k->layers.empty() ? k->layers.front().root : 0;
    t("LowNote", k ? int(k->low) : 24);
    t("HighNote", k ? int(k->high) : 24);
    t.flag("IgnoreBaseNote", false);
    t("ZonePlay", k && k->cycle ? 0 : 1); // in turn (takes), else the layers its velocity reaches
    t("MuteGroup", mono_side ? 1 : 0);
    for (const char* mute : {"MuteTarget1", "MuteTarget2", "MuteTarget3", "MuteTarget4"}) t(mute, 0);
    for (const char* simult : {"SimultTarget1", "SimultTarget2", "SimultTarget3", "SimultTarget4"}) t(simult, 0);
    t("TriggerMode", k && k->one_shot ? 0 : 2); // one shot (a drum), else held while the key is
    t("WarpTempo", 120.0);
    t.flag("BpmLock", true);
    t.flag("WarpEnable", false);
    t("StretchPercentage", 100);
    s += "        <Layers>\n";
    const std::vector<XpmLayer> written = k ? xpm_layers(*k, most) : std::vector<XpmLayer> {};
    for (unsigned l = 0; l < std::max(4u, most); ++l) {
        const XpmLayer* layer = l < written.size() ? &written[l] : nullptr;
        layer_text(s, l + 1, layer, layer ? layer->root : k ? root : 0, layer && k ? k->gain * layer->gain : 1.0);
    }
    s += "        </Layers>\n";
    s += "      </Instrument>\n";
}
} // namespace

std::vector<XpmLayer> xpm_layers(const XpmKeygroup& keygroup, unsigned most) {
    const auto& all = keygroup.layers;
    most = std::max(1u, most);
    if (all.size() <= most) return all;
    std::vector<XpmLayer> out;
    const size_t n = all.size(), base = n / most, longer = n % most;
    for (size_t run = 0, first = 0; run < most; ++run) {
        const size_t last = first + base + (run < longer ? 1 : 0) - 1;
        XpmLayer layer = all[last];
        layer.low_velocity = all[first].low_velocity;
        out.push_back(std::move(layer));
        first = last + 1;
    }
    return out;
}

double xpm_envelope_value(double seconds) {
    // keygroup_seconds(): 0.001 * 100000^value
    if (seconds <= 0.001) return 0;
    return std::clamp(std::log10(seconds * 1000.0) / 5.0, 0.0, 1.0);
}

std::string xpm_text(const XpmProgram& program, unsigned most_layers) {
    std::string s;
    s += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n";
    s += "<MPCVObject>\n";
    s += "  <Version>\n";
    Tags v {s, 4};
    v("File_Version", std::string("2.1"));
    v("Application", std::string("MPC-V"));
    v("Application_Version", std::string("2.11.7.39"));
    v("Platform", std::string("Linux"));
    s += "  </Version>\n";
    s += "  <Program type=\"Keygroup\">\n";
    Tags t {s, 4};
    const size_t count = std::min<size_t>(program.keygroups.size(), 128);
    bool layered = false;
    for (const auto& k : program.keygroups) layered |= k.layers.size() > 1;
    t("ProgramName", escaped(program.name));
    t("ProgramPads-v2.10", program_pads());
    t.flag("CueBusEnable", false);
    s += "    <AudioRoute>\n";
    Tags r {s, 6};
    r("AudioRoute", 2);
    r("AudioRouteSubIndex", 0);
    r("AudioRouteChannelBitmap", 3);
    r.flag("InsertsEnabled", true);
    s += "    </AudioRoute>\n";
    for (const char* send : {"Send1", "Send2", "Send3", "Send4"}) t(send, 0.0);
    t("Volume", std::clamp(std::pow(10.0, (program.volume_db - 6.0) / 40.0), 0.0, 1.0)); // the MPC's curve: 0.707946 is 0 dB
    t.flag("Mute", false);
    t.flag("Solo", false);
    t("Pan", 0.5);
    t("AutomationFilter", 1);
    t("Pitch", 0.0);
    t("TuneCoarse", 0);
    t("TuneFine", 0);
    t.flag("Mono", program.mono_all());
    t("Program_Polyphony", 0);
    // The program's portamento is its Mono's. A mono side's is kept apart, for Rhythmer (an MPC would slide the poly
    // side's notes with it): its own tags, which an MPC passes over.
    t("PortamentoTime", program.mono_all() && program.glide > 0 ? mpc_portamento_value(program.glide) : 0.0);
    t.flag("PortamentoLegato", program.mono_all() && program.legato);
    t.flag("PortamentoQuantized", false);
    t.flag("MonoRetrigger", program.mono && !program.tied);
    if (program.mono && !program.mono_all()) {
        t("MonoSidePortamentoTime", program.glide > 0 ? mpc_portamento_value(program.glide) : 0.0);
        t.flag("MonoSidePortamentoLegato", program.legato);
    }
    t("Program.Xfader.Route", 0);
    t("KeygroupMasterTranspose", 0.5);
    t("KeygroupNumKeygroups", int(count));
    t("KeygroupPitchBendRange", 0.0);
    t("KeygroupWheelToLfo", 0.0);
    t("KeygroupAftertouchToFilter", 0.0);
    s += "    <Instruments>\n";
    for (unsigned n = 0; n < 128; ++n) instrument_text(s, n + 1, n < count ? &program.keygroups[n] : nullptr, program.release, layered,
                                                         n < count && program.mono_side(program.keygroups[n]), most_layers);
    s += "    </Instruments>\n";
    s += "    <PadNoteMap>\n";
    for (unsigned n = 0; n < 128; ++n)
        s += "      <PadNote number=\"" + std::to_string(n + 1) + "\">\n        <Note>" + std::to_string(n) + "</Note>\n      </PadNote>\n";
    s += "    </PadNoteMap>\n";
    s += "    <PadGroupMap>\n";
    for (unsigned n = 0; n < 128; ++n)
        s += "      <PadGroup number=\"" + std::to_string(n + 1) + "\">\n        <Group>0</Group>\n      </PadGroup>\n";
    s += "    </PadGroupMap>\n";
    s += "  </Program>\n";
    s += "</MPCVObject>\n";
    return s;
}

std::string write_xpm(const std::string& path, const XpmProgram& program, unsigned most_layers) {
    const std::string text = xpm_text(program, most_layers);
    const std::string part = path + ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) return "cannot write " + fs::path(path).filename().string();
        out.write(text.data(), std::streamsize(text.size()));
        if (!out) return "cannot write " + fs::path(path).filename().string();
    }
    std::error_code e;
    fs::rename(part, path, e);
    if (e) { fs::remove(part, e); return "cannot write " + fs::path(path).filename().string(); }
    return {};
}

} // namespace rhythmer
