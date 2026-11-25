// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include "rhythmer/model/project.h"
#include "rhythmer/model/project_file.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>

// The project Rhythmer starts with, and Load Kit (parity phase 10): a
// project's kit onto the selected track, its pads, their samples and effects,
// no patterns; a bank file takes one bank of it. Pure.
namespace rhythmer {

// The project Rhythmer starts with: every track empty, C Chromatic.
void starting_project(Project& project, PatternBank& patterns);

// Load Kit: the track of `from` whose kit is taken (the first kit with a sample on a pad), -1 none.
int kit_track(const Project& from);
// That kit onto `into`, which becomes a kit track with its pads, their effects and the kit's own values
// (its filter, Follow, Cloud); its patterns, mix and track chain are its own still. `slot_for` gives this
// project's sample slot for a file (-1: none free, the pad stays empty).
struct KitImport { unsigned pads = 0, missing = 0, replaced = 0; };
KitImport import_kit(const Project& from, unsigned track, Track& into, const std::function<int(const SampleRef&)>& slot_for);
// A bank file onto bank `bank` (0-7) of the kit `into`: its sixteen pads and their chains replace that
// bank's, their mutes cleared; the other banks, the kit's own values and its patterns stay.
KitImport import_bank(const BankFile& from, unsigned bank, Track& into, const std::function<int(const SampleRef&)>& slot_for);
// Bank `from_bank` (0-7) of the kit on `track` of `from` onto bank `bank` of the kit `into`: only the pads that hold a
// sample, each with its chain and colour, unmuted, onto the same pad of that bank; the bank's other pads, the other
// banks, the kit's own values and its patterns stay. `replaced`: pads of `into` that held a sample before.
KitImport import_kit_bank(const Project& from, unsigned track, unsigned from_bank, unsigned bank, Track& into,
                          const std::function<int(const SampleRef&)>& slot_for);
// How many pads of each bank of the kit on `track` hold a sample.
std::array<uint8_t, sampler_banks> kit_bank_pads(const Track& kit);

} // namespace rhythmer
