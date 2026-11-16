#pragma once
// Where iw8r-acts finds the Replay schema (data/mw19/schema.json): beside the exe (the package's data folder), else in
// the first data/mw19 folder above it (a development build in bin/<mode> uses the repository's data folder). The fork
// read only <exe folder>/data/mw19/schema.json; tools/patches/acts_sync_fork.py points the handler here.

#include <filesystem>

#include <utils/utils.hpp>

namespace iw8r {

inline std::filesystem::path ReplaySchemaPath() {
    const std::filesystem::path rel = std::filesystem::path("data") / "mw19" / "schema.json";
    std::filesystem::path dir = utils::GetProgDir();
    for (int up = 0; up < 5 && !dir.empty(); ++up) {
        if (std::filesystem::exists(dir / rel)) return dir / rel;
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return utils::GetProgDir() / rel;
}

}  // namespace iw8r
