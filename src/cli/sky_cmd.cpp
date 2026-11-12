// iw8r sky-panorama <panorama.png|jpg|tga|hdr> <out.dds> [--yaw deg] [--ev stops] [--face N]
// Converts an equirectangular panorama to the sky cube DDS the build reads (src/core/sky_panorama.h) and prints the
// worldspawn keys to use: skyname, iw8r_sky_file and a sundirection aimed at the brightest spot. Exit 0, 1 on a read or
// write error, 2 on bad arguments.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "sky_panorama.h"
#include "sky_sun.h"
#include "stock_vision.h"
#include <map>
#include <vector>
#include <iterator>

int cmdSkyPanorama(int argc, char** argv) {
    const char* usage = "usage: iw8r sky-panorama <panorama.png|jpg|tga|hdr> <out.dds> [--yaw deg] [--ev stops] [--face N]\n";
    if (argc < 4) {
        std::fprintf(stderr, "%s", usage);
        return 2;
    }
    const std::string in = argv[2], out = argv[3];
    double yaw = 0, ev = 0;
    int face = 1024;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--yaw" && i + 1 < argc) yaw = std::atof(argv[++i]);
        else if (a == "--ev" && i + 1 < argc) ev = std::atof(argv[++i]);
        else if (a == "--face" && i + 1 < argc) face = std::atoi(argv[++i]);
        else {
            std::fprintf(stderr, "%s", usage);
            return 2;
        }
    }
    iw8r::FloatImage pano;
    std::string err;
    if (!iw8r::readPanorama(in, pano, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const std::string cube = iw8r::cubeDdsFromPanorama(pano, face, yaw, ev, &err);
    if (cube.empty()) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::ofstream f(out, std::ios::binary);
    if (!f || !f.write(cube.data(), std::streamsize(cube.size()))) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf("wrote %s (%dx%d panorama -> %d px cube faces, %zu bytes)\n", out.c_str(), pano.w, pano.h, face, cube.size());
    std::printf("sundirection \"%s\"\n", iw8r::sunDirectionKeyFor(iw8r::brightestDirection(pano, yaw)).c_str());
    return 0;
}

// iw8r sky-sun <sky.dds>: the sundirection aimed at the brightest part of a sky image (src/core/sky_sun.h).
int cmdSkySun(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: iw8r sky-sun <sky.dds>\n");
        return 2;
    }
    std::ifstream in(argv[2], std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string err;
    const auto s = iw8r::sunFromSkyDds(bytes, &err);
    if (!s) {
        std::fprintf(stderr, "no sun found in %s%s%s\n", argv[2], err.empty() ? "" : ": ", err.c_str());
        return 1;
    }
    std::printf("sundirection \"%s\" (peak %.3f, contrast %.2f)\n", iw8r::sunDirectionKeyFor(s->dir).c_str(), s->peak, s->contrast);
    return 0;
}

// iw8r vision-set <vision> <edits> -o <out>: applies `KEY "value"` lines of <edits> to a stock vision rawfile (values of
// existing keys only; src/core/stock_vision.h). Exit 1 when a key is not in the vision.
int cmdVisionSet(int argc, char** argv) {
    if (argc < 6 || std::string(argv[4]) != "-o") {
        std::fprintf(stderr, "usage: iw8r vision-set <vision> <edits> -o <out>\n");
        return 2;
    }
    auto slurp = [](const char* p) {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    const std::string vision = slurp(argv[2]);
    if (vision.empty()) {
        std::fprintf(stderr, "cannot read %s\n", argv[2]);
        return 1;
    }
    std::map<std::string, std::string> edits;
    for (const auto& [k, v] : iw8r::readVisionValues(slurp(argv[3]))) edits[k] = v;
    int changed = 0;
    std::vector<std::string> missing;
    const std::string out = iw8r::applyVisionEdits(vision, edits, &changed, &missing);
    for (const auto& m : missing) std::fprintf(stderr, "key %s is not in the vision (not added)\n", m.c_str());
    std::ofstream o(argv[5], std::ios::binary);
    o << out;
    std::printf("vision: %d value(s) changed\n", changed);
    return missing.empty() && o ? 0 : 1;
}
