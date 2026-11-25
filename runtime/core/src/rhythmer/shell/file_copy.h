// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <filesystem>
#include <fstream>

namespace rhythmer {
// Service/export threads only. Ordinary bounded I/O works across card and
// shared filesystems where copy_file may fail after creating an empty file.
// Publish a complete copy by rename, never a partial destination.
inline bool copy_card_file(const std::filesystem::path& source, const std::filesystem::path& target) {
    namespace fs = std::filesystem;
    const fs::path temporary = target.string() + ".rhythmer-copy";
    bool good = false;
    {
        std::ifstream in(source, std::ios::binary);
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        std::array<char, 65536> block;
        while (in && out) {
            in.read(block.data(), std::streamsize(block.size()));
            if (in.gcount()) out.write(block.data(), in.gcount());
        }
        const bool read_ok = in.eof() && !in.bad();
        out.flush(); out.close(); good = read_ok && bool(out);
    }
    std::error_code error;
    if (good) { fs::rename(temporary, target, error); if (!error) return true; }
    fs::remove(temporary, error);
    return false;
}
} // namespace rhythmer
