// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/shell/project_kits.h"
#include "rhythmer/engine/engine_info.h"
#include "rhythmer/model/scales.h"
#include <cstdio>

namespace rhythmer {
// A new project: no tracks (the track types open on track 1), every note in reach.
void starting_project(Project& project, PatternBank& patterns) {
    project = Project {};
    std::snprintf(project.name, sizeof(project.name), "Untitled");
    project.tempo = 1200;
    project.root = 0; project.scale = chromatic_scale; // C Chromatic
    patterns.clear();
}

int kit_track(const Project& from) {
    for (unsigned t = 0; t < track_count; ++t) {
        const auto& tr = from.tracks[t];
        if (tr.engine != EngineKind::Sampler) continue;
        for (const auto& pad : tr.pads) if (pad.used()) return int(t);
    }
    return -1;
}

KitImport import_kit(const Project& from, unsigned track, Track& into, const std::function<int(const SampleRef&)>& slot_for) {
    KitImport result;
    const auto& src = from.tracks[track % track_count];
    into.engine = EngineKind::Sampler;into.break_bars=src.break_bars;
    into.mods=src.mods;into.layout=PadLayout::Drums;
    into.preset = src.preset;
    into.params = src.params;
    into.name_from(src.name);
    into.pad_fx = src.pad_fx;
    into.pad_mutes = {};
    into.bank = 0; into.last_pad = 0; into.levels = false;
    std::array<int, sample_slots> mapped;
    mapped.fill(-2); // -2 not asked yet
    for (unsigned n = 0; n < sampler_pads; ++n) {
        PadSound pad = src.pads[n];
        if (pad.used()) {
            unsigned s = pad.sample % sample_slots;
            if (mapped[s] == -2) mapped[s] = from.samples[s].used() ? slot_for(from.samples[s]) : -1;
            if (mapped[s] < 0) { pad = PadSound {}; into.pad_fx.at[n] = Chain {{}, pad_slots}; ++result.missing; }
            else { pad.sample = uint16_t(mapped[s]); ++result.pads; }
        }
        for (auto& l : pad.layers) { // its layers' files too (roadmap P3); a layer without a slot is left out
            if (!l.used()) continue;
            unsigned s = l.sample % sample_slots;
            if (mapped[s] == -2) mapped[s] = from.samples[s].used() ? slot_for(from.samples[s]) : -1;
            if (mapped[s] < 0 || !pad.used()) l = PadLayer {};
            else l.sample = uint16_t(mapped[s]);
        }
        into.pads[n] = pad;
    }
    return result;
}

KitImport import_bank(const BankFile& from, unsigned bank, Track& into, const std::function<int(const SampleRef&)>& slot_for) {
    KitImport result;
    std::array<int, std::tuple_size_v<decltype(from.samples)>> mapped;
    mapped.fill(-2); // -2 not asked yet
    for (unsigned k = 0; k < bank_pads; ++k) {
        unsigned n = (bank % sampler_banks) * bank_pads + k;
        PadSound pad = from.pads[k];
        Chain chain = from.chains[k];
        if (pad.used()) {
            unsigned s = pad.sample % mapped.size();
            if (mapped[s] == -2) mapped[s] = from.samples[s].used() ? slot_for(from.samples[s]) : -1;
            if (mapped[s] < 0) { pad = PadSound {}; ++result.missing; }
            else { pad.sample = uint16_t(mapped[s]); ++result.pads; }
        }
        for (auto& l : pad.layers) { // layers (roadmap P3)
            if (!l.used()) continue;
            unsigned s = l.sample % mapped.size();
            if (mapped[s] == -2) mapped[s] = from.samples[s].used() ? slot_for(from.samples[s]) : -1;
            if (mapped[s] < 0 || !pad.used()) l = PadLayer {};
            else l.sample = uint16_t(mapped[s]);
        }
        if (!pad.used()) chain = Chain {{}, pad_slots};
        into.pads[n] = pad;
        into.pad_fx.at[n] = chain;
        into.set_pad_muted(n, false);
    }
    return result;
}

KitImport import_kit_bank(const Project& from, unsigned track, unsigned from_bank, unsigned bank, Track& into,
                          const std::function<int(const SampleRef&)>& slot_for) {
    KitImport result;
    const auto& src = from.tracks[track % track_count];
    std::array<int, sample_slots> mapped;
    mapped.fill(-2); // -2 not asked yet
    auto slot = [&](unsigned s) {
        s %= sample_slots;
        if (mapped[s] == -2) mapped[s] = from.samples[s].used() ? slot_for(from.samples[s]) : -1;
        return mapped[s];
    };
    for (unsigned k = 0; k < bank_pads; ++k) {
        const unsigned n = (from_bank % sampler_banks) * bank_pads + k, to = (bank % sampler_banks) * bank_pads + k;
        PadSound pad = src.pads[n];
        if (!pad.used()) continue; // an empty pad of the kit leaves this one as it is
        int s = slot(pad.sample);
        if (s < 0) { ++result.missing; continue; }
        pad.sample = uint16_t(s);
        for (auto& l : pad.layers) { // its layers' files too; a layer without a slot is left out
            if (!l.used()) continue;
            int ls = slot(l.sample);
            if (ls < 0) l = PadLayer {};
            else l.sample = uint16_t(ls);
        }
        if (into.pads[to].used()) ++result.replaced;
        into.pads[to] = pad;
        into.pad_fx.at[to] = src.pad_fx.at[n];
        into.set_pad_muted(to, false);
        ++result.pads;
    }
    into.drop_stale_trims();
    return result;
}

std::array<uint8_t, sampler_banks> kit_bank_pads(const Track& kit) {
    std::array<uint8_t, sampler_banks> count {};
    for (unsigned n = 0; n < sampler_pads; ++n) if (kit.pads[n].used()) ++count[n / bank_pads];
    return count;
}

} // namespace rhythmer
