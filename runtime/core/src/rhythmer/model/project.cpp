// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/model/project.h"

namespace rhythmer {

// A Project's construction is optimized here once (project.h, Project()).
Project::Project() = default;

void PatternBank::clear() {
    for (auto& track : at)
        for (auto& pattern : track) pattern = Pattern {};
}

} // namespace rhythmer
