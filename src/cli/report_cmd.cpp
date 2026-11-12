// iw8r report <extracted dir> <map id> [--save file.json] [--diff old.json]
// Prints the build report of a compiled extraction (src/core/build_report.h), its budget warnings, and with --diff the
// changes since an earlier saved report. Exit 0, 1 when a native limit is exceeded or a file cannot be written or read,
// 2 on bad arguments.
#include <cstdio>
#include <string>

#include "build_report.h"

int cmdReport(int argc, char** argv) {
    const char* usage = "usage: iw8r report <extracted dir> <map id> [--save file.json] [--diff old.json]\n";
    if (argc < 4) {
        std::fprintf(stderr, "%s", usage);
        return 2;
    }
    const std::string extractedDir = argv[2], mapId = argv[3];
    std::string savePath, diffPath;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--save" && i + 1 < argc) savePath = argv[++i];
        else if (a == "--diff" && i + 1 < argc) diffPath = argv[++i];
        else {
            std::fprintf(stderr, "%s", usage);
            return 2;
        }
    }

    const iw8r::BuildReport report = iw8r::makeBuildReport(extractedDir, mapId);
    std::printf("%s", iw8r::formatBuildReport(report).c_str());

    int status = 0;
    for (const std::string& w : iw8r::budgetWarnings(report)) {
        const bool native = w.rfind("native limit:", 0) == 0;
        if (native) status = 1;
        std::printf("%s budget: %s\n", native ? "error" : "warning", w.c_str());
    }

    if (!diffPath.empty()) {
        iw8r::BuildReport old;
        std::string err;
        if (!iw8r::loadBuildReport(diffPath, old, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::printf("changes since %s:\n%s", diffPath.c_str(), iw8r::diffBuildReports(old, report).c_str());
    }

    if (!savePath.empty()) {
        std::string err;
        if (!iw8r::saveBuildReport(report, savePath, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::printf("saved %s\n", savePath.c_str());
    }
    return status;
}
