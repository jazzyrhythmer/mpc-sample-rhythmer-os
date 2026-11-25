// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/plugin/nam.h"
#include <algorithm>
#include <array>
#include <system_error>
#include <utility>

namespace rhythmer {
namespace fs = std::filesystem;

void nam_folders(const fs::path& home) {
    if (home.empty()) return;
    std::error_code error;
    fs::create_directories(home / "Neural Amp Modeler" / "Models", error);
    fs::create_directories(home / "Neural Amp Modeler" / "IRs", error);
}

} // namespace rhythmer
