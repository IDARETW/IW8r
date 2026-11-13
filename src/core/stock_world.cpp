#include "stock_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>

#include "json_lite.h"

namespace iw8r {
namespace {

using json::Value;
namespace fs = std::filesystem;

constexpr uint32_t kPosStride = 12;  // float3, verified from the surface offsets (docs/research/30)
constexpr uint32_t kUvLayerStride = 8;  // float2 per texture layer, layer 0 first, layers interleaved per vertex
constexpr uint32_t kNoUv = 0xFFFFFFFFu;  // vertex key part for vertices without texture coordinates
constexpr uint32_t kLmapStride = 8;     // float2 lightmap UV per vertex (research 94 section 2)
constexpr int64_t kNoLightmap = 510;    // GfxSurface.lightmapIndex of a surface without a lightmap
constexpr int64_t kSurfDataBytes = 88;  // sizeof(GfxWorldSurfData)

// The per-surface fields of GfxWorldSurfData (gfx_map surfaces.surfData) that the reader needs.
using SurfData = StockWorldSurfaceTable::SurfData;

// Mesh vertex key: (position stream offset, texture stream offset), the lightmap UV stream offset, the transient zone
// and the lightmap index. The offsets are byte offsets in ONE zone's streams (every streamed tile starts again at 0),
// so the zone is part of the key: vertices shared between surfaces of one vertex block merge and nothing else does.
struct VertexKey {
    uint64_t posUv = 0;  // the texture record also selects every layer's UV and the normal transforms (same vertex j)
    uint32_t lmap = 0;
    uint32_t zone = 0;
    int32_t lightmap = -1;
    uint32_t color = 0;  // colour record offset (kNoUv when none)
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

uint32_t readU32(const std::vector<uint8_t>& b, size_t off) {
    return uint32_t(b[off]) | uint32_t(b[off + 1]) << 8 | uint32_t(b[off + 2]) << 16 | uint32_t(b[off + 3]) << 24;
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
    // ACTS writes a small array as decoded "values" instead of hex "bytes" (some streamed world tiles of mp_donetsk).
    const auto valuesOf = [&](const char* key) -> const Value* {
        const Value* node = dv ? dv->get(key) : nullptr;
        const Value* v = node ? node->get("values") : nullptr;
        return v && v->type == Value::Type::Array ? v : nullptr;
    };
    if (dv && (!posBytes || !idxBytes) && (posBytes || valuesOf("posData")) && (idxBytes || valuesOf("indices"))) {
        zoneIndex = int64_t(num(f->get("transientZoneIndex"), 0));
        if (posBytes && posBytes->string()) {
            if (!unhex(*posBytes->string(), out.pos)) {
                if (err) *err = "gfx_map_trzone posData bytes are not hex";
                return false;
            }
        } else {
            for (const Value& b : valuesOf("posData")->arr) out.pos.push_back(uint8_t(num(&b, 0)));
        }
        if (idxBytes && idxBytes->string()) {
            std::vector<uint8_t> raw;
            if (!unhex(*idxBytes->string(), raw) || raw.size() % 2) {
                if (err) *err = "gfx_map_trzone indices bytes are not hex";
                return false;
            }
            for (size_t i = 0; i + 1 < raw.size(); i += 2) out.idx.push_back(uint16_t(raw[i] | raw[i + 1] << 8));
        } else {
            for (const Value& i : valuesOf("indices")->arr) out.idx.push_back(uint16_t(num(&i, 0)));
        }
        if (const Value* aux = valuesOf("auxData"))
            for (const Value& b : aux->arr) out.aux.push_back(uint8_t(num(&b, 0)));
        else if (auxBytes && auxBytes->string() && !unhex(*auxBytes->string(), out.aux)) {
            if (err) *err = "gfx_map_trzone auxData bytes are not hex";
            return false;
        }
        return true;
    }
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

// The surface table of a gfx_map JSON text.
bool parseSurfaceTable(const std::string& gfxText, StockWorldSurfaceTable& table, std::string* err) {
    std::string jerr;
    const auto root = json::parse(gfxText, &jerr);
    if (!root) {
        if (err) *err = "gfx_map JSON: " + jerr;
        return false;
    }
    StockWorldSurfaceTable& out = table;
    const Value* f = fieldsOf(*root);
    // bakedLightScale lives in the draw sub-object (its path differs between exports): the first one found by name,
    // through objects only (the big arrays hold none).
    {
        std::vector<const Value*> stack{f};
        while (!stack.empty()) {
            const Value* v = stack.back();
            stack.pop_back();
            if (!v || v->type != Value::Type::Object) continue;
            if (const Value* b = v->get("bakedLightScale"); b && b->number() && *b->number() > 0) {
                out.bakedLightScale = *b->number();
                break;
            }
            for (const auto& [k, child] : v->obj)
                if (child.type == Value::Type::Object) stack.push_back(&child);
        }
    }
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
            d.xyzOffset = int64_t(num(r.get("xyzOffset"), -1));
            d.lmapCoordOffset = int64_t(num(r.get("lmapCoordOffset"), 0));
            d.colorOffset = int64_t(num(r.get("colorOffset"), 0));
            if (const Value* nt = r.get("normalTransformOffset"); nt && nt->type == Value::Type::Array)
                for (size_t i = 0; i < nt->arr.size() && i < d.normalTransformOffset.size(); ++i)
                    d.normalTransformOffset[i] = int64_t(num(&nt->arr[i], 0));
            surfData.push_back(d);
        }
    } else if (sd && sd->get("bytes") && sd->get("bytes")->string()) {
        // ACTS writes a large array as hex "bytes" (mp_donetsk: 57,471 records): 88 bytes each, u32 transientZone,
        // layerCount, xyzOffset, tangentFrameOffset, lmapCoordOffset, colorOffset, texCoordOffset at 0x00 .. 0x18
        // (x64-zt structs.hpp:12012; the "values" form above names the same fields).
        std::vector<uint8_t> raw;
        const int64_t stride = int64_t(num(sd->get("stride"), kSurfDataBytes));
        if (stride >= 0x1C && unhex(*sd->get("bytes")->string(), raw)) {
            const auto u32 = [&](size_t at) {
                return int64_t(uint32_t(raw[at]) | uint32_t(raw[at + 1]) << 8 | uint32_t(raw[at + 2]) << 16 | uint32_t(raw[at + 3]) << 24);
            };
            for (size_t at = 0; at + size_t(stride) <= raw.size(); at += size_t(stride)) {
                SurfData d;
                d.layerCount = u32(at + 0x04);
                d.xyzOffset = u32(at + 0x08);
                d.lmapCoordOffset = u32(at + 0x10);
                d.colorOffset = u32(at + 0x14);
                d.texCoordOffset = u32(at + 0x18);
                if (stride >= 0x38)  // normalTransformOffset[7] at 0x1C .. 0x37
                    for (size_t i = 0; i < d.normalTransformOffset.size(); ++i) d.normalTransformOffset[i] = u32(at + 0x1C + 4 * i);
                surfData.push_back(d);
            }
        }
    }
    out.surfData = surfData;
    out.surfaces.clear();
    out.surfaces.reserve(surfs->arr.size());
    for (const Value& s : surfs->arr) {
        StockWorldSurfaceTable::Surface r;
        const Value* mat = s.get("material");
        r.material = mat && mat->get("name") && mat->get("name")->string() ? *mat->get("name")->string() : "";
        if (!r.material.empty() && r.material[0] == ',') r.material.erase(0, 1);
        if (const Value* tris = s.get("tris")) {
            r.hasTris = true;
            r.posOffset = int64_t(num(tris->get("posOffset"), -1));
            r.vertexCount = int64_t(num(tris->get("vertexCount"), -1));
            r.triCount = int64_t(num(tris->get("triCount"), -1));
            r.baseIndex = int64_t(num(tris->get("baseIndex"), -1));
        }
        r.zone = int64_t(num(s.get("transientZone"), 0));
        r.surfDataIndex = int64_t(num(s.get("surfDataIndex"), -1));
        r.lightmapIndex = int64_t(num(s.get("lightmapIndex"), 0));
        out.surfaces.push_back(std::move(r));
    }
    // The layer count of each material: the largest layerCount of its surfaces (research 97), so every vertex of a
    // mesh has the same number of layer records.
    out.materialLayers.clear();
    for (const auto& r : out.surfaces) {
        int n = 1;
        if (r.surfDataIndex >= 0 && r.surfDataIndex < int64_t(surfData.size()))
            n = int(std::clamp<int64_t>(surfData[size_t(r.surfDataIndex)].layerCount, 1, kStockMaxLayers));
        int& m = out.materialLayers[r.material];
        m = std::max(m, n);
    }
    return true;
}

// Builds the meshes from the surface table and the transient zone buffers. quietMissing: surfaces of zones not in
// `trz` are expected (one streamed tile at a time) and not reported.
bool buildFromTable(const StockWorldSurfaceTable& table, std::map<int64_t, TrzData>& trz, StockWorld& out, std::string* err,
                    bool quietMissing) {
    const std::vector<SurfData>& surfData = table.surfData;
    out.bakedLightScale = table.bakedLightScale;

    out.meshes.clear();
    out.lightmapUv.clear();
    out.lightmapIndex.clear();
    out.layerCount.clear();
    out.layerUv.clear();
    out.vertexColor.clear();
    out.vertexColorHigh.clear();
    out.layerNormalTransform.clear();
    out.layeredSurfaces = 0;
    out.colorSurfaces = 0;
    out.bounds = AABB();
    out.warnings.clear();
    // Per mesh: VertexKey (position, texture and lightmap stream offsets) -> mesh vertex.
    std::vector<std::unordered_map<VertexKey, uint32_t, VertexKeyHash>> vmap;
    std::map<std::string, size_t> byMaterial;
    std::map<int64_t, size_t> missingZone;                      // transient zone -> surfaces skipped
    size_t surfacesUsed = 0, trisBad = 0, surfacesBad = 0;
    size_t uvNoAux = 0, uvUnresolved = 0;                       // surfaces whose texture coordinates are not read
    size_t lightmapSurfaces = 0;                                // surfaces with lightmap UVs
    size_t layerMismatch = 0;                                   // surfaces with fewer layers than their mesh
    size_t colorBad = 0;                                        // colour or normal transform records out of range

    const auto surfaceLayers = [&](const StockWorldSurfaceTable::Surface& s) -> int {
        const int64_t k = s.surfDataIndex;
        if (k < 0 || k >= int64_t(surfData.size())) return 1;
        return int(std::clamp<int64_t>(surfData[size_t(k)].layerCount, 1, kStockMaxLayers));
    };
    const auto materialOf = [](const StockWorldSurfaceTable::Surface& s) -> const std::string& { return s.material; };
    std::map<std::string, int> materialLayers = table.materialLayers;

    for (const StockWorldSurfaceTable::Surface& s : table.surfaces) {
        if (!s.hasTris) {
            if (!quietMissing || trz.count(s.zone)) ++surfacesBad;
            continue;
        }
        const int64_t posOffset = s.posOffset;
        const int64_t vertexCount = s.vertexCount;
        const int64_t triCount = s.triCount;
        const int64_t baseIndex = s.baseIndex;
        const int64_t zoneIndex = s.zone;
        const auto zit = trz.find(zoneIndex);
        if (zit == trz.end()) {
            if (!quietMissing) ++missingZone[zoneIndex];
            continue;
        }
        if (posOffset < 0 || vertexCount <= 0 || triCount <= 0 || baseIndex < 0) {
            ++surfacesBad;
            continue;
        }
        const TrzData& z = zit->second;
        if (uint64_t(posOffset) + kPosStride * uint64_t(vertexCount) > z.pos.size() ||
            uint64_t(baseIndex) + 3 * uint64_t(triCount) > z.idx.size()) {
            ++surfacesBad;
            continue;
        }
        // The surface names carry a leading ',' (ACTS prints the material name with it); the material asset is named without it.
        const std::string name = materialOf(s);
        size_t mi;
        if (auto it = byMaterial.find(name); it != byMaterial.end()) {
            mi = it->second;
        } else {
            mi = out.meshes.size();
            byMaterial[name] = mi;
            out.meshes.emplace_back();
            out.meshes.back().material = name;
            out.lightmapUv.emplace_back();
            out.lightmapIndex.emplace_back();
            out.layerCount.push_back(std::max(1, materialLayers[name]));
            out.layerUv.emplace_back();
            out.vertexColor.emplace_back();
            out.vertexColorHigh.emplace_back();
            out.layerNormalTransform.emplace_back();
            vmap.emplace_back();
        }
        ImportedMesh& m = out.meshes[mi];
        auto& lmUv = out.lightmapUv[mi];
        auto& lmIdx = out.lightmapIndex[mi];
        auto& layUv = out.layerUv[mi];
        auto& vcol = out.vertexColor[mi];
        auto& vcolHigh = out.vertexColorHigh[mi];
        auto& layNt = out.layerNormalTransform[mi];
        const int meshLayers = out.layerCount[mi];
        auto& vm = vmap[mi];
        ++surfacesUsed;

        // Texture coordinates (layer 0): the surface's GfxWorldSurfData gives texCoordOffset, and vertex j's record is
        // at texCoordOffset + 8 * layerCount * j, so the stride includes every layer (see stock_world.h).
        int64_t uvBase = -1;   // aux offset of vertex 0 (-1: no texture coordinates for this surface)
        int64_t uvStride = 0;
        const int64_t surfDataIndex = s.surfDataIndex;
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

        // Lightmap UV (research 94 section 2): float2 at lmapCoordOffset + 8 * (firstVertex + j), firstVertex the
        // surface's first vertex in its block ((posOffset - xyzOffset) / 12, 0 on every stock surface seen). None for
        // lightmapIndex 510 (shadow casters, sky), a record without lmapCoordOffset or a transient zone without aux data.
        int64_t lmBase = -1;  // aux offset of this surface's vertex 0 lightmap UV (-1: none)
        const int64_t surfLightmap = s.lightmapIndex;
        if (surfDataIndex >= 0 && surfDataIndex < int64_t(surfData.size()) && !z.aux.empty() && surfLightmap != kNoLightmap &&
            surfLightmap >= 0) {
            const SurfData& sdr = surfData[size_t(surfDataIndex)];
            int64_t firstVertex = 0;
            if (sdr.xyzOffset >= 0 && posOffset >= sdr.xyzOffset && (posOffset - sdr.xyzOffset) % kPosStride == 0)
                firstVertex = (posOffset - sdr.xyzOffset) / kPosStride;
            const int64_t base = sdr.lmapCoordOffset + int64_t(kLmapStride) * firstVertex;
            if (sdr.lmapCoordOffset > 0 && uint64_t(base) + uint64_t(kLmapStride) * uint64_t(vertexCount) <= z.aux.size())
                lmBase = base;
        }
        if (lmBase >= 0) ++lightmapSurfaces;
        const int32_t vertexLightmap = lmBase >= 0 ? int32_t(surfLightmap) : -1;

        // Layers, vertex colour and layer normal transforms (research 97). The colour record is 4 bytes per vertex, 8
        // for 5 to 8 layers; like the lightmap UV it is indexed from the surface's first vertex in its block.
        const int layers = surfaceLayers(s);
        if (layers > 1) ++out.layeredSurfaces;
        if (layers < meshLayers) ++layerMismatch;
        int64_t colBase = -1;  // aux offset of this surface's vertex 0 colour record (-1: none)
        const int64_t colStride = layers > 4 ? 8 : 4;
        std::array<int64_t, 7> ntBase{};  // per layer above 0: aux offset of this surface's vertex 0 transform (0: none)
        if (surfDataIndex >= 0 && surfDataIndex < int64_t(surfData.size()) && !z.aux.empty()) {
            const SurfData& sdr = surfData[size_t(surfDataIndex)];
            int64_t firstVertex = 0;
            if (sdr.xyzOffset >= 0 && posOffset >= sdr.xyzOffset && (posOffset - sdr.xyzOffset) % kPosStride == 0)
                firstVertex = (posOffset - sdr.xyzOffset) / kPosStride;
            bool bad = false;
            if (sdr.colorOffset > 0) {
                const int64_t base = sdr.colorOffset + colStride * firstVertex;
                if (uint64_t(base) + uint64_t(colStride) * uint64_t(vertexCount) <= z.aux.size()) colBase = base;
                else bad = true;
            }
            for (int l = 1; l < layers; ++l) {
                if (sdr.normalTransformOffset[size_t(l - 1)] <= 0) continue;
                const int64_t base = sdr.normalTransformOffset[size_t(l - 1)] + 4 * firstVertex;
                if (uint64_t(base) + 4 * uint64_t(vertexCount) <= z.aux.size()) ntBase[size_t(l - 1)] = base;
                else bad = true;
            }
            if (bad) ++colorBad;
        }
        if (colBase >= 0) ++out.colorSurfaces;

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
                const uint32_t lmKey = lmBase >= 0 ? uint32_t(lmBase + int64_t(kLmapStride) * t[c]) : kNoUv;
                const uint32_t colKey = colBase >= 0 ? uint32_t(colBase + colStride * t[c]) : kNoUv;
                const VertexKey key{uint64_t(posKey) << 32 | uvKey, lmKey, uint32_t(zoneIndex), vertexLightmap, colKey};
                auto it = vm.find(key);
                if (it == vm.end()) {
                    const size_t at = posKey;
                    const Vec3 p{readF32(z.pos, at), readF32(z.pos, at + 4), readF32(z.pos, at + 8)};
                    it = vm.emplace(key, uint32_t(m.pos.size())).first;
                    m.pos.push_back(p);
                    m.uv.push_back(uvBase >= 0 ? stockUv(z.aux, uvKey) : std::array<double, 2>{0, 0});
                    // Stored as is: v = 0 is the first row of the lightmap image (no flip, unlike uv).
                    lmUv.push_back(lmBase >= 0 ? std::array<float, 2>{readF32(z.aux, lmKey), readF32(z.aux, lmKey + 4)}
                                               : std::array<float, 2>{-1.0f, -1.0f});
                    lmIdx.push_back(vertexLightmap);
                    // Every layer's UV (v flipped like uv); a layer the surface does not have repeats its layer 0.
                    std::array<float, 2> uv0{0.0f, 0.0f};
                    for (int l = 0; l < meshLayers; ++l) {
                        std::array<float, 2> luv = uv0;
                        if (uvBase >= 0 && l < layers) {
                            const size_t at2 = size_t(uvKey) + kUvLayerStride * size_t(l);
                            luv = {readF32(z.aux, at2), 1.0f - readF32(z.aux, at2 + 4)};
                        }
                        if (l == 0) uv0 = luv;
                        layUv.push_back(luv);
                    }
                    vcol.push_back(colBase >= 0 ? readU32(z.aux, colKey) : 0xFFFFFFFFu);
                    vcolHigh.push_back(colBase >= 0 && colStride == 8 ? readU32(z.aux, size_t(colKey) + 4) : 0xFFFFFFFFu);
                    for (int l = 1; l < meshLayers; ++l)
                        layNt.push_back(l < layers && ntBase[size_t(l - 1)] > 0
                                            ? readU32(z.aux, size_t(ntBase[size_t(l - 1)] + 4 * int64_t(t[c])))
                                            : 0xFF8080FFu);
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
    // Empty meshes (every triangle skipped) are dropped, with their lightmap UV lists.
    {
        size_t keep = 0;
        for (size_t i = 0; i < out.meshes.size(); ++i) {
            if (out.meshes[i].idx.empty()) continue;
            if (keep != i) {
                out.meshes[keep] = std::move(out.meshes[i]);
                out.lightmapUv[keep] = std::move(out.lightmapUv[i]);
                out.lightmapIndex[keep] = std::move(out.lightmapIndex[i]);
                out.layerCount[keep] = out.layerCount[i];
                out.layerUv[keep] = std::move(out.layerUv[i]);
                out.vertexColor[keep] = std::move(out.vertexColor[i]);
                out.vertexColorHigh[keep] = std::move(out.vertexColorHigh[i]);
                out.layerNormalTransform[keep] = std::move(out.layerNormalTransform[i]);
            }
            ++keep;
        }
        out.meshes.resize(keep);
        out.lightmapUv.resize(keep);
        out.lightmapIndex.resize(keep);
        out.layerCount.resize(keep);
        out.layerUv.resize(keep);
        out.vertexColor.resize(keep);
        out.vertexColorHigh.resize(keep);
        out.layerNormalTransform.resize(keep);
    }
    out.lightmapSurfaces = lightmapSurfaces;

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
    if (layerMismatch)
        out.warnings.push_back(std::to_string(layerMismatch) + " surfaces with fewer texture layers than their material's other surfaces: their layer 0 repeated");
    if (colorBad)
        out.warnings.push_back(std::to_string(colorBad) + " surfaces with a colour or normal transform record out of range: not read");
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

namespace {
// The table parse, the mesh build, and the table kept with the world.
bool buildWorld(const std::string& gfxText, std::map<int64_t, TrzData>& trz, StockWorld& out, std::string* err) {
    auto table = std::make_shared<StockWorldSurfaceTable>();
    if (!parseSurfaceTable(gfxText, *table, err)) return false;
    if (!buildFromTable(*table, trz, out, err, false)) return false;
    out.surfaceTable = std::move(table);
    return true;
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

bool readStreamedWorldZones(const std::string& zoneDir, std::vector<StreamedWorldZone>& out, std::string* err) {
    out.clear();
    const fs::path base = fs::path(zoneDir) / "assets";
    const auto gfx = jsonFilesUnder(base / "gfx_map");
    if (gfx.size() != 1) {
        if (err) *err = "expected one gfx_map JSON under " + (base / "gfx_map").string();
        return false;
    }
    // The transient zones whose buffers this export holds (index 0 is the map zone itself). Every other transient zone
    // a surface names is streamed from <map>_<index, 5 digits>_tr.ff (checked on mp_donetsk: surface zone 30 is in
    // mp_donetsk_00030_tr, whose gfx_map_trzone has transientZoneIndex 30; the main zone's empty records 587 .. 867
    // are not world tiles).
    std::set<int64_t> resident{0};
    for (const fs::path& p : jsonFilesUnder(base / "gfx_map_trzone")) {
        std::string text;
        if (!readFile(p, text, err)) return false;
        std::string jerr;
        const auto root = json::parse(text, &jerr);
        if (!root) continue;
        const Value* f = fieldsOf(*root);
        const Value* dv = f ? f->get("drawVerts") : nullptr;
        if (dv && num(dv->get("posDataSize"), 0) > 0) resident.insert(int64_t(num(f->get("transientZoneIndex"), 0)));
    }
    const std::string mapName = fs::path(zoneDir).filename().string();
    std::map<int64_t, StreamedWorldZone> byIndex;
    std::string gfxText;
    if (!readFile(gfx.front(), gfxText, err)) return false;
    std::string jerr;
    const auto root = json::parse(gfxText, &jerr);
    if (!root) {
        if (err) *err = "gfx_map JSON: " + jerr;
        return false;
    }
    const Value* f = fieldsOf(*root);
    const Value* surfaces = f && f->get("surfaces") ? f->get("surfaces") : nullptr;
    const Value* surfs = surfaces && surfaces->get("surfaces") ? surfaces->get("surfaces")->get("values") : nullptr;
    const Value* bounds = surfaces && surfaces->get("surfaceBounds") ? surfaces->get("surfaceBounds")->get("bytes") : nullptr;
    if (!bounds) bounds = f && f->get("surfaceBounds") ? f->get("surfaceBounds")->get("bytes") : nullptr;
    if (!surfs || surfs->type != Value::Type::Array || !bounds || !bounds->string()) {
        if (err) *err = "gfx_map has no surfaces or surfaceBounds";
        return false;
    }
    std::vector<uint8_t> raw;
    if (!unhex(*bounds->string(), raw) || raw.size() / 56 < surfs->arr.size()) {
        if (err) *err = "gfx_map surfaceBounds do not cover the surfaces";
        return false;
    }
    for (size_t i = 0; i < surfs->arr.size(); ++i) {
        const int64_t zone = int64_t(num(surfs->arr[i].get("transientZone"), -1));
        if (zone < 0 || zone > 99999 || resident.count(zone)) continue;
        auto it = byIndex.find(zone);
        if (it == byIndex.end()) {
            char name[32];
            std::snprintf(name, sizeof(name), "_%05lld_tr", static_cast<long long>(zone));
            StreamedWorldZone z;
            z.index = zone;
            z.name = mapName + name;
            it = byIndex.emplace(zone, z).first;
        }
        const Vec3 mid{readF32(raw, i * 56), readF32(raw, i * 56 + 4), readF32(raw, i * 56 + 8)};
        const Vec3 half{readF32(raw, i * 56 + 12), readF32(raw, i * 56 + 16), readF32(raw, i * 56 + 20)};
        if (!std::isfinite(mid.x) || !std::isfinite(half.x)) continue;
        it->second.bounds.add(mid - half);
        it->second.bounds.add(mid + half);
        ++it->second.surfaces;
    }
    for (auto& [index, z] : byIndex)
        if (z.surfaces) out.push_back(z);
    return true;
}

std::vector<StreamedWorldZone> streamedZonesNear(const std::vector<StreamedWorldZone>& zones, double x, double y, double radius,
                                                 size_t limit) {
    std::vector<std::pair<double, const StreamedWorldZone*>> near;
    for (const StreamedWorldZone& z : zones) {
        if (!z.bounds.valid()) continue;
        const double dx = std::max({z.bounds.mins.x - x, 0.0, x - z.bounds.maxs.x});
        const double dy = std::max({z.bounds.mins.y - y, 0.0, y - z.bounds.maxs.y});
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d <= radius) near.push_back({d, &z});
    }
    std::stable_sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<StreamedWorldZone> out;
    for (const auto& [d, z] : near) {
        if (out.size() >= limit) break;
        out.push_back(*z);
    }
    return out;
}

float stockLayerWeight(uint32_t color, uint32_t colorHigh, int layer) {
    if (layer <= 0) return 1.0f;
    if (layer >= kStockMaxLayers) return 0.0f;
    const uint32_t word = layer < 4 ? color : colorHigh;
    return float((word >> (8 * (layer & 3))) & 0xFFu) / 255.0f;
}

void unpackStockTexCoords(uint32_t packed, double& u, double& v) {
    u = halfToDouble(uint16_t(packed & 0xFFFF));
    v = halfToDouble(uint16_t(packed >> 16));
}

bool readStockWorld(const std::string& zoneDir, StockWorld& out, std::string* err, bool includeStreamed) {
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
    std::vector<fs::path> allTrz = trzFiles;
    {
        // Streamed tiles exported later (WorldCache::requestStreamed): <zoneDir>/streamed/<tile>/.../gfx_map_trzone.
        std::error_code ec;
        const fs::path streamed = fs::path(zoneDir) / "streamed";
        if (includeStreamed && fs::is_directory(streamed, ec))
            for (fs::recursive_directory_iterator it(streamed, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec) && it->path().extension() == ".json" &&
                    it->path().string().find("gfx_map_trzone") != std::string::npos)
                    allTrz.push_back(it->path());
    }
    for (const fs::path& p : allTrz) {
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

bool buildStockWorldTile(const StockWorldSurfaceTable& table, const std::string& tileDir, StockWorld& out, std::string* err) {
    std::map<int64_t, TrzData> trz;
    std::error_code ec;
    if (!fs::is_directory(tileDir, ec)) {
        if (err) *err = "no tile export at " + tileDir;
        return false;
    }
    for (fs::recursive_directory_iterator it(tileDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".json" ||
            it->path().string().find("gfx_map_trzone") == std::string::npos)
            continue;
        std::string text;
        if (!readFile(it->path(), text, err)) return false;
        int64_t zone = 0;
        TrzData d;
        bool empty = false;
        if (!extractTrz(text, zone, d, err, &empty)) return false;
        if (!empty) trz[zone] = std::move(d);
    }
    if (trz.empty()) {
        if (err) *err = "the tile export at " + tileDir + " holds no transient zone buffers";
        return false;
    }
    return buildFromTable(table, trz, out, err, true);
}

}  // namespace iw8r
