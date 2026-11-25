// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// Neural Amp Modeler (docs/RHYTHMER.md, Plug-ins: Neural Amp Modeler), the
// official plug-in built for the MPC without its editor, an optional plug-in
// beside Rhythmer's own Amp sim effect. Its Model and Cab parameters pick files
// from two folders under its home. Its parameters are left as the user sets
// them, Slim (an A2 capture's size) too; its pages are a page file in Rhythmer's library (plugin/page_library.h).
namespace rhythmer {

inline bool is_nam(const std::string& name) { return name == "NeuralAmpModeler"; }

// The folders it lists, under the plug-in home (Rhythmer/Plugin Data):
// "Neural Amp Modeler/Models" and ".../IRs", made if missing. The rack's
// thread only.
void nam_folders(const std::filesystem::path& home);

} // namespace rhythmer
