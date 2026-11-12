// IW8 Radiant Suite - `iw8r iw8d-spawns`: the compiled spawnList of an x64-zt map_ents dump (milestone 2.2).
//   iw8r iw8d-spawns <map_ents .iw8> [-o spawns.ents]                 dump the spawns as entity text (stdout without -o)
//   iw8r iw8d-spawns <map_ents .iw8> --set <spawns.ents> -o <out .iw8> write the entity text back as the spawnList
// Every spawn that the text does not change keeps its bytes in the container; see src/core/spawnlist.h.

#include <cstdio>
#include <string>
#include <vector>

#include "iw8_dump.h"
#include "map_text.h"
#include "spawnlist.h"

using namespace iw8r;

int cmdIw8dSpawns(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: iw8r iw8d-spawns <map_ents .iw8> [-o spawns.ents]\n"
                             "       iw8r iw8d-spawns <map_ents .iw8> --set <spawns.ents> -o <out .iw8>\n");
        return 2;
    }
    const std::string in = argv[2];
    std::string setFile, out;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--set" && i + 1 < argc) setFile = argv[++i];
        else if (a == "-o" && i + 1 < argc) out = argv[++i];
        else {
            std::fprintf(stderr, "error: unexpected argument %s\n", a.c_str());
            return 2;
        }
    }

    std::string raw, err;
    if (!readFile(in, raw)) {
        std::fprintf(stderr, "error: cannot read %s\n", in.c_str());
        return 1;
    }
    Iw8Dump d;
    if (!parseIw8Dump(raw, d, &err)) {
        std::fprintf(stderr, "error: %s: %s\n", in.c_str(), err.c_str());
        return 1;
    }
    std::vector<SpawnRecord> spawns;
    if (!readSpawnList(d, spawns, &err)) {
        std::fprintf(stderr, "error: %s: %s\n", in.c_str(), err.c_str());
        return 1;
    }

    if (setFile.empty()) {
        const std::string text = spawnsToEntityText(spawns);
        std::printf("spawnList     %zu spawns (container re-serialized %s)\n", spawns.size(),
                    serializeIw8Dump(d) == raw ? "byte-identical" : "DIFFERS");
        if (out.empty()) {
            std::fputs(text.c_str(), stdout);
            return 0;
        }
        if (!writeFileAtomic(out, text, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::printf("wrote         %s\n", out.c_str());
        return 0;
    }

    if (out.empty()) {
        std::fprintf(stderr, "error: --set needs -o <out .iw8>\n");
        return 2;
    }
    std::string text;
    if (!readFile(setFile, text)) {
        std::fprintf(stderr, "error: cannot read %s\n", setFile.c_str());
        return 1;
    }
    std::vector<SpawnRecord> edited;
    if (!spawnsFromEntityText(text, edited, &err)) {
        std::fprintf(stderr, "error: %s: %s\n", setFile.c_str(), err.c_str());
        return 1;
    }
    if (!writeSpawnList(d, edited, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const std::string bytes = serializeIw8Dump(d);
    // Read back what was written before reporting success.
    Iw8Dump check;
    std::vector<SpawnRecord> back;
    if (!parseIw8Dump(bytes, check, &err) || !readSpawnList(check, back, &err) || back.size() != edited.size()) {
        std::fprintf(stderr, "error: the written dump does not read back: %s\n", err.c_str());
        return 1;
    }
    if (!writeFileAtomic(out, bytes, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("wrote         %s (%zu spawns, was %zu; other spawns unchanged)\n", out.c_str(), edited.size(), spawns.size());
    return 0;
}
