// iw8r preflight <extracted dir> <map id> [--data <dataDir>]
// Checks a compiled extraction folder before it is shipped (src/core/build_preflight.h). Prints one line per issue as
// `error|warning <category>: <message>`. Exit 0 when there are no errors, 1 when there are, 2 on bad arguments or when
// the catalogs cannot be read.
#include <cstdio>
#include <string>

#include "build_preflight.h"

int cmdPreflight(int argc, char** argv) {
    const char* usage = "usage: iw8r preflight <extracted dir> <map id> [--data <dataDir>]\n";
    if (argc < 4) {
        std::fprintf(stderr, "%s", usage);
        return 2;
    }
    const std::string extractedDir = argv[2], mapId = argv[3];
    std::string dataDir = "data";
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) {
            dataDir = argv[++i];
        } else {
            std::fprintf(stderr, "%s", usage);
            return 2;
        }
    }

    iw8r::PreflightCatalogs cat;
    std::string err;
    if (!iw8r::loadPreflightCatalogs(dataDir, cat, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }

    const std::vector<iw8r::PreflightIssue> issues = iw8r::preflightExtraction(extractedDir, mapId, cat);
    size_t errors = 0, warnings = 0;
    for (const iw8r::PreflightIssue& issue : issues) {
        if (issue.error) ++errors;
        else ++warnings;
        std::printf("%s %s: %s\n", issue.error ? "error" : "warning", issue.category.c_str(), issue.message.c_str());
    }
    std::printf("preflight %s: %zu error(s), %zu warning(s)\n", mapId.c_str(), errors, warnings);
    return errors ? 1 : 0;
}
