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
#include <memory>
#include <functional>
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
constexpr uint32_t kCacheVersion = 1;    // the array-image layout (MP)
constexpr uint32_t kCacheVersion2 = 2;   // the streamed layout: main and tile files (research 108 section 4.3)
constexpr uint32_t kHeaderBytes = 32;
constexpr uint32_t kHeaderBytesV2 = 40;
constexpr uint32_t kProbeRecordBytes = 208;
constexpr uint32_t kBoxRecordBytes = 140;
constexpr uint32_t kMaxBoxesPerProbe = 64;
constexpr uint32_t kMaxProbes = 65535;   // probe and instance indexes are u16 in the data (Verdansk: 5620 probes)
constexpr uint32_t kMaxBoxes = 65535;
constexpr int kMaxSubsetBoxes = 127;     // + the fallback = the renderer's 128 boxes (render.h kMaxProbeBoxes)

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

// Offsets of `needle` in a file of `size` bytes (at most `maxHits`), read in chunks of `chunk` bytes: the file is
// never held whole (a decompressed Warzone zone is about 0.6-1 GB).
bool findInFile(std::ifstream& f, uint64_t size, const std::vector<uint8_t>& needle, uint64_t chunk, size_t maxHits,
                std::vector<uint64_t>& hits) {
    if (needle.empty() || chunk == 0) return false;
    const std::boyer_moore_horspool_searcher<const uint8_t*> searcher(needle.data(), needle.data() + needle.size());
    std::vector<uint8_t> buf;
    for (uint64_t start = 0; start + needle.size() <= size; start += chunk) {
        const uint64_t len = std::min<uint64_t>(chunk + needle.size() - 1, size - start);
        buf.resize(size_t(len));
        if (!readAt(f, start, buf.data(), len)) return false;
        const uint8_t *b = buf.data(), *e = b + len;
        // Only matches that start in this chunk; the overlap belongs to the next one.
        for (const uint8_t* it = std::search(b, e, searcher); it != e; it = std::search(it + 1, e, searcher)) {
            if (uint64_t(it - b) >= chunk) break;
            hits.push_back(start + uint64_t(it - b));
            if (hits.size() >= maxHits) return true;
        }
    }
    return true;
}

// Byte size of level m of a square BC6H image of `size` texels.
size_t levelBytes(int size, int m) {
    const int s = std::max(1, size >> m);
    return size_t((s + 3) / 4) * size_t((s + 3) / 4) * 16;
}

// --- JSON helpers (ACTS export shapes) ---
float num(const json::Value* v, float fallback = 0) {
    if (!v) return fallback;
    if (auto n = v->number()) return float(*n);
    return fallback;
}
// Integers past 2^24 (probeImageIndirection) need the double.
double dnum(const json::Value* v, double fallback = 0) {
    if (!v) return fallback;
    if (auto n = v->number()) return *n;
    return fallback;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
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

// The value after a key whose closing quote ends at `keyEnd`: [first, second) of an object / array (balanced,
// strings honoured) or a literal (null, a number). first = npos while the text does not hold all of it yet.
std::pair<size_t, size_t> valueSpanAfterKey(const std::string& text, size_t keyEnd) {
    constexpr size_t npos = std::string::npos;
    size_t i = keyEnd;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
    if (i >= text.size() || text[i] != ':') return {npos, npos};
    ++i;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
    if (i >= text.size()) return {npos, npos};
    if (text[i] == '{' || text[i] == '[') {
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
            else if (c == '{' || c == '[') ++depth;
            else if ((c == '}' || c == ']') && --depth == 0) return {i, k + 1};
        }
        return {npos, npos};
    }
    size_t k = i;
    while (k < text.size() && text[k] != ',' && text[k] != '}' && text[k] != ']' && text[k] != ' ' && text[k] != '\r' &&
           text[k] != '\n')
        ++k;
    if (k >= text.size()) return {npos, npos};
    return {i, k};
}

// text[first, last) with the content of every "bytes": "<hex>" string dropped (bucket pixels: megabytes of hex the
// parser does not need).
std::string withoutHexBytes(const std::string& text, size_t first, size_t last) {
    static const std::string key = "\"bytes\"";
    std::string out;
    out.reserve(std::min<size_t>(last - first, size_t(1) << 20));
    size_t at = first;
    while (at < last) {
        const size_t k = text.find(key, at);
        if (k == std::string::npos || k >= last) break;
        size_t q = k + key.size();
        while (q < last && (text[q] == ' ' || text[q] == ':')) ++q;
        if (q >= last || text[q] != '"') {
            out.append(text, at, q - at);
            at = q;
            continue;
        }
        const size_t close = text.find('"', q + 1);
        if (close == std::string::npos || close >= last) break;
        out.append(text, at, q + 1 - at);  // up to and with the opening quote
        at = close;                        // from the closing quote on
    }
    out.append(text, at, last - at);
    return out;
}

// The integer after "transientZoneIndex" in an export text, -1 when absent.
int transientZoneIndexIn(const std::string& text) {
    static const std::string key = "\"transientZoneIndex\"";
    const size_t k = text.find(key);
    if (k == std::string::npos) return -1;
    const auto span = valueSpanAfterKey(text, k + key.size());
    if (span.first == std::string::npos) return -1;
    const std::string v = text.substr(span.first, span.second - span.first);
    if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos || v.size() > 9) return -1;
    return std::stoi(v);
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
    const double countD = dnum(doc->get("reflectionProbeCount"), -1);
    const auto* probes = values(doc->get("reflectionProbes"));
    if (countD > double(kMaxProbes)) {
        if (err) *err = "reflectionProbeData: reflectionProbeCount " + std::to_string(int64_t(countD)) + " is too many (at most " +
                        std::to_string(kMaxProbes) + ")";
        return false;
    }
    const int count = int(countD);
    if (count <= 0 || !probes || probes->size() != size_t(count)) {
        if (err) *err = "reflectionProbeData: reflectionProbeCount " + std::to_string(count) + " does not match its records";
        return false;
    }
    if (const json::Value* img = doc->get("reflectionProbeArrayImage_octahedron"); img && img->get("name"))
        if (const std::string* s = img->get("name")->string()) set.arrayImageName = *s;
    set.sharedCount = int(dnum(doc->get("sharedReflectionProbeCount"), 0));
    if (const auto* names = values(doc->get("reflectionProbeImages_octahedron")))
        for (const json::Value& v : *names) {
            const json::Value* n = v.get("name");
            const std::string* s = n ? n->string() : nullptr;
            set.sharedImageNames.push_back(s ? *s : std::string());
        }
    if (set.arrayImageName.empty()) {
        // The streamed layout (research 108): no array image, the images found through probeImageIndirection.
        bool indirect = false;
        for (const json::Value& v : *probes) indirect = indirect || dnum(v.get("probeImageIndirection")) != 0;
        if (set.sharedCount <= 0 && !indirect) {
            if (err) *err = "reflectionProbeData has no reflectionProbeArrayImage_octahedron";
            return false;
        }
        if (set.sharedCount < 0 || size_t(set.sharedCount) != set.sharedImageNames.size()) {
            if (err) *err = "reflectionProbeData: " + std::to_string(set.sharedImageNames.size()) +
                            " reflectionProbeImages_octahedron names for sharedReflectionProbeCount " + std::to_string(set.sharedCount);
            return false;
        }
        set.streamed = true;
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
    // Verdansk's export gives the SH as hex bytes ("encoding": "hex-little-endian"): 36 bytes per GfxSH3Vec.
    const std::string* shHex = nullptr;
    if (const json::Value* s = doc->get("reflectionProbeSH"); s && !sh)
        if (const json::Value* b = s->get("bytes")) shHex = b->string();
    if (shHex && shHex->size() < size_t(count) * 4 * 72) shHex = nullptr;
    for (int i = 0; i < count; ++i) {
        const json::Value& v = (*probes)[size_t(i)];
        StockProbe p;
        p.origin = vec<3>(v.get("origin"));
        p.angles = vec<3>(v.get("angles"));
        const int rel = int(num(v.get("probeRelightingIndex"), 65535));
        p.relightingIndex = rel == 65535 ? -1 : rel;
        p.imageIndirection = uint32_t(dnum(v.get("probeImageIndirection")));  // up to 0xFFFxxxxx: not through float
        if (shHex)
            for (int k = 0; k < 4; ++k)
                for (size_t j = 0; j < 9; ++j) {
                    uint8_t b[4];
                    const size_t at = ((size_t(i) * 4 + size_t(k)) * 36 + j * 4) * 2;
                    for (size_t q = 0; q < 4; ++q) b[q] = uint8_t(hexNibble((*shHex)[at + 2 * q]) << 4 | hexNibble((*shHex)[at + 2 * q + 1]));
                    p.sh[size_t(k)][j] = rdf(b);
                }
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
        if (err)
            *err = set.streamed ? "the probes are streamed (no array image): their images are in the transient zones and the XPaks"
                                : "no probe records";
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

std::string stockProbeSharedImageName(const StockProbeSet& set, int probeIndex) {
    if (probeIndex < 0 || size_t(probeIndex) >= set.probes.size()) return {};
    const uint32_t ind = set.probes[size_t(probeIndex)].imageIndirection;
    if (!stockProbeIsShared(ind)) return {};
    const uint32_t e = stockProbeElement(ind);
    if (e < set.sharedImageNames.size() && !set.sharedImageNames[e].empty()) return set.sharedImageNames[e];
    return "*reflection_probe_octahedron_" + std::to_string(probeIndex);
}

// ---------------------------------------------------------------------------------------------------------------
// Streamed layout: transient zones

namespace {

// reflectionProbes of a trzone export: `text` holds the key at `keyPos` and all of its value.
bool parseTransientZoneAt(const std::string& text, size_t keyPos, int zoneIndex, StockProbeTransientZone& out, std::string* err) {
    static const size_t keyLen = std::string("\"reflectionProbes\"").size();
    const auto span = valueSpanAfterKey(text, keyPos + keyLen);
    if (span.first == std::string::npos) {
        if (err) *err = "gfx_map_trzone reflectionProbes is incomplete";
        return false;
    }
    StockProbeTransientZone tz;
    tz.zoneIndex = zoneIndex;
    if (text.compare(span.first, span.second - span.first, "null") == 0) {
        out = tz;  // a zone without probe data
        return true;
    }
    if (text[span.first] != '{') {
        if (err) *err = "gfx_map_trzone reflectionProbes is not an object";
        return false;
    }
    std::string jerr;
    const auto doc = json::parse(withoutHexBytes(text, span.first, span.second), &jerr);
    if (!doc) {
        if (err) *err = "gfx_map_trzone reflectionProbes JSON: " + jerr;
        return false;
    }
    const auto* vals = values(&*doc);
    if (!vals || vals->empty() || (*vals)[0].type != json::Value::Type::Object) {
        if (err) *err = "gfx_map_trzone reflectionProbes has no GfxReflectionProbeTransientData";
        return false;
    }
    const json::Value& d = (*vals)[0];
    const double instanceCount = dnum(d.get("reflectionProbeInstanceCount"), -1);
    const double bucketCount = dnum(d.get("exclusiveProbeBucketCount"), -1);
    const double exclusive = dnum(d.get("exclusiveProbeCount"), -1);
    if (instanceCount < 0 || instanceCount > 65535 || bucketCount < 0 || bucketCount > 16 || exclusive < 0 || exclusive > 65535) {
        if (err) *err = "gfx_map_trzone reflectionProbes: bad counts";
        return false;
    }
    tz.hasProbes = true;
    tz.exclusiveCount = uint32_t(exclusive);
    if (const auto* inst = values(d.get("reflectionProbeInstances")))
        for (const json::Value& v : *inst) {
            const double x = dnum(&v, -1);
            if (x < 0 || x > 65535) {
                if (err) *err = "gfx_map_trzone reflectionProbes: instance index out of range";
                return false;
            }
            tz.instances.push_back(uint16_t(x));
        }
    const size_t per = bc6hBytesPerProbe(kStockProbeImageSize, kStockProbeImageMips);
    uint64_t sum = 0;
    if (const auto* buckets = values(d.get("exclusiveProbeBuckets")))
        for (const json::Value& v : *buckets) {
            const double n = dnum(v.get("elementCount"), -1), total = dnum(v.get("totalSize"), -1);
            if (n <= 0 || n > 65536 || (total >= 0 && total != n * double(per))) {
                if (err) *err = "gfx_map_trzone reflectionProbes: bucket " + std::to_string(tz.bucketCounts.size()) + " has " +
                                std::to_string(int64_t(n)) + " slices, " + std::to_string(int64_t(total)) + " bytes";
                return false;
            }
            tz.bucketCounts.push_back(uint32_t(n));
            sum += uint64_t(n);
        }
    if (tz.instances.size() != size_t(instanceCount) || tz.bucketCounts.size() != size_t(bucketCount) || sum != tz.exclusiveCount) {
        if (err) *err = "gfx_map_trzone reflectionProbes: " + std::to_string(tz.instances.size()) + " instances / " +
                        std::to_string(tz.bucketCounts.size()) + " buckets / " + std::to_string(sum) +
                        " slices do not match its counts";
        return false;
    }
    out = std::move(tz);
    return true;
}

}  // namespace

bool parseStockProbeTransientZone(const std::string& trzoneJsonText, StockProbeTransientZone& out, std::string* err) {
    const size_t key = trzoneJsonText.find("\"reflectionProbes\"");
    if (key == std::string::npos) {
        if (err) *err = "no reflectionProbes in the gfx_map_trzone JSON";
        return false;
    }
    return parseTransientZoneAt(trzoneJsonText, key, transientZoneIndexIn(trzoneJsonText), out, err);
}

bool readStockProbeTransientZone(const std::string& trzoneJsonPath, StockProbeTransientZone& out, std::string* err) {
    std::ifstream f(fs::path(trzoneJsonPath), std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + trzoneJsonPath;
        return false;
    }
    const std::string key = "\"reflectionProbes\"";
    std::string head, window, text;
    std::vector<char> buf(kDefaultChunk);
    bool found = false, complete = false;
    while (f) {
        f.read(buf.data(), std::streamsize(buf.size()));
        const std::streamsize n = f.gcount();
        if (n <= 0) break;
        if (head.size() < 65536) head.append(buf.data(), std::min(size_t(n), 65536 - head.size()));
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
        if (valueSpanAfterKey(text, key.size()).first != std::string::npos) {
            complete = true;
            break;
        }
    }
    if (!found || !complete) {
        if (err) *err = std::string(found ? "incomplete" : "no") + " reflectionProbes in " + fs::path(trzoneJsonPath).filename().string();
        return false;
    }
    return parseTransientZoneAt(text, 0, transientZoneIndexIn(head), out, err);
}

bool extractStockProbeZoneImages(const std::string& decPath, const StockProbeTransientZone& tz, const StockProbeSet& records,
                                 StockProbeImages& out, std::string* err, uint64_t* headerOffset, size_t chunkBytes) {
    const auto fail = [&](const std::string& e) {
        if (err) *err = e;
        return false;
    };
    if (tz.zoneIndex < 0 || uint32_t(tz.zoneIndex) >= kStockProbeSharedZone) return fail("the transient zone has no index");
    const uint32_t zone = uint32_t(tz.zoneIndex);
    const std::string zname = "transient zone " + std::to_string(zone);
    uint64_t sum = 0;
    for (uint32_t c : tz.bucketCounts) sum += c;
    if (tz.bucketCounts.size() > 16 || sum != tz.exclusiveCount) return fail(zname + ": its buckets do not add up to its exclusive count");
    // The records of this zone, by (bucket, element).
    std::vector<std::vector<int>> slot(tz.bucketCounts.size());
    for (size_t b = 0; b < slot.size(); ++b) slot[b].assign(tz.bucketCounts[b], -1);
    uint64_t mine = 0;
    for (size_t i = 0; i < records.probes.size(); ++i) {
        const uint32_t ind = records.probes[i].imageIndirection;
        if (stockProbeZone(ind) != zone) continue;
        ++mine;
        const uint32_t b = stockProbeBucket(ind), e = stockProbeElement(ind);
        if (b >= slot.size() || e >= slot[b].size())
            return fail("probe " + std::to_string(i) + " names bucket " + std::to_string(b) + " element " + std::to_string(e) +
                        ", which " + zname + " does not have");
        if (slot[b][e] >= 0)
            return fail("probes " + std::to_string(slot[b][e]) + " and " + std::to_string(i) + " share bucket " + std::to_string(b) +
                        " element " + std::to_string(e));
        slot[b][e] = int(i);
    }
    if (mine != tz.exclusiveCount)
        return fail("the records name " + std::to_string(mine) + " probes in " + zname + ", its export " +
                    std::to_string(tz.exclusiveCount));

    StockProbeImages res;
    res.zone = zone;
    res.size = kStockProbeImageSize;
    res.mipCount = kStockProbeImageMips;
    res.boxes.assign(tz.instances.begin(), tz.instances.end());
    if (tz.exclusiveCount == 0) {
        out = std::move(res);  // nothing to read
        return true;
    }

    // The needle: GfxReflectionProbeTransientData (pointers -2: what they point at follows), the instance list, the
    // bucket records (textureId 0, pixels -2).
    const size_t per = bc6hBytesPerProbe(res.size, res.mipCount);
    std::vector<uint8_t> needle;
    const auto put = [&](uint64_t v, int bytes) {
        for (int k = 0; k < bytes; ++k) needle.push_back(uint8_t(v >> (8 * k)));
    };
    put(tz.instances.size(), 2);
    put(tz.bucketCounts.size(), 2);
    put(tz.exclusiveCount, 4);
    put(kPixelsFollow, 8);
    put(kPixelsFollow, 8);
    for (uint16_t x : tz.instances) put(x, 2);
    for (uint32_t c : tz.bucketCounts) {
        put(uint64_t(c) * per, 4);
        put(c, 4);
        put(0, 8);
        put(kPixelsFollow, 8);
    }
    std::error_code ec;
    const uint64_t size = fs::file_size(fs::path(decPath), ec);
    std::ifstream f(fs::path(decPath), std::ios::binary);
    if (ec || !f) return fail("cannot read " + decPath);
    std::vector<uint64_t> hits;
    if (!findInFile(f, size, needle, chunkBytes ? chunkBytes : kDefaultChunk, 2, hits)) return fail("read error in " + decPath);
    const std::string file = fs::path(decPath).filename().string();
    if (hits.size() != 1)
        return fail(hits.empty() ? zname + " probe data not found in " + file : zname + " probe data found more than once in " + file);
    const uint64_t pixels = hits[0] + needle.size(), total = uint64_t(tz.exclusiveCount) * per;
    if (pixels + total > size) return fail(zname + " probe images run past the end of " + file);
    std::vector<uint8_t> block(static_cast<size_t>(total));
    if (!readAt(f, pixels, block.data(), total)) return fail("read error in " + decPath);

    // Bucket after bucket; inside a bucket level-major: level m of slice e at sum over l < m of (count * L[l]) + e * L[m].
    std::vector<std::vector<uint8_t>> images(records.probes.size());
    uint64_t base = 0;
    for (size_t b = 0; b < slot.size(); ++b) {
        const size_t count = slot[b].size();
        size_t src = size_t(base), dst = 0;
        for (int m = 0; m < res.mipCount; ++m) {
            const size_t L = levelBytes(res.size, m);
            for (size_t e = 0; e < count; ++e) {
                std::vector<uint8_t>& img = images[size_t(slot[b][e])];
                img.resize(per);
                std::memcpy(img.data() + dst, block.data() + src + e * L, L);
            }
            src += count * L;
            dst += L;
        }
        base += uint64_t(count) * per;
    }
    for (size_t i = 0; i < images.size(); ++i)
        if (!images[i].empty()) {
            res.probes.push_back(int(i));
            res.bc6h.push_back(std::move(images[i]));
        }
    if (headerOffset) *headerOffset = hits[0];
    out = std::move(res);
    return true;
}

bool parseStockProbeDds(const uint8_t* data, size_t bytes, std::vector<uint8_t>& bc6h, std::string* err) {
    const size_t per = bc6hBytesPerProbe(kStockProbeImageSize, kStockProbeImageMips);
    const auto fail = [&](const std::string& e) {
        if (err) *err = "probe DDS: " + e;
        return false;
    };
    if (!data || bytes < 148 || std::memcmp(data, "DDS ", 4) != 0 || rd32(data + 4) != 124) return fail("not a DDS file");
    if (!(rd32(data + 80) & 0x4) || std::memcmp(data + 84, "DX10", 4) != 0) return fail("no DX10 header");
    const uint32_t height = rd32(data + 12), width = rd32(data + 16), depth = rd32(data + 24), mips = rd32(data + 28);
    const uint32_t caps2 = rd32(data + 112), dxgi = rd32(data + 128), dim = rd32(data + 132), misc = rd32(data + 136),
                   arraySize = rd32(data + 140);
    if (dxgi != 95) return fail("DXGI format " + std::to_string(dxgi) + " is not BC6H_UF16 (95)");
    if (width != uint32_t(kStockProbeImageSize) || height != uint32_t(kStockProbeImageSize) || mips != uint32_t(kStockProbeImageMips))
        return fail(std::to_string(width) + "x" + std::to_string(height) + ", " + std::to_string(mips) + " mips is not 256x256, 6 mips");
    if (dim != 3 || (misc & 0x4) || arraySize != 1 || depth > 1 || (caps2 & 0x200000) || (caps2 & 0x200))
        return fail("not a single 2D image");
    if (bytes != 148 + per) return fail(std::to_string(bytes - 148) + " data bytes, not " + std::to_string(per));
    bc6h.assign(data + 148, data + 148 + per);
    return true;
}

bool readStockProbeDds(const std::string& ddsPath, std::vector<uint8_t>& bc6h, std::string* err) {
    std::error_code ec;
    const uint64_t size = fs::file_size(fs::path(ddsPath), ec);
    std::ifstream f(fs::path(ddsPath), std::ios::binary);
    if (ec || !f) {
        if (err) *err = "cannot read " + ddsPath;
        return false;
    }
    if (size > (uint64_t(1) << 20)) {
        if (err) *err = "probe DDS: " + fs::path(ddsPath).filename().string() + " is too large";
        return false;
    }
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!readAt(f, 0, data.data(), size)) {
        if (err) *err = "read error in " + ddsPath;
        return false;
    }
    return parseStockProbeDds(data.data(), data.size(), bc6h, err);
}

bool mergeStockProbeImages(StockProbeSet& set, const StockProbeImages& images, std::string* err) {
    const auto fail = [&](const std::string& e) {
        if (err) *err = e;
        return false;
    };
    if (images.probes.size() != images.bc6h.size()) return fail("probe images: index and image counts differ");
    if (images.probes.empty()) return true;
    if (images.size <= 0 || images.mipCount <= 0) return fail("probe images have no image size");
    if (set.size > 0 && (set.size != images.size || set.mipCount != images.mipCount))
        return fail("probe images are " + std::to_string(images.size) + " / " + std::to_string(images.mipCount) + " levels, the set " +
                    std::to_string(set.size) + " / " + std::to_string(set.mipCount));
    const size_t per = bc6hBytesPerProbe(images.size, images.mipCount);
    for (size_t k = 0; k < images.probes.size(); ++k) {
        const int i = images.probes[k];
        if (i < 0 || size_t(i) >= set.probes.size()) return fail("probe image for probe " + std::to_string(i) + ", out of range");
        if (images.bc6h[k].size() != per) return fail("probe image " + std::to_string(i) + " has the wrong size");
    }
    set.size = images.size;
    set.mipCount = images.mipCount;
    for (size_t k = 0; k < images.probes.size(); ++k) {
        StockProbe& p = set.probes[size_t(images.probes[k])];
        p.bc6h = images.bc6h[k];
        p.mips.clear();
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Camera-local subset

namespace {
// The subset's boxes and probes (research 108 section 4.4); `imageOf(probe)` says whether a probe has an image, `fetch`
// fills a chosen probe's bc6h (false: the subset fails).
template <class HasImage, class Fetch>
StockProbeSet chooseProbeSubset(const StockProbeSet& full, int size, int mipCount, const ProbeVec3& camera, int maxBoxes,
                                HasImage imageOf, Fetch fetch) {
    StockProbeSet sub;
    sub.size = size;
    sub.mipCount = mipCount;
    sub.arrayImageName = full.arrayImageName;
    sub.streamed = full.streamed;
    if (size <= 0 || mipCount <= 0) return sub;
    const auto hasImage = [&](const StockProbeBox& b) {
        return b.imageIndex >= 0 && size_t(b.imageIndex) < full.probes.size() && imageOf(b.imageIndex);
    };
    maxBoxes = std::clamp(maxBoxes, 0, kMaxSubsetBoxes);
    int fallback = -1;
    std::vector<std::pair<float, int>> near;  // (squared distance to the box's world AABB, box index)
    for (size_t i = 0; i < full.boxes.size(); ++i) {
        const StockProbeBox& b = full.boxes[i];
        if (stockProbeBoxIsFallback(b)) {
            if (fallback < 0 && hasImage(b)) fallback = int(i);
            continue;  // one fallback, always last
        }
        if (!hasImage(b)) continue;
        ProbeVec3 lo, hi;
        boxAabb(b, lo, hi);
        float d2 = 0;
        for (int k = 0; k < 3; ++k) {
            const float d = std::max({lo[k] - camera[k], 0.0f, camera[k] - hi[k]});
            d2 += d * d;
        }
        near.push_back({d2, int(i)});
    }
    const size_t keep = std::min(near.size(), size_t(maxBoxes));
    std::partial_sort(near.begin(), near.begin() + std::ptrdiff_t(keep), near.end());
    std::vector<int> order;
    for (size_t k = 0; k < keep; ++k) order.push_back(near[k].second);
    // The game's order (R_ReflectionProbe_CompareProbeInstancePriority): lodLevel ascending, priority descending.
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const StockProbeBox &x = full.boxes[size_t(a)], &y = full.boxes[size_t(b)];
        if (x.lodLevel != y.lodLevel) return x.lodLevel < y.lodLevel;
        if (x.priority != y.priority) return x.priority > y.priority;
        return a < b;
    });
    if (fallback >= 0) order.push_back(fallback);
    std::vector<int> slice(full.probes.size(), -1);
    for (int bi : order) {
        StockProbeBox b = full.boxes[size_t(bi)];
        int& s = slice[size_t(b.imageIndex)];
        if (s < 0) {
            s = int(sub.probes.size());
            StockProbe p;
            const StockProbe& src = full.probes[size_t(b.imageIndex)];
            p.origin = src.origin, p.angles = src.angles, p.mins = src.mins, p.maxs = src.maxs;
            p.relightingIndex = src.relightingIndex, p.imageIndirection = src.imageIndirection, p.sh = src.sh;
            if (!fetch(b.imageIndex, p.bc6h)) return StockProbeSet{};
            sub.probes.push_back(std::move(p));
        }
        b.imageIndex = s;
        sub.probes[size_t(s)].boxes.push_back(int(sub.boxes.size()));
        sub.boxes.push_back(b);
    }
    return sub;
}
}  // namespace

StockProbeSet buildStockProbeSubset(const StockProbeSet& full, const ProbeVec3& camera, int maxBoxes) {
    if (full.size <= 0 || full.mipCount <= 0) {
        StockProbeSet sub;
        sub.size = full.size, sub.mipCount = full.mipCount, sub.arrayImageName = full.arrayImageName, sub.streamed = full.streamed;
        return sub;
    }
    const size_t per = bc6hBytesPerProbe(full.size, full.mipCount);
    return chooseProbeSubset(
        full, full.size, full.mipCount, camera, maxBoxes, [&](int i) { return full.probes[size_t(i)].bc6h.size() == per; },
        [&](int i, std::vector<uint8_t>& out) {
            out = full.probes[size_t(i)].bc6h;
            return true;
        });
}

StockProbeSet buildStockProbeSubset(const StockProbeSet& records, const StockProbeImageIndex& idx, const ProbeVec3& camera,
                                    int maxBoxes, std::string* err) {
    if (idx.fileOf.size() != records.probes.size() || idx.size <= 0) {
        if (err) *err = "the image index does not match the probe records";
        return StockProbeSet{};
    }
    const size_t per = bc6hBytesPerProbe(idx.size, idx.mipCount);
    std::vector<std::unique_ptr<std::ifstream>> open(idx.files.size());
    return chooseProbeSubset(
        records, idx.size, idx.mipCount, camera, maxBoxes, [&](int i) { return idx.fileOf[size_t(i)] >= 0; },
        [&](int i, std::vector<uint8_t>& out) {
            const size_t f = size_t(idx.fileOf[size_t(i)]);
            if (!open[f]) open[f] = std::make_unique<std::ifstream>(fs::path(idx.files[f]), std::ios::binary);
            std::ifstream& in = *open[f];
            out.resize(per);
            in.clear();
            in.seekg(std::streamoff(idx.offsetOf[size_t(i)]), std::ios::beg);
            if (!in || !in.read(reinterpret_cast<char*>(out.data()), std::streamsize(per))) {
                if (err) *err = "cannot read a probe image from " + idx.files[f];
                return false;
            }
            return true;
        });
}

// ---------------------------------------------------------------------------------------------------------------
// Cache file

namespace {

// The records part shared by v1 and v2 main files: P probe records, the box index lists, B box records.
void writeRecords(Writer& w, const StockProbeSet& set) {
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
}

// Writes `head` and then each image beside the target, then renames it over the target: a reader never sees half a
// file, and the images are not copied into one buffer first (a Verdansk main file is about 164 MB).
bool commitCacheFile(const std::string& path, const std::vector<uint8_t>& head, const std::vector<const std::vector<uint8_t>*>& images,
                     std::string* err) {
    const fs::path target = fs::path(path);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        o.write(reinterpret_cast<const char*>(head.data()), std::streamsize(head.size()));
        for (const std::vector<uint8_t>* img : images) o.write(reinterpret_cast<const char*>(img->data()), std::streamsize(img->size()));
        o.flush();
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

void writeHeader(Writer& w, uint32_t version, uint32_t probes, uint32_t size, uint32_t mips, uint32_t boxes) {
    w.b.insert(w.b.end(), kMagic, kMagic + 8);
    w.u32(version);
    w.u32(probes);
    w.u32(size);
    w.u32(mips);
    w.u32(boxes);
    w.u32(kProbeFormatBC6H);
}

// Reads a v1 file or a v2 main / tile file. `set` for a main file, `tile` for a tile file (the other kind is
// refused). Images are read straight into their vectors when `withImages`.
bool readCacheFile(const std::string& path, StockProbeSet* set, StockProbeImages* tile, bool withImages, uint32_t* versionOut,
                   std::string* err, std::vector<int>* ownersOut = nullptr, uint64_t* imagesAtOut = nullptr) {
    const auto fail = [&](const std::string& e) {
        if (err) *err = path + e;
        return false;
    };
    std::error_code ec;
    const uint64_t fileBytes = fs::file_size(fs::path(path), ec);
    std::ifstream f(fs::path(path), std::ios::binary);
    if (ec || !f) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    uint8_t h[kHeaderBytesV2] = {};
    f.read(reinterpret_cast<char*>(h), std::streamsize(std::min<uint64_t>(fileBytes, kHeaderBytesV2)));
    if (fileBytes < kHeaderBytes || std::memcmp(h, kMagic, 8) != 0) return fail(" is not a probe cache file");
    const uint32_t version = rd32(h + 8);
    if (version != kCacheVersion && version != kCacheVersion2)
        return fail(": probe cache version " + std::to_string(version) + " is not supported");
    if (version == kCacheVersion2 && fileBytes < kHeaderBytesV2) return fail(": bad probe cache header");
    const uint32_t count = rd32(h + 12), size = rd32(h + 16), mips = rd32(h + 20), boxes = rd32(h + 24), format = rd32(h + 28);
    const uint32_t images = version == kCacheVersion ? count : rd32(h + 32);
    const uint32_t zone = version == kCacheVersion ? kStockProbeMainFile : rd32(h + 36);
    const bool isTile = zone != kStockProbeMainFile;
    if (size < 4 || size > 4096 || mips < 1 || mips > 13 || boxes > kMaxBoxes || count > kMaxProbes || format != kProbeFormatBC6H ||
        images > kMaxProbes || (isTile ? count != 0 || boxes != 0 : count == 0 || images > count))
        return fail(": bad probe cache header");
    if (isTile && !tile) return fail(" is a tile probe file (" + std::to_string(zone) + ")");
    if (!isTile && !set) return fail(" is not a tile probe file");
    const uint64_t headerBytes = version == kCacheVersion ? kHeaderBytes : kHeaderBytesV2;
    const size_t per = bc6hBytesPerProbe(int(size), int(mips));
    const uint64_t indexBytes = version == kCacheVersion ? 0 : uint64_t(images) * 4;
    if (fileBytes < headerBytes + uint64_t(count) * kProbeRecordBytes + uint64_t(boxes) * kBoxRecordBytes + indexBytes +
                        uint64_t(images) * per)
        return fail(": probe cache is shorter than its header says");

    uint64_t pos = headerBytes;
    f.clear();
    f.seekg(std::streamoff(pos), std::ios::beg);
    std::vector<uint8_t> buf;
    const auto take = [&](uint64_t n) {
        if (n > fileBytes - pos) return false;
        buf.resize(size_t(n));
        if (n && !f.read(reinterpret_cast<char*>(buf.data()), std::streamsize(n))) return false;
        pos += n;
        return true;
    };

    StockProbeSet s;
    s.size = int(size);
    s.mipCount = int(mips);
    s.streamed = version == kCacheVersion2;
    bool ok = take(uint64_t(count) * kProbeRecordBytes);
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    uint32_t totalIdx = 0;
    if (ok) {
        Reader r{buf.data(), buf.data() + buf.size()};
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
            if (stockProbeIsShared(p.imageIndirection)) ++s.sharedCount;
            s.probes.push_back(std::move(p));
        }
        ok = r.ok;
    }
    ok = ok && take(uint64_t(totalIdx) * 4);
    if (ok) {
        Reader r{buf.data(), buf.data() + buf.size()};
        for (uint32_t i = 0; i < count && r.ok; ++i)
            for (uint32_t k = 0; k < ranges[i].second; ++k) {
                const uint32_t b = r.u32();
                if (b >= boxes) r.ok = false;
                s.probes[i].boxes.push_back(int(b));
            }
        ok = r.ok;
    }
    ok = ok && take(uint64_t(boxes) * kBoxRecordBytes);
    if (ok) {
        Reader r{buf.data(), buf.data() + buf.size()};
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
            s.boxes.push_back(b);
        }
        ok = r.ok;
    }
    // Which probes the images belong to: v1 every probe in order, v2 an ascending index list.
    std::vector<int> owners;
    if (version == kCacheVersion) {
        for (uint32_t i = 0; i < count; ++i) owners.push_back(int(i));
    } else if (ok && (ok = take(uint64_t(images) * 4))) {
        Reader r{buf.data(), buf.data() + buf.size()};
        for (uint32_t k = 0; k < images && r.ok; ++k) {
            const uint32_t i = r.u32();
            if ((isTile ? i >= kMaxProbes : i >= count) || (!owners.empty() && i <= uint32_t(owners.back()))) r.ok = false;
            owners.push_back(int(i));
        }
        ok = r.ok;
    }
    std::vector<int> tileBoxes;
    if (ok && isTile && (ok = take(4))) {
        const uint32_t n = rd32(buf.data());
        ok = n <= kMaxBoxes && take(uint64_t(n) * 4);
        for (uint32_t k = 0; ok && k < n; ++k) {
            const uint32_t b = rd32(buf.data() + 4 * k);
            if (b >= kMaxBoxes) ok = false;
            tileBoxes.push_back(int(b));
        }
    }
    if (!ok || fileBytes - pos != uint64_t(images) * per) return fail(": probe cache size does not match its header");
    if (ownersOut) *ownersOut = owners;
    if (imagesAtOut) *imagesAtOut = pos;

    StockProbeImages t;
    if (isTile) {
        t.zone = zone;
        t.size = int(size);
        t.mipCount = int(mips);
        t.probes = owners;
        t.boxes = std::move(tileBoxes);
    }
    if (withImages)
        for (size_t k = 0; k < owners.size(); ++k) {
            std::vector<uint8_t>& dst = isTile ? t.bc6h.emplace_back() : s.probes[size_t(owners[k])].bc6h;
            dst.resize(per);
            if (!f.read(reinterpret_cast<char*>(dst.data()), std::streamsize(per))) return fail(": read error");
        }
    if (versionOut) *versionOut = version;
    if (isTile) *tile = std::move(t);
    else *set = std::move(s);
    return true;
}

}  // namespace

bool writeStockProbeCache(const std::string& path, const StockProbeSet& set, std::string* err) {
    if (set.probes.empty() || set.probes.size() > kMaxProbes || set.boxes.size() > kMaxBoxes) {
        if (err) *err = "probe set is empty or has no image size";
        return false;
    }
    for (const StockProbe& p : set.probes)
        if (p.boxes.size() > kMaxBoxesPerProbe) {
            if (err) *err = "probe image or box list does not match the set";
            return false;
        }
    Writer w;
    std::vector<const std::vector<uint8_t>*> images;
    if (!set.streamed) {
        // v1: the array-image layout, every probe with its image.
        const size_t per = bc6hBytesPerProbe(set.size, set.mipCount);
        if (set.size <= 0 || set.mipCount <= 0) {
            if (err) *err = "probe set is empty or has no image size";
            return false;
        }
        for (const StockProbe& p : set.probes)
            if (p.bc6h.size() != per) {
                if (err) *err = "probe image or box list does not match the set";
                return false;
            }
        writeHeader(w, kCacheVersion, uint32_t(set.probes.size()), uint32_t(set.size), uint32_t(set.mipCount), uint32_t(set.boxes.size()));
        writeRecords(w, set);
        for (const StockProbe& p : set.probes) images.push_back(&p.bc6h);
        return commitCacheFile(path, w.b, images, err);
    }
    // v2 main file: every record, the images present.
    const int size = set.size > 0 ? set.size : kStockProbeImageSize, mips = set.size > 0 ? set.mipCount : kStockProbeImageMips;
    if (mips <= 0 || mips > 13) {
        if (err) *err = "probe set has no image size";
        return false;
    }
    const size_t per = bc6hBytesPerProbe(size, mips);
    std::vector<uint32_t> owners;
    for (size_t i = 0; i < set.probes.size(); ++i) {
        const std::vector<uint8_t>& b = set.probes[i].bc6h;
        if (b.empty()) continue;
        if (b.size() != per || set.size <= 0) {
            if (err) *err = "probe " + std::to_string(i) + "'s image does not match the set's image size";
            return false;
        }
        owners.push_back(uint32_t(i));
        images.push_back(&b);
    }
    writeHeader(w, kCacheVersion2, uint32_t(set.probes.size()), uint32_t(size), uint32_t(mips), uint32_t(set.boxes.size()));
    w.u32(uint32_t(owners.size()));
    w.u32(kStockProbeMainFile);
    writeRecords(w, set);
    for (uint32_t i : owners) w.u32(i);
    return commitCacheFile(path, w.b, images, err);
}

bool writeStockProbeTileCache(const std::string& path, const StockProbeImages& images, std::string* err) {
    const auto fail = [&](const std::string& e) {
        if (err) *err = e;
        return false;
    };
    if (images.zone == kStockProbeMainFile) return fail("a tile probe file needs the tile's zone index");
    if (images.probes.size() != images.bc6h.size() || images.probes.size() > kMaxProbes || images.boxes.size() > kMaxBoxes)
        return fail("tile probe images: index and image counts differ");
    const int size = images.size > 0 ? images.size : kStockProbeImageSize;
    const int mips = images.size > 0 ? images.mipCount : kStockProbeImageMips;
    if (mips <= 0 || mips > 13 || (!images.probes.empty() && images.size <= 0)) return fail("tile probe images have no image size");
    const size_t per = bc6hBytesPerProbe(size, mips);
    for (size_t k = 0; k < images.probes.size(); ++k) {
        const int i = images.probes[k];
        if (i < 0 || uint32_t(i) >= kMaxProbes || (k && i <= images.probes[k - 1]) || images.bc6h[k].size() != per)
            return fail("tile probe image " + std::to_string(k) + " (probe " + std::to_string(i) + ") is out of order or the wrong size");
    }
    for (int b : images.boxes)
        if (b < 0 || uint32_t(b) >= kMaxBoxes) return fail("tile probe box index out of range");
    Writer w;
    writeHeader(w, kCacheVersion2, 0, uint32_t(size), uint32_t(mips), 0);
    w.u32(uint32_t(images.probes.size()));
    w.u32(images.zone);
    for (int i : images.probes) w.u32(uint32_t(i));
    w.u32(uint32_t(images.boxes.size()));
    for (int b : images.boxes) w.u32(uint32_t(b));
    std::vector<const std::vector<uint8_t>*> imgs;
    for (const auto& b : images.bc6h) imgs.push_back(&b);
    return commitCacheFile(path, w.b, imgs, err);
}

bool readStockProbeCache(const std::string& path, StockProbeSet& out, std::string* err, bool decode) {
    StockProbeSet set;
    uint32_t version = 0;
    if (!readCacheFile(path, &set, nullptr, true, &version, err)) return false;
    if (decode && version == kCacheVersion && !decodeStockProbeImages(set, err)) return false;
    out = std::move(set);
    return true;
}

bool readStockProbeCacheRecords(const std::string& path, StockProbeSet& out, std::string* err) {
    return readCacheFile(path, &out, nullptr, false, nullptr, err);
}

bool readStockProbeTileCache(const std::string& path, StockProbeImages& out, std::string* err) {
    return readCacheFile(path, nullptr, &out, true, nullptr, err);
}

bool indexStockProbeCacheImages(const std::string& path, size_t probeCount, StockProbeImageIndex& idx, std::string* err) {
    StockProbeSet s;
    StockProbeImages t;
    std::vector<int> owners;
    uint64_t at = 0;
    // A main file needs a set to read into, a tile file an image record: try the main form first.
    std::string e1;
    bool ok = readCacheFile(path, &s, nullptr, false, nullptr, &e1, &owners, &at);
    int size = s.size, mips = s.mipCount;
    if (!ok) {
        ok = readCacheFile(path, nullptr, &t, false, nullptr, err, &owners, &at);
        size = t.size, mips = t.mipCount;
        if (!ok) return false;
    }
    if (idx.fileOf.empty()) idx.fileOf.assign(probeCount, -1), idx.offsetOf.assign(probeCount, 0);
    if (idx.size == 0) idx.size = size, idx.mipCount = mips;
    if (size != idx.size || mips != idx.mipCount) {
        if (err) *err = path + ": its probe images are not the index's size";
        return false;
    }
    const size_t per = bc6hBytesPerProbe(size, mips);
    const int fileNo = int(idx.files.size());
    idx.files.push_back(path);
    for (size_t k = 0; k < owners.size(); ++k) {
        const int p = owners[k];
        if (p < 0 || size_t(p) >= idx.fileOf.size()) continue;
        idx.fileOf[size_t(p)] = fileNo;
        idx.offsetOf[size_t(p)] = at + uint64_t(k) * per;
    }
    return true;
}

}  // namespace iw8r
