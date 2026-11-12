#include "build_report.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>

#include "build_preflight.h"
#include "json_lite.h"
#include "map_text.h"

namespace iw8r {

namespace {

namespace fs = std::filesystem;

// ---- Budget thresholds.
// Editor guidelines are not measured engine limits: they are round numbers a mapper can exceed on purpose. Each one is
// stated as a warning. The native limits below are the ones x64-zt enforces, with the source line that enforces them
// (x64-zt is the sibling repo E:\mw124\x64-zt; paths are under its src\).
constexpr uint64_t kImageMemoryWarnBytes = 512ull * 1024 * 1024;  // editor guideline, not a measured engine limit
constexpr uint32_t kImageSideWarnPx = 4096;                       // editor guideline, not a measured engine limit
constexpr uint64_t kWorldTrianglesWarn = 2000000;                 // editor guideline, not a measured engine limit
constexpr uint64_t kEntitiesWarn = 4000;                          // editor guideline, not a measured engine limit
constexpr uint64_t kPrimaryLightsWarn = 250;                      // editor guideline, not a measured engine limit
constexpr uint64_t kScriptBytesWarn = 512ull * 1024;              // editor guideline, not a measured engine limit

// games\iw8\common\replay_render.cpp:383: 2D images up to 8192 px a side (BC7 too); cube maps and other non-BC7 images
// up to 4096.
constexpr uint32_t kNativeImageSide2DPx = 8192;
constexpr uint32_t kNativeImageSideCubePx = 4096;
// games\iw8\common\replay_render.cpp:1682: a static model index must fit uint16, so 65536 placements at most.
constexpr uint64_t kNativeStaticModelsMax = 65536;
// games\iw8\zone\map_zone.h:1448: the sun's primary-light index must fit uint8.
constexpr uint64_t kNativeSunPrimaryLightIndexMax = 255;

constexpr size_t kTopImages = 10;
constexpr size_t kTopClasses = 10;

uint64_t arraySize(const json::Value* v) { return v && v->type == json::Value::Type::Array ? v->arr.size() : 0; }

uint64_t numberOf(const json::Value* obj, const char* key) {
    if (!obj) return 0;
    const json::Value* v = obj->get(key);
    const auto n = v ? v->number() : std::nullopt;
    return n && *n > 0 ? uint64_t(*n) : 0;
}

std::string stringOf(const json::Value* obj, const char* key) {
    if (!obj) return "";
    const json::Value* v = obj->get(key);
    const std::string* s = v ? v->string() : nullptr;
    return s ? *s : "";
}

std::string lowerStr(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Reads the DDS header (128 bytes, plus the DX10 extension when present). Returns false when the file is not a DDS.
bool readDdsHeader(const fs::path& file, ImageInfo& info) {
    std::ifstream f(file, std::ios::binary);
    unsigned char h[132] = {};
    f.read(reinterpret_cast<char*>(h), sizeof h);
    const std::streamsize got = f.gcount();
    if (got < 128 || std::memcmp(h, "DDS ", 4) != 0) return false;
    const auto u32 = [&](size_t o) {
        return uint32_t(h[o]) | (uint32_t(h[o + 1]) << 8) | (uint32_t(h[o + 2]) << 16) | (uint32_t(h[o + 3]) << 24);
    };
    info.height = u32(12);
    info.width = u32(16);
    info.cubemap = (u32(112) & 0x200u) != 0;  // dwCaps2 DDSCAPS2_CUBEMAP
    const uint32_t flags = u32(80);            // DDPF flags of the pixel format
    if (flags & 0x4u) {                        // DDPF_FOURCC
        if (std::memcmp(h + 84, "DX10", 4) == 0 && got >= 132) {
            info.format = "DXGI " + std::to_string(u32(128));
        } else {
            info.format.assign(reinterpret_cast<const char*>(h + 84), 4);
            while (!info.format.empty() && info.format.back() == '\0') info.format.pop_back();
        }
    } else if (flags & 0x40u) {  // DDPF_RGB
        info.format = "RGB" + std::to_string(u32(88)) + "-bit";
    } else {
        info.format = "unknown";
    }
    return true;
}

// Largest side this image may have in game (the native limits above). BC7 is DXGI 98-100.
uint32_t nativeSideLimit(const ImageInfo& i) {
    const bool bc7 = i.format == "DXGI 98" || i.format == "DXGI 99" || i.format == "DXGI 100";
    return (i.cubemap && !bc7) ? kNativeImageSideCubePx : kNativeImageSide2DPx;
}

std::string fmtMb(uint64_t bytes) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    return b;
}

std::string fmtBytes(uint64_t bytes) {
    char b[32];
    if (bytes < 1024) std::snprintf(b, sizeof b, "%llu B", static_cast<unsigned long long>(bytes));
    else if (bytes < 1024ull * 1024) std::snprintf(b, sizeof b, "%.1f KB", double(bytes) / 1024.0);
    else return fmtMb(bytes);
    return b;
}

std::string signedDelta(int64_t d) { return (d > 0 ? "+" : "") + std::to_string(d); }

// "a -> b (+d)" for a count that changed; empty when it did not.
std::string countChange(const std::string& label, uint64_t a, uint64_t b) {
    if (a == b) return "";
    return label + ": " + std::to_string(a) + " -> " + std::to_string(b) + " (" + signedDelta(int64_t(b) - int64_t(a)) + ")";
}

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += b;
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

std::string imageJson(const ImageInfo& i) {
    return "{\"name\":" + jsonString(i.name) + ",\"bytes\":" + std::to_string(i.bytes) + ",\"width\":" + std::to_string(i.width) +
           ",\"height\":" + std::to_string(i.height) + ",\"format\":" + jsonString(i.format) +
           ",\"cubemap\":" + (i.cubemap ? "true" : "false") + "}";
}

ImageInfo imageFromJson(const json::Value& v) {
    ImageInfo i;
    i.name = stringOf(&v, "name");
    i.bytes = numberOf(&v, "bytes");
    i.width = uint32_t(numberOf(&v, "width"));
    i.height = uint32_t(numberOf(&v, "height"));
    i.format = stringOf(&v, "format");
    const json::Value* c = v.get("cubemap");
    i.cubemap = c && c->type == json::Value::Type::Bool && c->b;
    return i;
}

}  // namespace

BuildReport makeBuildReport(const std::string& extractedDir, const std::string& mapId) {
    BuildReport r;
    r.mapId = mapId;
    const fs::path root(extractedDir);
    std::error_code ec;

    // Materials: every .json under materials\ (the build writes one per material it carries).
    if (fs::is_directory(root / "materials", ec))
        for (const auto& e : fs::recursive_directory_iterator(root / "materials", ec))
            if (e.is_regular_file(ec) && lowerStr(e.path().extension().string()) == ".json") ++r.materials;

    // Images: every .dds under images\.
    std::vector<ImageInfo> images;
    if (fs::is_directory(root / "images", ec)) {
        for (const auto& e : fs::directory_iterator(root / "images", ec)) {
            if (!e.is_regular_file(ec) || lowerStr(e.path().extension().string()) != ".dds") continue;
            ImageInfo info;
            info.name = e.path().stem().string();
            info.bytes = uint64_t(e.file_size(ec));
            if (!readDdsHeader(e.path(), info)) info.format = "unknown";  // not a DDS: bytes only
            images.push_back(info);
        }
    }
    r.images = images.size();
    for (const ImageInfo& i : images) {
        r.imageBytes += i.bytes;
        const uint32_t side = std::max(i.width, i.height);
        if (side > r.maxImageSide) {
            r.maxImageSide = side;
            r.maxImageSideName = i.name;
        }
        if (side > kImageSideWarnPx) r.largeImages.push_back(i);
    }
    std::sort(images.begin(), images.end(), [](const ImageInfo& a, const ImageInfo& b) {
        return a.bytes != b.bytes ? a.bytes > b.bytes : a.name < b.name;
    });
    images.resize(std::min(images.size(), kTopImages));
    r.largestImages = images;
    std::sort(r.largeImages.begin(), r.largeImages.end(), [](const ImageInfo& a, const ImageInfo& b) { return a.name < b.name; });

    // Models: one table per model (xmodel\<name>.json); the geometry is in <name>.replay.json, where LOD 0 holds the
    // vertices and indices of each surface.
    if (fs::is_directory(root / "xmodel", ec)) {
        for (const auto& e : fs::directory_iterator(root / "xmodel", ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::string name = e.path().filename().string();
            const std::string lower = lowerStr(name);
            if (lower.size() > 12 && lower.compare(lower.size() - 12, 12, ".replay.json") == 0) {
                std::string text;
                if (!readFile(e.path().string(), text)) continue;
                const auto v = json::parse(text);
                if (!v) continue;
                const json::Value* lods = v->get("lods");
                if (!lods || lods->arr.empty()) continue;
                const json::Value* surfaces = lods->arr[0].get("surfaces");
                if (!surfaces) continue;
                for (const json::Value& s : surfaces->arr) {
                    r.xmodelVerticesLod0 += arraySize(s.get("vertices"));
                    r.xmodelTrianglesLod0 += arraySize(s.get("indices")) / 3;
                }
            } else if (lower.size() > 5 && lower.compare(lower.size() - 5, 5, ".json") == 0) {
                ++r.xmodels;
            }
        }
    }

    const fs::path mapDir = root / "maps" / "mp";
    // World render json: surfaces (brush models index into this list, so it is not counted again).
    std::string text, jerr;
    if (readFile((mapDir / (mapId + ".d3dbsp.replay-world.json")).string(), text)) {
        if (const auto v = json::parse(text, &jerr)) {
            const json::Value* surfaces = v->get("surfaces");
            r.worldSurfaces = arraySize(surfaces);
            if (surfaces)
                for (const json::Value& s : surfaces->arr) r.worldTriangles += arraySize(s.get("indices")) / 3;
            r.worldModels = arraySize(v->get("models"));
            r.reflectionProbes = arraySize(v->get("reflection_probes"));
            r.sunPrimaryLightIndex = numberOf(&*v, "sun_primary_light_index");
        }
    }
    // Collision json: submodels, brushes (the convex hulls) and triangle meshes.
    if (readFile((mapDir / (mapId + ".d3dbsp.replay-collision.json")).string(), text)) {
        if (const auto v = json::parse(text, &jerr)) {
            r.collisionSubmodels = arraySize(v->get("submodels"));
            r.collisionBrushes = arraySize(v->get("brushes"));
            r.collisionTriangles = arraySize(v->get("triangles"));
        }
    }
    // Comworld json: primary lights (the lights the renderer binds; the sun is one of them).
    if (readFile((mapDir / (mapId + ".d3dbsp.replay-comworld.json")).string(), text)) {
        if (const auto v = json::parse(text, &jerr)) r.primaryLights = arraySize(v->get("primary_lights"));
    }
    // Entities: the .ents text, counted per classname.
    if (readFile((mapDir / (mapId + ".d3dbsp.ents")).string(), text)) {
        std::vector<EntityKeyValues> entities;
        if (parseEntityBlocks(text, entities, nullptr)) {
            std::map<std::string, uint64_t> byClass;
            for (const EntityKeyValues& kv : entities) {
                std::string cls = "(none)";
                for (const auto& [k, v] : kv)
                    if (k == "classname") cls = v;
                ++byClass[cls];
            }
            r.entities = entities.size();
            for (const auto& [cls, n] : byClass) r.entityClasses.push_back({cls, n});
            std::sort(r.entityClasses.begin(), r.entityClasses.end(), [](const ClassCount& a, const ClassCount& b) {
                return a.count != b.count ? a.count > b.count : a.classname < b.classname;
            });
            if (r.entityClasses.size() > kTopClasses) r.entityClasses.resize(kTopClasses);
        }
    }
    // Script: its size on disk.
    r.scriptBytes = uint64_t(fs::file_size(mapDir / (mapId + ".gsc"), ec));
    if (ec) r.scriptBytes = 0;
    return r;
}

std::vector<BudgetGauge> budgetGauges(const BuildReport& r) {
    std::vector<BudgetGauge> g;
    g.push_back({"Image memory (DDS on disk)", double(r.imageBytes) / (1024.0 * 1024.0), double(kImageMemoryWarnBytes) / (1024.0 * 1024.0), false, "MB"});
    g.push_back({"Largest image side", double(r.maxImageSide), double(kNativeImageSide2DPx), true, "px"});
    g.push_back({"World triangles", double(r.worldTriangles), double(kWorldTrianglesWarn), false, ""});
    g.push_back({"Static models", double(r.worldModels), double(kNativeStaticModelsMax), true, ""});
    g.push_back({"Entities", double(r.entities), double(kEntitiesWarn), false, ""});
    g.push_back({"Primary lights", double(r.primaryLights), double(kPrimaryLightsWarn), false, ""});
    g.push_back({"Sun primary light index", double(r.sunPrimaryLightIndex), double(kNativeSunPrimaryLightIndexMax), true, ""});
    g.push_back({"Map script", double(r.scriptBytes) / 1024.0, double(kScriptBytesWarn) / 1024.0, false, "KB"});
    return g;
}

std::vector<std::string> budgetWarnings(const BuildReport& r) {
    std::vector<std::string> out;
    for (const ImageInfo& i : r.largeImages) {
        const uint32_t side = std::max(i.width, i.height);
        const uint32_t native = nativeSideLimit(i);
        if (side > native)
            out.push_back("native limit: image " + i.name + " is " + std::to_string(i.width) + " x " + std::to_string(i.height) +
                          " px; x64-zt rejects more than " + std::to_string(native) +
                          " px a side (src\\games\\iw8\\common\\replay_render.cpp:383)");
        else
            out.push_back("guideline: image " + i.name + " is " + std::to_string(i.width) + " x " + std::to_string(i.height) +
                          " px, over the editor guideline of " + std::to_string(kImageSideWarnPx) + " px a side");
    }
    if (r.imageBytes > kImageMemoryWarnBytes)
        out.push_back("guideline: images total " + fmtMb(r.imageBytes) + " on disk, over the editor guideline of " +
                      fmtMb(kImageMemoryWarnBytes) + " (a proxy: GPU memory is not measured)");
    if (r.worldModels > kNativeStaticModelsMax)
        out.push_back("native limit: " + std::to_string(r.worldModels) + " static model placements; a static model index must fit "
                      "uint16, 65536 at most (src\\games\\iw8\\common\\replay_render.cpp:1682)");
    if (r.sunPrimaryLightIndex > kNativeSunPrimaryLightIndexMax)
        out.push_back("native limit: sun primary-light index " + std::to_string(r.sunPrimaryLightIndex) +
                      " exceeds 255 (src\\games\\iw8\\zone\\map_zone.h:1448)");
    if (r.worldTriangles > kWorldTrianglesWarn)
        out.push_back("guideline: " + std::to_string(r.worldTriangles) + " world triangles, over the editor guideline of " +
                      std::to_string(kWorldTrianglesWarn));
    if (r.entities > kEntitiesWarn)
        out.push_back("guideline: " + std::to_string(r.entities) + " entities, over the editor guideline of " +
                      std::to_string(kEntitiesWarn));
    if (r.primaryLights > kPrimaryLightsWarn)
        out.push_back("guideline: " + std::to_string(r.primaryLights) + " primary lights, over the editor guideline of " +
                      std::to_string(kPrimaryLightsWarn));
    if (r.scriptBytes > kScriptBytesWarn)
        out.push_back("guideline: script is " + fmtBytes(r.scriptBytes) + ", over the editor guideline of " +
                      fmtBytes(kScriptBytesWarn));
    return out;
}

std::string formatBuildReport(const BuildReport& r) {
    std::string s;
    const auto line = [&](const std::string& label, const std::string& value) {
        char b[96];
        std::snprintf(b, sizeof b, "  %-18s", label.c_str());
        s += b + value + "\n";
    };
    s += "build report: " + r.mapId + "\n";
    line("materials", std::to_string(r.materials));
    std::string images = std::to_string(r.images) + "  " + fmtMb(r.imageBytes) + " on disk";
    if (!r.largestImages.empty()) {
        const ImageInfo& top = r.largestImages.front();
        images += "  (largest " + top.name + " " + std::to_string(top.width) + "x" + std::to_string(top.height) + ", " + fmtMb(top.bytes) + ")";
    }
    line("images", images);
    line("max image side", std::to_string(r.maxImageSide) + " px" + (r.maxImageSideName.empty() ? "" : " (" + r.maxImageSideName + ")"));
    line("xmodels", std::to_string(r.xmodels) + "  LOD 0: " + std::to_string(r.xmodelVerticesLod0) + " vertices, " +
                        std::to_string(r.xmodelTrianglesLod0) + " triangles");
    line("world", std::to_string(r.worldSurfaces) + " surfaces, " + std::to_string(r.worldTriangles) + " triangles, " +
                      std::to_string(r.worldModels) + " static models, " + std::to_string(r.reflectionProbes) + " probes");
    line("collision", std::to_string(r.collisionBrushes) + " brushes, " + std::to_string(r.collisionSubmodels) + " submodels, " +
                          std::to_string(r.collisionTriangles) + " triangles");
    std::string ents = std::to_string(r.entities);
    if (!r.entityClasses.empty()) {
        ents += "  top:";
        for (const ClassCount& c : r.entityClasses) ents += " " + c.classname + " " + std::to_string(c.count) + ",";
        ents.pop_back();
    }
    line("entities", ents);
    line("primary lights", std::to_string(r.primaryLights) + " (sun index " + std::to_string(r.sunPrimaryLightIndex) + ")");
    line("script", fmtBytes(r.scriptBytes));
    return s;
}

std::string diffBuildReports(const BuildReport& before, const BuildReport& after) {
    std::vector<std::string> lines;
    const auto add = [&](const std::string& l) {
        if (!l.empty()) lines.push_back(l);
    };
    if (before.mapId != after.mapId) lines.push_back("map: " + before.mapId + " -> " + after.mapId);
    add(countChange("materials", before.materials, after.materials));
    if (before.images != after.images || before.imageBytes != after.imageBytes)
        lines.push_back("images: " + std::to_string(before.images) + " -> " + std::to_string(after.images) + " (" +
                        signedDelta(int64_t(after.images) - int64_t(before.images)) + "), " + fmtMb(before.imageBytes) + " -> " +
                        fmtMb(after.imageBytes));
    if (before.maxImageSide != after.maxImageSide || before.maxImageSideName != after.maxImageSideName)
        lines.push_back("max image side: " + std::to_string(before.maxImageSide) + " px (" + before.maxImageSideName + ") -> " +
                        std::to_string(after.maxImageSide) + " px (" + after.maxImageSideName + ")");
    add(countChange("xmodels", before.xmodels, after.xmodels));
    add(countChange("xmodel LOD 0 vertices", before.xmodelVerticesLod0, after.xmodelVerticesLod0));
    add(countChange("xmodel LOD 0 triangles", before.xmodelTrianglesLod0, after.xmodelTrianglesLod0));
    add(countChange("world surfaces", before.worldSurfaces, after.worldSurfaces));
    add(countChange("world triangles", before.worldTriangles, after.worldTriangles));
    add(countChange("static models", before.worldModels, after.worldModels));
    add(countChange("reflection probes", before.reflectionProbes, after.reflectionProbes));
    add(countChange("sun primary light index", before.sunPrimaryLightIndex, after.sunPrimaryLightIndex));
    add(countChange("collision brushes", before.collisionBrushes, after.collisionBrushes));
    add(countChange("collision submodels", before.collisionSubmodels, after.collisionSubmodels));
    add(countChange("collision triangles", before.collisionTriangles, after.collisionTriangles));
    add(countChange("entities", before.entities, after.entities));
    // Per class: only the classes in either top-10 list, so a class that fell out of the list shows as its last count.
    std::map<std::string, std::pair<uint64_t, uint64_t>> classes;
    for (const ClassCount& c : before.entityClasses) classes[c.classname].first = c.count;
    for (const ClassCount& c : after.entityClasses) classes[c.classname].second = c.count;
    for (const auto& [cls, n] : classes) add(countChange("entity class " + cls, n.first, n.second));
    add(countChange("primary lights", before.primaryLights, after.primaryLights));
    if (before.scriptBytes != after.scriptBytes)
        lines.push_back("script: " + fmtBytes(before.scriptBytes) + " -> " + fmtBytes(after.scriptBytes) + " (" +
                        (after.scriptBytes >= before.scriptBytes ? "+" : "-") +
                        fmtBytes(after.scriptBytes >= before.scriptBytes ? after.scriptBytes - before.scriptBytes
                                                                         : before.scriptBytes - after.scriptBytes) +
                        ")");
    if (lines.empty()) return "no changes\n";
    std::string s;
    for (const std::string& l : lines) s += l + "\n";
    return s;
}

bool saveBuildReport(const BuildReport& r, const std::string& jsonPath, std::string* err) {
    std::string j = "{\"schema\":1,\"map\":" + jsonString(r.mapId);
    const auto num = [&](const char* key, uint64_t v) { j += std::string(",\"") + key + "\":" + std::to_string(v); };
    num("materials", r.materials);
    num("images", r.images);
    num("image_bytes", r.imageBytes);
    num("max_image_side", r.maxImageSide);
    j += ",\"max_image_side_name\":" + jsonString(r.maxImageSideName);
    j += ",\"largest_images\":[";
    for (size_t i = 0; i < r.largestImages.size(); ++i) j += (i ? "," : "") + imageJson(r.largestImages[i]);
    j += "],\"large_images\":[";
    for (size_t i = 0; i < r.largeImages.size(); ++i) j += (i ? "," : "") + imageJson(r.largeImages[i]);
    j += "]";
    num("xmodels", r.xmodels);
    num("xmodel_vertices_lod0", r.xmodelVerticesLod0);
    num("xmodel_triangles_lod0", r.xmodelTrianglesLod0);
    num("world_surfaces", r.worldSurfaces);
    num("world_triangles", r.worldTriangles);
    num("world_models", r.worldModels);
    num("reflection_probes", r.reflectionProbes);
    num("sun_primary_light_index", r.sunPrimaryLightIndex);
    num("collision_submodels", r.collisionSubmodels);
    num("collision_brushes", r.collisionBrushes);
    num("collision_triangles", r.collisionTriangles);
    num("entities", r.entities);
    j += ",\"entity_classes\":[";
    for (size_t i = 0; i < r.entityClasses.size(); ++i)
        j += std::string(i ? "," : "") + "{\"classname\":" + jsonString(r.entityClasses[i].classname) +
             ",\"count\":" + std::to_string(r.entityClasses[i].count) + "}";
    j += "]";
    num("primary_lights", r.primaryLights);
    num("script_bytes", r.scriptBytes);
    j += "}\n";
    return writeFileAtomic(jsonPath, j, err);
}

bool loadBuildReport(const std::string& jsonPath, BuildReport& out, std::string* err) {
    std::string text, jerr;
    if (!readFile(jsonPath, text)) {
        if (err) *err = "cannot read " + jsonPath;
        return false;
    }
    const auto v = json::parse(text, &jerr);
    if (!v) {
        if (err) *err = jsonPath + ": " + jerr;
        return false;
    }
    if (numberOf(&*v, "schema") != 1) {
        if (err) *err = jsonPath + ": not a build report of schema 1";
        return false;
    }
    BuildReport r;
    r.mapId = stringOf(&*v, "map");
    r.materials = numberOf(&*v, "materials");
    r.images = numberOf(&*v, "images");
    r.imageBytes = numberOf(&*v, "image_bytes");
    r.maxImageSide = uint32_t(numberOf(&*v, "max_image_side"));
    r.maxImageSideName = stringOf(&*v, "max_image_side_name");
    if (const json::Value* a = v->get("largest_images"))
        for (const json::Value& i : a->arr) r.largestImages.push_back(imageFromJson(i));
    if (const json::Value* a = v->get("large_images"))
        for (const json::Value& i : a->arr) r.largeImages.push_back(imageFromJson(i));
    r.xmodels = numberOf(&*v, "xmodels");
    r.xmodelVerticesLod0 = numberOf(&*v, "xmodel_vertices_lod0");
    r.xmodelTrianglesLod0 = numberOf(&*v, "xmodel_triangles_lod0");
    r.worldSurfaces = numberOf(&*v, "world_surfaces");
    r.worldTriangles = numberOf(&*v, "world_triangles");
    r.worldModels = numberOf(&*v, "world_models");
    r.reflectionProbes = numberOf(&*v, "reflection_probes");
    r.sunPrimaryLightIndex = numberOf(&*v, "sun_primary_light_index");
    r.collisionSubmodels = numberOf(&*v, "collision_submodels");
    r.collisionBrushes = numberOf(&*v, "collision_brushes");
    r.collisionTriangles = numberOf(&*v, "collision_triangles");
    r.entities = numberOf(&*v, "entities");
    if (const json::Value* a = v->get("entity_classes"))
        for (const json::Value& c : a->arr) r.entityClasses.push_back({stringOf(&c, "classname"), numberOf(&c, "count")});
    r.primaryLights = numberOf(&*v, "primary_lights");
    r.scriptBytes = numberOf(&*v, "script_bytes");
    out = r;
    return true;
}

}  // namespace iw8r
