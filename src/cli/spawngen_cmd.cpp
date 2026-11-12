// iw8r generate-spawns <in.map> -o <out.map> [--ffa N] [--starts N] [--spacing U] [--replace]
// Roadmap 6.18: adds a generated spawn set (core spawn_generator) to a map: FFA / TDM / DOM spawns spread over the
// walkable floors and team starts on opposite sides. --replace removes the map's existing spawn entities first.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "collision_world.h"
#include "document.h"
#include "spawn_generator.h"

using namespace iw8r;

int cmdGenerateSpawns(int argc, char** argv) {
    const char* usage =
        "usage: iw8r generate-spawns <in.map> -o <out.map> [--ffa N (16)] [--starts N per side (4)] [--spacing units (256)] [--replace]\n";
    std::string in, out;
    SpawnGenOptions opt;
    bool replace = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "--ffa" && i + 1 < argc) opt.ffaCount = std::atoi(argv[++i]);
        else if (a == "--starts" && i + 1 < argc) opt.teamStartsPerSide = std::atoi(argv[++i]);
        else if (a == "--spacing" && i + 1 < argc) opt.minSpacing = std::atof(argv[++i]);
        else if (a == "--replace") replace = true;
        else if (in.empty()) in = a;
        else {
            std::fputs(usage, stderr);
            return 2;
        }
    }
    if (in.empty() || out.empty()) {
        std::fputs(usage, stderr);
        return 2;
    }
    Document doc;
    std::string err;
    if (!doc.load(in, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (replace) {
        doc.deselectAll();
        for (const Entity& e : doc.map().entities) {
            const std::string c = e.classname();
            if (c == "info_player_start" || (c.rfind("mp_", 0) == 0 && c.find("spawn") != std::string::npos)) doc.select(SelRef{e.id, kNoId}, true);
        }
        if (!doc.selection().empty()) doc.deleteSelection();
    }
    CollisionWorld world;
    world.build(doc.map());
    AABB area;
    for (const Entity& e : doc.map().entities)
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) area.add(p->bounds());
    if (!area.valid()) {
        std::fprintf(stderr, "error: the map has no brushes or patches to stand on\n");
        return 1;
    }
    const SpawnGenResult r = generateSpawns(world, area, opt);
    for (const std::string& w : r.warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
    if (r.spawns.empty()) {
        std::fprintf(stderr, "error: no walkable floor found (%zu candidates)\n", r.candidates);
        return 1;
    }
    doc.beginGroup("Generate Spawns");
    for (const GeneratedSpawn& s : r.spawns) {
        char o[96], a[48];
        std::snprintf(o, sizeof o, "%s %s %s", formatNumber(s.origin.x).c_str(), formatNumber(s.origin.y).c_str(), formatNumber(s.origin.z).c_str());
        std::snprintf(a, sizeof a, "0 %s 0", formatNumber(s.yaw).c_str());
        doc.addEntity({{"classname", s.classname}, {"origin", o}, {"angles", a}}, {}, "Generate Spawns");
    }
    doc.endGroup();
    if (!doc.saveCopy(out, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("generate-spawns: %zu spawn entities from %zu walkable points -> %s\n", r.spawns.size(), r.candidates, out.c_str());
    return 0;
}
