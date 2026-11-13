// Stock super-terrain material layers. See stock_terrain_layers.h and docs/research/103_stock_terrain_layers.md.
#include "stock_terrain_layers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <utility>

#include "json_lite.h"

namespace iw8r {
namespace {

using json::Value;
using Span = std::pair<size_t, size_t>;

bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}

double num(const Value* v, double def = 0) {
    if (!v) return def;
    if (auto n = v->number()) return *n;
    return def;
}

// Up to n numbers from an array, an ACTS union view ({"v": [...]}) or x y z w members.
size_t numbers(const Value* v, double* out, size_t n) {
    if (!v) return 0;
    if (v->type == Value::Type::Array) {
        size_t k = 0;
        for (; k < n && k < v->arr.size(); ++k) out[k] = num(&v->arr[k]);
        return k;
    }
    if (v->type == Value::Type::Object) {
        if (const Value* a = v->get("v")) return numbers(a, out, n);
        static const char* names[4] = {"x", "y", "z", "w"};
        size_t k = 0;
        for (; k < n; ++k) {
            const Value* c = v->get(names[k]);
            if (!c || !c->number()) break;
            out[k] = *c->number();
        }
        return k;
    }
    return 0;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// A u16 array: {"values": [...]}, a plain array, or {"bytes": "<hex little endian>"}.
std::vector<int> u16Array(const Value* v) {
    std::vector<int> out;
    if (!v || v->isNull()) return out;
    if (v->type == Value::Type::Array) {
        for (const Value& e : v->arr) out.push_back(int(num(&e)));
        return out;
    }
    if (const Value* vals = v->get("values"); vals && vals->type == Value::Type::Array) return u16Array(vals);
    if (const Value* b = v->get("bytes"); b && b->string()) {
        const std::string& h = *b->string();
        for (size_t i = 0; i + 4 <= h.size(); i += 4) {
            const int a = hexNibble(h[i]), c = hexNibble(h[i + 1]), d = hexNibble(h[i + 2]), e = hexNibble(h[i + 3]);
            if (a < 0 || c < 0 || d < 0 || e < 0) break;
            out.push_back((a << 4 | c) | (d << 4 | e) << 8);
        }
    }
    return out;
}

std::string stripLead(std::string s) {
    while (!s.empty() && (s[0] == ',' || s[0] == '#')) s.erase(0, 1);
    return s;
}

std::string refName(const Value* v) {
    if (!v || v->type != Value::Type::Object) return {};
    const Value* n = v->get("name");
    return n && n->string() ? *n->string() : std::string();
}

size_t skipWs(const std::string& t, size_t i) {
    while (i < t.size() && std::isspace(uint8_t(t[i]))) ++i;
    return i;
}

// Skips one JSON value at t[i]; the index just past it, or npos. Strings inside are skipped with their escapes.
size_t skipValue(const std::string& t, size_t i) {
    const size_t n = t.size();
    i = skipWs(t, i);
    if (i >= n) return std::string::npos;
    const char c = t[i];
    if (c == '"') {
        for (++i; i < n; ++i) {
            if (t[i] == '\\') ++i;
            else if (t[i] == '"') return i + 1;
        }
        return std::string::npos;
    }
    if (c == '{' || c == '[') {
        int depth = 0;
        for (; i < n; ++i) {
            const char d = t[i];
            if (d == '"') {
                for (++i; i < n && t[i] != '"'; ++i)
                    if (t[i] == '\\') ++i;
                if (i >= n) return std::string::npos;
            } else if (d == '{' || d == '[') {
                ++depth;
            } else if (d == '}' || d == ']') {
                if (--depth == 0) return i + 1;
            }
        }
        return std::string::npos;
    }
    while (i < n && t[i] != ',' && t[i] != '}' && t[i] != ']' && !std::isspace(uint8_t(t[i]))) ++i;
    return i;
}

using Members = std::vector<std::pair<std::string, Span>>;

bool objectMembers(const std::string& t, size_t open, Members& out) {
    const size_t n = t.size();
    open = skipWs(t, open);
    if (open >= n || t[open] != '{') return false;
    size_t i = open + 1;
    while (i < n) {
        while (i < n && (std::isspace(uint8_t(t[i])) || t[i] == ',')) ++i;
        if (i < n && t[i] == '}') return true;
        if (i >= n || t[i] != '"') return false;
        const size_t ke = skipValue(t, i);
        if (ke == std::string::npos) return false;
        std::string key = t.substr(i + 1, ke - i - 2);
        i = skipWs(t, ke);
        if (i >= n || t[i] != ':') return false;
        i = skipWs(t, i + 1);
        const size_t ve = skipValue(t, i);
        if (ve == std::string::npos) return false;
        out.push_back({std::move(key), {i, ve}});
        i = ve;
    }
    return false;
}

const Span* member(const Members& m, const char* key) {
    for (const auto& kv : m)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

bool arrayElements(const std::string& t, size_t open, std::vector<Span>& out) {
    const size_t n = t.size();
    open = skipWs(t, open);
    if (open >= n || t[open] != '[') return false;
    size_t i = open + 1;
    while (i < n) {
        while (i < n && (std::isspace(uint8_t(t[i])) || t[i] == ',')) ++i;
        if (i < n && t[i] == ']') return true;
        const size_t ve = skipValue(t, i);
        if (ve == std::string::npos) return false;
        out.push_back({i, ve});
        i = ve;
    }
    return false;
}

// The array span of an ACTS array member: the member itself when it is an array, else its "values".
bool valuesSpan(const std::string& t, const Span& s, Span& out) {
    const size_t i = skipWs(t, s.first);
    if (i >= t.size()) return false;
    if (t[i] == '[') {
        out = s;
        return true;
    }
    if (t[i] != '{') return false;
    Members m;
    if (!objectMembers(t, i, m)) return false;
    const Span* v = member(m, "values");
    if (!v) return false;
    out = *v;
    return t[skipWs(t, v->first)] == '[';
}

const char* const kLayerSurfaceKeys[] = {"layerMaterials", "layerMaskMapIdxs", "combinedMapStreamRadiusInvSq"};

uint32_t rd32(const std::string& b, size_t o) {
    return uint32_t(uint8_t(b[o])) | uint32_t(uint8_t(b[o + 1])) << 8 | uint32_t(uint8_t(b[o + 2])) << 16 |
           uint32_t(uint8_t(b[o + 3])) << 24;
}

bool readBinary(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(size_t(size));
    if (size > 0) f.read(out.data(), size);
    return bool(f);
}

double sat(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Layer info

int StockTerrainLayerInfo::softEdgeByte() const {
    // TerrainLayerData::SetFrom: min(255, int(255 * soft)), clamped at 0, in bits 24..31 of the packed flags.
    int b = int(float(255.0 * alphaRevealSoftEdge));
    if (b > 255) b = 255;
    if (b < 0) b = 0;
    return b;
}

bool StockTerrainLayerInfo::coversAtFullWeight() const {
    if (!alphaReveal()) return true;
    return revealImage.empty() || revealImage.rfind("white&", 0) == 0 || revealImage.rfind(",$white", 0) == 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Parsing

bool parseStockTerrainLayers(const std::string& t, StockTerrainLayerTable& out, std::string* err) {
    out = StockTerrainLayerTable{};
    Members root;
    if (!objectMembers(t, 0, root)) return fail(err, "stterrain JSON: the root is not an object");
    Members fields = root;
    if (const Span* a = member(root, "asset")) {
        Members asset;
        if (!objectMembers(t, a->first, asset)) return fail(err, "stterrain JSON: asset is not an object");
        fields = std::move(asset);
    }
    if (const Span* f = member(fields, "fields")) {
        Members ff;
        if (!objectMembers(t, f->first, ff)) return fail(err, "stterrain JSON: fields is not an object");
        fields = std::move(ff);
    }
    std::string jerr;
    if (const Span* fi = member(fields, "flattenedImages")) {
        Span arr;
        if (valuesSpan(t, *fi, arr)) {
            auto v = json::parse(t.substr(arr.first, arr.second - arr.first), &jerr);
            if (!v) return fail(err, "stterrain flattenedImages: " + jerr);
            for (const Value& e : v->arr) out.flattenedImages.push_back(refName(&e));
        }
    }
    const Span* ss = member(fields, "surfaces");
    if (!ss) return fail(err, "stterrain has no surfaces");
    Span arr;
    if (!valuesSpan(t, *ss, arr)) return fail(err, "stterrain surfaces has no values array");
    std::vector<Span> elems;
    if (!arrayElements(t, arr.first, elems)) return fail(err, "stterrain surfaces.values is malformed");

    auto flat = [&](int idx) -> std::string {
        if (idx < 0 || idx == 0xFFFF || size_t(idx) >= out.flattenedImages.size()) return {};
        return out.flattenedImages[size_t(idx)];
    };
    for (size_t si = 0; si < elems.size(); ++si) {
        Members m;
        if (!objectMembers(t, elems[si].first, m)) return fail(err, "surface " + std::to_string(si) + " is not an object");
        std::string small = "{";
        for (const char* key : kLayerSurfaceKeys) {
            const Span* s = member(m, key);
            if (!s) continue;
            if (small.size() > 1) small += ',';
            small += '"';
            small += key;
            small += "\":";
            small.append(t, s->first, s->second - s->first);
        }
        small += '}';
        auto v = json::parse(small, &jerr);
        if (!v) return fail(err, "surface " + std::to_string(si) + ": " + jerr);
        StockTerrainSurfaceLayers sl;
        sl.surface = int(si);
        sl.combinedMapStreamRadiusInvSq = num(v->get("combinedMapStreamRadiusInvSq"));
        const std::vector<int> maskIdx = u16Array(v->get("layerMaskMapIdxs"));
        if (const Value* lm = v->get("layerMaterials"); lm && !lm->isNull()) {
            const Value* vals = lm->type == Value::Type::Array ? lm : lm->get("values");
            if (vals && vals->type == Value::Type::Array) {
                for (size_t row = 0; row < vals->arr.size(); ++row) {
                    const Value& l = vals->arr[row];
                    StockTerrainLayerInfo L;
                    L.layer = int(num(l.get("layer"), double(row)));
                    if (L.layer != int(row))
                        out.warnings.push_back("surface " + std::to_string(si) + " row " + std::to_string(row) +
                                               ": layer " + std::to_string(L.layer) + " differs from the row");
                    L.material = stripLead(refName(l.get("material")));
                    if (const Value* st = l.get("stFromXYZ"); st && st->type == Value::Type::Array)
                        for (size_t r = 0; r < 2 && r < st->arr.size(); ++r) numbers(&st->arr[r], L.stFromXYZ[r], 4);
                    L.himipRadiusInvSq = num(l.get("himipRadiusInvSq"));
                    if (const Value* ld = l.get("layerData")) {
                        numbers(ld->get("textureScale"), L.textureScale, 2);
                        numbers(ld->get("colorTint"), L.colorTint, 3);
                        L.flags = uint32_t(num(ld->get("flags")));
                        L.alphaRevealSoftEdge = num(ld->get("alphaRevealSoftEdge"), 1.0);
                    }
                    const std::vector<int> tex = u16Array(l.get("materialTextureIdxs"));
                    if (tex.size() > 0) L.colorImage = flat(tex[0]);
                    if (tex.size() > 1) L.normalImage = flat(tex[1]);
                    if (tex.size() > 2) L.revealImage = flat(tex[2]);
                    if (tex.size() > 3) L.displacementImage = flat(tex[3]);
                    // The mask: layerMaskMapIdxs[layer] (R_ST_SetupMaterialLayer).
                    const int li = L.layer >= 0 && size_t(L.layer) < maskIdx.size() ? maskIdx[size_t(L.layer)] : -1;
                    const std::string mn = flat(li);
                    if (li < 0 || mn.empty()) {
                        L.maskKind = StockTerrainLayerInfo::Mask::None;
                        if (!maskIdx.empty())
                            out.warnings.push_back("surface " + std::to_string(si) + " layer " + std::to_string(row) +
                                                   ": no layer mask");
                    } else if (mn == ",$white") {
                        L.maskKind = StockTerrainLayerInfo::Mask::White;
                    } else if (mn == ",$black") {
                        L.maskKind = StockTerrainLayerInfo::Mask::Black;
                    } else {
                        L.maskKind = StockTerrainLayerInfo::Mask::Image;
                        L.maskImage = stripLead(mn);
                    }
                    sl.layers.push_back(std::move(L));
                }
            }
        }
        out.surfaces.push_back(std::move(sl));
    }
    if (out.surfaces.empty()) return fail(err, "stterrain has no surfaces");
    return true;
}

bool readStockTerrainLayers(const std::string& jsonPath, StockTerrainLayerTable& out, std::string* err) {
    std::string text;
    if (!readBinary(jsonPath, text)) return fail(err, "cannot read " + jsonPath);
    return parseStockTerrainLayers(text, out, err);
}

std::vector<std::string> stockTerrainLayerMaskImageNames(const StockTerrainLayerTable& table, int surface) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const StockTerrainSurfaceLayers& s : table.surfaces) {
        if (surface >= 0 && s.surface != surface) continue;
        for (const StockTerrainLayerInfo& L : s.layers)
            if (L.maskKind == StockTerrainLayerInfo::Mask::Image && seen.insert(L.maskImage).second) out.push_back(L.maskImage);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Masks

uint8_t StockTerrainLayerMask::at(int i, int j) const {
    if (data.empty()) return 0;
    i = std::clamp(i, 0, width - 1);
    j = std::clamp(j, 0, height - 1);
    return data[size_t(j) * size_t(width) + size_t(i)];
}

double StockTerrainLayerMask::sample(double tx, double ty) const {
    if (data.empty()) return 0;
    const double fx = std::floor(tx), fy = std::floor(ty);
    const int i = int(fx), j = int(fy);
    const double ax = tx - fx, ay = ty - fy;
    const double a = at(i, j), b = at(i + 1, j), c = at(i, j + 1), d = at(i + 1, j + 1);
    return ((a * (1 - ax) + b * ax) * (1 - ay) + (c * (1 - ax) + d * ax) * ay) / 255.0;
}

void decodeStockTerrainBc4Block(const uint8_t block[8], uint8_t out[16]) {
    const int r0 = block[0], r1 = block[1];
    int pal[8];
    pal[0] = r0;
    pal[1] = r1;
    if (r0 > r1) {
        for (int i = 1; i <= 6; ++i) pal[1 + i] = ((7 - i) * r0 + i * r1 + 3) / 7;
    } else {
        for (int i = 1; i <= 4; ++i) pal[1 + i] = ((5 - i) * r0 + i * r1 + 2) / 5;
        pal[6] = 0;
        pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(block[2 + i]) << (8 * i);
    for (int k = 0; k < 16; ++k) out[k] = uint8_t(pal[(bits >> (3 * k)) & 7]);
}

bool decodeStockTerrainLayerMaskDds(const std::string& b, StockTerrainLayerMask& out, std::string* err) {
    out = StockTerrainLayerMask{};
    if (b.size() < 148 || b.compare(0, 4, "DDS ") != 0) return fail(err, "not a DDS file");
    if (b.compare(84, 4, "DX10") != 0) return fail(err, "DDS has no DX10 header");
    const int h = int(rd32(b, 12)), w = int(rd32(b, 16));
    const uint32_t dxgi = rd32(b, 128);
    if (w <= 0 || h <= 0 || w > 65536 || h > 65536) return fail(err, "DDS size out of range");
    const size_t base = 148;
    if (dxgi == 61) {  // R8_UNORM
        const size_t need = size_t(w) * size_t(h);
        if (b.size() < base + need) return fail(err, "R8 layer mask is truncated");
        out.width = w;
        out.height = h;
        out.data.assign(b.begin() + std::ptrdiff_t(base), b.begin() + std::ptrdiff_t(base + need));
        return true;
    }
    if (dxgi != 80) return fail(err, "layer mask DXGI " + std::to_string(dxgi) + " (expected 80 BC4_UNORM or 61 R8_UNORM)");
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    if (b.size() < base + size_t(bw) * size_t(bh) * 8) return fail(err, "BC4 layer mask is truncated");
    out.width = w;
    out.height = h;
    out.data.assign(size_t(w) * size_t(h), 0);
    uint8_t px[16];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            decodeStockTerrainBc4Block(reinterpret_cast<const uint8_t*>(b.data()) + base + (size_t(by) * size_t(bw) + size_t(bx)) * 8, px);
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const int i = bx * 4 + x, j = by * 4 + y;
                    if (i < w && j < h) out.data[size_t(j) * size_t(w) + size_t(i)] = px[y * 4 + x];
                }
        }
    return true;
}

bool readStockTerrainLayerMaskDds(const std::string& path, StockTerrainLayerMask& out, std::string* err) {
    std::string b;
    if (!readBinary(path, b)) return fail(err, "cannot read " + path);
    return decodeStockTerrainLayerMaskDds(b, out, err);
}

// ---------------------------------------------------------------------------------------------------------------
// Blend arithmetic

double stockTerrainRevealRamp(double w, double reveal, double k) {
    w = sat(w);
    const double lo = sat(1 - w - k * std::sqrt(w));
    const double hi = sat(1 - w + k * std::sqrt(1 - w));
    if (hi - lo <= 1e-12) return reveal > lo ? 1.0 : 0.0;
    return sat((reveal - lo) / (hi - lo));
}

double stockTerrainLayerBlendWeight(const StockTerrainLayerInfo& L, double mask, double alpha, double reveal) {
    // PS afc8b731 lines 200-238: the threshold applies to alpha * mask (or alpha alone under the ramp), then * ramp.
    double m = sat(mask), ramp = 1.0;
    if (L.layer == 0) return m;
    if (L.alphaReveal()) {
        if (L.revealRamp()) {
            ramp = stockTerrainRevealRamp(m, reveal, L.revealK());
            m = 1.0;
        }
        m *= alpha;
    }
    if (L.hardEdge()) m = m > 0.501961 ? 1.0 : 0.0;
    return ramp * m;
}

std::array<double, 2> stockTerrainLayerUv(const StockTerrainLayerInfo& L, const Vec3& p) {
    const double s = L.stFromXYZ[0][0] * p.x + L.stFromXYZ[0][1] * p.y + L.stFromXYZ[0][2] * p.z + L.stFromXYZ[0][3];
    const double t = L.stFromXYZ[1][0] * p.x + L.stFromXYZ[1][1] * p.y + L.stFromXYZ[1][2] * p.z + L.stFromXYZ[1][3];
    return {s, 1.0 - t};
}

namespace {

double maskWeight(const StockTerrainLayerInfo& L, const std::vector<StockTerrainLayerMask>& masks, size_t row, double tx, double ty) {
    switch (L.maskKind) {
        case StockTerrainLayerInfo::Mask::White: return 1.0;
        case StockTerrainLayerInfo::Mask::Image:
            return row < masks.size() && !masks[row].empty() ? masks[row].sample(tx, ty) : 0.0;
        default: return 0.0;
    }
}

}  // namespace

void stockTerrainLayerWeightsAt(const StockTerrainSurface& surface, const StockTerrainSurfaceLayers& layers,
                                const std::vector<StockTerrainLayerMask>& masks, double x, double y,
                                std::vector<double>& weights) {
    weights.assign(layers.layers.size(), 0.0);
    if (!(surface.texelSize > 0)) return;
    const double tx = stockTerrainTexelX(surface, x), ty = stockTerrainTexelY(surface, y);
    for (size_t L = 0; L < layers.layers.size(); ++L) weights[L] = maskWeight(layers.layers[L], masks, L, tx, ty);
}

int stockTerrainLayerContributions(const std::vector<double>& weights, std::vector<double>& c) {
    c.assign(weights.size(), 0.0);
    double above = 1.0;  // prod over K > L of (1 - w_K)
    int best = -1;
    for (size_t k = weights.size(); k-- > 0;) {
        const double w = sat(weights[k]);
        c[k] = w * above;
        above *= 1 - w;
    }
    for (size_t k = 0; k < c.size(); ++k)
        if (best < 0 || c[k] > c[size_t(best)]) best = int(k);
    return best;
}

std::vector<int> stockTerrainSurfacesNear(const StockTerrain& terrain, double x, double y, double radius) {
    std::vector<std::pair<double, int>> hits;
    for (size_t i = 0; i < terrain.surfaces.size(); ++i) {
        const StockTerrainSurface& s = terrain.surfaces[i];
        const double dx = std::max(0.0, std::fabs(x - s.origin.x) - s.halfWldSize);
        const double dy = std::max(0.0, std::fabs(y - s.origin.y) - s.halfWldSize);
        if (dx * dx + dy * dy <= radius * radius) hits.push_back({s.halfWldSize, int(i)});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<int> out;
    for (const auto& h : hits) out.push_back(h.second);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Patch

bool buildStockTerrainLayerPatch(const StockTerrain& terrain, const StockTerrainLayerTable& table, const StockTerrainMesh& mesh,
                                 const std::vector<StockTerrainLayerMask>& masks, const StockTerrainLayerPatchOptions& opt,
                                 StockTerrainLayerPatch& out, std::string* err) {
    out = StockTerrainLayerPatch{};
    const int si = mesh.surface;
    if (si < 0 || size_t(si) >= terrain.surfaces.size()) return fail(err, "mesh surface index out of range");
    const StockTerrainSurface& s = terrain.surfaces[size_t(si)];
    const StockTerrainSurfaceLayers* sl = nullptr;
    for (const StockTerrainSurfaceLayers& c : table.surfaces)
        if (c.surface == si) sl = &c;
    if (!sl) return fail(err, "surface " + std::to_string(si) + " is not in the layer table");
    if (sl->layers.empty()) return fail(err, "surface " + std::to_string(si) + " has no layers");
    if (!(s.texelSize > 0)) return fail(err, "surface " + std::to_string(si) + " has no texel size");
    const ImportedMesh& m = mesh.mesh;
    if (m.pos.size() != m.normal.size()) return fail(err, "mesh has no normals");
    const size_t nLayers = sl->layers.size();
    out.surface = si;
    out.baseLayer = 0;
    out.baseMaterial = sl->layers[0].material;
    out.base.material = out.baseMaterial;
    out.dominantCount.assign(nLayers, 0);

    // 1. The kept triangles and their vertices (remapped, in first-use order).
    const double r2 = opt.radius * opt.radius;
    std::vector<uint32_t> remap(m.pos.size(), UINT32_MAX);
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        const Vec3& a = m.pos[m.idx[t]];
        const Vec3& b = m.pos[m.idx[t + 1]];
        const Vec3& c = m.pos[m.idx[t + 2]];
        if (opt.radius > 0) {
            const double cx = (a.x + b.x + c.x) / 3 - opt.centerX, cy = (a.y + b.y + c.y) / 3 - opt.centerY;
            if (cx * cx + cy * cy > r2) continue;
        }
        for (int k = 0; k < 3; ++k) {
            const uint32_t src = m.idx[t + size_t(k)];
            if (remap[src] == UINT32_MAX) {
                remap[src] = uint32_t(out.sourceVertex.size());
                out.sourceVertex.push_back(src);
                out.base.pos.push_back(m.pos[src]);
                out.base.normal.push_back(m.normal[src]);
                out.base.uv.push_back(stockTerrainLayerUv(sl->layers[0], m.pos[src]));
                const double half = s.halfWldSize, size2 = 2 * half;
                out.albedoUv.push_back({(m.pos[src].x - s.origin.x + half) / size2, 1.0 - (m.pos[src].y - s.origin.y + half) / size2});
            }
            out.base.idx.push_back(remap[src]);
        }
    }
    const size_t nv = out.base.pos.size();
    const size_t nt = out.base.idx.size() / 3;
    if (nt == 0) return true;

    for (size_t L = 1; L < nLayers; ++L) {
        const StockTerrainLayerInfo& info = sl->layers[L];
        if (info.maskKind == StockTerrainLayerInfo::Mask::Image && (L >= masks.size() || masks[L].empty()))
            out.warnings.push_back("surface " + std::to_string(si) + " layer " + std::to_string(L) + " (" + info.material +
                                   "): mask " + info.maskImage + " not given, layer skipped");
    }

    // Texel coordinates of the kept vertices.
    std::vector<double> tx(nv), ty(nv);
    for (size_t v = 0; v < nv; ++v) {
        tx[v] = stockTerrainTexelX(s, out.base.pos[v].x);
        ty[v] = stockTerrainTexelY(s, out.base.pos[v].y);
    }

    // 2. Layers from the top down: `covered` marks vertices where a later covering layer is at weight 1.
    constexpr float kFull = float(254.5 / 255.0);
    std::vector<uint8_t> covered(nv, 0);
    std::vector<double> aboveCover(nv, 1.0);  // prod over later covering layers of (1 - w)
    std::vector<float> w(nv);
    std::vector<double> above(nv, 1.0), best(nv, -1.0);
    std::vector<int> bestLayer(nv, -1);
    std::vector<StockTerrainLayerSet> sets;
    for (size_t L = nLayers; L-- > 0;) {
        const StockTerrainLayerInfo& info = sl->layers[L];
        for (size_t v = 0; v < nv; ++v) w[v] = float(maskWeight(info, masks, L, tx[v], ty[v]));
        // Dominant layer statistics (contribution w * prod_{K > L} (1 - w_K)).
        for (size_t v = 0; v < nv; ++v) {
            const double c = double(w[v]) * above[v];
            if (c >= best[v]) {  // ties keep the lower layer, as stockTerrainLayerContributions
                best[v] = c;
                bestLayer[v] = int(L);
            }
            above[v] *= 1.0 - double(w[v]);
        }
        const bool emit = !(L == 0 && opt.skipLayerZero);
        if (emit) {
            StockTerrainLayerSet set;
            set.layer = int(L);
            set.material = info.material;
            set.flags = info.flags;
            set.revealK = info.revealK();
            set.revealRamp = info.revealRamp();
            set.alphaReveal = info.alphaReveal();
            set.hardEdge = info.hardEdge();
            set.colorImage = info.colorImage;
            set.normalImage = info.normalImage;
            set.revealImage = info.revealImage;
            set.mesh.material = info.material;
            std::vector<uint32_t> lv(nv, UINT32_MAX);
            double sum = 0;
            for (size_t t = 0; t < nt; ++t) {
                const uint32_t a = out.base.idx[3 * t], b = out.base.idx[3 * t + 1], c = out.base.idx[3 * t + 2];
                const float mx = std::max({w[a], w[b], w[c]});
                if (double(mx) < opt.minWeight) continue;
                if (opt.cullHidden) {
                    if (covered[a] && covered[b] && covered[c]) continue;
                    const double vis = std::max({double(w[a]) * aboveCover[a], double(w[b]) * aboveCover[b],
                                                 double(w[c]) * aboveCover[c]});
                    if (vis < opt.minContribution) continue;
                }
                for (uint32_t src : {a, b, c}) {
                    if (lv[src] == UINT32_MAX) {
                        lv[src] = uint32_t(set.mesh.pos.size());
                        set.mesh.pos.push_back(out.base.pos[src]);
                        set.mesh.normal.push_back(out.base.normal[src]);
                        set.mesh.uv.push_back(stockTerrainLayerUv(info, out.base.pos[src]));
                        set.weight.push_back(w[src]);
                        sum += w[src];
                    }
                    set.mesh.idx.push_back(lv[src]);
                }
            }
            if (!set.mesh.idx.empty()) {
                set.meanWeight = sum / double(set.weight.size());
                out.layerTriangles += set.mesh.idx.size() / 3;
                sets.push_back(std::move(set));
            }
        }
        if (info.coversAtFullWeight())
            for (size_t v = 0; v < nv; ++v) {
                if (w[v] >= kFull) covered[v] = 1;
                aboveCover[v] *= 1.0 - double(w[v]);
            }
    }
    std::reverse(sets.begin(), sets.end());
    out.layers = std::move(sets);
    for (size_t v = 0; v < nv; ++v)
        if (bestLayer[v] >= 0) ++out.dominantCount[size_t(bestLayer[v])];
    return true;
}

}  // namespace iw8r
