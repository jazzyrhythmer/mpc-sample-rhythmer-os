// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 jazzy rhythmer
#pragma once
#include "rhythmer/model/project.h"
#include <cstring>
namespace rhythmer {
inline bool rhythmer_kit_track(const Project& p, const Track& t) {
    if(t.engine!=EngineKind::Plugin || t.plugin>=p.plugins.size())return false;
    const auto& ref=p.plugins[t.plugin];
    return ref.used() && (!std::strcmp(ref.name,"Rhythmer Kit") || !std::strcmp(ref.name,"Groove Kit"));
}
}
