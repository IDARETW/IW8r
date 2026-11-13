#include "stock_displacement.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <type_traits>
#include <unordered_map>

#include "json_lite.h"
#include "stock_decal.h"

namespace iw8r {
namespace {

using json::Value;
namespace fs = std::filesystem;

// The same constants and vertex numbering as stock_world.cpp (buildWorld): the meshes must line up vertex for vertex.
constexpr uint32_t kPosStride = 12;
constexpr uint32_t kUvLayerStride = 8;
constexpr uint32_t kNoUv = 0xFFFFFFFFu;
constexpr uint32_t kLmapStride = 8;
constexpr int64_t kNoLightmap = 510;
constexpr int64_t kSurfDataBytes = 88;

struct SurfData {
    int64_t layerCount = 0;
    int64_t xyzOffset = -1;
    int64_t tangentFrameOffset = 0;
    int64_t lmapCoordOffset = 0;
    int64_t colorOffset = 0;
    int64_t texCoordOffset = 0;
    std::array<int64_t, kStockMaxLayers> displacementOffset{};
};

struct VertexKey {
    uint64_t posUv = 0;
    uint32_t lmap = 0;
    uint32_t zone = 0;
    int32_t lightmap = -1;
    uint32_t color = 0;
    bool operator==(const VertexKey& o) const {
        return posUv == o.posUv && lmap == o.lmap && zone == o.zone && lightmap == o.lightmap && color == o.color;
    }
};
struct VertexKeyHash {
    size_t operator()(const VertexKey& k) const {
        uint64_t h = k.posUv ^ (uint64_t(k.lmap) * 0x9E3779B97F4A7C15ull);
        h ^= (uint64_t(k.zone) << 20 | uint64_t(uint32_t(k.lightmap))) * 0xC2B2AE3D27D4EB4Full;
        h ^= uint64_t(k.color) * 0x165667B19E3779F9ull;
        return std::hash<uint64_t>()(h);
    }
};

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

uint32_t readU32(const std::vector<uint8_t>& b, size_t off) {
    return uint32_t(b[off]) | uint32_t(b[off + 1]) << 8 | uint32_t(b[off + 2]) << 16 | uint32_t(b[off + 3]) << 24;
}

float readF32(const std::vector<uint8_t>& b, size_t off) {
    const uint32_t u = readU32(b, off);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

struct TrzData {
    std::vector<uint8_t> pos, aux;
    std::vector<uint16_t> idx;
};

// The draw buffers of one gfx_map_trzone text (the forms stock_world.cpp extractTrz accepts). *empty: a size-0 stub.
bool extractTrz(const std::string& text, int64_t& zoneIndex, TrzData& out, std::string* err, bool* empty) {
    *empty = false;
    std::string jerr;
    const auto root = json::parse(text, &jerr);
    if (!root) {
        if (err) *err = "gfx_map_trzone JSON: " + jerr;
        return false;
    }
    const Value* f = fieldsOf(*root);
    const Value* dv = f ? f->get("drawVerts") : nullptr;
    if (!dv) {
        if (err) *err = "gfx_map_trzone has no drawVerts";
        return false;
    }
    zoneIndex = int64_t(num(f->get("transientZoneIndex"), 0));
    if (num(dv->get("posDataSize"), -1) == 0 && num(dv->get("indexCount"), -1) == 0) {
        *empty = true;
        return true;
    }
    const auto bytesOf = [&](const char* key, std::vector<uint8_t>& dst) -> bool {
        const Value* node = dv->get(key);
        if (!node) return false;
        if (const Value* b = node->get("bytes"); b && b->string()) return unhex(*b->string(), dst);
        if (const Value* v = node->get("values"); v && v->type == Value::Type::Array) {
            dst.clear();
            for (const Value& x : v->arr) dst.push_back(uint8_t(num(&x, 0)));
            return true;
        }
        return false;
    };
    std::vector<uint8_t> idxRaw;
    if (!bytesOf("posData", out.pos)) {
        if (err) *err = "gfx_map_trzone posData is missing or not hex";
        return false;
    }
    // indices: hex bytes (uint16 little endian) or decoded values (one per index)
    if (const Value* node = dv->get("indices"); node && node->get("values") && node->get("values")->type == Value::Type::Array) {
        for (const Value& i : node->get("values")->arr) out.idx.push_back(uint16_t(num(&i, 0)));
    } else if (!bytesOf("indices", idxRaw) || idxRaw.size() % 2) {
        if (err) *err = "gfx_map_trzone indices are missing or not hex";
        return false;
    } else {
        out.idx.resize(idxRaw.size() / 2);
        for (size_t i = 0; i < out.idx.size(); ++i) out.idx[i] = uint16_t(idxRaw[2 * i] | idxRaw[2 * i + 1] << 8);
    }
    if (dv->get("auxData") && !bytesOf("auxData", out.aux)) {
        if (err) *err = "gfx_map_trzone auxData is not hex";
        return false;
    }
    return true;
}

std::vector<SurfData> readSurfData(const Value* f) {
    std::vector<SurfData> surfData;
    const Value* sd = f->get("surfaces") ? f->get("surfaces")->get("surfData") : nullptr;
    if (sd && sd->get("values") && sd->get("values")->type == Value::Type::Array) {
        for (const Value& r : sd->get("values")->arr) {
            SurfData d;
            d.layerCount = int64_t(num(r.get("layerCount"), 0));
            d.xyzOffset = int64_t(num(r.get("xyzOffset"), -1));
            d.tangentFrameOffset = int64_t(num(r.get("tangentFrameOffset"), 0));
            d.lmapCoordOffset = int64_t(num(r.get("lmapCoordOffset"), 0));
            d.colorOffset = int64_t(num(r.get("colorOffset"), 0));
            d.texCoordOffset = int64_t(num(r.get("texCoordOffset"), 0));
            if (const Value* dp = r.get("displacementOffset"); dp && dp->type == Value::Type::Array)
                for (size_t i = 0; i < dp->arr.size() && i < d.displacementOffset.size(); ++i)
                    d.displacementOffset[i] = int64_t(num(&dp->arr[i], 0));
            surfData.push_back(d);
        }
    } else if (sd && sd->get("bytes") && sd->get("bytes")->string()) {
        // 88-byte records (x64-zt structs.hpp:12012): u32 transientZone, layerCount, xyzOffset, tangentFrameOffset,
        // lmapCoordOffset, colorOffset, texCoordOffset, normalTransformOffset[7], displacementOffset[8] at 0x38.
        std::vector<uint8_t> raw;
        const int64_t stride = int64_t(num(sd->get("stride"), kSurfDataBytes));
        if (stride >= 0x1C && unhex(*sd->get("bytes")->string(), raw)) {
            for (size_t at = 0; at + size_t(stride) <= raw.size(); at += size_t(stride)) {
                SurfData d;
                d.layerCount = readU32(raw, at + 0x04);
                d.xyzOffset = readU32(raw, at + 0x08);
                d.tangentFrameOffset = readU32(raw, at + 0x0C);
                d.lmapCoordOffset = readU32(raw, at + 0x10);
                d.colorOffset = readU32(raw, at + 0x14);
                d.texCoordOffset = readU32(raw, at + 0x18);
                if (stride >= 0x58)
                    for (size_t i = 0; i < d.displacementOffset.size(); ++i) d.displacementOffset[i] = readU32(raw, at + 0x38 + 4 * i);
                surfData.push_back(d);
            }
        }
    }
    return surfData;
}

std::string materialOf(const Value& s) {
    const Value* mat = s.get("material");
    std::string name = mat && mat->get("name") && mat->get("name")->string() ? *mat->get("name")->string() : "";
    if (!name.empty() && name[0] == ',') name.erase(0, 1);
    return name;
}

// The gfx_map surfaces and their surfData, and which materials have a displaced surface.
struct GfxInfo {
    std::optional<Value> root;
    const Value* surfs = nullptr;
    std::vector<SurfData> surfData;
    std::set<std::string> displacedMaterials;
    std::set<int64_t> zones;  // transient zones holding a surface of a displaced material
};

bool readGfx(const std::string& gfxText, GfxInfo& g, std::string* err) {
    std::string jerr;
    g.root = json::parse(gfxText, &jerr);
    if (!g.root) {
        if (err) *err = "gfx_map JSON: " + jerr;
        return false;
    }
    const Value* f = fieldsOf(*g.root);
    g.surfs = f && f->get("surfaces") && f->get("surfaces")->get("surfaces") ? f->get("surfaces")->get("surfaces")->get("values") : nullptr;
    if (!g.surfs || g.surfs->type != Value::Type::Array) {
        if (err) *err = "gfx_map has no surfaces.surfaces.values";
        return false;
    }
    g.surfData = readSurfData(f);
    for (const Value& s : g.surfs->arr) {
        const int64_t k = int64_t(num(s.get("surfDataIndex"), -1));
        if (k < 0 || k >= int64_t(g.surfData.size())) continue;
        for (int64_t o : g.surfData[size_t(k)].displacementOffset)
            if (o > 0) {
                g.displacedMaterials.insert(materialOf(s));
                break;
            }
    }
    for (const Value& s : g.surfs->arr)
        if (g.displacedMaterials.count(materialOf(s))) g.zones.insert(int64_t(num(s.get("transientZone"), 0)));
    return true;
}

// Walks the surfaces exactly like stock_world.cpp buildWorld, for the displaced materials only, and fills one
// StockMeshDisplacement per material (mesh = -1 until matched to the StockWorld).
bool buildDisplacement(const GfxInfo& g, const std::map<int64_t, TrzData>& trz, const StockWorld& world, StockDisplacementData& out,
                       std::string* err) {
    out = StockDisplacementData();
    const auto& surfData = g.surfData;
    const auto surfaceLayers = [&](const Value& s) -> int {
        const int64_t k = int64_t(num(s.get("surfDataIndex"), -1));
        if (k < 0 || k >= int64_t(surfData.size())) return 1;
        return int(std::clamp<int64_t>(surfData[size_t(k)].layerCount, 1, kStockMaxLayers));
    };
    std::map<std::string, int> materialLayers;
    for (const Value& s : g.surfs->arr) {
        const std::string m = materialOf(s);
        if (!g.displacedMaterials.count(m)) continue;
        int& n = materialLayers[m];
        n = std::max(n, surfaceLayers(s));
    }

    struct Work {
        StockMeshDisplacement d;
        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vm;
        std::vector<Vec3> pos;
        std::vector<uint8_t> hasNormal;
    };
    std::map<std::string, Work> work;
    std::vector<std::string> order;
    size_t recordsBad = 0;

    for (const Value& s : g.surfs->arr) {
        const std::string name = materialOf(s);
        if (!g.displacedMaterials.count(name)) continue;
        const Value* tris = s.get("tris");
        if (!tris) continue;
        const int64_t posOffset = int64_t(num(tris->get("posOffset"), -1));
        const int64_t vertexCount = int64_t(num(tris->get("vertexCount"), -1));
        const int64_t triCount = int64_t(num(tris->get("triCount"), -1));
        const int64_t baseIndex = int64_t(num(tris->get("baseIndex"), -1));
        const int64_t zoneIndex = int64_t(num(s.get("transientZone"), 0));
        if (posOffset < 0 || vertexCount <= 0 || triCount <= 0 || baseIndex < 0) continue;
        const auto zit = trz.find(zoneIndex);
        if (zit == trz.end()) continue;
        const TrzData& z = zit->second;
        if (uint64_t(posOffset) + kPosStride * uint64_t(vertexCount) > z.pos.size() ||
            uint64_t(baseIndex) + 3 * uint64_t(triCount) > z.idx.size())
            continue;
        auto [wit, fresh] = work.try_emplace(name);
        Work& w = wit->second;
        if (fresh) {
            order.push_back(name);
            w.d.material = name;
            w.d.layerCount = std::max(1, materialLayers[name]);
        }
        const int meshLayers = w.d.layerCount;
        ++w.d.surfaces;

        // The same stream bases as buildWorld (texture, lightmap, colour: they are part of the vertex key).
        int64_t uvBase = -1, uvStride = 0;
        const int64_t surfDataIndex = int64_t(num(s.get("surfDataIndex"), -1));
        const SurfData* sdr = surfDataIndex >= 0 && surfDataIndex < int64_t(surfData.size()) ? &surfData[size_t(surfDataIndex)] : nullptr;
        if (sdr && sdr->texCoordOffset > 0 && sdr->layerCount >= 1 && !z.aux.empty()) {
            uvStride = int64_t(kUvLayerStride) * sdr->layerCount;
            if (uint64_t(sdr->texCoordOffset) + uint64_t(uvStride) * uint64_t(vertexCount) <= z.aux.size()) uvBase = sdr->texCoordOffset;
        }
        int64_t firstVertex = 0;
        if (sdr && sdr->xyzOffset >= 0 && posOffset >= sdr->xyzOffset && (posOffset - sdr->xyzOffset) % kPosStride == 0)
            firstVertex = (posOffset - sdr->xyzOffset) / kPosStride;
        int64_t lmBase = -1;
        const int64_t surfLightmap = int64_t(num(s.get("lightmapIndex"), 0));
        if (sdr && !z.aux.empty() && surfLightmap != kNoLightmap && surfLightmap >= 0) {
            const int64_t base = sdr->lmapCoordOffset + int64_t(kLmapStride) * firstVertex;
            if (sdr->lmapCoordOffset > 0 && uint64_t(base) + uint64_t(kLmapStride) * uint64_t(vertexCount) <= z.aux.size()) lmBase = base;
        }
        const int32_t vertexLightmap = lmBase >= 0 ? int32_t(surfLightmap) : -1;
        const int layers = surfaceLayers(s);
        int64_t colBase = -1;
        const int64_t colStride = layers > 4 ? 8 : 4;
        if (sdr && !z.aux.empty() && sdr->colorOffset > 0) {
            const int64_t base = sdr->colorOffset + colStride * firstVertex;
            if (uint64_t(base) + uint64_t(colStride) * uint64_t(vertexCount) <= z.aux.size()) colBase = base;
        }
        // Displacement and tangent frame records, block-relative like the colour record.
        std::array<int64_t, kStockMaxLayers> dispBase{};
        dispBase.fill(-1);
        bool displaced = false;
        int64_t tfBase = -1;
        if (sdr && !z.aux.empty()) {
            for (int l = 0; l < layers && l < kStockMaxLayers; ++l) {
                if (sdr->displacementOffset[size_t(l)] <= 0) continue;
                const int64_t base = sdr->displacementOffset[size_t(l)] + 4 * firstVertex;
                if (uint64_t(base) + 4 * uint64_t(vertexCount) <= z.aux.size()) {
                    dispBase[size_t(l)] = base;
                    displaced = true;
                    w.d.displacedLayers |= 1u << l;
                } else {
                    ++recordsBad;
                }
            }
            if (sdr->tangentFrameOffset > 0) {
                const int64_t base = sdr->tangentFrameOffset + 4 * firstVertex;
                if (uint64_t(base) + 4 * uint64_t(vertexCount) <= z.aux.size()) tfBase = base;
            }
        }
        if (displaced) ++w.d.displacedSurfaces;

        for (int64_t j = 0; j < triCount; ++j) {
            const uint16_t* t = &z.idx[size_t(baseIndex + 3 * j)];
            if (t[0] >= vertexCount || t[1] >= vertexCount || t[2] >= vertexCount) continue;
            ++w.d.triangles;
            bool triDisplaced = false;
            for (int c = 0; c < 3; ++c) {
                const uint32_t posKey = uint32_t(posOffset) + kPosStride * t[c];
                const uint32_t uvKey = uvBase >= 0 ? uint32_t(uvBase + uvStride * t[c]) : kNoUv;
                const uint32_t lmKey = lmBase >= 0 ? uint32_t(lmBase + int64_t(kLmapStride) * t[c]) : kNoUv;
                const uint32_t colKey = colBase >= 0 ? uint32_t(colBase + colStride * t[c]) : kNoUv;
                const VertexKey key{uint64_t(posKey) << 32 | uvKey, lmKey, uint32_t(zoneIndex), vertexLightmap, colKey};
                auto it = w.vm.find(key);
                if (it == w.vm.end()) {
                    it = w.vm.emplace(key, uint32_t(w.pos.size())).first;
                    w.pos.push_back(Vec3{readF32(z.pos, posKey), readF32(z.pos, posKey + 4), readF32(z.pos, posKey + 8)});
                    for (int l = 0; l < meshLayers; ++l) {
                        StockDisplacementVertex v;
                        if (l < kStockMaxLayers && dispBase[size_t(l)] >= 0) {
                            const uint32_t rec = readU32(z.aux, size_t(dispBase[size_t(l)] + 4 * int64_t(t[c])));
                            v.lo = stockHalfToFloat(uint16_t(rec & 0xFFFF));
                            v.hi = stockHalfToFloat(uint16_t(rec >> 16));
                        }
                        w.d.vertices.push_back(v);
                    }
                    if (tfBase >= 0) {
                        w.d.normal.push_back(stockTangentFrameNormal(readU32(z.aux, size_t(tfBase + 4 * int64_t(t[c])))));
                        w.hasNormal.push_back(1);
                    } else {
                        w.d.normal.push_back(Vec3{});
                        w.hasNormal.push_back(0);
                    }
                }
                for (int l = 0; l < meshLayers; ++l)
                    if (w.d.vertices[size_t(it->second) * size_t(meshLayers) + size_t(l)].lo > 0.0f) triDisplaced = true;
            }
            if (triDisplaced) ++w.d.displacedTriangles;
        }
    }

    // Match each material to its StockWorld mesh (one mesh per material name) and check the vertices.
    std::map<std::string, size_t> meshOf;
    for (size_t i = 0; i < world.meshes.size(); ++i) meshOf.emplace(world.meshes[i].material, i);
    for (const std::string& name : order) {
        Work& w = work[name];
        if (!w.d.displacedLayers) continue;
        const auto mit = meshOf.find(name);
        if (mit == meshOf.end()) {
            out.warnings.push_back(name + ": no StockWorld mesh of that material (skipped)");
            continue;
        }
        const size_t mi = mit->second;
        const ImportedMesh& m = world.meshes[mi];
        bool same = m.pos.size() == w.pos.size() && mi < world.layerCount.size() && world.layerCount[mi] == w.d.layerCount;
        for (size_t k = 0; same && k < m.pos.size(); ++k)
            same = m.pos[k].x == w.pos[k].x && m.pos[k].y == w.pos[k].y && m.pos[k].z == w.pos[k].z;
        if (!same) {
            out.warnings.push_back(name + ": the vertices do not match the StockWorld mesh (another export?): skipped");
            continue;
        }
        w.d.mesh = mi;
        // Vertices without a tangent frame: the world's area-weighted normal.
        bool any = false;
        for (size_t k = 0; k < w.d.normal.size(); ++k) {
            if (w.hasNormal[k]) any = true;
            else if (k < m.normal.size()) w.d.normal[k] = m.normal[k];
        }
        if (!any) w.d.normal.clear();
        out.displacedSurfaces += w.d.displacedSurfaces;
        out.vertices += w.pos.size();
        out.triangles += w.d.triangles;
        out.displacedTriangles += w.d.displacedTriangles;
        out.meshes.push_back(std::move(w.d));
    }
    std::sort(out.meshes.begin(), out.meshes.end(), [](const auto& a, const auto& b) { return a.mesh < b.mesh; });
    if (recordsBad) out.warnings.push_back(std::to_string(recordsBad) + " displacement records out of the aux stream: not read");
    (void)err;
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

float sat(float x) { return std::clamp(x, 0.0f, 1.0f); }

}  // namespace

float stockHalfToFloat(uint16_t h) {
    const bool neg = (h >> 15) != 0;
    const int exp = (h >> 10) & 31;
    const int man = h & 1023;
    float v;
    if (exp == 0)
        v = std::ldexp(float(man), -24);
    else if (exp == 31)
        v = man ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else
        v = std::ldexp(float(1024 + man), exp - 25);
    return neg ? -v : v;
}

Vec3 stockTangentFrameNormal(uint32_t w) {
    // Displaced VS f06c0749 lines 84-113 (and VT 9a82745f): 10 / 10 / 9 bit components in [-1, 1] scaled by sqrt(1/2),
    // bit 29 the bitangent sign, bits 30-31 the index of the dropped (largest) quaternion component.
    const double a = (double(w & 1023) / 1023.0 * 2.0 - 1.0) * 0.707107;
    const double b = (double((w >> 10) & 1023) / 1023.0 * 2.0 - 1.0) * 0.707107;
    const double c = (double((w >> 20) & 511) / 511.0 * 2.0 - 1.0) * 0.707107;
    const double s = std::sqrt(1.0 - std::min(a * a + b * b + c * c, 1.0));
    double q[4];
    switch (w >> 30) {
    case 0: q[0] = s, q[1] = a, q[2] = b, q[3] = c; break;
    case 1: q[0] = a, q[1] = s, q[2] = b, q[3] = c; break;
    case 2: q[0] = a, q[1] = b, q[2] = s, q[3] = c; break;
    default: q[0] = a, q[1] = b, q[2] = c, q[3] = s; break;
    }
    const Vec3 n{2.0 * (q[0] * q[2] + q[1] * q[3]), 2.0 * (q[1] * q[2] - q[0] * q[3]), 1.0 - 2.0 * (q[0] * q[0] + q[1] * q[1])};
    const double len = length(n);
    return len > 1e-12 ? n * (1.0 / len) : Vec3{0, 0, 1};
}

bool parseStockDisplacement(const std::string& gfxMapJson, const std::vector<std::string>& trzoneJson, const StockWorld& world,
                            StockDisplacementData& out, std::string* err) {
    out = StockDisplacementData();
    GfxInfo g;
    if (!readGfx(gfxMapJson, g, err)) return false;
    if (g.displacedMaterials.empty()) return true;
    std::map<int64_t, TrzData> trz;
    for (const std::string& text : trzoneJson) {
        int64_t zone = 0;
        TrzData d;
        bool empty = false;
        if (!extractTrz(text, zone, d, err, &empty)) return false;
        if (!empty) trz[zone] = std::move(d);
    }
    return buildDisplacement(g, trz, world, out, err);
}

bool readStockDisplacement(const std::string& zoneDir, const StockWorld& world, StockDisplacementData& out, std::string* err) {
    out = StockDisplacementData();
    const fs::path base = fs::path(zoneDir) / "assets";
    const auto gfx = jsonFilesUnder(base / "gfx_map");
    if (gfx.size() != 1) {
        if (err) *err = "expected one gfx_map JSON under " + (base / "gfx_map").string() + ", found " + std::to_string(gfx.size());
        return false;
    }
    std::string gfxText;
    if (!readFile(gfx.front(), gfxText, err)) return false;
    GfxInfo g;
    if (!readGfx(gfxText, g, err)) return false;
    gfxText.clear();
    gfxText.shrink_to_fit();
    if (g.displacedMaterials.empty()) return true;  // most maps: the transient zones are not read
    std::vector<fs::path> allTrz = jsonFilesUnder(base / "gfx_map_trzone");
    {
        std::error_code ec;
        const fs::path streamed = fs::path(zoneDir) / "streamed";
        if (fs::is_directory(streamed, ec))
            for (fs::recursive_directory_iterator it(streamed, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec) && it->path().extension() == ".json" &&
                    it->path().string().find("gfx_map_trzone") != std::string::npos)
                    allTrz.push_back(it->path());
    }
    std::map<int64_t, TrzData> trz;
    for (const fs::path& p : allTrz) {
        std::string text;
        if (!readFile(p, text, err)) return false;
        int64_t zone = 0;
        TrzData d;
        bool empty = false;
        if (!extractTrz(text, zone, d, err, &empty)) return false;
        // Only the zones holding displaced materials are kept (memory: a big map has hundreds of tiles).
        if (!empty && g.zones.count(zone)) trz[zone] = std::move(d);
    }
    return buildDisplacement(g, trz, world, out, err);
}

// ---------------------------------------------------------------------------------------------------------------------
// Height maps

namespace {

void buildMips(StockHeightMap& m) {
    while (true) {
        const StockHeightMap::Level& src = m.levels.back();
        if (src.width <= 1 && src.height <= 1) break;
        StockHeightMap::Level dst;
        dst.width = std::max(1, src.width / 2);
        dst.height = std::max(1, src.height / 2);
        dst.texels.resize(size_t(dst.width) * size_t(dst.height));
        for (int y = 0; y < dst.height; ++y)
            for (int x = 0; x < dst.width; ++x) {
                // 2x2 box (1-wide sources repeat their single row / column)
                const int x0 = std::min(2 * x, src.width - 1), x1 = std::min(2 * x + 1, src.width - 1);
                const int y0 = std::min(2 * y, src.height - 1), y1 = std::min(2 * y + 1, src.height - 1);
                const auto at = [&](int xx, int yy) { return src.texels[size_t(yy) * size_t(src.width) + size_t(xx)]; };
                dst.texels[size_t(y) * size_t(dst.width) + size_t(x)] = 0.25f * (at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1));
            }
        m.levels.push_back(std::move(dst));
    }
}

float sampleLevel(const StockHeightMap::Level& l, double u, double v) {
    // Texel centres at (i + 0.5) / size, wrap addressing (the world sampler s3 wraps; the layer UVs run past 0..1).
    const double x = u * l.width - 0.5, y = v * l.height - 0.5;
    const double fx = std::floor(x), fy = std::floor(y);
    const float tx = float(x - fx), ty = float(y - fy);
    const auto wrap = [](int64_t i, int n) { return int(((i % n) + n) % n); };
    const int x0 = wrap(int64_t(fx), l.width), x1 = wrap(int64_t(fx) + 1, l.width);
    const int y0 = wrap(int64_t(fy), l.height), y1 = wrap(int64_t(fy) + 1, l.height);
    const auto at = [&](int xx, int yy) { return l.texels[size_t(yy) * size_t(l.width) + size_t(xx)]; };
    const float a = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
    const float b = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
    return a + (b - a) * ty;
}

}  // namespace

StockHeightMap stockHeightMapFromR8(int width, int height, const uint8_t* data, size_t pixelStride, size_t rowStride) {
    StockHeightMap m;
    if (width <= 0 || height <= 0 || !data) return m;
    if (pixelStride == 0) pixelStride = 1;
    if (rowStride == 0) rowStride = size_t(width) * pixelStride;
    StockHeightMap::Level l;
    l.width = width, l.height = height;
    l.texels.resize(size_t(width) * size_t(height));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) l.texels[size_t(y) * size_t(width) + size_t(x)] = float(data[size_t(y) * rowStride + size_t(x) * pixelStride]) / 255.0f;
    m.levels.push_back(std::move(l));
    buildMips(m);
    return m;
}

StockHeightMap stockHeightMapFromR16(int width, int height, const uint16_t* data) {
    StockHeightMap m;
    if (width <= 0 || height <= 0 || !data) return m;
    StockHeightMap::Level l;
    l.width = width, l.height = height;
    l.texels.resize(size_t(width) * size_t(height));
    for (size_t i = 0; i < l.texels.size(); ++i) l.texels[i] = float(data[i]) / 65535.0f;
    m.levels.push_back(std::move(l));
    buildMips(m);
    return m;
}

float sampleStockHeightMap(const StockHeightMap& map, double u, double v, double lod) {
    if (map.empty()) return 0.5f;
    if (!std::isfinite(u) || !std::isfinite(v)) return 0.5f;
    const double maxLod = double(map.levels.size() - 1);
    lod = std::isfinite(lod) ? std::clamp(lod, 0.0, maxLod) : 0.0;
    const size_t l0 = size_t(std::floor(lod));
    const float t = float(lod - double(l0));
    const float a = sampleLevel(map.levels[l0], u, v);
    if (t <= 0.0f || l0 + 1 >= map.levels.size()) return a;
    return a + (sampleLevel(map.levels[l0 + 1], u, v) - a) * t;
}

// ---------------------------------------------------------------------------------------------------------------------
// CPU displacement

namespace {

struct SubVertex {
    Vec3 flat;                    // undisplaced position (linear in the source triangle)
    uint32_t origin = 0;          // a source triangle containing it
    std::array<double, 3> bary{}; // relative to that triangle's corners
    float relief = 0.0f;          // sum over the layers of amp * max(|scale + bias|, |bias|): the largest possible move
    float logHi = 0.0f;           // log2(hi) of the first displaced layer (UV per inch)
};

struct SubTri {
    uint32_t v[3];
    uint32_t origin;
    std::array<std::array<double, 3>, 3> bary;  // per corner, relative to `origin`
};

uint32_t lerpBytes(const uint32_t* c, const std::array<double, 3>& b) {
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        const double v = b[0] * double((c[0] >> s) & 0xFF) + b[1] * double((c[1] >> s) & 0xFF) + b[2] * double((c[2] >> s) & 0xFF);
        out |= uint32_t(std::clamp(int(std::lround(v)), 0, 255)) << s;
    }
    return out;
}

}  // namespace

bool displaceStockMesh(const StockWorld& world, const StockMeshDisplacement& disp, const std::vector<StockDisplacementLayer>& layers,
                       const StockDisplacementOptions& opt, StockDisplacedMesh& out, std::string* err) {
    out = StockDisplacedMesh();
    const size_t mi = disp.mesh;
    if (mi >= world.meshes.size()) {
        if (err) *err = "mesh index out of range";
        return false;
    }
    const ImportedMesh& src = world.meshes[mi];
    const size_t nv = src.pos.size();
    const int L = std::max(1, disp.layerCount);
    if (disp.vertices.size() != nv * size_t(L) || (!disp.normal.empty() && disp.normal.size() != nv)) {
        if (err) *err = "displacement data does not match mesh " + src.material;
        return false;
    }
    const auto par = [&](const auto& vecs) {
        using T = std::remove_reference_t<decltype(vecs[0])>;
        return mi < vecs.size() ? &vecs[mi] : static_cast<T*>(nullptr);
    };
    const auto* lmUv = par(world.lightmapUv);
    const auto* lmIdx = par(world.lightmapIndex);
    const auto* layUv = par(world.layerUv);
    const auto* vcol = par(world.vertexColor);
    const auto* vcolHi = par(world.vertexColorHigh);
    const auto* layNt = par(world.layerNormalTransform);
    const bool hasLm = lmUv && lmUv->size() == nv && lmIdx && lmIdx->size() == nv;
    const bool hasLayUv = layUv && layUv->size() == nv * size_t(L);
    const bool hasCol = vcol && vcol->size() == nv;
    const bool hasColHi = vcolHi && vcolHi->size() == nv;
    const bool hasNt = L > 1 && layNt && layNt->size() == nv * size_t(L - 1);
    const bool hasUv = src.uv.size() == nv;
    const auto normalOf = [&](uint32_t k) -> Vec3 {
        if (!disp.normal.empty()) return disp.normal[k];
        return k < src.normal.size() ? src.normal[k] : Vec3{0, 0, 1};
    };

    // Active layers: those with a height description; per vertex amp for each.
    std::vector<StockDisplacementLayer> act;
    for (const auto& l : layers)
        if (l.layer >= 0 && l.layer < L) act.push_back(l);
    std::vector<float> amp(nv * act.size(), 0.0f);
    for (size_t k = 0; k < nv; ++k)
        for (size_t a = 0; a < act.size(); ++a) {
            const StockDisplacementVertex& r = disp.vertices[k * size_t(L) + size_t(act[a].layer)];
            amp[k * act.size() + a] = r.lo > 0.0f ? stockDisplacementAmplitude(r.lo, r.hi, opt.sceneParam) : 0.0f;
        }
    const auto loOf = [&](uint32_t k, size_t a) { return disp.vertices[size_t(k) * size_t(L) + size_t(act[a].layer)].lo; };
    const auto reliefOf = [&](size_t k) {
        float r = 0.0f;
        for (size_t a = 0; a < act.size(); ++a)
            r += std::fabs(amp[k * act.size() + a]) * std::max(std::fabs(act[a].scale + act[a].bias), std::fabs(act[a].bias));
        return r;
    };
    const auto logHiOf = [&](size_t k) {
        return act.empty() ? 0.0f : std::log2(std::max(disp.vertices[k * size_t(L) + size_t(act[0].layer)].hi, 1e-8f));
    };

    // Source triangles; an inactive one (no corner with lo > 0 in an active layer) is copied as is.
    const size_t ntri = src.idx.size() / 3;
    out.sourceTriangles = ntri;
    std::vector<SubVertex> verts(nv);
    for (size_t k = 0; k < nv; ++k) verts[k].flat = src.pos[k], verts[k].bary = {1, 0, 0}, verts[k].relief = reliefOf(k), verts[k].logHi = logHiOf(k);
    std::vector<uint8_t> vertexSet(nv, 0);  // origin assigned
    std::vector<SubTri> tris;
    tris.reserve(ntri);
    std::vector<uint8_t> triActive;
    for (size_t t = 0; t < ntri; ++t) {
        SubTri s;
        bool bad = false, active = false;
        for (int c = 0; c < 3; ++c) {
            s.v[c] = src.idx[3 * t + size_t(c)];
            if (s.v[c] >= nv) bad = true;
        }
        if (bad) continue;
        s.origin = uint32_t(t);
        s.bary = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
        for (int c = 0; c < 3; ++c) {
            if (!vertexSet[s.v[c]]) {
                vertexSet[s.v[c]] = 1;
                verts[s.v[c]].origin = uint32_t(t);
                verts[s.v[c]].bary = s.bary[size_t(c)];
            }
            for (size_t a = 0; a < act.size(); ++a)
                if (loOf(s.v[c], a) > 0.0f) active = true;
        }
        tris.push_back(s);
        triActive.push_back(active ? 1 : 0);
        if (active) ++out.subdividedTriangles;
    }

    // Conforming bisection: every pass halves each edge that needs it; the decision depends on the edge alone (its
    // length and its two vertices), so both triangles of an edge split it and share the midpoint (keyed by the vertex
    // pair): no T-junctions, no cracks. An edge is split when it is longer than max(targetEdge, targetUv / hi) and the
    // largest possible move at one of its ends is at least minRelief.
    std::unordered_map<uint64_t, uint32_t> mid;
    const auto edgeKey = [](uint32_t a, uint32_t b) { return a < b ? uint64_t(a) << 32 | b : uint64_t(b) << 32 | a; };
    const double target = opt.targetEdge;
    if (target > 0.0) {
        for (int pass = 0; pass < std::max(0, opt.maxPasses); ++pass) {
            // Count first: stop before going over the triangle budget.
            size_t grow = 0;
            bool anySplit = false;
            std::vector<uint8_t> splitMask(tris.size(), 0);
            for (size_t t = 0; t < tris.size(); ++t) {
                const SubTri& s = tris[t];
                uint8_t m = 0;
                for (int e = 0; e < 3; ++e) {
                    const SubVertex& va = verts[s.v[e]];
                    const SubVertex& vb = verts[s.v[(e + 1) % 3]];
                    if (std::max(va.relief, vb.relief) < float(opt.minRelief) || std::max(va.relief, vb.relief) <= 0.0f) continue;
                    const double hi = std::exp2(double(std::max(va.logHi, vb.logHi)));
                    const double limit = std::max(target, opt.targetUv > 0.0 ? opt.targetUv / hi : 0.0);
                    if (length(vb.flat - va.flat) > limit) m |= uint8_t(1 << e);
                }
                splitMask[t] = m;
                const int n = (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1);
                grow += size_t(n);  // 1 split: +1 triangle, 2: +2, 3: +3
                if (m) anySplit = true;
            }
            if (!anySplit) break;
            if (tris.size() + grow > opt.maxTriangles) {
                out.truncated = true;
                break;
            }
            std::vector<SubTri> next;
            std::vector<uint8_t> nextActive;
            next.reserve(tris.size() + grow);
            nextActive.reserve(tris.size() + grow);
            for (size_t t = 0; t < tris.size(); ++t) {
                const SubTri& s = tris[t];
                const uint8_t m = splitMask[t];
                if (!m) {
                    next.push_back(s);
                    nextActive.push_back(triActive[t]);
                    continue;
                }
                uint32_t mv[3] = {0, 0, 0};
                std::array<double, 3> mb[3];
                for (int e = 0; e < 3; ++e) {
                    if (!(m & (1 << e))) continue;
                    const uint32_t a = s.v[e], b = s.v[(e + 1) % 3];
                    for (int i = 0; i < 3; ++i) mb[e][size_t(i)] = 0.5 * (s.bary[size_t(e)][size_t(i)] + s.bary[size_t((e + 1) % 3)][size_t(i)]);
                    const uint64_t key = edgeKey(a, b);
                    auto it = mid.find(key);
                    if (it == mid.end()) {
                        SubVertex v;
                        v.flat = (verts[a].flat + verts[b].flat) * 0.5;
                        v.origin = s.origin;
                        v.bary = mb[e];
                        v.relief = 0.5f * (verts[a].relief + verts[b].relief);  // linear along the edge
                        v.logHi = 0.5f * (verts[a].logHi + verts[b].logHi);
                        it = mid.emplace(key, uint32_t(verts.size())).first;
                        verts.push_back(v);
                    }
                    mv[e] = it->second;
                }
                const auto emit = [&](uint32_t a, const std::array<double, 3>& ba, uint32_t b, const std::array<double, 3>& bb, uint32_t c,
                                      const std::array<double, 3>& bc) {
                    SubTri n;
                    n.v[0] = a, n.v[1] = b, n.v[2] = c;
                    n.origin = s.origin;
                    n.bary = {ba, bb, bc};
                    next.push_back(n);
                    nextActive.push_back(1);
                };
                const auto& B = s.bary;
                const uint32_t* V = s.v;
                if (m == 7) {
                    emit(V[0], B[0], mv[0], mb[0], mv[2], mb[2]);
                    emit(mv[0], mb[0], V[1], B[1], mv[1], mb[1]);
                    emit(mv[2], mb[2], mv[1], mb[1], V[2], B[2]);
                    emit(mv[0], mb[0], mv[1], mb[1], mv[2], mb[2]);
                    continue;
                }
                const int n = (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1);
                if (n == 1) {
                    const int e = m == 1 ? 0 : m == 2 ? 1 : 2;  // split edge (e, e+1); opposite corner e+2
                    const int i0 = e, i1 = (e + 1) % 3, i2 = (e + 2) % 3;
                    emit(V[i0], B[i0], mv[e], mb[e], V[i2], B[i2]);
                    emit(mv[e], mb[e], V[i1], B[i1], V[i2], B[i2]);
                    continue;
                }
                // Two splits: rotate so the unsplit edge is (i2, i0); split edges (i0, i1) and (i1, i2).
                const int keep = !(m & 1) ? 0 : !(m & 2) ? 1 : 2;  // the unsplit edge index
                const int i0 = (keep + 1) % 3, i1 = (keep + 2) % 3, i2 = keep;
                const int ea = i0, eb = i1;  // edge (i0, i1) is index i0, edge (i1, i2) is index i1
                emit(mv[ea], mb[ea], V[i1], B[i1], mv[eb], mb[eb]);
                // The quad (i0, m_a, m_b, i2): split along the shorter diagonal.
                const double d1 = length(verts[mv[eb]].flat - verts[V[i0]].flat);
                const double d2 = length(verts[V[i2]].flat - verts[mv[ea]].flat);
                if (d1 <= d2) {
                    emit(V[i0], B[i0], mv[ea], mb[ea], mv[eb], mb[eb]);
                    emit(V[i0], B[i0], mv[eb], mb[eb], V[i2], B[i2]);
                } else {
                    emit(V[i0], B[i0], mv[ea], mb[ea], V[i2], B[i2]);
                    emit(mv[ea], mb[ea], mv[eb], mb[eb], V[i2], B[i2]);
                }
            }
            tris.swap(next);
            triActive.swap(nextActive);
            out.passes = pass + 1;
        }
    }

    // Evaluate every vertex from its source triangle: attributes by barycentric interpolation, then Phong and the
    // displacement of the domain shader (an original vertex has a unit barycentric: plain VS displacement).
    const size_t n = verts.size();
    out.layerCount = L;
    out.mesh.material = src.material;
    out.mesh.pos.resize(n);
    out.mesh.uv.resize(n);
    out.lightmapUv.resize(n);
    out.lightmapIndex.resize(n);
    out.layerUv.resize(n * size_t(L));
    out.vertexColor.resize(n);
    out.vertexColorHigh.resize(n);
    if (L > 1) out.layerNormalTransform.resize(n * size_t(L - 1));
    out.offset.assign(n, 0.0f);
    const double lodEdge = opt.lodEdge > 0.0 ? opt.lodEdge : opt.targetEdge > 0.0 ? opt.targetEdge : 8.0;
    for (size_t k = 0; k < n; ++k) {
        const SubVertex& sv = verts[k];
        const auto& b = sv.bary;
        // Unused source vertices (no triangle) keep their own data.
        uint32_t c[3];
        if (k < nv && !vertexSet[k]) {
            c[0] = c[1] = c[2] = uint32_t(k);
        } else {
            for (int i = 0; i < 3; ++i) c[i] = src.idx[3 * size_t(sv.origin) + size_t(i)];
        }
        if (hasUv)
            out.mesh.uv[k] = {b[0] * src.uv[c[0]][0] + b[1] * src.uv[c[1]][0] + b[2] * src.uv[c[2]][0],
                              b[0] * src.uv[c[0]][1] + b[1] * src.uv[c[1]][1] + b[2] * src.uv[c[2]][1]};
        for (int l = 0; l < L; ++l) {
            std::array<float, 2> uv{0.0f, 0.0f};
            if (hasLayUv)
                for (int i = 0; i < 3; ++i) {
                    const auto& s = (*layUv)[size_t(c[i]) * size_t(L) + size_t(l)];
                    uv[0] += float(b[size_t(i)]) * s[0];
                    uv[1] += float(b[size_t(i)]) * s[1];
                }
            out.layerUv[k * size_t(L) + size_t(l)] = uv;
        }
        if (hasLm && (*lmUv)[c[0]][0] >= 0 && (*lmUv)[c[1]][0] >= 0 && (*lmUv)[c[2]][0] >= 0 && (*lmIdx)[c[0]] >= 0) {
            std::array<float, 2> uv{0.0f, 0.0f};
            for (int i = 0; i < 3; ++i) {
                uv[0] += float(b[size_t(i)]) * (*lmUv)[c[i]][0];
                uv[1] += float(b[size_t(i)]) * (*lmUv)[c[i]][1];
            }
            out.lightmapUv[k] = uv;
            out.lightmapIndex[k] = (*lmIdx)[c[0]];
        } else {
            out.lightmapUv[k] = {-1.0f, -1.0f};
            out.lightmapIndex[k] = -1;
        }
        {
            const uint32_t cc[3] = {hasCol ? (*vcol)[c[0]] : 0xFFFFFFFFu, hasCol ? (*vcol)[c[1]] : 0xFFFFFFFFu, hasCol ? (*vcol)[c[2]] : 0xFFFFFFFFu};
            out.vertexColor[k] = lerpBytes(cc, b);
            const uint32_t ch[3] = {hasColHi ? (*vcolHi)[c[0]] : 0xFFFFFFFFu, hasColHi ? (*vcolHi)[c[1]] : 0xFFFFFFFFu,
                                    hasColHi ? (*vcolHi)[c[2]] : 0xFFFFFFFFu};
            out.vertexColorHigh[k] = lerpBytes(ch, b);
        }
        for (int l = 1; l < L; ++l) {
            uint32_t nt[3];
            for (int i = 0; i < 3; ++i) nt[i] = hasNt ? (*layNt)[size_t(c[i]) * size_t(L - 1) + size_t(l - 1)] : 0xFF8080FFu;
            out.layerNormalTransform[k * size_t(L - 1) + size_t(l - 1)] = lerpBytes(nt, b);
        }

        // Geometry.
        const Vec3 P[3] = {src.pos[c[0]], src.pos[c[1]], src.pos[c[2]]};
        const Vec3 N[3] = {normalOf(c[0]), normalOf(c[1]), normalOf(c[2])};
        Vec3 p = P[0] * b[0] + P[1] * b[1] + P[2] * b[2];
        // Phong tessellation (DS 2a7a2c67:304-321): alpha = 0.75 saturate(sum b_i (1 - saturate(w)) lo_i), first layer.
        if (opt.phong && !act.empty()) {
            const float w1 = 1.0f - sat(act[0].w);
            float lo = 0.0f;
            for (int i = 0; i < 3; ++i) lo += float(b[size_t(i)]) * w1 * loOf(c[i], 0);
            const double alpha = 0.75 * double(sat(lo));
            if (alpha > 0.0) {
                Vec3 corr{};
                for (int i = 0; i < 3; ++i) corr = corr + N[i] * (b[size_t(i)] * dot(p - P[i], N[i]));
                p = p - corr * alpha;
            }
        }
        float moved = 0.0f;
        for (size_t a = 0; a < act.size(); ++a) {
            const StockDisplacementLayer& Ly = act[a];
            float ampS[3], ampB[3], sumS = 0.0f, sumB = 0.0f, logHi = 0.0f, wlo = 0.0f;
            bool any = false;
            for (int i = 0; i < 3; ++i) {
                const float am = amp[size_t(c[i]) * act.size() + a];
                if (am != 0.0f) any = true;
                ampS[i] = am * Ly.scale;
                ampB[i] = am * Ly.bias;
                sumS += float(b[size_t(i)]) * ampS[i];
                sumB += float(b[size_t(i)]) * ampB[i];
                const StockDisplacementVertex& r = disp.vertices[size_t(c[i]) * size_t(L) + size_t(Ly.layer)];
                logHi += float(b[size_t(i)]) * std::log2(std::max(r.hi, 1e-8f));
                wlo += float(b[size_t(i)]) * sat(Ly.w) * r.lo;
            }
            if (!any) continue;
            // Height at the layer's own UV (the game's v), mip from the edge length (VS f06c0749:117-130, DS:221-247).
            const auto& luv = out.layerUv[k * size_t(L) + size_t(Ly.layer)];
            // The edge length the mesh was cut to here: lodEdge, or max(targetEdge, targetUv / hi).
            const double edge = opt.lodEdge > 0.0 ? opt.lodEdge
                                : opt.targetEdge > 0.0 ? std::max(opt.targetEdge, opt.targetUv > 0.0 ? opt.targetUv / std::exp2(double(logHi)) : 0.0)
                                                       : lodEdge;
            const double lod = opt.autoLod && Ly.height && !Ly.height->empty()
                                   ? std::log2(double(Ly.height->width()) * edge) + double(logHi) + opt.lodBias
                                   : opt.lod;
            const float h = Ly.height ? sampleStockHeightMap(*Ly.height, luv[0], 1.0 - double(luv[1]), lod) : 0.5f;
            const float d = h * sumS + sumB;
            // Direction (DS:241-302): the corner normals weighted by b_i max(amp_i scale, 1e-4), normalised; the length
            // factor 1 + (min(max(|sum|, min_i a_i / (n_i . dir)) / sum_b, 2) - 1) * sum b_i saturate(w) lo_i.
            Vec3 dir{};
            double aw[3];
            for (int i = 0; i < 3; ++i) {
                aw[i] = std::max(double(ampS[i]), 1e-4);
                dir = dir + N[i] * (b[size_t(i)] * aw[i]);
            }
            const double len = length(dir);
            if (len < 1e-12) continue;
            dir = dir * (1.0 / len);
            double k2 = 1.0;
            if (wlo > 0.0f) {
                double best = 0.0;  // max_i (n_i . dir) / a_i over the corners with weight
                for (int i = 0; i < 3; ++i)
                    if (b[size_t(i)] > 1e-5) best = std::max(best, dot(N[i], dir) / aw[i]);
                const double r = best > 0.0 ? 1.0 / best : 0.0;
                const double ratio = std::min(std::max(len, r) / std::max(double(sumS), 1e-4), 2.0);
                k2 = 1.0 + (ratio - 1.0) * double(wlo);
            }
            p = p + dir * (k2 * double(d));
            moved += float(k2 * double(d));
        }
        out.mesh.pos[k] = p;
        out.offset[k] = moved;
    }
    out.mesh.idx.reserve(tris.size() * 3);
    for (const SubTri& s : tris)
        for (int c = 0; c < 3; ++c) out.mesh.idx.push_back(s.v[c]);
    // Normals of the displaced surface (area weighted), as StockWorld computes them.
    out.mesh.normal.assign(n, Vec3{});
    for (size_t i = 0; i + 2 < out.mesh.idx.size(); i += 3) {
        const uint32_t a = out.mesh.idx[i], b = out.mesh.idx[i + 1], c = out.mesh.idx[i + 2];
        const Vec3 fn = cross(out.mesh.pos[b] - out.mesh.pos[a], out.mesh.pos[c] - out.mesh.pos[a]);
        out.mesh.normal[a] = out.mesh.normal[a] + fn;
        out.mesh.normal[b] = out.mesh.normal[b] + fn;
        out.mesh.normal[c] = out.mesh.normal[c] + fn;
    }
    for (size_t k = 0; k < n; ++k) {
        const double len = length(out.mesh.normal[k]);
        out.mesh.normal[k] = len > 1e-12 ? out.mesh.normal[k] * (1.0 / len) : (k < nv && k < src.normal.size() ? src.normal[k] : Vec3{0, 0, 1});
    }
    return true;
}

double stockDisplacementEdgeForBudget(const StockWorld& world, const StockDisplacementData& data, size_t triangleBudget,
                                      double minEdge, double targetUv) {
    struct T {
        double area, minLimit;  // area (sq in) and targetUv / hi
    };
    std::vector<T> tris;
    for (const StockMeshDisplacement& md : data.meshes) {
        if (md.mesh >= world.meshes.size()) continue;
        const ImportedMesh& m = world.meshes[md.mesh];
        const int L = std::max(1, md.layerCount);
        if (md.vertices.size() != m.pos.size() * size_t(L)) continue;
        int first = -1;
        for (int l = 0; l < L && first < 0; ++l)
            if (md.displacedLayers & (1u << l)) first = l;
        if (first < 0) continue;
        for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
            float hi = 0.0f;
            bool on = false;
            for (int c = 0; c < 3; ++c) {
                const uint32_t k = m.idx[i + size_t(c)];
                if (k >= m.pos.size()) continue;
                for (int l = 0; l < L; ++l)
                    if (md.vertices[size_t(k) * size_t(L) + size_t(l)].lo > 0.0f) on = true;
                hi = std::max(hi, md.vertices[size_t(k) * size_t(L) + size_t(first)].hi);
            }
            if (!on) continue;
            const double a = 0.5 * length(cross(m.pos[m.idx[i + 1]] - m.pos[m.idx[i]], m.pos[m.idx[i + 2]] - m.pos[m.idx[i]]));
            tris.push_back({a, targetUv > 0.0 && hi > 0.0f ? targetUv / double(hi) : 0.0});
        }
    }
    const auto count = [&](double e) {
        double n = 0;
        for (const T& t : tris) {
            const double lim = std::max(e, t.minLimit);
            n += std::max(1.0, 9.0 * t.area / (lim * lim));  // measured: 4 A / E^2 undercounts red-green bisection ~2x
        }
        return n;
    };
    const double budget = double(triangleBudget);
    double lo = std::max(minEdge, 1e-3);
    if (count(lo) <= budget) return lo;
    double hi = lo;
    for (int i = 0; i < 40 && count(hi) > budget; ++i) hi *= 2.0;
    for (int i = 0; i < 30; ++i) {
        const double midE = 0.5 * (lo + hi);
        (count(midE) > budget ? lo : hi) = midE;
    }
    return hi;
}

void replaceStockWorldMesh(StockWorld& world, size_t mesh, StockDisplacedMesh&& m) {
    if (mesh >= world.meshes.size()) return;
    for (const Vec3& p : m.mesh.pos) world.bounds.add(p);
    world.meshes[mesh] = std::move(m.mesh);
    const auto put = [&](auto& vecs, auto&& v) {
        if (mesh < vecs.size()) vecs[mesh] = std::move(v);
    };
    put(world.lightmapUv, std::move(m.lightmapUv));
    put(world.lightmapIndex, std::move(m.lightmapIndex));
    if (mesh < world.layerCount.size()) world.layerCount[mesh] = m.layerCount;
    put(world.layerUv, std::move(m.layerUv));
    put(world.vertexColor, std::move(m.vertexColor));
    put(world.vertexColorHigh, std::move(m.vertexColorHigh));
    put(world.layerNormalTransform, std::move(m.layerNormalTransform));
}

}  // namespace iw8r
