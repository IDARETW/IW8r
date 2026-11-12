// iw8r stock-prefabs <world_cache dir> <out dir> [--min-count N] [--max-per-category N] [--status <iw8_models_status.tsv>]
// Harvests the prefab library from the shipped IW8 maps in a world_cache folder (src/core/stock_prefabs.h): every zone
// folder with an assets/gfx_map export is read, the library is written to <out dir>/<category>/*.map with index.tsv and
// README.md. Defaults: min-count 1, max-per-category 400, status data/asset_catalog/iw8_models_status.tsv (relative to
// the working directory). Exit 0 on success, 1 on a read or write error, 2 on bad arguments.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "stock_prefabs.h"

int cmdStockPrefabs(int argc, char** argv) {
    const char* usage =
        "usage: iw8r stock-prefabs <world_cache dir> <out dir> [--min-count N] [--max-per-category N] "
        "[--status <iw8_models_status.tsv>]\n";
    if (argc < 4) {
        std::fprintf(stderr, "%s", usage);
        return 2;
    }
    const std::string cacheDir = argv[2], outDir = argv[3];
    iw8r::StockPrefabOptions opt;
    std::string status = "data/asset_catalog/iw8_models_status.tsv";
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--min-count" && i + 1 < argc) {
            const long v = std::atol(argv[++i]);
            if (v < 1) {
                std::fprintf(stderr, "--min-count must be at least 1\n");
                return 2;
            }
            opt.minCount = static_cast<size_t>(v);
        } else if (a == "--max-per-category" && i + 1 < argc) {
            const long v = std::atol(argv[++i]);
            if (v < 1) {
                std::fprintf(stderr, "--max-per-category must be at least 1\n");
                return 2;
            }
            opt.maxPerCategory = static_cast<size_t>(v);
        } else if (a == "--status" && i + 1 < argc) {
            status = argv[++i];
        } else {
            std::fprintf(stderr, "%s", usage);
            return 2;
        }
    }

    std::string err;
    if (!iw8r::loadStockModelStatus(status, opt.okModels, &err) || opt.okModels.empty()) {
        std::fprintf(stderr, "error: no ok models in %s%s%s\n", status.c_str(), err.empty() ? "" : ": ", err.c_str());
        return 1;
    }

    std::vector<std::string> warnings;
    std::vector<iw8r::StockZone> zones;
    if (!iw8r::readStockWorldCache(cacheDir, zones, &warnings, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    for (const auto& w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
    if (zones.empty()) {
        std::fprintf(stderr, "error: no zone with an assets/gfx_map export in %s\n", cacheDir.c_str());
        return 1;
    }

    std::printf("zones processed (%zu):\n", zones.size());
    for (const auto& z : zones) std::printf("  %s: %zu instances\n", z.name.c_str(), z.instances.size());

    const iw8r::StockPrefabLibrary lib = iw8r::harvestStockPrefabs(zones, opt);
    iw8r::StockWriteStats st;
    if (!iw8r::writeStockPrefabLibrary(outDir, lib, &err, &st)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    std::map<std::string, std::pair<size_t, size_t>> perCat;  // category -> singles, clusters
    for (const auto& p : lib.prefabs) {
        auto& c = perCat[p.category];
        if (p.kind == "single") ++c.first;
        else ++c.second;
    }
    std::printf("prefabs per category (singles / clusters, bytes):\n");
    for (const auto& [cat, c] : perCat) {
        const auto b = st.bytesPerCategory.count(cat) ? st.bytesPerCategory.at(cat) : size_t(0);
        std::printf("  %-10s %5zu / %5zu  %9zu bytes\n", cat.c_str(), c.first, c.second, b);
    }
    std::printf("instances seen %zu (not ok %zu, no category %zu)\n", lib.instancesSeen, lib.instancesSkipped,
                lib.instancesUncategorised);
    std::printf("wrote %zu prefabs, %zu bytes to %s (index.tsv, README.md)\n", st.files, st.bytes, outDir.c_str());
    size_t dropped = 0;
    for (const auto& [cat, n] : lib.droppedPerCategory) dropped += n;
    if (dropped) std::printf("%zu candidates dropped by --max-per-category\n", dropped);
    return 0;
}
