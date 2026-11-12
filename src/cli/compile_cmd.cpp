// iw8r compile <map file> -o <outDir> --map <id> [--materials <library dir>]...
// Compiles an iwmap 4 document into the x64-zt IW8 extraction folder (src/core/compile_iw8.h).
// argv[1] is the command name, argv[2] the map file (same convention as the other iw8r commands).
// --materials may repeat: each folder is scanned recursively for *.iw8mat.json custom materials (doc 21).
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <string>
#include <vector>

#include "compile_iw8.h"
#include "custom_materials.h"
#include "map_text.h"

int cmdCompileIw8(int argc, char** argv) {
    const char* usage =
        "usage: iw8r compile <map file> -o <outDir> --map <id> [--materials <library dir>]... [--models <dir>]... [--model-scale s]\n"
        "       [--sky-dds <sky .dds>] [--sky-face n]   (the DDS of the worldspawn skyname, doc 33)\n";
    if (argc < 3) {
        std::fprintf(stderr, "%s", usage);
        return 1;
    }
    const std::string mapPath = argv[2];
    std::string outDir, mapId;
    std::vector<std::string> materialRoots, modelRoots;
    double modelScale = 1.0;
    std::string skyDds;
    int skyFace = 1024;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc)
            outDir = argv[++i];
        else if (a == "--map" && i + 1 < argc)
            mapId = argv[++i];
        else if (a == "--materials" && i + 1 < argc)
            materialRoots.push_back(argv[++i]);
        else if (a == "--models" && i + 1 < argc)
            modelRoots.push_back(argv[++i]);
        else if (a == "--model-scale" && i + 1 < argc)
            modelScale = std::atof(argv[++i]);
        else if (a == "--sky-dds" && i + 1 < argc)
            skyDds = argv[++i];
        else if (a == "--sky-face" && i + 1 < argc)
            skyFace = std::atoi(argv[++i]);
        else {
            std::fprintf(stderr, "error: unknown or incomplete argument '%s'\n", a.c_str());
            return 1;
        }
    }
    if (outDir.empty() || mapId.empty()) {
        std::fprintf(stderr, "%s", usage);
        return 1;
    }
    iw8r::MaterialLibrary library;
    if (!materialRoots.empty()) {
        std::string libErr;
        if (!library.load(materialRoots, &libErr)) {
            std::fprintf(stderr, "error: custom materials:\n%s\n", libErr.c_str());
            return 1;
        }
    }

    std::string text;
    if (!iw8r::readFile(mapPath, text)) {
        std::fprintf(stderr, "error: cannot read %s\n", mapPath.c_str());
        return 1;
    }
    iw8r::Id nextId = 1;
    iw8r::ParseResult parsed = iw8r::parseMap(text, nextId);
    if (parsed.error || !parsed.map) {
        std::fprintf(stderr, "error: %s line %zu: %s\n", mapPath.c_str(), parsed.error ? parsed.error->line : size_t(0),
                     parsed.error ? parsed.error->message.c_str() : "parse failed");
        return 1;
    }

    iw8r::CompileOptions opt;
    opt.mapId = mapId;
    opt.mapDir = std::filesystem::absolute(std::filesystem::path(mapPath)).parent_path().string();
    if (!materialRoots.empty()) opt.library = &library;
    // Stock scriptable definitions: the catalogue found from the working folder upward.
    opt.scriptables = &iw8r::ScriptableCatalog::shared(std::filesystem::current_path().string());
    opt.carryScriptables = std::getenv("IW8R_CARRY_SCRIPTABLES") != nullptr;  // carry stock definitions into the map
    opt.modelRoots = modelRoots;
    opt.modelScale = modelScale;
    opt.skyDdsPath = skyDds;
    opt.skyFaceSize = skyFace;
    iw8r::CompileReport rep;
    std::string err;
    if (!iw8r::compileIw8Extraction(*parsed.map, outDir, opt, rep, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    std::printf("compiled    %s -> %s\n", mapId.c_str(), outDir.c_str());
    std::printf("surfaces    %zu\n", rep.surfaces);
    std::printf("triangles   %zu\n", rep.triangles);
    std::printf("collision   %zu brushes\n", rep.collisionBrushes);
    std::printf("brush models %zu (triggers / script_brushmodel as *N)\n", rep.brushModels);
    if (!modelRoots.empty()) std::printf("models      %zu custom model(s) written\n", rep.customModels);
    std::printf("entities    %zu (%zu spawns)\n", rep.entities, rep.spawns);
    if (!materialRoots.empty()) std::printf("materials   %zu custom material(s) in the library\n", library.names().size());
    if (rep.bounds.valid())
        std::printf("bounds      (%s %s %s) - (%s %s %s)\n", iw8r::formatNumber(rep.bounds.mins.x).c_str(),
                    iw8r::formatNumber(rep.bounds.mins.y).c_str(), iw8r::formatNumber(rep.bounds.mins.z).c_str(),
                    iw8r::formatNumber(rep.bounds.maxs.x).c_str(), iw8r::formatNumber(rep.bounds.maxs.y).c_str(),
                    iw8r::formatNumber(rep.bounds.maxs.z).c_str());
    if (!rep.sky.empty())
        std::printf("sky         %s (images/%s.dds): %s\n", rep.sky.c_str(), rep.sky.c_str(), rep.skyNote.c_str());
    std::printf("warnings    %zu\n", rep.warnings.size());
    for (const auto& w : rep.warnings) std::printf("  - %s\n", w.c_str());
    return 0;
}
