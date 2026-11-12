// Stock reflection probes (see stock_probes.h).
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "../../third_party/bcdec/bcdec.h"

#include "stock_probes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

#include "json_lite.h"
#include "stock_lightmap.h"  // halfToFloat

namespace iw8r {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kGfxImageBytes = 0xE8;  // sizeof(GfxImage), x64-zt structs.hpp:3402 / 17804
constexpr uint64_t kPixelsFollow = 0xFFFFFFFFFFFFFFFEull;
constexpr size_t kDefaultChunk = size_t(16) << 20;
constexpr char kMagic[8] = {'I', 'W', '8', 'R', 'P', 'R', 'B', '\1'};
constexpr uint32_t kCacheVersion = 1;
constexpr uint32_t kHeaderBytes = 32;
constexpr uint32_t kProbeRecordBytes = 208;
constexpr uint32_t kBoxRecordBytes = 140;
constexpr uint32_t kMaxBoxesPerProbe = 64;
constexpr uint32_t kMaxProbes = 4096;
constexpr uint32_t kMaxBoxes = 65535;

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t rd64(const uint8_t* p) { return uint64_t(rd32(p)) | uint64_t(rd32(p + 4)) << 32; }
float rdf(const uint8_t* p) {
    const uint32_t v = rd32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

struct Writer {
    std::vector<uint8_t> b;
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
    }
    void i32(int v) { u32(uint32_t(v)); }
    void f(float v) {
        uint32_t x;
        std::memcpy(&x, &v, 4);
        u32(x);
    }
    void v3(const ProbeVec3& v) {
        for (float x : v) f(x);
    }
};

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    bool need(size_t n) {
        if (!ok || size_t(end - p) < n) ok = false;
        return ok;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        const uint32_t v = rd32(p);
        p += 4;
        return v;
    }
    int i32() { return int(u32()); }
    float f() {
        if (!need(4)) return 0;
        const float v = rdf(p);
        p += 4;
        return v;
    }
    ProbeVec3 v3() {
        ProbeVec3 v;
        for (float& x : v) x = f();
        return v;
    }
};

bool readAt(std::ifstream& f, uint64_t offset, uint8_t* dst, uint64_t bytes) {
    f.clear();
    f.seekg(std::streamoff(offset), std::ios::beg);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(dst), std::streamsize(bytes));
    return uint64_t(f.gcount()) == bytes;
}

// --- JSON helpers (ACTS export shapes) ---
float num(const json::Value* v, float fallback = 0) {
    if (!v) return fallback;
    if (auto n = v->number()) return float(*n);
    return fallback;
}

// {"v":[x,y,z]}, {"x":..,"y":..,"z":..} or [x,y,z].
template <size_t N>
std::array<float, N> vec(const json::Value* v) {
    std::array<float, N> out{};
    if (!v) return out;
    const json::Value* arr = v->type == json::Value::Type::Array ? v : v->get("v");
    if (arr && arr->type == json::Value::Type::Array) {
        for (size_t i = 0; i < N && i < arr->arr.size(); ++i) out[i] = num(&arr->arr[i]);
        return out;
    }
    static const char* names[4] = {"x", "y", "z", "w"};
    for (size_t i = 0; i < N && i < 4; ++i) out[i] = num(v->get(names[i]));
    return out;
}

// An ACTS pointer-to-array field: {"count":..,"values":[...]}; nullptr when null.
const std::vector<json::Value>* values(const json::Value* v) {
    if (!v || v->type != json::Value::Type::Object) return nullptr;
    const json::Value* vals = v->get("values");
    return vals && vals->type == json::Value::Type::Array ? &vals->arr : nullptr;
}

// The balanced {...} that follows `key` in `text` (strings honoured), or "".
std::string objectAfterKey(const std::string& text, size_t keyPos) {
    size_t i = text.find('{', keyPos);
    if (i == std::string::npos) return {};
    int depth = 0;
    bool inStr = false;
    for (size_t k = i; k < text.size(); ++k) {
        const char c = text[k];
        if (inStr) {
            if (c == '\\') ++k;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return text.substr(i, k - i + 1);
    }
    return {};
}

void boxAabb(const StockProbeBox& b, ProbeVec3& mins, ProbeVec3& maxs) {
    for (int k = 0; k < 3; ++k) {
        float e = 0;
        for (int a = 0; a < 3; ++a) e += std::fabs(b.axes[a][k]) * b.halfSize[a];
        mins[k] = b.center[k] - e;
        maxs[k] = b.center[k] + e;
    }
}

void computeBounds(StockProbeSet& set) {
    for (StockProbe& p : set.probes) {
        bool any = false;
        for (int bi : p.boxes) {
            if (bi < 0 || size_t(bi) >= set.boxes.size()) continue;
            const StockProbeBox& b = set.boxes[size_t(bi)];
            if (stockProbeBoxIsFallback(b)) continue;  // spans the whole map
            ProbeVec3 lo, hi;
            boxAabb(b, lo, hi);
            for (int k = 0; k < 3; ++k) {
                p.mins[k] = any ? std::min(p.mins[k], lo[k]) : lo[k];
                p.maxs[k] = any ? std::max(p.maxs[k], hi[k]) : hi[k];
            }
            any = true;
        }
        if (!any) p.mins = p.maxs = p.origin;
    }
}

}  // namespace

bool stockProbeBoxIsFallback(const StockProbeBox& b) { return b.priority <= -std::numeric_limits<float>::max() * 0.5f; }

size_t bc6hBytesPerProbe(int size, int mipCount) {
    size_t total = 0;
    for (int m = 0; m < mipCount; ++m) {
        const int s = std::max(1, size >> m);
        total += size_t((s + 3) / 4) * size_t((s + 3) / 4) * 16;
    }
    return total;
}

// ---------------------------------------------------------------------------------------------------------------
// BC6H

bool decodeBC6H(const uint8_t* data, size_t bytes, int width, int height, bool isSigned, std::vector<uint16_t>& out) {
    if (width <= 0 || height <= 0) return false;
    const int bw = (width + 3) / 4, bh = (height + 3) / 4;
    if (uint64_t(bw) * uint64_t(bh) * 16 > bytes) return false;
    out.assign(size_t(width) * size_t(height) * 3, 0);
    uint16_t tile[16 * 3];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            bcdec_bc6h_half(data + (size_t(by) * size_t(bw) + size_t(bx)) * 16, tile, 12, isSigned ? 1 : 0);
            for (int t = 0; t < 16; ++t) {
                const int x = bx * 4 + (t & 3), y = by * 4 + (t >> 2);
                if (x >= width || y >= height) continue;
                uint16_t* dst = &out[(size_t(y) * size_t(width) + size_t(x)) * 3];
                dst[0] = tile[t * 3];
                dst[1] = tile[t * 3 + 1];
                dst[2] = tile[t * 3 + 2];
            }
        }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Records

bool parseStockProbeRecords(const std::string& jsonText, StockProbeSet& out, std::string* err) {
    const size_t key = jsonText.find("\"reflectionProbeData\"");
    if (key == std::string::npos) {
        if (err) *err = "no reflectionProbeData in the gfx_map JSON";
        return false;
    }
    const std::string obj = objectAfterKey(jsonText, key + 21);
    std::string jerr;
    const auto doc = json::parse(obj, &jerr);
    if (!doc) {
        if (err) *err = "reflectionProbeData JSON: " + jerr;
        return false;
    }
    StockProbeSet set;
    const int count = int(num(doc->get("reflectionProbeCount"), -1));
    const auto* probes = values(doc->get("reflectionProbes"));
    if (count <= 0 || !probes || probes->size() != size_t(count) || size_t(count) > kMaxProbes) {
        if (err) *err = "reflectionProbeData: reflectionProbeCount " + std::to_string(count) + " does not match its records";
        return false;
    }
    if (const json::Value* img = doc->get("reflectionProbeArrayImage_octahedron"); img && img->get("name"))
        if (const std::string* s = img->get("name")->string()) set.arrayImageName = *s;
    if (set.arrayImageName.empty()) {
        if (err) *err = "reflectionProbeData has no reflectionProbeArrayImage_octahedron";
        return false;
    }
    if (const auto* insts = values(doc->get("reflectionProbeInstances"))) {
        if (insts->size() > kMaxBoxes) {
            if (err) *err = "reflectionProbeData: too many probe instances";
            return false;
        }
        for (const json::Value& v : *insts) {
            StockProbeBox b;
            b.position = vec<3>(v.get("probePosition"));
            b.imageIndex = int(num(v.get("probeImageIndex")));
            b.lodLevel = int(num(v.get("lodLevel")));
            b.flags = int(num(v.get("flags")));
            b.rotation = vec<4>(v.get("probeRotation"));
            if (const json::Value* o = v.get("volumeObb")) {
                b.center = vec<3>(o->get("center"));
                b.axes[0] = vec<3>(o->get("xAxis"));
                b.axes[1] = vec<3>(o->get("yAxis"));
                b.axes[2] = vec<3>(o->get("zAxis"));
                b.halfSize = vec<3>(o->get("halfSize"));
            }
            b.priority = num(v.get("priority"));
            b.feather = vec<3>(v.get("feather"));
            b.expandNeg = vec<3>(v.get("expandProjectionNeg"));
            b.expandPos = vec<3>(v.get("expandProjectionPos"));
            set.boxes.push_back(b);
        }
    }
    const auto* sh = values(doc->get("reflectionProbeSH"));
    for (int i = 0; i < count; ++i) {
        const json::Value& v = (*probes)[size_t(i)];
        StockProbe p;
        p.origin = vec<3>(v.get("origin"));
        p.angles = vec<3>(v.get("angles"));
        const int rel = int(num(v.get("probeRelightingIndex"), 65535));
        p.relightingIndex = rel == 65535 ? -1 : rel;
        p.imageIndirection = uint32_t(num(v.get("probeImageIndirection")));
        if (const auto* idx = values(v.get("probeInstances"))) {
            if (idx->size() > kMaxBoxesPerProbe) {
                if (err) *err = "reflection probe " + std::to_string(i) + " has too many instances";
                return false;
            }
            for (const json::Value& x : *idx) {
                const int bi = int(num(&x, -1));
                if (bi < 0 || size_t(bi) >= set.boxes.size()) {
                    if (err) *err = "reflection probe " + std::to_string(i) + " names instance " + std::to_string(bi) + " out of range";
                    return false;
                }
                p.boxes.push_back(bi);
            }
        }
        if (sh && sh->size() >= size_t(count) * 4)
            for (int k = 0; k < 4; ++k)
                if (const json::Value* c = (*sh)[size_t(i) * 4 + size_t(k)].get("coeffs"); c && c->type == json::Value::Type::Array)
                    for (size_t j = 0; j < 9 && j < c->arr.size(); ++j) p.sh[size_t(k)][j] = num(&c->arr[j]);
        set.probes.push_back(std::move(p));
    }
    computeBounds(set);
    out = std::move(set);
    return true;
}

bool readStockProbeRecords(const std::string& gfxMapJsonPath, StockProbeSet& out, std::string* err) {
    std::ifstream f(fs::path(gfxMapJsonPath), std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + gfxMapJsonPath;
        return false;
    }
    // Stream in chunks until the key, then read on until its object closes. The key is near the start of the draw
    // fields (ACTS prints GfxWorldDraw's members in order), but the reader does not rely on it.
    const std::string key = "\"reflectionProbeData\"";
    std::string window;  // the tail of the previous chunk (key overlap) + this chunk
    std::vector<char> buf(kDefaultChunk);
    std::string text;
    bool found = false;
    while (f) {
        f.read(buf.data(), std::streamsize(buf.size()));
        const std::streamsize n = f.gcount();
        if (n <= 0) break;
        if (!found) {
            window.append(buf.data(), size_t(n));
            const size_t at = window.find(key);
            if (at == std::string::npos) {
                if (window.size() > key.size()) window.erase(0, window.size() - key.size());
                continue;
            }
            found = true;
            text = window.substr(at);
            window.clear();
        } else {
            text.append(buf.data(), size_t(n));
        }
        // Done once the object is complete.
        if (!objectAfterKey(text, key.size()).empty()) break;
    }
    if (!found) {
        if (err) *err = "no reflectionProbeData in " + fs::path(gfxMapJsonPath).filename().string();
        return false;
    }
    return parseStockProbeRecords(text, out, err);
}

std::string findStockGfxMapJson(const std::string& zoneDir) {
    std::error_code ec;
    const fs::path base = fs::path(zoneDir) / "assets" / "gfx_map";
    std::string found;
    int n = 0;
    for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".json") {
            found = it->path().string();
            ++n;
        }
    return n == 1 ? found : std::string();
}

// ---------------------------------------------------------------------------------------------------------------
// Images

bool extractStockProbeImages(const std::string& decPath, StockProbeSet& set, std::string* err, uint64_t* imageOffset,
                             size_t chunkBytes) {
    if (set.probes.empty() || set.arrayImageName.empty()) {
        if (err) *err = "no probe records";
        return false;
    }
    std::error_code ec;
    const uint64_t size = fs::file_size(fs::path(decPath), ec);
    std::ifstream f(fs::path(decPath), std::ios::binary);
    if (ec || !f) {
        if (err) *err = "cannot read " + decPath;
        return false;
    }
    // The GfxImage (name pointer -2 ... pixels pointer -2), then the name string, then the pixels.
    const std::string needle = set.arrayImageName + std::string(1, '\0');
    const uint64_t chunk = chunkBytes ? chunkBytes : kDefaultChunk;
    std::vector<uint8_t> buf, hdr(kGfxImageBytes);
    std::vector<uint64_t> hits;
    std::string why;  // the last rejected candidate
    for (uint64_t start = 0; start + needle.size() <= size; start += chunk) {
        const uint64_t len = std::min<uint64_t>(chunk + needle.size() - 1, size - start);
        buf.resize(size_t(len));
        if (!readAt(f, start, buf.data(), len)) {
            if (err) *err = "read error in " + decPath;
            return false;
        }
        const uint64_t last = std::min<uint64_t>(chunk, len - needle.size() + 1);
        const uint8_t* p = buf.data();
        for (uint64_t i = 0; i < last; ++i) {
            if (p[i] != uint8_t(needle[0]) || std::memcmp(p + i, needle.data(), needle.size()) != 0) continue;
            const uint64_t name = start + i;
            if (name < kGfxImageBytes) continue;
            if (!readAt(f, name - kGfxImageBytes, hdr.data(), kGfxImageBytes)) continue;
            const uint8_t* h = hdr.data();
            if (rd64(h) != kPixelsFollow || rd64(h + 0xE0) != kPixelsFollow) continue;
            const uint32_t format = rd32(h + 0x14), total = rd32(h + 0x1C);
            const int w = rd16(h + 0x24), hh = rd16(h + 0x26), depth = rd16(h + 0x28), elems = rd16(h + 0x2A);
            const int levels = h[0x30];
            if (format != kProbeFormatBC6H || w != hh || w < 4 || w > 4096 || depth != 1 || levels < 1 || levels > 13) {
                why = "array image format " + std::to_string(format) + ", " + std::to_string(w) + "x" + std::to_string(hh) +
                      ", " + std::to_string(levels) + " levels: not supported";
                continue;
            }
            if (size_t(elems) != set.probes.size()) {
                why = "array image has " + std::to_string(elems) + " slices for " + std::to_string(set.probes.size()) + " probes";
                continue;
            }
            const size_t per = bc6hBytesPerProbe(w, levels);
            if (uint64_t(per) * uint64_t(elems) != total) {
                why = "array image size " + std::to_string(total) + " is not " + std::to_string(elems) + " x " + std::to_string(per);
                continue;
            }
            const uint64_t pixels = name + needle.size();
            if (pixels + total > size) {
                why = "array image pixels run past the end of the file";
                continue;
            }
            hits.push_back(name - kGfxImageBytes);
            set.size = w;
            set.mipCount = levels;
        }
    }
    if (hits.empty()) {
        if (err) *err = why.empty() ? "no " + set.arrayImageName + " image in " + fs::path(decPath).filename().string() : why;
        return false;
    }
    if (hits.size() > 1) {
        if (err) *err = std::to_string(hits.size()) + " " + set.arrayImageName + " images found: not supported yet";
        return false;
    }
    if (imageOffset) *imageOffset = hits.front();
    const size_t per = bc6hBytesPerProbe(set.size, set.mipCount);
    const size_t count = set.probes.size();
    std::vector<uint8_t> all(per * count);
    if (!readAt(f, hits.front() + kGfxImageBytes + needle.size(), all.data(), all.size())) {
        if (err) *err = "cannot read the probe images of " + decPath;
        return false;
    }
    // Level-major: level 0 of every slice, then level 1 of every slice, ... (as the x64-zt converter writes it,
    // BuildReflectionProbeArray). Verified on mp_shipment: level 1 read this way matches a 2x downsample of level 0
    // (relative error 0.29, BC6H and filtering), read slice-major it does not (1.78, unrelated data).
    for (StockProbe& p : set.probes) {
        p.bc6h.resize(per);
        p.mips.clear();
    }
    size_t src = 0, dstLevel = 0;
    for (int m = 0; m < set.mipCount; ++m) {
        const int s = std::max(1, set.size >> m);
        const size_t bytes = size_t((s + 3) / 4) * size_t((s + 3) / 4) * 16;
        for (size_t i = 0; i < count; ++i, src += bytes)
            std::memcpy(set.probes[i].bc6h.data() + dstLevel, all.data() + src, bytes);
        dstLevel += bytes;
    }
    return true;
}

bool decodeStockProbeImages(StockProbeSet& set, std::string* err) {
    const size_t per = bc6hBytesPerProbe(set.size, set.mipCount);
    for (size_t i = 0; i < set.probes.size(); ++i) {
        StockProbe& p = set.probes[i];
        if (set.size <= 0 || p.bc6h.size() != per) {
            if (err) *err = "probe " + std::to_string(i) + " has no image";
            return false;
        }
        p.mips.assign(size_t(set.mipCount), {});
        size_t at = 0;
        for (int m = 0; m < set.mipCount; ++m) {
            const int s = std::max(1, set.size >> m);
            const size_t bytes = size_t((s + 3) / 4) * size_t((s + 3) / 4) * 16;
            if (!decodeBC6H(p.bc6h.data() + at, bytes, s, s, false, p.mips[size_t(m)])) {
                if (err) *err = "probe " + std::to_string(i) + " level " + std::to_string(m) + " is short";
                return false;
            }
            at += bytes;
        }
    }
    return true;
}

bool extractStockProbes(const std::string& gfxMapJsonPath, const std::string& decPath, StockProbeSet& out, std::string* err) {
    StockProbeSet set;
    if (!readStockProbeRecords(gfxMapJsonPath, set, err)) return false;
    if (!extractStockProbeImages(decPath, set, err)) return false;
    out = std::move(set);
    return true;
}

ProbeVec3 stockProbeTexel(const StockProbe& p, int size, int mip, int x, int y) {
    if (mip < 0 || size_t(mip) >= p.mips.size()) return {};
    const int s = std::max(1, size >> mip);
    if (x < 0 || y < 0 || x >= s || y >= s) return {};
    const std::vector<uint16_t>& img = p.mips[size_t(mip)];
    const size_t i = (size_t(y) * size_t(s) + size_t(x)) * 3;
    if (i + 2 >= img.size()) return {};
    return {halfToFloat(img[i]), halfToFloat(img[i + 1]), halfToFloat(img[i + 2])};
}

double stockProbeMeanLuminance(const StockProbe& p, int size, int mip, bool skipGutter) {
    const int s = std::max(1, size >> mip);
    const int g = skipGutter && s > 8 ? 4 : 0;
    double sum = 0;
    size_t n = 0;
    for (int y = g; y < s - g; ++y)
        for (int x = g; x < s - g; ++x) {
            const ProbeVec3 c = stockProbeTexel(p, size, mip, x, y);
            sum += 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
            ++n;
        }
    return n ? sum / double(n) : 0.0;
}

// ---------------------------------------------------------------------------------------------------------------
// Cache file

bool writeStockProbeCache(const std::string& path, const StockProbeSet& set, std::string* err) {
    const size_t per = bc6hBytesPerProbe(set.size, set.mipCount);
    if (set.probes.empty() || set.size <= 0 || set.mipCount <= 0 || set.probes.size() > kMaxProbes || set.boxes.size() > kMaxBoxes) {
        if (err) *err = "probe set is empty or has no image size";
        return false;
    }
    for (const StockProbe& p : set.probes)
        if (p.bc6h.size() != per || p.boxes.size() > kMaxBoxesPerProbe) {
            if (err) *err = "probe image or box list does not match the set";
            return false;
        }
    Writer w;
    w.b.insert(w.b.end(), kMagic, kMagic + 8);
    w.u32(kCacheVersion);
    w.u32(uint32_t(set.probes.size()));
    w.u32(uint32_t(set.size));
    w.u32(uint32_t(set.mipCount));
    w.u32(uint32_t(set.boxes.size()));
    w.u32(kProbeFormatBC6H);
    uint32_t first = 0;
    for (const StockProbe& p : set.probes) {
        w.v3(p.origin);
        w.v3(p.angles);
        w.v3(p.mins);
        w.v3(p.maxs);
        w.i32(p.relightingIndex);
        w.u32(p.imageIndirection);
        for (const auto& v : p.sh)
            for (float x : v) w.f(x);
        w.u32(first);
        w.u32(uint32_t(p.boxes.size()));
        first += uint32_t(p.boxes.size());
    }
    for (const StockProbe& p : set.probes)
        for (int b : p.boxes) w.u32(uint32_t(b));
    for (const StockProbeBox& b : set.boxes) {
        w.v3(b.position);
        w.u32(uint32_t(b.imageIndex));
        w.u32(uint32_t(b.lodLevel));
        w.u32(uint32_t(b.flags));
        for (float x : b.rotation) w.f(x);
        w.v3(b.center);
        for (const auto& a : b.axes) w.v3(a);
        w.v3(b.halfSize);
        w.f(b.priority);
        w.v3(b.feather);
        w.v3(b.expandNeg);
        w.v3(b.expandPos);
    }
    for (const StockProbe& p : set.probes) w.b.insert(w.b.end(), p.bc6h.begin(), p.bc6h.end());

    // Written beside the target, then renamed over it: a reader never sees half a file.
    const fs::path target = fs::path(path);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        o.write(reinterpret_cast<const char*>(w.b.data()), std::streamsize(w.b.size()));
        if (!o) {
            o.close();
            std::error_code ec;
            fs::remove(tmp, ec);
            if (err) *err = "write error in " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (err) *err = "cannot rename " + tmp.string() + " to " + target.string();
        return false;
    }
    return true;
}

bool readStockProbeCache(const std::string& path, StockProbeSet& out, std::string* err, bool decode) {
    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < kHeaderBytes || std::memcmp(data.data(), kMagic, 8) != 0) {
        if (err) *err = path + " is not a probe cache file";
        return false;
    }
    const uint8_t* h = data.data();
    if (rd32(h + 8) != kCacheVersion) {
        if (err) *err = path + ": probe cache version " + std::to_string(rd32(h + 8)) + " is not supported";
        return false;
    }
    const uint32_t count = rd32(h + 12), size = rd32(h + 16), mips = rd32(h + 20), boxes = rd32(h + 24), format = rd32(h + 28);
    if (count == 0 || count > kMaxProbes || size < 4 || size > 4096 || mips < 1 || mips > 13 || boxes > kMaxBoxes ||
        format != kProbeFormatBC6H) {
        if (err) *err = path + ": bad probe cache header";
        return false;
    }
    if (data.size() < kHeaderBytes + uint64_t(count) * kProbeRecordBytes + uint64_t(boxes) * kBoxRecordBytes +
                          uint64_t(count) * bc6hBytesPerProbe(int(size), int(mips))) {
        if (err) *err = path + ": probe cache is shorter than its header says";
        return false;
    }
    StockProbeSet set;
    set.size = int(size);
    set.mipCount = int(mips);
    Reader r{h + kHeaderBytes, h + data.size()};
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    uint32_t totalIdx = 0;
    for (uint32_t i = 0; i < count; ++i) {
        StockProbe p;
        p.origin = r.v3();
        p.angles = r.v3();
        p.mins = r.v3();
        p.maxs = r.v3();
        p.relightingIndex = r.i32();
        p.imageIndirection = r.u32();
        for (auto& v : p.sh)
            for (float& x : v) x = r.f();
        const uint32_t first = r.u32(), n = r.u32();
        if (n > kMaxBoxesPerProbe || first != totalIdx) r.ok = false;
        totalIdx += n;
        ranges.push_back({first, n});
        set.probes.push_back(std::move(p));
    }
    for (uint32_t i = 0; i < count && r.ok; ++i)
        for (uint32_t k = 0; k < ranges[i].second; ++k) {
            const uint32_t b = r.u32();
            if (b >= boxes) r.ok = false;
            set.probes[i].boxes.push_back(int(b));
        }
    for (uint32_t i = 0; i < boxes && r.ok; ++i) {
        StockProbeBox b;
        b.position = r.v3();
        b.imageIndex = r.i32();
        b.lodLevel = r.i32();
        b.flags = r.i32();
        for (float& x : b.rotation) x = r.f();
        b.center = r.v3();
        for (auto& a : b.axes) a = r.v3();
        b.halfSize = r.v3();
        b.priority = r.f();
        b.feather = r.v3();
        b.expandNeg = r.v3();
        b.expandPos = r.v3();
        set.boxes.push_back(b);
    }
    const size_t per = bc6hBytesPerProbe(set.size, set.mipCount);
    if (!r.ok || size_t(r.end - r.p) != per * count) {
        if (err) *err = path + ": probe cache size does not match its header";
        return false;
    }
    for (StockProbe& p : set.probes) {
        p.bc6h.assign(r.p, r.p + per);
        r.p += per;
    }
    if (decode && !decodeStockProbeImages(set, err)) return false;
    out = std::move(set);
    return true;
}

}  // namespace iw8r
