#include "stock_decal_volumes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string_view>

#include "json_lite.h"
#include "stock_world.h"

namespace iw8r {
namespace {

using json::Value;
namespace fs = std::filesystem;

constexpr double kSqrtHalf = 0.70710678118654752;

double num(const Value* v, double def = 0) { return v && v->type == Value::Type::Number ? v->num : def; }

double halfToDouble(uint16_t h) {
    const bool neg = (h >> 15) != 0;
    const int exp = (h >> 10) & 31;
    const int man = h & 1023;
    double v;
    if (exp == 0)
        v = std::ldexp(double(man), -24);
    else if (exp == 31)
        v = man ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity();
    else
        v = std::ldexp(double(1024 + man), exp - 25);
    return neg ? -v : v;
}

// An unsigned small float field placed at bit `shift` of a binary16 (f16tof32(bits << shift) in the shader).
double smallFloat(uint32_t bits, int shift) { return halfToDouble(uint16_t((bits << shift) & 0x7FFF)); }

double saturate(double x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

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

// ---- JSON key scan ----

// The end (one past) of the JSON value that starts at text[pos] (after whitespace). npos when the text ends first
// (truncated) or the value is malformed at the start.
size_t jsonValueEnd(const char* text, size_t size, size_t pos) {
    while (pos < size && (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\r' || text[pos] == '\t')) ++pos;
    if (pos >= size) return std::string::npos;
    const char c = text[pos];
    if (c == '"') {
        for (size_t i = pos + 1; i < size; ++i) {
            if (text[i] == '\\') ++i;
            else if (text[i] == '"') return i + 1;
        }
        return std::string::npos;
    }
    if (c == '{' || c == '[') {
        int depth = 0;
        for (size_t i = pos; i < size; ++i) {
            const char d = text[i];
            if (d == '"') {
                for (++i; i < size && text[i] != '"'; ++i)
                    if (text[i] == '\\') ++i;
                if (i >= size) return std::string::npos;
            } else if (d == '{' || d == '[') {
                ++depth;
            } else if (d == '}' || d == ']') {
                if (--depth == 0) return i + 1;
            }
        }
        return std::string::npos;
    }
    for (size_t i = pos; i < size; ++i)
        if (text[i] == ',' || text[i] == '}' || text[i] == ']' || text[i] == ' ' || text[i] == '\n' || text[i] == '\r')
            return i;
    return std::string::npos;
}

// Position just after `"key"` + optional whitespace + ':' of the first occurrence at or after `from`; npos if none.
size_t findKey(const char* text, size_t size, const std::string& key, size_t from = 0) {
    const std::string pat = "\"" + key + "\"";
    std::string_view hay(text, size);
    for (size_t at = hay.find(pat, from); at != std::string_view::npos; at = hay.find(pat, at + 1)) {
        size_t p = at + pat.size();
        while (p < size && (text[p] == ' ' || text[p] == '\n' || text[p] == '\r' || text[p] == '\t')) ++p;
        if (p < size && text[p] == ':') return p + 1;
    }
    return std::string::npos;
}

// The value text of the first `key` in `text` ("" when absent). *bad is set for a truncated value.
std::string valueText(const std::string& text, const std::string& key, bool* bad = nullptr) {
    const size_t p = findKey(text.data(), text.size(), key);
    if (p == std::string::npos) return {};
    const size_t e = jsonValueEnd(text.data(), text.size(), p);
    if (e == std::string::npos) {
        if (bad) *bad = true;
        return {};
    }
    return text.substr(p, e - p);
}

// Chunked scan of a file: the value texts of the first occurrence of each key ("" when absent). The file is read in
// blocks; only the values are kept.
bool scanFileKeys(const fs::path& path, const std::vector<std::string>& keys, std::vector<std::string>& values,
                  std::string* err) {
    values.assign(keys.size(), {});
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + path.string();
        return false;
    }
    f.seekg(0, std::ios::end);
    const uint64_t fileSize = uint64_t(f.tellg());
    f.seekg(0);
    constexpr size_t kBlock = 8u << 20;
    size_t overlap = 0;
    for (const std::string& k : keys) overlap = std::max(overlap, k.size() + 16);
    std::vector<uint64_t> at(keys.size(), UINT64_MAX);  // file offset of the value start
    std::string buf;
    uint64_t bufStart = 0;
    size_t left = keys.size();
    while (left && bufStart + buf.size() < fileSize) {
        // Keep the last `overlap` bytes so a key split across blocks is still found.
        if (buf.size() > overlap) {
            bufStart += buf.size() - overlap;
            buf.erase(0, buf.size() - overlap);
        }
        const size_t old = buf.size();
        const size_t want = size_t(std::min<uint64_t>(kBlock, fileSize - (bufStart + old)));
        buf.resize(old + want);
        f.read(buf.data() + old, std::streamsize(want));
        buf.resize(old + size_t(f.gcount()));
        if (size_t(f.gcount()) == 0) break;
        for (size_t k = 0; k < keys.size(); ++k) {
            if (at[k] != UINT64_MAX) continue;
            const size_t p = findKey(buf.data(), buf.size(), keys[k]);
            if (p != std::string::npos && p < buf.size()) {
                at[k] = bufStart + p;
                --left;
            }
        }
    }
    for (size_t k = 0; k < keys.size(); ++k) {
        if (at[k] == UINT64_MAX) continue;
        std::ifstream g(path, std::ios::binary);
        g.seekg(std::streamoff(at[k]));
        std::string v;
        size_t chunk = 1u << 18;
        for (;;) {
            const size_t old = v.size();
            v.resize(old + chunk);
            g.read(v.data() + old, std::streamsize(chunk));
            v.resize(old + size_t(g.gcount()));
            const size_t e = jsonValueEnd(v.data(), v.size(), 0);
            if (e != std::string::npos) {
                v.resize(e);
                break;
            }
            if (g.gcount() == 0 || !g) {
                if (err) *err = path.string() + ": value of \"" + keys[k] + "\" is truncated";
                return false;
            }
            chunk = std::min<size_t>(chunk * 2, 64u << 20);
        }
        values[k] = std::move(v);
    }
    return true;
}

std::vector<fs::path> jsonFilesUnder(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".json") out.push_back(it->path());
    std::sort(out.begin(), out.end());
    return out;
}

// ---- Table parsing ----

const Value* valuesOf(const Value& node) {
    const Value* v = node.get("values");
    return v && v->type == Value::Type::Array ? v : nullptr;
}

// Asset reference name without the leading comma ("" for null).
std::string refName(const Value* ref) {
    if (!ref || ref->type != Value::Type::Object) return {};
    const Value* n = ref->get("name");
    if (!n || !n->string()) return {};
    std::string s = *n->string();
    if (!s.empty() && s[0] == ',') s.erase(0, 1);
    return s;
}

bool parseValue(const std::string& text, const char* what, Value& out, std::string* err) {
    std::string jerr;
    auto v = json::parse(text, &jerr);
    if (!v) {
        if (err) *err = std::string(what) + " JSON: " + jerr;
        return false;
    }
    out = std::move(*v);
    return true;
}

Vec3 vec3Of(const Value* node) {
    if (!node) return {};
    if (const Value* v = node->get("v"); v && v->type == Value::Type::Array && v->arr.size() >= 3)
        return {num(&v->arr[0]), num(&v->arr[1]), num(&v->arr[2])};
    if (const Value* s = node->get("__s1")) return {num(s->get("x")), num(s->get("y")), num(s->get("z"))};
    if (node->type == Value::Type::Array && node->arr.size() >= 3)
        return {num(&node->arr[0]), num(&node->arr[1]), num(&node->arr[2])};
    return {};
}

struct ZoneDecals {
    int64_t zone = 0;
    std::vector<StockDecalVolume> records;  // decoded, names unresolved
    std::vector<AABB> bounds;               // collectionBounds
    uint32_t firstCollection = 0;
};

// One gfx_map_trzone's decal block (the value of its first "decalVolumes" key) into `z`.
bool parseZoneBlock(const std::string& blockText, ZoneDecals& z, std::vector<std::string>& warnings, std::string* err) {
    Value root;
    if (!parseValue(blockText, "gfx_map_trzone decalVolumes", root, err)) return false;
    if (root.type != Value::Type::Object) return true;  // null pointer: no decals
    if (const Value* t = root.get("type"); t && t->string() && *t->string() != "GfxDecalVolumeTransientData") {
        warnings.push_back("gfx_map_trzone decalVolumes has type " + *t->string() + ": skipped");
        return true;
    }
    const Value* blocks = valuesOf(root);
    if (!blocks) return true;
    for (const Value& blk : blocks->arr) {
        if (blk.type != Value::Type::Object) continue;
        z.firstCollection = uint32_t(num(blk.get("firstCollection")));
        const size_t base = z.records.size();
        if (const Value* recs = blk.get("decalVolumes"); recs && recs->type == Value::Type::Object) {
            if (const Value* vals = valuesOf(*recs)) {
                for (const Value& r : vals->arr) {
                    StockDecalVolume d;
                    std::array<uint32_t, 3> uv{0, 0, 0};
                    if (const Value* u = r.get("uvMatrixAndOffset"); u && u->type == Value::Type::Array)
                        for (size_t i = 0; i < 3 && i < u->arr.size(); ++i) uv[i] = uint32_t(num(&u->arr[i]));
                    decodeStockDecalVolumeRecord(vec3Of(r.get("obbCenter")), uint32_t(num(r.get("obbQuat"))),
                                                 uint32_t(num(r.get("obbHalfSize"))), uint32_t(num(r.get("opacityParams"))), uv,
                                                 uint32_t(num(r.get("packedMaterialInfo"))), uint32_t(num(r.get("reserved"))), d);
                    z.records.push_back(std::move(d));
                }
            } else if (const Value* hex = recs->get("bytes"); hex && hex->string()) {
                // Undecoded export: 44-byte GfxStaticDecalVolumePacked records.
                std::vector<uint8_t> b;
                if (!unhex(*hex->string(), b) || b.size() % 44) {
                    if (err) *err = "gfx_map_trzone decalVolumes bytes are not 44-byte records";
                    return false;
                }
                for (size_t o = 0; o + 44 <= b.size(); o += 44) {
                    StockDecalVolume d;
                    decodeStockDecalVolumeRecord({readF32(b, o), readF32(b, o + 4), readF32(b, o + 8)}, readU32(b, o + 12),
                                                 readU32(b, o + 16), readU32(b, o + 20),
                                                 {readU32(b, o + 24), readU32(b, o + 28), readU32(b, o + 32)}, readU32(b, o + 36),
                                                 readU32(b, o + 40), d);
                    z.records.push_back(std::move(d));
                }
            }
        }
        const size_t declared = size_t(num(blk.get("decalVolumeCount"), double(z.records.size() - base)));
        if (declared != z.records.size() - base)
            warnings.push_back("transient zone " + std::to_string(z.zone) + ": decalVolumeCount " + std::to_string(declared) +
                               ", records read " + std::to_string(z.records.size() - base));
        if (const Value* uids = blk.get("decalVolumeUids"); uids && uids->type == Value::Type::Object)
            if (const Value* vals = valuesOf(*uids))
                for (size_t i = 0; i < vals->arr.size() && base + i < z.records.size(); ++i)
                    z.records[base + i].uid = uint32_t(num(vals->arr[i].get("uid")));
        if (const Value* cb = blk.get("collectionBounds"); cb && cb->type == Value::Type::Object)
            if (const Value* vals = valuesOf(*cb))
                for (const Value& b : vals->arr) {
                    const Vec3 mid = vec3Of(b.get("midPoint")), half = vec3Of(b.get("halfSize"));
                    AABB box;
                    box.add(mid - half);
                    box.add(mid + half);
                    z.bounds.push_back(box);
                }
    }
    return true;
}

struct GfxTables {
    std::string materials, masks, collections;
};

bool assemble(const GfxTables& g, std::vector<ZoneDecals>& zones, StockDecalVolumeSet& out, std::string* err) {
    out = {};
    if (!g.materials.empty()) {
        Value v;
        if (!parseValue(g.materials, "gfx_map decalVolumeMaterials", v, err)) return false;
        if (v.type == Value::Type::Object)
            if (const Value* vals = valuesOf(v))
                for (const Value& m : vals->arr) out.materials.push_back(refName(m.get("material")));
    }
    if (!g.masks.empty()) {
        Value v;
        if (!parseValue(g.masks, "gfx_map decalVolumeMasks", v, err)) return false;
        if (v.type == Value::Type::Object)
            if (const Value* vals = valuesOf(v))
                for (const Value& m : vals->arr) out.masks.push_back(refName(m.get("blendMapOverride")));
    }
    if (!g.collections.empty()) {
        Value v;
        if (!parseValue(g.collections, "gfx_map decalVolumeCollections", v, err)) return false;
        if (v.type == Value::Type::Object) {
            if (const Value* vals = valuesOf(v)) {
                for (const Value& c : vals->arr) {
                    StockDecalVolumeCollection col;
                    col.transientZone = int64_t(num(c.get("transientZoneIndex")));
                    col.decalCount = uint32_t(num(c.get("decalCount")));
                    col.firstZoneDecal = uint32_t(num(c.get("firstZoneDecal")));
                    out.collections.push_back(col);
                }
            } else if (const Value* hex = v.get("bytes"); hex && hex->string()) {
                std::vector<uint8_t> b;
                if (unhex(*hex->string(), b))
                    for (size_t o = 0; o + 8 <= b.size(); o += 8) {
                        StockDecalVolumeCollection col;
                        col.transientZone = int64_t(b[o] | b[o + 1] << 8);
                        col.decalCount = uint32_t(b[o + 2] | b[o + 3] << 8);
                        col.firstZoneDecal = readU32(b, o + 4);
                        out.collections.push_back(col);
                    }
            }
        }
    }
    std::sort(zones.begin(), zones.end(), [](const ZoneDecals& a, const ZoneDecals& b) { return a.zone < b.zone; });
    std::map<int64_t, size_t> zoneBase;  // zone -> index of its first volume in out.volumes
    size_t unnamed = 0, noMaterial = 0;
    for (ZoneDecals& z : zones) {
        zoneBase[z.zone] = out.volumes.size();
        for (size_t i = 0; i < z.records.size(); ++i) {
            StockDecalVolume d = std::move(z.records[i]);
            d.index = out.volumes.size();
            d.transientZone = z.zone;
            d.zoneRecord = uint32_t(i);
            const uint32_t mi = d.rawInfo & 0x7FF, ki = (d.rawInfo >> 11) & 0x7FF;
            if (mi != kStockDecalNoIndex && mi < out.materials.size()) {
                d.materialIndex = int(mi);
                d.material = out.materials[mi];
                if (d.material.empty()) ++unnamed;
            } else {
                ++noMaterial;
            }
            if (ki != kStockDecalNoIndex && ki < out.masks.size()) {
                d.maskIndex = int(ki);
                d.mask = out.masks[ki];
            }
            out.volumes.push_back(std::move(d));
        }
    }
    // Collections: (zone, first, count) ranges; bounds come from the zone block, indexed from its firstCollection.
    std::map<int64_t, uint32_t> zoneCollectionOrdinal;
    for (size_t c = 0; c < out.collections.size(); ++c) {
        StockDecalVolumeCollection& col = out.collections[c];
        const auto zb = zoneBase.find(col.transientZone);
        if (zb == zoneBase.end()) continue;
        const ZoneDecals* z = nullptr;
        for (const ZoneDecals& zz : zones)
            if (zz.zone == col.transientZone) z = &zz;
        const uint32_t ord = zoneCollectionOrdinal[col.transientZone]++;
        if (z && ord < z->bounds.size()) col.bounds = z->bounds[ord];
        for (uint32_t k = 0; k < col.decalCount; ++k) {
            const size_t vi = zb->second + col.firstZoneDecal + k;
            if (vi < out.volumes.size() && out.volumes[vi].transientZone == col.transientZone) out.volumes[vi].collection = int(c);
        }
    }
    size_t orphan = 0;
    for (const StockDecalVolume& d : out.volumes)
        if (d.collection < 0) ++orphan;
    if (noMaterial) out.warnings.push_back(std::to_string(noMaterial) + " decal volumes without a material index in range");
    if (unnamed) out.warnings.push_back(std::to_string(unnamed) + " decal volumes whose material reference is null");
    if (orphan && !out.collections.empty())
        out.warnings.push_back(std::to_string(orphan) + " decal volumes not covered by a collection (the game never draws them)");
    return true;
}

// The transientZoneIndex of a gfx_map_trzone text: the first key (it is the asset's second field).
int64_t zoneIndexOf(const std::string& v) { return v.empty() ? 0 : int64_t(std::strtoll(v.c_str(), nullptr, 10)); }

}  // namespace

// ---- Decoding ----

std::array<double, 4> decodeStockDecalQuat(uint32_t packed) {
    const uint32_t dropped = packed >> 30;
    const double a = (double(packed & 1023) * (2.0 / 1023.0) - 1.0) * kSqrtHalf;
    const double b = (double((packed >> 10) & 1023) * (2.0 / 1023.0) - 1.0) * kSqrtHalf;
    const double c = (double((packed >> 20) & 511) * (2.0 / 511.0) - 1.0) * kSqrtHalf;
    const double d = std::sqrt(1.0 - std::min(1.0, a * a + b * b + c * c));
    std::array<double, 4> q{};
    const double kept[3] = {a, b, c};
    for (int i = 0, k = 0; i < 4; ++i) q[i] = (uint32_t(i) == dropped) ? d : kept[k++];
    return q;
}

std::array<Vec3, 3> stockDecalAxesFromQuat(const std::array<double, 4>& q) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    return {Vec3{1 - 2 * y * y - 2 * z * z, 2 * x * y + 2 * w * z, 2 * x * z - 2 * w * y},
            Vec3{2 * x * y - 2 * w * z, 1 - 2 * x * x - 2 * z * z, 2 * y * z + 2 * w * x},
            Vec3{2 * x * z + 2 * w * y, 2 * y * z - 2 * w * x, 1 - 2 * x * x - 2 * y * y}};
}

Vec3 decodeStockDecalHalfSize(uint32_t packed) {
    return {smallFloat(packed & 2047, 4), smallFloat((packed >> 11) & 2047, 4), smallFloat(packed >> 22, 5)};
}

void decodeStockDecalVolumeRecord(const Vec3& center, uint32_t quat, uint32_t halfSize, uint32_t opacity,
                                  const std::array<uint32_t, 3>& uv, uint32_t info, uint32_t reserved, StockDecalVolume& out) {
    out.center = center;
    out.rawQuat = quat;
    out.rawHalfSize = halfSize;
    out.rawOpacity = opacity;
    out.rawUv = uv;
    out.rawInfo = info;
    out.rawReserved = reserved;
    out.quat = decodeStockDecalQuat(quat);
    out.axis = stockDecalAxesFromQuat(out.quat);
    for (Vec3& a : out.axis) a = normalize(a);
    out.halfSize = decodeStockDecalHalfSize(halfSize);
    out.blendEdge0 = double(opacity & 255) / 255.0;
    out.blendEdge1 = double((opacity >> 8) & 255) / 255.0;
    out.blendScale = double((opacity >> 16) & 255) / 255.0;
    out.feather = double(opacity >> 24) / 255.0;
    double e0 = out.blendEdge0, e1 = out.blendEdge1, s = out.blendScale;
    if (e0 >= e1 || s <= 0) e0 = 0, e1 = 1, s = 1;
    out.opacityScale = s / (e1 - e0);
    out.opacityBias = -e0 * s / (e1 - e0);
    out.uvMatrix = {halfToDouble(uint16_t(uv[0])), halfToDouble(uint16_t(uv[0] >> 16)), halfToDouble(uint16_t(uv[1])),
                    halfToDouble(uint16_t(uv[1] >> 16))};
    out.uvOffset = {halfToDouble(uint16_t(uv[2])), halfToDouble(uint16_t(uv[2] >> 16))};
    out.normalBlendMode = int((info >> 22) & 3);
    out.mapDrawOrder = int((info >> 24) & 31);
    out.drawOrder = int(info >> 29) - 4;
}

// ---- Reading ----

bool parseStockDecalVolumes(const std::string& gfxMapJson, const std::vector<std::string>& trzoneJson,
                            StockDecalVolumeSet& out, std::string* err) {
    GfxTables g;
    bool bad = false;
    g.materials = valueText(gfxMapJson, "decalVolumeMaterials", &bad);
    g.masks = valueText(gfxMapJson, "decalVolumeMasks", &bad);
    g.collections = valueText(gfxMapJson, "decalVolumeCollections", &bad);
    if (bad) {
        if (err) *err = "gfx_map decal volume tables are truncated";
        return false;
    }
    std::vector<ZoneDecals> zones;
    std::vector<std::string> warnings;
    for (const std::string& t : trzoneJson) {
        bool tb = false;
        const std::string block = valueText(t, "decalVolumes", &tb);
        if (tb) {
            if (err) *err = "gfx_map_trzone decalVolumes is truncated";
            return false;
        }
        if (block.empty()) continue;
        ZoneDecals z;
        z.zone = zoneIndexOf(valueText(t, "transientZoneIndex"));
        if (!parseZoneBlock(block, z, warnings, err)) return false;
        if (!z.records.empty()) zones.push_back(std::move(z));
    }
    if (!assemble(g, zones, out, err)) return false;
    out.warnings.insert(out.warnings.begin(), warnings.begin(), warnings.end());
    return true;
}

bool readStockDecalVolumes(const std::string& zoneDir, StockDecalVolumeSet& out, std::string* err) {
    const fs::path base = fs::path(zoneDir) / "assets";
    const auto gfx = jsonFilesUnder(base / "gfx_map");
    if (gfx.size() != 1) {
        if (err) *err = "expected one gfx_map JSON under " + (base / "gfx_map").string() + ", found " + std::to_string(gfx.size());
        return false;
    }
    std::vector<fs::path> trz = jsonFilesUnder(base / "gfx_map_trzone");
    {
        std::error_code ec;
        const fs::path streamed = fs::path(zoneDir) / "streamed";
        if (fs::is_directory(streamed, ec))
            for (fs::recursive_directory_iterator it(streamed, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec) && it->path().extension() == ".json" &&
                    it->path().string().find("gfx_map_trzone") != std::string::npos)
                    trz.push_back(it->path());
    }
    std::vector<std::string> gv;
    if (!scanFileKeys(gfx.front(), {"decalVolumeMaterials", "decalVolumeMasks", "decalVolumeCollections"}, gv, err))
        return false;
    GfxTables g{gv[0], gv[1], gv[2]};
    std::vector<ZoneDecals> zones;
    std::vector<std::string> warnings;
    for (const fs::path& p : trz) {
        std::vector<std::string> tv;
        if (!scanFileKeys(p, {"transientZoneIndex", "decalVolumes"}, tv, err)) return false;
        if (tv[1].empty()) continue;
        ZoneDecals z;
        z.zone = zoneIndexOf(tv[0]);
        if (!parseZoneBlock(tv[1], z, warnings, err)) return false;
        if (!z.records.empty()) zones.push_back(std::move(z));
    }
    if (!assemble(g, zones, out, err)) return false;
    out.warnings.insert(out.warnings.begin(), warnings.begin(), warnings.end());
    return true;
}

// ---- Projection maths ----

StockDecalCoord stockDecalCoord(const StockDecalVolume& vol, const Vec3& p) {
    const Vec3 d = p - vol.center;
    StockDecalCoord c;
    c.u = 0.5 + 0.5 * dot(vol.axis[1], d) / vol.halfSize.y;
    c.v = 0.5 + 0.5 * dot(vol.axis[2], d) / vol.halfSize.z;
    c.depth = -dot(vol.axis[0], d) / vol.halfSize.x;
    return c;
}

std::array<double, 2> stockDecalMaterialUv(const StockDecalVolume& vol, double u, double v) {
    return {vol.uvMatrix[0] * u + vol.uvMatrix[1] * v + vol.uvOffset[0], vol.uvMatrix[2] * u + vol.uvMatrix[3] * v + vol.uvOffset[1]};
}

AABB stockDecalVolumeBounds(const StockDecalVolume& vol) {
    Vec3 ext;
    for (int k = 0; k < 3; ++k)
        ext[k] = std::fabs(vol.axis[0][k]) * vol.halfSize.x + std::fabs(vol.axis[1][k]) * vol.halfSize.y +
                 std::fabs(vol.axis[2][k]) * vol.halfSize.z;
    AABB b;
    b.add(vol.center - ext);
    b.add(vol.center + ext);
    return b;
}

double stockDecalAngleFade(const StockDecalVolume& vol, const Vec3& normal, const StockDecalSceneParams& p) {
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
    const double cosCut = std::cos(p.cutoffAngleDeg * kDeg);
    const double cosIn = std::cos(std::max(p.cutoffAngleDeg - p.cutoffAngleRangeDeg, 0.0) * kDeg);
    const double a = saturate(-dot(vol.axis[0], normal) + p.normalBias);
    if (!(a > cosCut)) return 0.0;
    return cosIn > cosCut ? saturate((a - cosCut) / (cosIn - cosCut)) : 1.0;
}

double stockDecalEdgeFade(double u, double v, const StockDecalSceneParams& p) {
    const double m = std::max(std::fabs(2 * u - 1), std::fabs(2 * v - 1));
    return 1.0 - saturate(256.0 * (m - (1.0 - p.edgeFade)));
}

bool parseStockDecalMaterial(const std::string& text, StockDecalMaterial& out, std::string* err) {
    out = {};
    Value root;
    if (!parseValue(text, "decalvolumematerial", root, err)) return false;
    const Value* a = root.get("asset");
    const Value* f = a ? a->get("fields") : nullptr;
    if (!f) {
        if (err) *err = "decalvolumematerial JSON has no asset.fields";
        return false;
    }
    if (const Value* n = f->get("name"))
        out.name = n->string() ? *n->string() : (n->get("string") && n->get("string")->string() ? *n->get("string")->string() : "");
    if (const Value* ch = f->get("channels"); ch && ch->type == Value::Type::Array)
        for (size_t i = 0; i < 6 && i < ch->arr.size(); ++i) out.channels[i] = refName(&ch->arr[i]);
    out.flags = uint32_t(num(f->get("flags")));
    if (f->get("colorTint")) out.colorTint = vec3Of(f->get("colorTint"));
    out.alphaDissolve = num(f->get("alphaDissolveParms"));
    out.emissiveScale = num(f->get("emissiveScale"), 1);
    out.atlasRows = int(num(f->get("textureAtlasRowCount"), 1));
    out.atlasColumns = int(num(f->get("textureAtlasColumnCount"), 1));
    return true;
}

bool parseStockDecalMask(const std::string& text, std::string& name, std::string& image, std::string* err) {
    name.clear();
    image.clear();
    Value root;
    if (!parseValue(text, "decalvolumemask", root, err)) return false;
    const Value* a = root.get("asset");
    const Value* f = a ? a->get("fields") : nullptr;
    if (!f) {
        if (err) *err = "decalvolumemask JSON has no asset.fields";
        return false;
    }
    if (const Value* n = f->get("name"))
        name = n->string() ? *n->string() : (n->get("string") && n->get("string")->string() ? *n->get("string")->string() : "");
    image = refName(f->get("blendMap"));
    return true;
}

StockDecalShading stockDecalShading(const StockDecalVolume& vol, const StockDecalMaterial& mat) {
    StockDecalShading s;
    const uint32_t fl = mat.flags;
    s.colour = (fl & 4) != 0;
    s.normal = (fl & 8) != 0;
    s.emissive = (fl & 16) != 0;
    s.dissolve = (fl & 128) != 0;
    s.normalAdd = vol.normalBlendMode == 1 || (vol.normalBlendMode >= 2 && (fl & 1));
    s.opacityScale = vol.opacityScale;
    s.opacityBias = vol.opacityBias;
    s.invFeather = vol.feather > 0 ? 1.0 / vol.feather : 0.0;
    s.alphaDissolve = mat.alphaDissolve;
    s.tint = mat.colorTint;
    const bool hasOpacityMap = (fl & 64) != 0;
    const bool identity = vol.uvMatrix[0] == 1 && vol.uvMatrix[1] == 0 && vol.uvMatrix[2] == 0 && vol.uvMatrix[3] == 1 &&
                          vol.uvOffset[0] == 0 && vol.uvOffset[1] == 0 && s.opacityScale == 1 && s.opacityBias == 0;
    if (vol.maskIndex >= 0) {
        s.maskFromVolume = true;
        s.opacityMap = hasOpacityMap;
    } else if (identity && hasOpacityMap) {
        s.maskFromOpacityMap = true;  // draw_data lines 179-210: moved into the raw-UV slot, flag 64 cleared
    } else {
        s.opacityMap = hasOpacityMap;
    }
    return s;
}

double stockDecalOpacity(const StockDecalShading& s, double maskSample, double opacitySample, double u, double v,
                         double depth, double angleFade, const StockDecalSceneParams& p) {
    if (u < 0 || u >= 1 || v < 0 || v >= 1 || std::fabs(depth) >= 1 || !(angleFade > 0)) return 0.0;
    const double m = ((s.maskFromVolume || s.maskFromOpacityMap) ? maskSample : 1.0) * stockDecalEdgeFade(u, v, p);
    double op = m * saturate(m * s.opacityScale + s.opacityBias);
    if (!(op > p.alphaThreshold)) return 0.0;
    const double feather = saturate((1.0 - std::fabs(depth)) * (s.invFeather > 0 ? s.invFeather : 100000.0));
    op *= (s.opacityMap ? opacitySample : 1.0) * feather;
    if (s.dissolve) op = saturate(op * s.alphaDissolve);
    op *= angleFade;
    return op > p.alphaThreshold ? op : 0.0;
}

// ---- Receivers ----

void StockDecalReceivers::addMesh(const ImportedMesh& mesh, const std::vector<std::array<float, 2>>* lmUv,
                                  const std::vector<int>* lmIndex) {
    const uint32_t mi = uint32_t(meshes_.size());
    meshes_.push_back({&mesh, lmUv && lmUv->size() == mesh.pos.size() ? lmUv : nullptr,
                       lmIndex && lmIndex->size() == mesh.pos.size() ? lmIndex : nullptr});
    for (size_t i = 0; i + 2 < mesh.idx.size(); i += 3) {
        const uint32_t a = mesh.idx[i], b = mesh.idx[i + 1], c = mesh.idx[i + 2];
        if (a >= mesh.pos.size() || b >= mesh.pos.size() || c >= mesh.pos.size()) continue;
        Tri t;
        t.mesh = mi;
        t.first = uint32_t(i);
        for (int k = 0; k < 3; ++k) {
            const double va = mesh.pos[a][k], vb = mesh.pos[b][k], vc = mesh.pos[c][k];
            t.mins[k] = float(std::min({va, vb, vc}));
            t.maxs[k] = float(std::max({va, vb, vc}));
            // float rounding must not shrink the bounds
            t.mins[k] = std::nextafter(t.mins[k], -std::numeric_limits<float>::infinity());
            t.maxs[k] = std::nextafter(t.maxs[k], std::numeric_limits<float>::infinity());
        }
        tris_.push_back(t);
    }
}

void StockDecalReceivers::addWorld(const StockWorld& world, const std::vector<bool>& meshMask) {
    for (size_t m = 0; m < world.meshes.size(); ++m) {
        if (!meshMask.empty() && (m >= meshMask.size() || !meshMask[m])) continue;
        addMesh(world.meshes[m], m < world.lightmapUv.size() ? &world.lightmapUv[m] : nullptr,
                m < world.lightmapIndex.size() ? &world.lightmapIndex[m] : nullptr);
    }
}

void StockDecalReceivers::build() {
    nodes_.clear();
    if (tris_.empty()) return;
    nodes_.reserve(2 * tris_.size() / 4 + 8);
    struct Job {
        uint32_t node, begin, end;
    };
    std::vector<Job> jobs;
    nodes_.push_back({});
    jobs.push_back({0, 0, uint32_t(tris_.size())});
    while (!jobs.empty()) {
        const Job j = jobs.back();
        jobs.pop_back();
        Node n;
        float cmin[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        float cmax[3] = {-cmin[0], -cmin[1], -cmin[2]};
        for (int k = 0; k < 3; ++k) n.mins[k] = std::numeric_limits<float>::max(), n.maxs[k] = -n.mins[k];
        for (uint32_t i = j.begin; i < j.end; ++i)
            for (int k = 0; k < 3; ++k) {
                n.mins[k] = std::min(n.mins[k], tris_[i].mins[k]);
                n.maxs[k] = std::max(n.maxs[k], tris_[i].maxs[k]);
                const float c = 0.5f * (tris_[i].mins[k] + tris_[i].maxs[k]);
                cmin[k] = std::min(cmin[k], c);
                cmax[k] = std::max(cmax[k], c);
            }
        const uint32_t count = j.end - j.begin;
        int axis = 0;
        for (int k = 1; k < 3; ++k)
            if (cmax[k] - cmin[k] > cmax[axis] - cmin[axis]) axis = k;
        if (count <= 4 || cmax[axis] - cmin[axis] <= 0) {
            n.left = j.begin;
            n.count = count;
            nodes_[j.node] = n;
            continue;
        }
        const uint32_t mid = j.begin + count / 2;
        std::nth_element(tris_.begin() + j.begin, tris_.begin() + mid, tris_.begin() + j.end, [axis](const Tri& a, const Tri& b) {
            return a.mins[axis] + a.maxs[axis] < b.mins[axis] + b.maxs[axis];
        });
        n.left = uint32_t(nodes_.size());
        n.count = 0;
        nodes_[j.node] = n;
        nodes_.push_back({});
        nodes_.push_back({});
        jobs.push_back({n.left, j.begin, mid});
        jobs.push_back({n.left + 1, mid, j.end});
    }
}

// ---- Projection ----

namespace {

struct ClipVert {
    double s[3];  // box-normalised coordinates: dot(p - c, axis[k]) / h[k]
    Vec3 pos, normal;
    double lm[2];
};

ClipVert lerpVert(const ClipVert& a, const ClipVert& b, double t) {
    ClipVert r;
    for (int k = 0; k < 3; ++k) r.s[k] = a.s[k] + (b.s[k] - a.s[k]) * t;
    r.pos = a.pos + (b.pos - a.pos) * t;
    r.normal = a.normal + (b.normal - a.normal) * t;
    r.lm[0] = a.lm[0] + (b.lm[0] - a.lm[0]) * t;
    r.lm[1] = a.lm[1] + (b.lm[1] - a.lm[1]) * t;
    return r;
}

// Keeps the part of `in` with sign * s[axis] <= 1.
int clipPlane(const ClipVert* in, int n, ClipVert* out, int axis, double sign) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const ClipVert& a = in[i];
        const ClipVert& b = in[(i + 1) % n];
        const double da = 1.0 - sign * a.s[axis], db = 1.0 - sign * b.s[axis];  // >= 0 inside
        if (da >= 0) out[m++] = a;
        if ((da >= 0) != (db >= 0)) out[m++] = lerpVert(a, b, da / (da - db));
    }
    return m;
}

}  // namespace

size_t projectDecalVolume(const StockDecalReceivers& receivers, const StockDecalVolume& vol, StockDecalBatch& out,
                          const StockDecalProjectOptions& opt) {
    if (!(vol.halfSize.x > 0 && vol.halfSize.y > 0 && vol.halfSize.z > 0)) return 0;
    const Vec3 sc[3] = {vol.axis[0] * (1.0 / vol.halfSize.x), vol.axis[1] * (1.0 / vol.halfSize.y),
                        vol.axis[2] * (1.0 / vol.halfSize.z)};
    const auto& meshes = receivers.meshes();
    const auto& tris = receivers.tris();
    size_t added = 0;
    ClipVert bufA[16], bufB[16];
    receivers.query(stockDecalVolumeBounds(vol), [&](uint32_t ti) {
        ++out.candidateTriangles;
        const auto& t = tris[ti];
        const auto& mr = meshes[t.mesh];
        const ImportedMesh& mesh = *mr.mesh;
        const uint32_t vi[3] = {mesh.idx[t.first], mesh.idx[t.first + 1], mesh.idx[t.first + 2]};
        ClipVert v[3];
        bool hasLm = mr.lmUv != nullptr;
        for (int c = 0; c < 3; ++c) {
            const Vec3 d = mesh.pos[vi[c]] - vol.center;
            for (int k = 0; k < 3; ++k) v[c].s[k] = dot(d, sc[k]);
            v[c].pos = mesh.pos[vi[c]];
            v[c].normal = vi[c] < mesh.normal.size() ? mesh.normal[vi[c]] : Vec3{};
            if (hasLm) {
                const auto& l = (*mr.lmUv)[vi[c]];
                v[c].lm[0] = l[0];
                v[c].lm[1] = l[1];
                if (l[0] < 0 && l[1] < 0) hasLm = false;
            } else {
                v[c].lm[0] = v[c].lm[1] = -1;
            }
        }
        // Separating box face.
        for (int k = 0; k < 3; ++k) {
            if (v[0].s[k] > 1 && v[1].s[k] > 1 && v[2].s[k] > 1) return;
            if (v[0].s[k] < -1 && v[1].s[k] < -1 && v[2].s[k] < -1) return;
        }
        // Face normal when the mesh has none.
        const Vec3 fn = normalize(cross(v[1].pos - v[0].pos, v[2].pos - v[0].pos));
        for (ClipVert& c : v)
            if (dot(c.normal, c.normal) < 1e-12) c.normal = fn;
        // Fast reject: the triangle faces away or is edge-on at every corner (fade 0 at each corner normal).
        if (opt.dropUnlit) {
            bool any = false;
            for (const ClipVert& c : v)
                if (stockDecalAngleFade(vol, normalize(c.normal), opt.scene) > 0) any = true;
            if (!any) return;
        }
        int n = 3;
        ClipVert* cur = bufA;
        ClipVert* nxt = bufB;
        for (int c = 0; c < 3; ++c) cur[c] = v[c];
        for (int k = 0; k < 3 && n; ++k) {
            n = clipPlane(cur, n, nxt, k, 1.0);
            std::swap(cur, nxt);
            if (!n) break;
            n = clipPlane(cur, n, nxt, k, -1.0);
            std::swap(cur, nxt);
        }
        if (n < 3) return;
        const int lightmap = mr.lmIndex ? (*mr.lmIndex)[vi[0]] : -1;
        const uint32_t base = uint32_t(out.verts.size());
        float maxFade = 0;
        for (int i = 0; i < n; ++i) {
            const ClipVert& c = cur[i];
            StockDecalVertex o;
            o.normal = normalize(c.normal);
            o.pos = c.pos + o.normal * opt.offsetAlongNormal;
            o.u = float(saturate(0.5 + 0.5 * c.s[1]));
            o.v = float(saturate(0.5 + 0.5 * c.s[2]));
            o.depth = float(std::clamp(-c.s[0], -1.0, 1.0));
            const auto muv = stockDecalMaterialUv(vol, o.u, o.v);
            o.mu = float(muv[0]);
            o.mv = float(muv[1]);
            o.fade = float(stockDecalAngleFade(vol, o.normal, opt.scene));
            maxFade = std::max(maxFade, o.fade);
            if (hasLm) {
                o.lmU = float(c.lm[0]);
                o.lmV = float(c.lm[1]);
                o.lightmap = lightmap;
            }
            out.verts.push_back(o);
        }
        if (opt.dropUnlit && maxFade <= 0) {
            out.verts.resize(base);
            return;
        }
        size_t tri = 0;
        for (int i = 1; i + 1 < n; ++i) {
            const Vec3 e = cross(out.verts[base + i].pos - out.verts[base].pos, out.verts[base + i + 1].pos - out.verts[base].pos);
            if (dot(e, e) < 1e-12) continue;
            out.idx.push_back(base);
            out.idx.push_back(base + uint32_t(i));
            out.idx.push_back(base + uint32_t(i + 1));
            ++tri;
        }
        if (!tri) {
            out.verts.resize(base);
            return;
        }
        ++out.clippedTriangles;
        added += tri;
    });
    return added;
}

size_t projectDecalVolume(const StockWorld& world, const StockDecalVolume& vol, StockDecalBatch& out,
                          const StockDecalProjectOptions& opt) {
    StockDecalReceivers r;
    r.addWorld(world);
    r.build();
    return projectDecalVolume(r, vol, out, opt);
}

std::vector<size_t> stockDecalDrawOrder(const StockDecalVolumeSet& set) {
    std::vector<size_t> order(set.volumes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return set.volumes[a].drawOrder < set.volumes[b].drawOrder; });
    return order;
}

std::vector<StockDecalBatch> projectDecalVolumes(const StockDecalReceivers& receivers, const StockDecalVolumeSet& set,
                                                 const StockDecalProjectOptions& opt) {
    std::vector<StockDecalBatch> out;
    for (size_t i : stockDecalDrawOrder(set)) {
        StockDecalBatch b;
        b.volume = i;
        if (projectDecalVolume(receivers, set.volumes[i], b, opt)) out.push_back(std::move(b));
    }
    return out;
}

}  // namespace iw8r
