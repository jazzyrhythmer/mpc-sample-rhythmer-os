// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <filesystem>
#include <fstream>
#include <string>

// Where the stem separator (shell/stems.h) is on the card: in the stems folder of the card folder the running app was
// copied from, which the base's sd-worker.sh writes to sd-folder beside the app's copy in RAM. That is
// autorun/rhythmer for a release, autorun/sample-base for `device.py install-sd`, and wherever a boot menu keeps the
// release (SampleBoot: autorun/mods/<name>/rhythmer). Without it (an app started by `device.py start`), the fixed
// folders: autorun/rhythmer, autorun/sample-base, then sample-base at the card's top for an install from before.
namespace rhythmer {

inline constexpr const char* stems_program_name = "rhythmer-stems";

// The program's path in `folder`/stems, empty when it is not there.
inline std::string stems_in(const std::filesystem::path& folder) {
    std::error_code error;
    const auto path = folder / "stems" / stems_program_name;
    return std::filesystem::is_regular_file(path, error) ? path.string() : std::string();
}

// The card folder the app in `app_folder` was copied from (install/sd-worker.sh), empty when it was not.
inline std::string release_sd_folder(const std::filesystem::path& app_folder) {
    std::ifstream in(app_folder / "sd-folder");
    std::string folder;
    if (!std::getline(in, folder) || folder.empty() || folder[0] != '/') return {};
    return folder;
}

// `card_top`: the card's top folder; `sd_folder`: the card folder the app was copied from (empty: not known). Empty
// when the program is in neither.
inline std::string find_stems_program(const std::string& card_top, const std::string& sd_folder = {}) {
    namespace fs = std::filesystem;
    if (!sd_folder.empty())
        if (auto path = stems_in(sd_folder); !path.empty()) return path;
    if (card_top.empty()) return {};
    for (const char* folder : {"autorun/rhythmer", "autorun/sample-base", "sample-base"})
        if (auto path = stems_in(fs::path(card_top) / folder); !path.empty()) return path;
    return {};
}

} // namespace rhythmer
