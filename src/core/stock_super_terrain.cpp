// Stock super-terrain mesh reader. See stock_super_terrain.h and docs/research/102_stock_super_terrain.md.
#include "stock_super_terrain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include "json_lite.h"

namespace iw8r {
namespace {

using json::Value;
namespace fs = std::filesystem;
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

std::string assetName(std::string s) {
    while (!s.empty() && (s[0] == ',' || s[0] == '#')) s.erase(0, 1);
    return s;
}

// The name of an asset reference ({"name": ...}), "" for null.
std::string refName(const Value* v, bool* builtin = nullptr) {
    if (builtin) *builtin = false;
    if (!v || v->type != Value::Type::Object) return {};
    const Value* n = v->get("name");
    if (!n || !n->string()) return {};
    const std::string& s = *n->string();
    if (builtin) *builtin = s.size() > 1 && s[0] == ',' && s[1] == '$';
    return assetName(s);
}

size_t skipWs(const std::string& t, size_t i) {
    while (i < t.size() && std::isspace(uint8_t(t[i]))) ++i;
    return i;
}

// Skips one JSON value at t[i] (after whitespace); the index just past it, or npos.
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

// Members of the object at t[open] ('{'), in order: key and value span.
bool objectMembers(const std::string& t, size_t open, std::vector<std::pair<std::string, Span>>& out) {
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

const Span* member(const std::vector<std::pair<std::string, Span>>& m, const char* key) {
    for (const auto& kv : m)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

// Element spans of the array at t[open] ('[').
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

// The members a surface needs; the rest (grid, highLODIndices, clutter tables) are never parsed.
const char* const kSurfaceKeys[] = {"objToWld", "wldBounds", "surfaceMapSetIndex", "surfaceMapWorldSize", "heightOffset",
                                    "heightScale", "halfWldSize", "layerMaterials", "heightMap", "cutoutMap",
                                    "combinedAlbedoMap", "combinedNogMap"};

uint32_t rd32(const std::string& b, size_t o) {
    return uint32_t(uint8_t(b[o])) | uint32_t(uint8_t(b[o + 1])) << 8 | uint32_t(uint8_t(b[o + 2])) << 16 |
           uint32_t(uint8_t(b[o + 3])) << 24;
}

// DX10 DDS header: width, height, DXGI format; data offset 148.
bool ddsHeader(const std::string& b, int& w, int& h, uint32_t& dxgi, std::string* err) {
    if (b.size() < 148 || b.compare(0, 4, "DDS ") != 0) return fail(err, "not a DDS file");
    if (b.compare(84, 4, "DX10") != 0) return fail(err, "DDS has no DX10 header");
    h = int(rd32(b, 12));
    w = int(rd32(b, 16));
    dxgi = rd32(b, 128);
    if (w <= 0 || h <= 0 || w > 65536 || h > 65536) return fail(err, "DDS size out of range");
    return true;
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

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Parsing

bool parseStockTerrain(const std::string& t, StockTerrain& out, std::string* err) {
    out = StockTerrain{};
    std::vector<std::pair<std::string, Span>> root;
    if (!objectMembers(t, 0, root)) return fail(err, "stterrain JSON: the root is not an object");
    // asset.fields, or fields, or the fields object itself.
    std::vector<std::pair<std::string, Span>> fields = root;
    if (const Span* a = member(root, "asset")) {
        std::vector<std::pair<std::string, Span>> asset;
        if (!objectMembers(t, a->first, asset)) return fail(err, "stterrain JSON: asset is not an object");
        fields = asset;
    }
    if (const Span* f = member(fields, "fields")) {
        std::vector<std::pair<std::string, Span>> ff;
        if (!objectMembers(t, f->first, ff)) return fail(err, "stterrain JSON: fields is not an object");
        fields = std::move(ff);
    }
    std::string jerr;
    auto parse = [&](const Span& s) { return json::parse(t.substr(s.first, s.second - s.first), &jerr); };

    if (const Span* nm = member(fields, "name")) {
        if (auto v = parse(*nm)) {
            if (v->string()) out.name = *v->string();
            else if (const Value* s = v->get("string"); s && s->string()) out.name = *s->string();
        }
    }
    std::vector<std::pair<int, int>> maps;  // mapResolution, paddedResolution
    if (const Span* ms = member(fields, "maps")) {
        auto v = parse(*ms);
        if (!v) return fail(err, "stterrain maps: " + jerr);
        const Value* vals = v->type == Value::Type::Array ? &*v : v->get("values");
        if (vals && vals->type == Value::Type::Array)
            for (const Value& m : vals->arr) maps.push_back({int(num(m.get("mapResolution"))), int(num(m.get("paddedResolution")))});
    }
    const Span* ss = member(fields, "surfaces");
    if (!ss) return fail(err, "stterrain has no surfaces");
    size_t arrOpen = ss->first;
    if (t[arrOpen] == '{') {
        std::vector<std::pair<std::string, Span>> so;
        if (!objectMembers(t, arrOpen, so)) return fail(err, "stterrain surfaces is malformed");
        const Span* vs = member(so, "values");
        if (!vs) return fail(err, "stterrain surfaces has no values");
        arrOpen = vs->first;
    } else if (t.compare(arrOpen, 4, "null") == 0) {
        return fail(err, "stterrain surfaces is null");
    }
    std::vector<Span> elems;
    if (!arrayElements(t, arrOpen, elems)) return fail(err, "stterrain surfaces.values is not an array");

    for (size_t si = 0; si < elems.size(); ++si) {
        std::vector<std::pair<std::string, Span>> m;
        if (!objectMembers(t, elems[si].first, m)) return fail(err, "surface " + std::to_string(si) + " is not an object");
        std::string small = "{";
        for (const char* key : kSurfaceKeys) {
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
        StockTerrainSurface sf;
        sf.index = int(si);
        if (const Value* o = v->get("objToWld")) {
            double p[3] = {0, 0, 0};
            if (numbers(o->get("origin"), p, 3) == 3) sf.origin = Vec3{p[0], p[1], p[2]};
            double q[4] = {0, 0, 0, 1};
            if (numbers(o->get("quat"), q, 4) == 4 && (std::fabs(q[0]) > 1e-6 || std::fabs(q[1]) > 1e-6 || std::fabs(q[2]) > 1e-6))
                out.warnings.push_back("surface " + std::to_string(si) + ": objToWld.quat is not identity (ignored)");
        }
        if (const Value* b = v->get("wldBounds")) {
            double mid[3] = {0, 0, 0}, half[3] = {0, 0, 0};
            if (numbers(b->get("midPoint"), mid, 3) == 3 && numbers(b->get("halfSize"), half, 3) == 3) {
                sf.wldBounds.add(Vec3{mid[0] - half[0], mid[1] - half[1], mid[2] - half[2]});
                sf.wldBounds.add(Vec3{mid[0] + half[0], mid[1] + half[1], mid[2] + half[2]});
            }
        }
        sf.halfWldSize = num(v->get("halfWldSize"));
        sf.texelSize = num(v->get("surfaceMapWorldSize"));
        sf.heightOffset = num(v->get("heightOffset"));
        sf.heightScale = num(v->get("heightScale"));
        const int mapSet = int(num(v->get("surfaceMapSetIndex"), -1));
        if (mapSet >= 0 && size_t(mapSet) < maps.size()) {
            sf.mapResolution = maps[size_t(mapSet)].first;
            sf.paddedResolution = maps[size_t(mapSet)].second;
        } else {
            out.warnings.push_back("surface " + std::to_string(si) + ": surfaceMapSetIndex " + std::to_string(mapSet) +
                                   " is not in maps");
        }
        bool builtin = false;
        sf.heightImage = refName(v->get("heightMap"), &builtin);
        if (builtin) sf.heightImage.clear();
        sf.cutoutImage = refName(v->get("cutoutMap"), &builtin);
        if (builtin) sf.cutoutImage.clear();
        sf.combinedAlbedoImage = refName(v->get("combinedAlbedoMap"), &builtin);
        if (builtin) sf.combinedAlbedoImage.clear();
        sf.combinedNogImage = refName(v->get("combinedNogMap"), &builtin);
        if (builtin) sf.combinedNogImage.clear();
        if (sf.heightImage.empty()) out.warnings.push_back("surface " + std::to_string(si) + ": no heightMap");
        if (!(sf.texelSize > 0) || !(sf.halfWldSize > 0))
            out.warnings.push_back("surface " + std::to_string(si) + ": no surfaceMapWorldSize / halfWldSize");
        if (const Value* lm = v->get("layerMaterials"); lm && !lm->isNull()) {
            const Value* vals = lm->type == Value::Type::Array ? lm : lm->get("values");
            if (vals && vals->type == Value::Type::Array) {
                for (const Value& l : vals->arr) {
                    StockTerrainLayer L;
                    L.material = refName(l.get("material"));
                    if (const Value* st = l.get("stFromXYZ"); st && st->type == Value::Type::Array) {
                        for (size_t r = 0; r < 2 && r < st->arr.size(); ++r) numbers(&st->arr[r], L.stFromXYZ[r], 4);
                    }
                    if (const Value* ld = l.get("layerData")) numbers(ld->get("textureScale"), L.textureScale, 2);
                    sf.layers.push_back(std::move(L));
                }
            }
        }
        out.surfaces.push_back(std::move(sf));
    }
    if (out.surfaces.empty()) return fail(err, "stterrain has no surfaces");
    return true;
}

bool readStockTerrain(const std::string& pathOrZoneDir, StockTerrain& out, std::string* err) {
    std::error_code ec;
    std::string file;
    if (fs::is_regular_file(pathOrZoneDir, ec)) {
        file = pathOrZoneDir;
    } else {
        const fs::path dir = fs::path(pathOrZoneDir) / "assets" / "stterrain";
        if (!fs::is_directory(dir, ec)) return fail(err, "no assets/stterrain under " + pathOrZoneDir);
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".json") {
                if (!file.empty()) return fail(err, "more than one stterrain export under " + dir.string());
                file = it->path().string();
            }
        }
        if (file.empty()) return fail(err, "no stterrain JSON under " + dir.string());
    }
    std::string text;
    if (!readBinary(file, text)) return fail(err, "cannot read " + file);
    return parseStockTerrain(text, out, err);
}

std::vector<std::string> stockTerrainImageNames(const StockTerrain& terrain, unsigned kinds) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& n) {
        if (!n.empty() && seen.insert(n).second) out.push_back(n);
    };
    for (const auto& s : terrain.surfaces) {
        if (kinds & kTerrainHeight) add(s.heightImage);
        if (kinds & kTerrainCutout) add(s.cutoutImage);
        if (kinds & kTerrainAlbedo) add(s.combinedAlbedoImage);
        if (kinds & kTerrainNog) add(s.combinedNogImage);
    }
    return out;
}

std::string findStockTerrainImage(const std::string& imageDir, const std::string& name) {
    std::error_code ec;
    if (name.empty()) return {};
    const std::string prefix = name + ".";
    for (fs::directory_iterator it(imageDir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string fn = it->path().filename().string();
        if (fn.size() <= prefix.size() + 4 || fn.compare(0, prefix.size(), prefix) != 0) continue;
        if (fn.compare(fn.size() - 4, 4, ".dds") != 0) continue;
        // <name>.<digits>.dds (or <name>.dds)
        const std::string mid = fn.substr(prefix.size(), fn.size() - 4 - prefix.size());
        if (mid.empty() || std::all_of(mid.begin(), mid.end(), [](char c) { return std::isdigit(uint8_t(c)) != 0; }))
            return it->path().string();
    }
    const fs::path plain = fs::path(imageDir) / (name + ".dds");
    if (fs::is_regular_file(plain, ec)) return plain.string();
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// Images

bool decodeStockTerrainHeightDds(const std::string& b, StockTerrainHeightGrid& out, std::string* err) {
    out = StockTerrainHeightGrid{};
    int w = 0, h = 0;
    uint32_t dxgi = 0;
    if (!ddsHeader(b, w, h, dxgi, err)) return false;
    if (dxgi != 56) return fail(err, "height DDS is DXGI " + std::to_string(dxgi) + ", expected 56 (R16_UNORM)");
    const size_t count = size_t(w) * size_t(h);
    if (b.size() < 148 + 2 * count) return fail(err, "height DDS is truncated");
    out.width = w;
    out.height = h;
    out.data.resize(count);
    for (size_t k = 0; k < count; ++k)
        out.data[k] = uint16_t(uint8_t(b[148 + 2 * k]) | uint8_t(b[148 + 2 * k + 1]) << 8);
    return true;
}

bool decodeStockTerrainCutoutDds(const std::string& b, StockTerrainCutout& out, std::string* err) {
    out = StockTerrainCutout{};
    int w = 0, h = 0;
    uint32_t dxgi = 0;
    if (!ddsHeader(b, w, h, dxgi, err)) return false;
    if (dxgi != 42) return fail(err, "cutout DDS is DXGI " + std::to_string(dxgi) + ", expected 42 (R32_UINT)");
    const size_t count = size_t(w) * size_t(h);
    if (b.size() < 148 + 4 * count) return fail(err, "cutout DDS is truncated");
    out.wordsX = w;
    out.wordsY = h;
    out.words.resize(count);
    for (size_t k = 0; k < count; ++k) out.words[k] = rd32(b, 148 + 4 * k);
    return true;
}

bool readStockTerrainHeightDds(const std::string& path, StockTerrainHeightGrid& out, std::string* err) {
    std::string b;
    if (!readBinary(path, b)) return fail(err, "cannot read " + path);
    return decodeStockTerrainHeightDds(b, out, err);
}

bool readStockTerrainCutoutDds(const std::string& path, StockTerrainCutout& out, std::string* err) {
    std::string b;
    if (!readBinary(path, b)) return fail(err, "cannot read " + path);
    return decodeStockTerrainCutoutDds(b, out, err);
}

// ---------------------------------------------------------------------------------------------------------------
// Meshes

int stockTerrainSamples(int cells, int stride) {
    if (cells <= 0) return 0;
    if (stride < 1) stride = 1;
    return (cells + stride - 1) / stride + 1;
}

namespace {

// Sample texel index k (0 .. samples - 1) of a surface with `cells` cells.
int sampleTexel(int k, int cells, int stride) { return std::min(k * stride, cells); }

// Coarse cell (a, b) is kept: at least half of its fine cells are present.
bool coarseKept(const StockTerrainCutout& c, int a, int b, int cells, int stride) {
    const int x0 = sampleTexel(a, cells, stride), x1 = sampleTexel(a + 1, cells, stride);
    const int y0 = sampleTexel(b, cells, stride), y1 = sampleTexel(b + 1, cells, stride);
    if (stride == 1) return c.present(x0, y0);
    size_t on = 0, all = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            ++all;
            on += c.present(x, y) ? 1 : 0;
        }
    return 2 * on >= all;
}

bool usableCutout(const StockTerrainCutout* c) { return c && c->wordsX > 0 && c->wordsY > 0 && !c->words.empty(); }

}  // namespace

bool buildStockTerrainMesh(const StockTerrain& terrain, int surface, const StockTerrainHeightGrid& heights,
                           const StockTerrainCutout* cutout, const StockTerrainBuildOptions& opt, StockTerrainMesh& out,
                           std::string* err) {
    out = StockTerrainMesh{};
    if (surface < 0 || size_t(surface) >= terrain.surfaces.size()) return fail(err, "surface index out of range");
    const StockTerrainSurface& s = terrain.surfaces[size_t(surface)];
    const int cells = s.cells();
    if (cells <= 0) return fail(err, "surface " + std::to_string(surface) + " has no map resolution");
    if (!(s.texelSize > 0) || !(s.halfWldSize > 0)) return fail(err, "surface " + std::to_string(surface) + " has no size");
    if (heights.width < s.mapResolution || heights.height < s.mapResolution ||
        heights.data.size() < size_t(heights.width) * size_t(heights.height))
        return fail(err, "surface " + std::to_string(surface) + ": height grid " + std::to_string(heights.width) + "x" +
                             std::to_string(heights.height) + " is smaller than mapResolution " + std::to_string(s.mapResolution));
    const int stride = std::max(1, opt.stride);
    const int n = stockTerrainSamples(cells, stride);  // samples per side
    const bool cut = opt.applyCutout && usableCutout(cutout);

    out.surface = surface;
    out.stride = stride;
    out.albedoImage = s.combinedAlbedoImage;
    out.mesh.material = s.layers.empty() ? std::string() : s.layers[0].material;
    out.cellsTotal = size_t(n - 1) * size_t(n - 1);

    // Which coarse cells are kept, and which samples they use.
    std::vector<uint8_t> keep(size_t(n - 1) * size_t(n - 1), 1);
    if (cut)
        for (int b = 0; b + 1 < n; ++b)
            for (int a = 0; a + 1 < n; ++a) keep[size_t(b) * size_t(n - 1) + size_t(a)] = coarseKept(*cutout, a, b, cells, stride);
    std::vector<uint32_t> vid(size_t(n) * size_t(n), UINT32_MAX);
    for (int b = 0; b + 1 < n; ++b)
        for (int a = 0; a + 1 < n; ++a) {
            if (!keep[size_t(b) * size_t(n - 1) + size_t(a)]) continue;
            ++out.cellsKept;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) vid[size_t(b + dy) * size_t(n) + size_t(a + dx)] = 0;
        }

    const double half = s.halfWldSize, size2 = 2 * half;
    const double* st0 = s.layers.empty() ? nullptr : s.layers[0].stFromXYZ[0];
    const double* st1 = s.layers.empty() ? nullptr : s.layers[0].stFromXYZ[1];
    auto zAt = [&](int i, int j) { return s.heightToZ(heights.at(i, j)); };
    uint32_t next = 0;
    for (int b = 0; b < n; ++b) {
        const int j = sampleTexel(b, cells, stride);
        for (int a = 0; a < n; ++a) {
            uint32_t& id = vid[size_t(b) * size_t(n) + size_t(a)];
            if (id == UINT32_MAX) continue;
            id = next++;
            const int i = sampleTexel(a, cells, stride);
            const Vec3 p{s.texelX(i), s.texelY(j), zAt(i, j)};
            out.mesh.pos.push_back(p);
            out.bounds.add(p);
            // Normal: central differences over +-stride texels, clamped to the grid.
            const int il = std::max(0, i - stride), ir = std::min(cells, i + stride);
            const int jd = std::max(0, j - stride), ju = std::min(cells, j + stride);
            const double dzdx = (zAt(ir, j) - zAt(il, j)) / (double(ir - il) * s.texelSize);
            const double dzdy = (zAt(i, ju) - zAt(i, jd)) / (double(ju - jd) * s.texelSize);
            out.mesh.normal.push_back(normalize(Vec3{-dzdx, -dzdy, 1.0}));
            double u = 0, v = 0;
            if (st0) {
                u = st0[0] * p.x + st0[1] * p.y + st0[2] * p.z + st0[3];
                v = st1[0] * p.x + st1[1] * p.y + st1[2] * p.z + st1[3];
            }
            out.mesh.uv.push_back({u, 1.0 - v});
            out.albedoUv.push_back({(p.x - s.origin.x + half) / size2, 1.0 - (p.y - s.origin.y + half) / size2});
        }
    }
    out.mesh.idx.reserve(out.cellsKept * 6);
    for (int b = 0; b + 1 < n; ++b)
        for (int a = 0; a + 1 < n; ++a) {
            if (!keep[size_t(b) * size_t(n - 1) + size_t(a)]) continue;
            const uint32_t p00 = vid[size_t(b) * size_t(n) + size_t(a)], p10 = vid[size_t(b) * size_t(n) + size_t(a + 1)];
            const uint32_t p01 = vid[size_t(b + 1) * size_t(n) + size_t(a)], p11 = vid[size_t(b + 1) * size_t(n) + size_t(a + 1)];
            // Diagonal (i, j) - (i + 1, j + 1), as the game's highLODIndices; counter-clockwise from +Z.
            out.mesh.idx.insert(out.mesh.idx.end(), {p00, p10, p11, p00, p11, p01});
        }
    return true;
}

bool buildStockTerrainMeshes(const StockTerrain& terrain, const std::vector<StockTerrainHeightGrid>& heights,
                             const std::vector<StockTerrainCutout>& cutouts, const StockTerrainBuildOptions& opt,
                             StockTerrainMeshes& out, std::string* err) {
    out = StockTerrainMeshes{};
    for (size_t s = 0; s < terrain.surfaces.size(); ++s) {
        if (s >= heights.size() || heights[s].data.empty()) {
            out.warnings.push_back("surface " + std::to_string(s) + ": no height grid, skipped");
            continue;
        }
        const StockTerrainCutout* c = s < cutouts.size() && usableCutout(&cutouts[s]) ? &cutouts[s] : nullptr;
        if (!c && !terrain.surfaces[s].cutoutImage.empty() && opt.applyCutout)
            out.warnings.push_back("surface " + std::to_string(s) + ": cutout map " + terrain.surfaces[s].cutoutImage +
                                   " not given, every cell drawn");
        StockTerrainMesh m;
        std::string e;
        if (!buildStockTerrainMesh(terrain, int(s), heights[s], c, opt, m, &e)) {
            out.warnings.push_back(e);
            continue;
        }
        out.vertices += m.mesh.pos.size();
        out.triangles += m.mesh.idx.size() / 3;
        if (m.bounds.valid()) out.bounds.add(m.bounds);
        out.meshes.push_back(std::move(m));
    }
    if (out.meshes.empty()) return fail(err, "no terrain surface could be built");
    return true;
}

size_t stockTerrainTriangleCount(const StockTerrain& terrain, int stride, const std::vector<StockTerrainCutout>* cutouts) {
    stride = std::max(1, stride);
    size_t tris = 0;
    for (size_t s = 0; s < terrain.surfaces.size(); ++s) {
        const StockTerrainSurface& sf = terrain.surfaces[s];
        if (sf.heightImage.empty()) continue;
        const int cells = sf.cells();
        const int n = stockTerrainSamples(cells, stride);
        if (n < 2) continue;
        const StockTerrainCutout* c = cutouts && s < cutouts->size() && usableCutout(&(*cutouts)[s]) ? &(*cutouts)[s] : nullptr;
        if (!c) {
            tris += 2 * size_t(n - 1) * size_t(n - 1);
            continue;
        }
        for (int b = 0; b + 1 < n; ++b)
            for (int a = 0; a + 1 < n; ++a) tris += coarseKept(*c, a, b, cells, stride) ? 2 : 0;
    }
    return tris;
}

int stockTerrainStrideForBudget(const StockTerrain& terrain, size_t maxTriangles, const std::vector<StockTerrainCutout>* cutouts,
                                int maxStride) {
    maxStride = std::max(1, maxStride);
    // The upper bound decreases with the stride: find the smallest stride under budget by it first, then (with
    // cutouts) step down while the exact count still fits.
    int lo = 1;
    while (lo < maxStride && stockTerrainTriangleCount(terrain, lo) > maxTriangles) ++lo;
    if (!cutouts) return lo;
    while (lo > 1 && stockTerrainTriangleCount(terrain, lo - 1, cutouts) <= maxTriangles) --lo;
    return lo;
}

}  // namespace iw8r
