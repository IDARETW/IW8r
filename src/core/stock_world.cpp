#include "stock_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <unordered_map>

#include "json_lite.h"

namespace iw8r {
namespace {

using json::Value;
namespace fs = std::filesystem;

constexpr uint32_t kPosStride = 12;  // float3, verified from the surface offsets (docs/research/30)
constexpr uint32_t kUvLayerStride = 8;  // float2 per texture layer, layer 0 first, layers interleaved per vertex
constexpr uint32_t kNoUv = 0xFFFFFFFFu;  // vertex key part for vertices without texture coordinates

// The per-surface fields of GfxWorldSurfData (gfx_map surfaces.surfData) that the reader needs.
struct SurfData {
    int64_t layerCount = 0;
    int64_t texCoordOffset = 0;  // aux stream byte offset of vertex 0, layer 0 (0 when absent)
};

// The GfxWorld / GfxWorldTransientZone asset object: root.asset.fields.
const Value* fieldsOf(const Value& root) {
    const Value* a = root.get("asset");
    return a ? a->get("fields") : nullptr;
}

double num(const Value* v, double def = -1) { return v && v->type == Value::Type::Number ? v->num : def; }

bool unhex(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2) return false;
    out.resize(hex.size() / 2);
    const auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = uint8_t(hi << 4 | lo);
    }
    return true;
}

float readF32(const std::vector<uint8_t>& b, size_t off) {
    const uint32_t u = uint32_t(b[off]) | uint32_t(b[off + 1]) << 8 | uint32_t(b[off + 2]) << 16 | uint32_t(b[off + 3]) << 24;
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// One GfxWorldTransientZone: the position stream (float3 at 12 bytes), the uint16 index stream and the aux stream
// (texture coordinates and the other per-vertex records; empty when the export has no auxData).
struct TrzData {
    std::vector<uint8_t> pos;
    std::vector<uint8_t> aux;
    std::vector<uint16_t> idx;
};

std::array<double, 2> stockUv(const std::vector<uint8_t>& aux, size_t off) {
    // float32 u, v as stored (v = 0 at the top of the image); ImportedMesh uses v = 0 at the bottom (model_import.h).
    return {double(readF32(aux, off)), 1.0 - double(readF32(aux, off + 4))};
}

// Reads the transient zone index and the draw buffers of one gfx_map_trzone JSON text.
// *empty is set for a stub with no buffers: big maps (mp_donetsk) keep most transient zones in their own streamed
// fastfiles (mp_donetsk_00587_tr.ff, ...), and the main zone holds only a size-0 entry for each.
bool extractTrz(const std::string& text, int64_t& zoneIndex, TrzData& out, std::string* err, bool* empty = nullptr) {
    std::string jerr;
    const auto root = json::parse(text, &jerr);
    if (!root) {
        if (err) *err = "gfx_map_trzone JSON: " + jerr;
        return false;
    }
    const Value* f = fieldsOf(*root);
    const Value* dv = f ? f->get("drawVerts") : nullptr;
    const Value* posBytes = dv && dv->get("posData") ? dv->get("posData")->get("bytes") : nullptr;
    const Value* idxBytes = dv && dv->get("indices") ? dv->get("indices")->get("bytes") : nullptr;
    const Value* auxBytes = dv && dv->get("auxData") ? dv->get("auxData")->get("bytes") : nullptr;
    if (empty) *empty = false;
    if (dv && num(dv->get("posDataSize"), -1) == 0 && num(dv->get("indexCount"), -1) == 0) {
        zoneIndex = int64_t(num(f->get("transientZoneIndex"), 0));
        if (empty) *empty = true;
        return true;
    }
    if (!posBytes || !idxBytes || !posBytes->string() || !idxBytes->string()) {
        if (err) *err = "gfx_map_trzone has no drawVerts posData/indices bytes (hex)";
        return false;
    }
    zoneIndex = int64_t(num(f->get("transientZoneIndex"), 0));
    std::vector<uint8_t> idxRaw;
    if (!unhex(*posBytes->string(), out.pos) || !unhex(*idxBytes->string(), idxRaw) || idxRaw.size() % 2) {
        if (err) *err = "gfx_map_trzone buffers are not hex";
        return false;
    }
    if (auxBytes && auxBytes->string() && !unhex(*auxBytes->string(), out.aux)) {
        if (err) *err = "gfx_map_trzone auxData bytes are not hex";
        return false;
    }
    out.idx.resize(idxRaw.size() / 2);
    for (size_t i = 0; i < out.idx.size(); ++i) out.idx[i] = uint16_t(idxRaw[2 * i] | idxRaw[2 * i + 1] << 8);
    return true;
}

// Builds the meshes from the gfx_map surfaces and the transient zone buffers.
bool buildWorld(const std::string& gfxText, std::map<int64_t, TrzData>& trz, StockWorld& out, std::string* err) {
    std::string jerr;
    const auto root = json::parse(gfxText, &jerr);
    if (!root) {
        if (err) *err = "gfx_map JSON: " + jerr;
        return false;
    }
    const Value* f = fieldsOf(*root);
    const Value* surfs = f && f->get("surfaces") && f->get("surfaces")->get("surfaces")
                             ? f->get("surfaces")->get("surfaces")->get("values")
                             : nullptr;
    if (!surfs || surfs->type != Value::Type::Array) {
        if (err) *err = "gfx_map has no surfaces.surfaces.values";
        return false;
    }

    // surfaces.surfData: one GfxWorldSurfData per vertex block (the surface's surfDataIndex selects it).
    std::vector<SurfData> surfData;
    if (const Value* sd = f->get("surfaces") ? f->get("surfaces")->get("surfData") : nullptr;
        sd && sd->get("values") && sd->get("values")->type == Value::Type::Array) {
        for (const Value& r : sd->get("values")->arr) {
            SurfData d;
            d.layerCount = int64_t(num(r.get("layerCount"), 0));
            d.texCoordOffset = int64_t(num(r.get("texCoordOffset"), 0));
            surfData.push_back(d);
        }
    }

    out.meshes.clear();
    out.bounds = AABB();
    out.warnings.clear();
    // Per mesh: (position stream offset, texture stream offset) -> mesh vertex. Both are byte offsets in the zone's
    // streams, so vertices shared between surfaces of one vertex block merge and nothing else does.
    std::vector<std::unordered_map<uint64_t, uint32_t>> vmap;
    std::map<std::string, size_t> byMaterial;
    std::map<int64_t, size_t> missingZone;                      // transient zone -> surfaces skipped
    size_t surfacesUsed = 0, trisBad = 0, surfacesBad = 0;
    size_t uvNoAux = 0, uvUnresolved = 0;                       // surfaces whose texture coordinates are not read

    for (const Value& s : surfs->arr) {
        const Value* tris = s.get("tris");
        if (!tris) {
            ++surfacesBad;
            continue;
        }
        const int64_t posOffset = int64_t(num(tris->get("posOffset"), -1));
        const int64_t vertexCount = int64_t(num(tris->get("vertexCount"), -1));
        const int64_t triCount = int64_t(num(tris->get("triCount"), -1));
        const int64_t baseIndex = int64_t(num(tris->get("baseIndex"), -1));
        const int64_t zoneIndex = int64_t(num(s.get("transientZone"), 0));
        if (posOffset < 0 || vertexCount <= 0 || triCount <= 0 || baseIndex < 0) {
            ++surfacesBad;
            continue;
        }
        const auto zit = trz.find(zoneIndex);
        if (zit == trz.end()) {
            ++missingZone[zoneIndex];
            continue;
        }
        const TrzData& z = zit->second;
        if (uint64_t(posOffset) + kPosStride * uint64_t(vertexCount) > z.pos.size() ||
            uint64_t(baseIndex) + 3 * uint64_t(triCount) > z.idx.size()) {
            ++surfacesBad;
            continue;
        }
        const Value* mat = s.get("material");
        // The surface names carry a leading ',' (ACTS prints the material name with it); the material asset is named without it.
        std::string name = mat && mat->get("name") && mat->get("name")->string() ? *mat->get("name")->string() : "";
        if (!name.empty() && name[0] == ',') name.erase(0, 1);
        size_t mi;
        if (auto it = byMaterial.find(name); it != byMaterial.end()) {
            mi = it->second;
        } else {
            mi = out.meshes.size();
            byMaterial[name] = mi;
            out.meshes.emplace_back();
            out.meshes.back().material = name;
            vmap.emplace_back();
        }
        ImportedMesh& m = out.meshes[mi];
        auto& vm = vmap[mi];
        ++surfacesUsed;

        // Texture coordinates (layer 0): the surface's GfxWorldSurfData gives texCoordOffset, and vertex j's record is
        // at texCoordOffset + 8 * layerCount * j, so the stride includes every layer (see stock_world.h).
        int64_t uvBase = -1;   // aux offset of vertex 0 (-1: no texture coordinates for this surface)
        int64_t uvStride = 0;
        const int64_t surfDataIndex = int64_t(num(s.get("surfDataIndex"), -1));
        if (surfDataIndex >= 0) {
            if (surfDataIndex >= int64_t(surfData.size()) || surfData[size_t(surfDataIndex)].texCoordOffset <= 0 ||
                surfData[size_t(surfDataIndex)].layerCount < 1) {
                ++uvUnresolved;
            } else if (z.aux.empty()) {
                ++uvNoAux;
            } else {
                const SurfData& sdr = surfData[size_t(surfDataIndex)];
                uvStride = int64_t(kUvLayerStride) * sdr.layerCount;
                if (uint64_t(sdr.texCoordOffset) + uint64_t(uvStride) * uint64_t(vertexCount) > z.aux.size()) {
                    ++uvUnresolved;
                } else {
                    uvBase = sdr.texCoordOffset;
                }
            }
        }

        for (int64_t j = 0; j < triCount; ++j) {
            const uint16_t* t = &z.idx[size_t(baseIndex + 3 * j)];
            if (t[0] >= vertexCount || t[1] >= vertexCount || t[2] >= vertexCount) {
                ++trisBad;
                continue;
            }
            uint32_t corner[3];
            for (int c = 0; c < 3; ++c) {
                const uint32_t posKey = uint32_t(posOffset) + kPosStride * t[c];  // stream byte offset: shared across surfaces
                const uint32_t uvKey = uvBase >= 0 ? uint32_t(uvBase + uvStride * t[c]) : kNoUv;
                const uint64_t key = uint64_t(posKey) << 32 | uvKey;
                auto it = vm.find(key);
                if (it == vm.end()) {
                    const size_t at = posKey;
                    const Vec3 p{readF32(z.pos, at), readF32(z.pos, at + 4), readF32(z.pos, at + 8)};
                    it = vm.emplace(key, uint32_t(m.pos.size())).first;
                    m.pos.push_back(p);
                    m.uv.push_back(uvBase >= 0 ? stockUv(z.aux, uvKey) : std::array<double, 2>{0, 0});
                    out.bounds.add(p);
                }
                corner[c] = it->second;
            }
            // ACTS winds clockwise seen from outside; ImportedMesh is counter-clockwise, so swap two corners.
            m.idx.push_back(corner[0]);
            m.idx.push_back(corner[2]);
            m.idx.push_back(corner[1]);
        }
    }

    // Area-weighted vertex normals (no normal stream is read). uv was filled per vertex above.
    for (ImportedMesh& m : out.meshes) {
        m.normal.assign(m.pos.size(), Vec3{});
        for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
            const Vec3 a = m.pos[m.idx[i]], b = m.pos[m.idx[i + 1]], c = m.pos[m.idx[i + 2]];
            const Vec3 n = cross(b - a, c - a);
            m.normal[m.idx[i]] += n;
            m.normal[m.idx[i + 1]] += n;
            m.normal[m.idx[i + 2]] += n;
        }
        for (Vec3& n : m.normal) {
            const double len = length(n);
            n = len > 1e-12 ? n * (1.0 / len) : Vec3{0, 0, 1};
        }
    }
    // Empty meshes (every triangle skipped) are dropped.
    out.meshes.erase(std::remove_if(out.meshes.begin(), out.meshes.end(),
                                    [](const ImportedMesh& m) { return m.idx.empty(); }),
                     out.meshes.end());

    if (missingZone.size() > 4) {
        size_t surfaces = 0;
        for (const auto& [zone, count] : missingZone) surfaces += count;
        out.warnings.push_back(std::to_string(missingZone.size()) + " transient zones not in the export (streamed from their own "
                               "_tr fastfiles): " + std::to_string(surfaces) + " surfaces skipped");
    } else {
        for (const auto& [zone, count] : missingZone)
            out.warnings.push_back("transient zone " + std::to_string(zone) + " not in the export: " + std::to_string(count) +
                                   " surfaces skipped");
    }
    if (surfacesBad) out.warnings.push_back(std::to_string(surfacesBad) + " surfaces skipped (bad or out-of-range tris)");
    if (trisBad) out.warnings.push_back(std::to_string(trisBad) + " triangles skipped (index out of the surface range)");
    if (uvNoAux)
        out.warnings.push_back(std::to_string(uvNoAux) + " surfaces without texture coordinates (no auxData in the transient zone): uv zero");
    if (uvUnresolved)
        out.warnings.push_back(std::to_string(uvUnresolved) + " surfaces without texture coordinates (surfData missing or out of range): uv zero");
    if (surfacesUsed == 0) {
        if (err) *err = "no gfx_map surface could be read";
        return false;
    }
    return true;
}

bool readFile(const fs::path& p, std::string& out, std::string* err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + p.string();
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

std::vector<fs::path> jsonFilesUnder(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".json") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

bool parseStockWorld(const std::string& gfxMapJson, const std::vector<std::string>& trzoneJson, StockWorld& out,
                     std::string* err) {
    std::map<int64_t, TrzData> trz;
    for (const std::string& text : trzoneJson) {
        int64_t zone = 0;
        TrzData d;
        bool empty = false;
        if (!extractTrz(text, zone, d, err, &empty)) return false;
        if (!empty) trz[zone] = std::move(d);
    }
    return buildWorld(gfxMapJson, trz, out, err);
}

namespace {

// IEEE 754 binary16 to double.
double halfToDouble(uint16_t h) {
    const bool neg = (h >> 15) != 0;
    const int exp = (h >> 10) & 31;
    const int man = h & 1023;
    double v;
    if (exp == 0)
        v = std::ldexp(double(man), -24);  // zero or subnormal
    else if (exp == 31)
        v = man ? std::nan("") : std::numeric_limits<double>::infinity();
    else
        v = std::ldexp(double(1024 + man), exp - 25);
    return neg ? -v : v;
}

}  // namespace

void unpackStockTexCoords(uint32_t packed, double& u, double& v) {
    u = halfToDouble(uint16_t(packed & 0xFFFF));
    v = halfToDouble(uint16_t(packed >> 16));
}

bool readStockWorld(const std::string& zoneDir, StockWorld& out, std::string* err) {
    const fs::path base = fs::path(zoneDir) / "assets";
    const auto gfx = jsonFilesUnder(base / "gfx_map");
    const auto trzFiles = jsonFilesUnder(base / "gfx_map_trzone");
    if (gfx.size() != 1) {
        if (err) *err = "expected one gfx_map JSON under " + (base / "gfx_map").string() + ", found " + std::to_string(gfx.size());
        return false;
    }
    if (trzFiles.empty()) {
        if (err) *err = "no gfx_map_trzone JSON under " + (base / "gfx_map_trzone").string();
        return false;
    }
    // One transient zone text at a time: the largest parse holds a hex copy of its buffers, so peak memory stays low.
    std::map<int64_t, TrzData> trz;
    for (const fs::path& p : trzFiles) {
        std::string text;
        if (!readFile(p, text, err)) return false;
        int64_t zone = 0;
        TrzData d;
        bool empty = false;
        if (!extractTrz(text, zone, d, err, &empty)) return false;
        if (!empty) trz[zone] = std::move(d);
    }
    std::string gfxText;
    if (!readFile(gfx.front(), gfxText, err)) return false;
    return buildWorld(gfxText, trz, out, err);
}

}  // namespace iw8r
