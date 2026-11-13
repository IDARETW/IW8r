#include "stock_clutter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <system_error>
#include <tuple>

#include "json_lite.h"
#include "stock_world.h"

namespace iw8r {
namespace {

using json::Value;
namespace fs = std::filesystem;
using Quat = std::array<double, 4>;

// ---------------------------------------------------------------------------------------------------------------
// Little JSON helpers.

double num(const Value* v, double def = 0) {
    if (!v) return def;
    if (auto n = v->number()) return *n;
    if (v->type == Value::Type::Bool) return v->b ? 1 : 0;
    return def;
}

bool unhex(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2) return false;
    out.resize(hex.size() / 2);
    auto nib = [](char c) -> int {
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

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
float rdf(const uint8_t* p) {
    const uint32_t u = rd32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
void wr32(uint8_t* p, uint32_t v) {
    for (int k = 0; k < 4; ++k) p[k] = uint8_t(v >> (8 * k));
}
void wrf(uint8_t* p, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    wr32(p, u);
}

// Up to n numbers from an array, an object with "v" (ACTS vec / union view), or x y z w members.
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
        for (; k < n && k < 4; ++k) {
            const Value* c = v->get(names[k]);
            if (!c || !c->number()) break;
            out[k] = *c->number();
        }
        return k;
    }
    return 0;
}

// The raw record bytes of an exported array: hex "bytes", or nothing (values are read by the caller).
bool arrayBytes(const Value* node, std::vector<uint8_t>& bytes, bool& isBytes) {
    isBytes = false;
    if (!node) return true;
    if (const Value* b = node->get("bytes")) {
        if (!b->string() || !unhex(*b->string(), bytes)) return false;
        isBytes = true;
    }
    return true;
}

// Skips one JSON value starting at text[i] (after whitespace); returns the index just past it, or npos.
size_t skipValue(const std::string& t, size_t i) {
    const size_t n = t.size();
    while (i < n && std::isspace(uint8_t(t[i]))) ++i;
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

// Top-level members of the object that starts at text[open] ('{'): key -> (value begin, value end).
bool objectMembers(const std::string& t, size_t open, std::map<std::string, std::pair<size_t, size_t>>& out) {
    const size_t n = t.size();
    if (open >= n || t[open] != '{') return false;
    size_t i = open + 1;
    while (i < n) {
        while (i < n && (std::isspace(uint8_t(t[i])) || t[i] == ',')) ++i;
        if (i < n && t[i] == '}') return true;
        if (i >= n || t[i] != '"') return false;
        const size_t ke = skipValue(t, i);
        if (ke == std::string::npos) return false;
        const std::string key = t.substr(i + 1, ke - i - 2);
        i = ke;
        while (i < n && std::isspace(uint8_t(t[i]))) ++i;
        if (i >= n || t[i] != ':') return false;
        ++i;
        while (i < n && std::isspace(uint8_t(t[i]))) ++i;
        const size_t ve = skipValue(t, i);
        if (ve == std::string::npos) return false;
        out[key] = {i, ve};
        i = ve;
    }
    return false;
}

std::optional<Value> parseSpan(const std::string& t, const std::pair<size_t, size_t>& span, std::string* err) {
    return json::parse(t.substr(span.first, span.second - span.first), err);
}

std::string assetName(std::string s) {
    while (!s.empty() && (s[0] == '#' || s[0] == ',')) s.erase(0, 1);
    return s;
}

bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// Quaternion helpers.

Quat normalizeQ(Quat q) {
    const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(n > 1e-300)) return {0, 0, 0, 1};
    for (double& c : q) c /= n;
    return q;
}

Quat conj(const Quat& q) { return {-q[0], -q[1], -q[2], q[3]}; }

}  // namespace

// =================================================================================================================
// GfxWorld clutter collections.

bool parseStockClutterCollections(const std::string& text, std::vector<StockClutterCollection>& out, std::string* err) {
    out.clear();
    const size_t key = text.find("\"smodels\":");
    if (key == std::string::npos) return fail(err, "gfx_map has no smodels");
    size_t open = key + 10;
    while (open < text.size() && std::isspace(uint8_t(text[open]))) ++open;
    std::map<std::string, std::pair<size_t, size_t>> members;
    if (!objectMembers(text, open, members)) return fail(err, "smodels is not a readable object");

    const auto cc = members.find("clutterCollections");
    if (cc == members.end()) return fail(err, "smodels has no clutterCollections");
    std::string jerr;
    auto ccv = parseSpan(text, cc->second, &jerr);
    if (!ccv) return fail(err, "clutterCollections: " + jerr);
    if (ccv->isNull()) return true;  // no clutter on this map (every 6v6 map)

    // Records: maxInstanceCount, smodelCollectionIndex, terrainSurfaceIndex, clutterLayerIndex.
    std::vector<std::array<uint32_t, 4>> recs;
    std::vector<uint8_t> bytes;
    bool isBytes = false;
    if (!arrayBytes(&*ccv, bytes, isBytes)) return fail(err, "clutterCollections bytes are not hex");
    if (isBytes) {
        if (bytes.size() % 12) return fail(err, "clutterCollections bytes are not a multiple of 12");
        for (size_t o = 0; o < bytes.size(); o += 12)
            recs.push_back({rd32(&bytes[o]), rd16(&bytes[o + 4]), rd16(&bytes[o + 6]), rd16(&bytes[o + 8])});
    } else if (const Value* vals = ccv->get("values"); vals && vals->type == Value::Type::Array) {
        for (const Value& v : vals->arr)
            recs.push_back({uint32_t(num(v.get("maxInstanceCount"))), uint32_t(num(v.get("smodelCollectionIndex"))),
                            uint32_t(num(v.get("terrainSurfaceIndex"))), uint32_t(num(v.get("clutterLayerIndex")))});
    } else {
        return fail(err, "clutterCollections has neither values nor bytes");
    }
    if (recs.empty()) return true;

    // smodels.collections: GfxStaticModelCollection (16 bytes), smodelIndex is the u16 at +8.
    const auto co = members.find("collections");
    if (co == members.end()) return fail(err, "smodels has no collections");
    auto cov = parseSpan(text, co->second, &jerr);
    if (!cov) return fail(err, "collections: " + jerr);
    std::vector<uint32_t> collModel;
    if (!arrayBytes(&*cov, bytes, isBytes)) return fail(err, "collections bytes are not hex");
    if (isBytes) {
        if (bytes.size() % 16) return fail(err, "collections bytes are not a multiple of 16");
        for (size_t o = 0; o < bytes.size(); o += 16) collModel.push_back(rd16(&bytes[o + 8]));
    } else if (const Value* vals = cov->get("values"); vals && vals->type == Value::Type::Array) {
        for (const Value& v : vals->arr) collModel.push_back(uint32_t(num(v.get("smodelIndex"))));
    } else {
        return fail(err, "collections has neither values nor bytes");
    }

    const auto mo = members.find("models");
    if (mo == members.end()) return fail(err, "smodels has no models");
    auto mov = parseSpan(text, mo->second, &jerr);
    if (!mov) return fail(err, "models: " + jerr);
    const Value* mvals = mov->get("values");
    if (!mvals || mvals->type != Value::Type::Array) return fail(err, "models has no values");
    std::vector<std::string> names;
    for (const Value& m : mvals->arr) {
        const Value* mm = m.get("model");
        const std::string* s = mm && mm->get("name") ? mm->get("name")->string() : nullptr;
        names.push_back(s ? assetName(*s) : std::string());
    }

    for (size_t i = 0; i < recs.size(); ++i) {
        StockClutterCollection c;
        c.index = uint32_t(i);
        c.maxInstanceCount = recs[i][0];
        c.smodelCollectionIndex = recs[i][1];
        c.terrainSurfaceIndex = recs[i][2];
        c.clutterLayerIndex = recs[i][3];
        if (c.smodelCollectionIndex >= collModel.size()) {
            out.clear();
            return fail(err, "clutter collection " + std::to_string(i) + " names smodel collection " +
                                 std::to_string(c.smodelCollectionIndex) + " of " + std::to_string(collModel.size()));
        }
        c.smodelIndex = collModel[c.smodelCollectionIndex];
        if (c.smodelIndex >= names.size()) {
            out.clear();
            return fail(err, "clutter collection " + std::to_string(i) + " names model " + std::to_string(c.smodelIndex) +
                                 " of " + std::to_string(names.size()));
        }
        c.model = names[c.smodelIndex];
        out.push_back(std::move(c));
    }
    return true;
}

bool readStockClutterCollections(const std::string& exportPathOrDir, std::vector<StockClutterCollection>& out,
                                 std::string* err) {
    out.clear();
    std::error_code ec;
    fs::path file(exportPathOrDir);
    if (!fs::is_regular_file(file, ec)) {
        std::vector<fs::path> found;
        const fs::path dir = file / "assets" / "gfx_map";
        if (fs::is_directory(dir, ec))
            for (const auto& e : fs::recursive_directory_iterator(dir, ec))
                if (e.is_regular_file(ec) && e.path().extension() == ".json") found.push_back(e.path());
        if (found.size() != 1)
            return fail(err, "expected one gfx_map JSON under " + dir.string() + ", found " + std::to_string(found.size()));
        file = found.front();
    }
    std::ifstream f(file, std::ios::binary);
    if (!f) return fail(err, "cannot read " + file.string());
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    std::string text(size_t(std::max<std::streamoff>(n, 0)), '\0');
    if (!text.empty()) f.read(text.data(), std::streamsize(text.size()));
    if (!f) return fail(err, "short read of " + file.string());
    return parseStockClutterCollections(text, out, err);
}

// =================================================================================================================
// StTerrain clutter data.

bool decodeStockClutterLayer(const uint8_t* r, StockClutterLayer& o) {
    if (!r) return false;
    o.sampleOffset = rd32(r + 0);
    o.bitmaskOffset = rd32(r + 4);
    o.tileCount[0] = rd32(r + 8);
    o.tileCount[1] = rd32(r + 12);
    o.tileSizeUV[0] = rdf(r + 16);
    o.tileSizeUV[1] = rdf(r + 20);
    o.layerSize[0] = rdf(r + 24);
    o.layerSize[1] = rdf(r + 28);
    for (int k = 0; k < 3; ++k) o.angleMin[k] = rdf(r + 32 + 4 * k);
    for (int k = 0; k < 3; ++k) o.angleMax[k] = rdf(r + 44 + 4 * k);
    o.zOffsetMinMax[0] = rdf(r + 56);
    o.zOffsetMinMax[1] = rdf(r + 60);
    o.scaleMinMax[0] = rdf(r + 64);
    o.scaleMinMax[1] = rdf(r + 68);
    o.maxInstanceCount = rd32(r + 72);
    o.tileInstanceCountOffset = rd32(r + 76);
    o.useSurfaceNormal = rd32(r + 80);
    return true;
}

void encodeStockClutterLayer(const StockClutterLayer& o, uint8_t* r) {
    if (!r) return;
    wr32(r + 0, o.sampleOffset);
    wr32(r + 4, o.bitmaskOffset);
    wr32(r + 8, o.tileCount[0]);
    wr32(r + 12, o.tileCount[1]);
    wrf(r + 16, o.tileSizeUV[0]);
    wrf(r + 20, o.tileSizeUV[1]);
    wrf(r + 24, o.layerSize[0]);
    wrf(r + 28, o.layerSize[1]);
    for (int k = 0; k < 3; ++k) wrf(r + 32 + 4 * k, o.angleMin[k]);
    for (int k = 0; k < 3; ++k) wrf(r + 44 + 4 * k, o.angleMax[k]);
    wrf(r + 56, o.zOffsetMinMax[0]);
    wrf(r + 60, o.zOffsetMinMax[1]);
    wrf(r + 64, o.scaleMinMax[0]);
    wrf(r + 68, o.scaleMinMax[1]);
    wr32(r + 72, o.maxInstanceCount);
    wr32(r + 76, o.tileInstanceCountOffset);
    wr32(r + 80, o.useSurfaceNormal);
}

namespace {

void layerFromObject(const Value& v, StockClutterLayer& o) {
    double t[3] = {0, 0, 0};
    o.sampleOffset = uint32_t(num(v.get("sampleOffset")));
    o.bitmaskOffset = uint32_t(num(v.get("bitmaskOffset")));
    if (numbers(v.get("tileCount"), t, 2) == 2) o.tileCount[0] = uint32_t(t[0]), o.tileCount[1] = uint32_t(t[1]);
    if (numbers(v.get("tileSizeUV"), t, 2) == 2) o.tileSizeUV[0] = float(t[0]), o.tileSizeUV[1] = float(t[1]);
    if (numbers(v.get("layerSize"), t, 2) == 2) o.layerSize[0] = float(t[0]), o.layerSize[1] = float(t[1]);
    if (numbers(v.get("angleMin"), t, 3) == 3)
        for (int k = 0; k < 3; ++k) o.angleMin[k] = float(t[k]);
    if (numbers(v.get("angleMax"), t, 3) == 3)
        for (int k = 0; k < 3; ++k) o.angleMax[k] = float(t[k]);
    if (numbers(v.get("zOffsetMinMax"), t, 2) == 2) o.zOffsetMinMax[0] = float(t[0]), o.zOffsetMinMax[1] = float(t[1]);
    if (numbers(v.get("scaleMinMax"), t, 2) == 2) o.scaleMinMax[0] = float(t[0]), o.scaleMinMax[1] = float(t[1]);
    o.maxInstanceCount = uint32_t(num(v.get("maxInstanceCount")));
    o.tileInstanceCountOffset = uint32_t(num(v.get("tileInstanceCountOffset")));
    o.useSurfaceNormal = uint32_t(num(v.get("useSurfaceNormal")));
}

bool readWords(const Value* node, std::vector<uint32_t>& out) {
    out.clear();
    if (!node || node->isNull()) return true;
    std::vector<uint8_t> b;
    bool isBytes = false;
    if (!arrayBytes(node, b, isBytes)) return false;
    if (isBytes) {
        if (b.size() % 4) return false;
        for (size_t o = 0; o < b.size(); o += 4) out.push_back(rd32(&b[o]));
        return true;
    }
    const Value* vals = node->type == Value::Type::Array ? node : node->get("values");
    if (!vals || vals->type != Value::Type::Array) return false;
    for (const Value& v : vals->arr) out.push_back(uint32_t(num(&v)));
    return true;
}

bool readPoints(const Value* node, std::vector<std::array<float, 2>>& out) {
    out.clear();
    if (!node || node->isNull()) return true;
    std::vector<uint8_t> b;
    bool isBytes = false;
    if (!arrayBytes(node, b, isBytes)) return false;
    if (isBytes) {
        if (b.size() % 8) return false;
        for (size_t o = 0; o < b.size(); o += 8) out.push_back({rdf(&b[o]), rdf(&b[o + 4])});
        return true;
    }
    const Value* vals = node->type == Value::Type::Array ? node : node->get("values");
    if (!vals || vals->type != Value::Type::Array) return false;
    for (const Value& v : vals->arr) {
        double t[2] = {0, 0};
        if (numbers(&v, t, 2) != 2) return false;
        out.push_back({float(t[0]), float(t[1])});
    }
    return true;
}

}  // namespace

bool parseStockClutterTerrain(const std::string& text, StockClutterTerrain& out, std::string* err) {
    out = StockClutterTerrain{};
    std::string jerr;
    const auto root = json::parse(text, &jerr);
    if (!root) return fail(err, "stterrain JSON: " + jerr);
    const Value* fields = root->get("asset") ? root->get("asset")->get("fields") : nullptr;
    if (!fields) fields = root->get("fields");
    if (!fields) return fail(err, "stterrain has no asset.fields");
    if (const Value* nm = fields->get("name")) {
        if (const std::string* s = nm->string()) out.name = *s;
        else if (nm->get("string") && nm->get("string")->string()) out.name = *nm->get("string")->string();
    }
    if (const Value* csp = fields->get("clutterSamplePoints"); csp && !csp->isNull()) {
        if (!readPoints(csp->get("samplePoints"), out.samplePoints)) return fail(err, "clutterSamplePoints.samplePoints unreadable");
    }
    const Value* surfaces = fields->get("surfaces");
    if (!surfaces || surfaces->isNull()) return true;
    const Value* svals = surfaces->type == Value::Type::Array ? surfaces : surfaces->get("values");
    if (!svals || svals->type != Value::Type::Array) return fail(err, "surfaces has no values");
    for (const Value& s : svals->arr) {
        StockClutterSurface sf;
        if (const Value* p = s.get("objToWld")) {
            double q[4] = {0, 0, 0, 1}, o[3] = {0, 0, 0};
            if (numbers(p->get("quat"), q, 4) == 4) sf.objToWld.quat = normalizeQ({q[0], q[1], q[2], q[3]});
            if (numbers(p->get("origin"), o, 3) == 3) sf.objToWld.origin = Vec3{o[0], o[1], o[2]};
        }
        sf.heightOffset = num(s.get("heightOffset"));
        sf.heightScale = num(s.get("heightScale"), 1);
        sf.halfWldSize = num(s.get("halfWldSize"));
        sf.surfaceMapWorldSize = num(s.get("surfaceMapWorldSize"));
        if (const Value* cl = s.get("clutterLayers"); cl && !cl->isNull()) {
            std::vector<uint8_t> b;
            bool isBytes = false;
            if (!arrayBytes(cl, b, isBytes)) return fail(err, "clutterLayers bytes are not hex");
            if (isBytes) {
                if (b.size() % kClutterLayerBytes) return fail(err, "clutterLayers bytes are not a multiple of 84");
                for (size_t o = 0; o < b.size(); o += kClutterLayerBytes) {
                    StockClutterLayer l;
                    decodeStockClutterLayer(&b[o], l);
                    sf.layers.push_back(l);
                }
            } else {
                const Value* lv = cl->type == Value::Type::Array ? cl : cl->get("values");
                if (!lv || lv->type != Value::Type::Array) return fail(err, "clutterLayers has neither values nor bytes");
                for (const Value& v : lv->arr) {
                    StockClutterLayer l;
                    layerFromObject(v, l);
                    sf.layers.push_back(l);
                }
            }
        }
        if (const Value* bm = s.get("clutterSampleBitmask"); bm && !bm->isNull()) {
            if (!readWords(bm->get("bitmask"), sf.bitmask)) return fail(err, "clutterSampleBitmask.bitmask unreadable");
        }
        out.surfaces.push_back(std::move(sf));
    }
    return true;
}

// =================================================================================================================
// Shader arithmetic.

uint32_t clutterSeed(uint32_t tileX, uint32_t tileY, uint32_t sample, float tileSizeU) {
    uint32_t h = tileX ^ 0x811c9dc5u;  // clutter_gen_pass.219 lines 103-109
    h *= 0x01000193u;
    h ^= tileY;
    h *= 0x01000193u;
    h ^= sample;
    h *= 0x01000193u;
    uint32_t bits;
    std::memcpy(&bits, &tileSizeU, 4);
    return h ^ bits;
}

uint32_t clutterXorshift(uint32_t h) {  // lines 110-115
    h ^= h << 13;
    h ^= h >> 17;
    h ^= h << 5;
    return h;
}

float clutterUnit(uint32_t h) { return float(h) * 2.3283064365386963e-10f; }  // utof * 0x2f800000

Quat clutterEulerQuat(double ax, double ay, double az) {  // lines 166-179
    const double a = ax * 0.5, b = ay * 0.5, c = az * 0.5;
    const double sa = std::sin(a), ca = std::cos(a), sb = std::sin(b), cb = std::cos(b), sc = std::sin(c), cc = std::cos(c);
    return normalizeQ({sa * sb * cc - ca * cb * sc, ca * sb * sc + sa * cb * cc, sa * cb * sc - ca * sb * cc,
                       sa * sb * sc + ca * cb * cc});
}

Quat clutterAlignQuat(const Vec3& from, const Vec3& to) {  // lines 202-222
    const double d = dot(from, to);
    if (d > 0.999) return {0, 0, 0, 1};
    if (d < -0.999) {
        Vec3 axis{0, -from.z, from.y};  // cross(X, from)
        if (axis.y * axis.y + axis.z * axis.z < 0.001) axis = Vec3{from.z, 0, -from.x};  // cross(Y, from)
        axis = normalize(axis);
        return {axis.x, axis.y, axis.z, 0};
    }
    const Vec3 c = cross(from, to);
    return normalizeQ({c.x, c.y, c.z, 1 + d});
}

Quat quatMultiply(const Quat& a, const Quat& b) {  // lines 223-229: xyz = aw*b + bw*a + cross(a, b), w = aw bw - a.b
    return {a[3] * b[0] + b[3] * a[0] + (a[1] * b[2] - a[2] * b[1]), a[3] * b[1] + b[3] * a[1] + (a[2] * b[0] - a[0] * b[2]),
            a[3] * b[2] + b[3] * a[2] + (a[0] * b[1] - a[1] * b[0]), a[3] * b[3] - (a[0] * b[0] + a[1] * b[1] + a[2] * b[2])};
}

Vec3 quatRotate(const Quat& q, const Vec3& v) {  // v + w t + cross(q, t), t = 2 cross(q, v) (lines 158-164)
    const Vec3 u{q[0], q[1], q[2]};
    const Vec3 t = cross(u, v) * 2.0;
    return v + t * q[3] + cross(u, t);
}

std::array<Vec3, 3> quatAxes(const Quat& q) {
    const Quat n = normalizeQ(q);
    return {quatRotate(n, Vec3{1, 0, 0}), quatRotate(n, Vec3{0, 1, 0}), quatRotate(n, Vec3{0, 0, 1})};
}

// =================================================================================================================
// Grounds.

bool ClutterHeightmap::sample(double layerU, double layerV, ClutterGroundSample& out) const {
    if (width <= 0 || height <= 0 || texels.size() < size_t(width) * size_t(height)) return false;
    const double u = layerU * uvScale + uvBias, v = (1.0 - layerV) * uvScale + uvBias;
    auto texel = [&](int x, int y) {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return double(texels[size_t(y) * size_t(width) + size_t(x)]);
    };
    auto bilinear = [&](int ox, int oy) {  // texel-space offset as sample_l_aoffimmi
        const double fx = u * width - 0.5 + ox, fy = v * height - 0.5 + oy;
        const double x0 = std::floor(fx), y0 = std::floor(fy);
        const double tx = fx - x0, ty = fy - y0;
        const int ix = int(x0), iy = int(y0);
        const double a = texel(ix, iy) * (1 - tx) + texel(ix + 1, iy) * tx;
        const double b = texel(ix, iy + 1) * (1 - tx) + texel(ix + 1, iy + 1) * tx;
        return (a * (1 - ty) + b * ty) * heightScale + heightBias;
    };
    out.height = bilinear(0, 0);
    const double hU0 = bilinear(-1, 0), hU1 = bilinear(1, 0), hV0 = bilinear(0, -1), hV1 = bilinear(0, 1);
    out.normal = normalize(Vec3{hU0 - hU1, hV0 - hV1, 2.0 * texelWorldSize});  // lines 180-193
    return std::isfinite(out.height);
}

ClutterGroundFn heightmapGround(const ClutterHeightmap& map) {
    return [map](double u, double v, double, double, ClutterGroundSample& s) { return map.sample(u, v, s); };
}

void StockGroundGrid::build(const StockWorld& world, double cellSize, const std::function<bool(const std::string&)>& keep,
                            double minUpZ) {
    cell_ = cellSize > 1e-6 ? cellSize : 256.0;
    tris_.clear();
    cells_.clear();
    for (const ImportedMesh& m : world.meshes) {
        if (keep && !keep(m.material)) continue;
        for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
            if (m.idx[i] >= m.pos.size() || m.idx[i + 1] >= m.pos.size() || m.idx[i + 2] >= m.pos.size()) continue;
            const Vec3 &a = m.pos[m.idx[i]], &b = m.pos[m.idx[i + 1]], &c = m.pos[m.idx[i + 2]];
            const Vec3 n = cross(b - a, c - a);
            const double len = length(n);
            if (!(len > 1e-9) || n.z / len < minUpZ) continue;  // counter-clockwise from outside: up faces have n.z > 0
            addTriangle(a, b, c);
        }
    }
    finish();
}

void StockGroundGrid::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 n = cross(b - a, c - a);
    const double len = length(n);
    if (!(len > 1e-12) || std::fabs(n.z) < 1e-12 * len) return;  // vertical: no height under any point
    n = n * (1.0 / len);
    if (n.z < 0) n = -n;
    const uint32_t t = uint32_t(tris_.size());
    tris_.push_back({a, b, c, n});
    const double x0 = std::min({a.x, b.x, c.x}), x1 = std::max({a.x, b.x, c.x});
    const double y0 = std::min({a.y, b.y, c.y}), y1 = std::max({a.y, b.y, c.y});
    const int64_t cx0 = int64_t(std::floor(x0 / cell_)), cx1 = int64_t(std::floor(x1 / cell_));
    const int64_t cy0 = int64_t(std::floor(y0 / cell_)), cy1 = int64_t(std::floor(y1 / cell_));
    for (int64_t cy = cy0; cy <= cy1; ++cy)
        for (int64_t cx = cx0; cx <= cx1; ++cx) cells_.push_back({(cx << 32) ^ (cy & 0xffffffffll), t});
    sorted_ = false;
}

void StockGroundGrid::finish() {
    std::sort(cells_.begin(), cells_.end());
    sorted_ = true;
}

bool StockGroundGrid::sample(double x, double y, double zMax, double& z, Vec3& normal) const {
    const int64_t cx = int64_t(std::floor(x / cell_)), cy = int64_t(std::floor(y / cell_));
    const int64_t key = (cx << 32) ^ (cy & 0xffffffffll);
    bool found = false;
    double best = -1e300;
    auto test = [&](uint32_t t) {
        const Tri& tr = tris_[t];
        // Point in triangle (XY), either winding.
        const double d1 = (x - tr.b.x) * (tr.a.y - tr.b.y) - (tr.a.x - tr.b.x) * (y - tr.b.y);
        const double d2 = (x - tr.c.x) * (tr.b.y - tr.c.y) - (tr.b.x - tr.c.x) * (y - tr.c.y);
        const double d3 = (x - tr.a.x) * (tr.c.y - tr.a.y) - (tr.c.x - tr.a.x) * (y - tr.a.y);
        const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
        if (neg && pos) return;
        const double h = tr.a.z - (tr.n.x * (x - tr.a.x) + tr.n.y * (y - tr.a.y)) / tr.n.z;
        if (h <= zMax && h > best) {
            best = h;
            normal = tr.n;
            found = true;
        }
    };
    if (sorted_) {
        auto lo = std::lower_bound(cells_.begin(), cells_.end(), std::make_pair(key, uint32_t(0)));
        for (; lo != cells_.end() && lo->first == key; ++lo) test(lo->second);
    } else {
        for (const auto& c : cells_)
            if (c.first == key) test(c.second);
    }
    if (found) z = best;
    return found;
}

ClutterGroundFn meshGround(const StockGroundGrid& grid, const ClutterFrame& frame, double zMaxAbove) {
    const Quat q = normalizeQ(frame.quat);
    const Vec3 origin = frame.origin;
    return [&grid, q, origin, zMaxAbove](double, double, double lx, double ly, ClutterGroundSample& s) {
        const Vec3 p = quatRotate(q, Vec3{lx, ly, 0}) + origin;
        double z = 0;
        Vec3 n;
        if (!grid.sample(p.x, p.y, origin.z + zMaxAbove, z, n)) return false;
        s.height = z - p.z;
        s.normal = quatRotate(conj(q), n);
        return true;
    };
}

// =================================================================================================================
// The scatter.

namespace {

struct TileJob {
    double dist2 = 0;
    uint32_t job = 0;  // index into the layer list
    uint32_t tx = 0, ty = 0;
};

struct LayerJob {
    const StockClutterLayer* layer = nullptr;
    uint32_t model = 0;
    uint32_t collection = 0;
    const std::vector<uint32_t>* bitmask = nullptr;
    ClutterFrame frame;
    ClutterGroundFn ground;
};

uint32_t modelIndex(StockClutterScatter& out, const std::string& name) {
    for (size_t i = 0; i < out.models.size(); ++i)
        if (out.models[i] == name) return uint32_t(i);
    out.models.push_back(name);
    return uint32_t(out.models.size() - 1);
}

void addTiles(const LayerJob& j, uint32_t jobIndex, const ClutterScatterOptions& opt, std::vector<TileJob>& tiles) {
    const StockClutterLayer& L = *j.layer;
    const Quat q = normalizeQ(j.frame.quat);
    const double tw = double(L.tileSizeUV[0]) * L.layerSize[0], th = double(L.tileSizeUV[1]) * L.layerSize[1];
    const double halfDiag = 0.5 * std::sqrt(tw * tw + th * th);
    // With a radius, only the tiles under the circle's local bounding box are looked at (Verdansk layers have
    // thousands of tiles each). The circle is taken to the surface's local frame (translation and yaw).
    uint32_t tx0 = 0, ty0 = 0, tx1 = L.tileCount[0], ty1 = L.tileCount[1];
    if (opt.radius > 0 && L.tileSizeUV[0] > 0 && L.tileSizeUV[1] > 0 && L.layerSize[0] != 0 && L.layerSize[1] != 0) {
        const Vec3 lc = quatRotate(conj(q), Vec3{opt.centreX, opt.centreY, j.frame.origin.z} - j.frame.origin);
        const double cu = lc.x / L.layerSize[0] + 0.5, cv = 0.5 - lc.y / L.layerSize[1];
        const double ru = opt.radius / std::fabs(double(L.layerSize[0])), rv = opt.radius / std::fabs(double(L.layerSize[1]));
        auto clampTile = [](double t, uint32_t n) {
            if (!(t > 0)) return uint32_t(0);
            return t >= double(n) ? n : uint32_t(t);
        };
        tx0 = clampTile(std::floor((cu - ru) / L.tileSizeUV[0]) - 1, L.tileCount[0]);
        tx1 = clampTile(std::floor((cu + ru) / L.tileSizeUV[0]) + 2, L.tileCount[0]);
        ty0 = clampTile(std::floor((cv - rv) / L.tileSizeUV[1]) - 1, L.tileCount[1]);
        ty1 = clampTile(std::floor((cv + rv) / L.tileSizeUV[1]) + 2, L.tileCount[1]);
    }
    for (uint32_t ty = ty0; ty < ty1; ++ty) {
        for (uint32_t tx = tx0; tx < tx1; ++tx) {
            const double u = (tx + 0.5) * L.tileSizeUV[0], v = (ty + 0.5) * L.tileSizeUV[1];
            const Vec3 c = quatRotate(q, Vec3{(u - 0.5) * L.layerSize[0], (0.5 - v) * L.layerSize[1], 0}) + j.frame.origin;
            const double dx = c.x - opt.centreX, dy = c.y - opt.centreY, d2 = dx * dx + dy * dy;
            if (opt.radius > 0 && std::sqrt(d2) > opt.radius + halfDiag) continue;
            tiles.push_back({d2, jobIndex, tx, ty});
        }
    }
}

void runTiles(const std::vector<LayerJob>& jobs, std::vector<TileJob>& tiles,
              const std::vector<std::array<float, 2>>& samplePoints, const ClutterScatterOptions& opt,
              StockClutterScatter& out) {
    std::stable_sort(tiles.begin(), tiles.end(), [](const TileJob& a, const TileJob& b) {
        return std::tie(a.dist2, a.job, a.ty, a.tx) < std::tie(b.dist2, b.job, b.ty, b.tx);
    });
    const uint32_t every = std::max<uint32_t>(1, opt.keepEvery);
    const double r2 = opt.radius * opt.radius;
    for (const TileJob& t : tiles) {
        const LayerJob& j = jobs[t.job];
        const StockClutterLayer& L = *j.layer;
        const std::vector<uint32_t>& bm = *j.bitmask;
        const Quat fq = normalizeQ(j.frame.quat);
        const uint64_t tile = uint64_t(t.ty) * L.tileCount[0] + t.tx;  // line 51
        const uint64_t base = uint64_t(L.bitmaskOffset) + tile * kClutterWordsPerTile;
        if (base + kClutterWordsPerTile > bm.size()) continue;
        ++out.tilesVisited;
        for (uint32_t s = 0; s < kClutterSamplesPerTile; ++s) {
            const uint32_t word = bm[size_t(base + s / 32)];
            if (!(word >> (s % 32) & 1u)) continue;  // lines 56, 68-76
            if (every > 1 && s % every) continue;
            const uint64_t sp = uint64_t(L.sampleOffset) + s;  // line 84
            if (sp >= samplePoints.size()) {
                ++out.samplesDropped;
                continue;
            }
            // Layer UV and local position (lines 83-98).
            const float pu = (samplePoints[size_t(sp)][0] + float(t.tx)) * L.tileSizeUV[0];
            const float pv = (samplePoints[size_t(sp)][1] + float(t.ty)) * L.tileSizeUV[1];
            const double lx = (double(pu) - 0.5) * L.layerSize[0], ly = (0.5 - double(pv)) * L.layerSize[1];
            ClutterGroundSample g;
            if (!j.ground || !j.ground(pu, pv, lx, ly, g)) {
                ++out.samplesDropped;
                continue;
            }
            // Random numbers (lines 103-149): scale, angle x, y, z, z offset.
            uint32_t h = clutterSeed(t.tx, t.ty, s, L.tileSizeUV[0]);
            float r[5];
            for (float& x : r) {
                h = clutterXorshift(h);
                x = clutterUnit(h);
            }
            const double scale = r[0] * (double(L.scaleMinMax[1]) - L.scaleMinMax[0]) + L.scaleMinMax[0];
            const double zoff = r[4] * (double(L.zOffsetMinMax[1]) - L.zOffsetMinMax[0]) + L.zOffsetMinMax[0];
            double ang[3];
            for (int k = 0; k < 3; ++k) ang[k] = r[1 + k] * (double(L.angleMax[k]) - L.angleMin[k]) + L.angleMin[k];
            const Vec3 local{lx, ly, g.height + zoff};
            const Vec3 world = quatRotate(fq, local) + j.frame.origin;  // lines 158-165
            if (opt.radius > 0) {
                const double dx = world.x - opt.centreX, dy = world.y - opt.centreY;
                if (dx * dx + dy * dy > r2) continue;
            }
            Quat q = clutterEulerQuat(ang[0], ang[1], ang[2]);
            if (L.useSurfaceNormal) {  // lines 180-230
                const Vec3 up = quatRotate(q, Vec3{0, 0, 1});
                q = normalizeQ(quatMultiply(clutterAlignQuat(up, normalize(g.normal)), q));
            }
            StockClutterInstance inst;
            inst.model = j.model;
            inst.collection = j.collection;
            inst.tileX = t.tx;
            inst.tileY = t.ty;
            inst.sample = s;
            inst.origin = world;
            inst.quat = q;
            inst.axis = quatAxes(q);
            inst.scale = scale;
            out.instances.push_back(inst);
            if (out.instances.size() >= opt.maxInstances) {
                out.capped = true;
                return;
            }
        }
    }
}

}  // namespace

void scatterStockClutterLayer(const StockClutterLayer& layer, const std::string& model, uint32_t collection,
                              const std::vector<std::array<float, 2>>& samplePoints, const std::vector<uint32_t>& bitmask,
                              const ClutterFrame& frame, const ClutterGroundFn& ground, const ClutterScatterOptions& opt,
                              StockClutterScatter& out) {
    if (out.instances.size() >= opt.maxInstances) {
        out.capped = true;
        return;
    }
    std::vector<LayerJob> jobs(1);
    jobs[0].layer = &layer;
    jobs[0].model = modelIndex(out, model);
    jobs[0].collection = collection;
    jobs[0].bitmask = &bitmask;
    jobs[0].frame = frame;
    jobs[0].ground = ground;
    std::vector<TileJob> tiles;
    addTiles(jobs[0], 0, opt, tiles);
    runTiles(jobs, tiles, samplePoints, opt, out);
}

void scatterStockClutter(const std::vector<StockClutterCollection>& collections, const StockClutterTerrain& terrain,
                         const std::function<ClutterGroundFn(uint32_t surface)>& groundForSurface,
                         const ClutterScatterOptions& opt, StockClutterScatter& out) {
    if (out.instances.size() >= opt.maxInstances) {
        out.capped = true;
        return;
    }
    std::vector<LayerJob> jobs;
    std::map<uint32_t, ClutterGroundFn> grounds;
    for (const StockClutterCollection& c : collections) {
        if (c.terrainSurfaceIndex >= terrain.surfaces.size()) continue;
        const StockClutterSurface& sf = terrain.surfaces[c.terrainSurfaceIndex];
        if (c.clutterLayerIndex >= sf.layers.size()) continue;
        auto g = grounds.find(c.terrainSurfaceIndex);
        if (g == grounds.end())
            g = grounds.emplace(c.terrainSurfaceIndex, groundForSurface ? groundForSurface(c.terrainSurfaceIndex) : ClutterGroundFn{}).first;
        if (!g->second) continue;
        LayerJob j;
        j.layer = &sf.layers[c.clutterLayerIndex];
        j.model = modelIndex(out, c.model);
        j.collection = c.index;
        j.bitmask = &sf.bitmask;
        j.frame = sf.objToWld;
        j.ground = g->second;
        jobs.push_back(std::move(j));
    }
    std::vector<TileJob> tiles;
    for (size_t i = 0; i < jobs.size(); ++i) addTiles(jobs[i], uint32_t(i), opt, tiles);
    runTiles(jobs, tiles, terrain.samplePoints, opt, out);
}

}  // namespace iw8r
