#include "stock_lightgrid.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <system_error>

#include "json_lite.h"

namespace iw8r {
namespace {

using json::Value;

constexpr size_t kBoxBytes = 64;           // GfxStaticModelLightGridVolume
constexpr size_t kRootBytes = 240;         // GfxLightGridVolumeAtlasDataTemp
constexpr size_t kPackBytes = 32;          // LightGridVolumePackingParams
constexpr size_t kTransformBytes = 64;     // LightGridVolumeTransformParams
constexpr uint32_t kFormatR11G11B10F = 32;  // GfxImageRaw format of texture 0
constexpr uint32_t kFormatR8G8B8A8 = 6;     // texture 1
constexpr size_t kChunk = size_t(4) << 20;  // streaming window
constexpr size_t kTableNeedle = 256;        // the first bytes of the box table are searched, then the rest compared
constexpr uint32_t kMaxWidth = 256;
constexpr uint32_t kMaxDepthSignature = 64;
constexpr uint32_t kMaxDepthAfterTable = 1024;

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
int64_t rd64s(const uint8_t* p) {
    uint64_t u = 0;
    for (int i = 7; i >= 0; --i) u = u << 8 | p[i];
    int64_t v;
    std::memcpy(&v, &u, 8);
    return v;
}
float rdf(const uint8_t* p) {
    const uint32_t u = rd32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void putF(std::vector<uint8_t>& b, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(b, u);
}

bool fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

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

// ---- targeted JSON value extraction (the trzone export is 70+ MB; only one member is needed) ----

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Offset of the value of the first member `"key"` (followed by optional whitespace and ':'), or npos.
size_t findMemberValue(const std::string& text, const std::string& key) {
    const std::string quoted = "\"" + key + "\"";
    size_t at = 0;
    while ((at = text.find(quoted, at)) != std::string::npos) {
        size_t p = at + quoted.size();
        while (p < text.size() && isWs(text[p])) ++p;
        if (p < text.size() && text[p] == ':') {
            ++p;
            while (p < text.size() && isWs(text[p])) ++p;
            return p < text.size() ? p : std::string::npos;
        }
        at += quoted.size();
    }
    return std::string::npos;
}

// One past the end of the JSON value that starts at `start`, or npos when it is not closed.
size_t valueEnd(const std::string& text, size_t start) {
    const size_t n = text.size();
    if (start >= n) return std::string::npos;
    const auto skipString = [&](size_t p) -> size_t {  // p at the opening quote; returns one past the closing quote
        for (++p; p < n; ++p) {
            if (text[p] == '\\') ++p;
            else if (text[p] == '"') return p + 1;
        }
        return std::string::npos;
    };
    const char c = text[start];
    if (c == '"') return skipString(start);
    if (c == '{' || c == '[') {
        int depth = 0;
        for (size_t p = start; p < n;) {
            const char d = text[p];
            if (d == '"') {
                p = skipString(p);
                if (p == std::string::npos) return p;
                continue;
            }
            if (d == '{' || d == '[') ++depth;
            else if (d == '}' || d == ']') {
                if (--depth == 0) return p + 1;
            }
            ++p;
        }
        return std::string::npos;
    }
    size_t p = start;
    while (p < n && text[p] != ',' && text[p] != '}' && text[p] != ']' && !isWs(text[p])) ++p;
    return p;
}

std::optional<Value> parseMember(const std::string& text, const std::string& key, std::string* err) {
    const size_t v = findMemberValue(text, key);
    if (v == std::string::npos) return std::nullopt;
    const size_t e = valueEnd(text, v);
    if (e == std::string::npos) {
        if (err) *err = key + ": value is not closed";
        return std::nullopt;
    }
    std::string jerr;
    auto out = json::parse(text.substr(v, e - v), &jerr);
    if (!out && err) *err = key + ": " + jerr;
    return out;
}

double num(const Value* v, double def = 0) {
    if (!v) return def;
    if (v->number()) return *v->number();
    if (v->type == Value::Type::Object) {
        const Value* x = v->get("value");  // enum fields: {"value": n, "name": "..."}
        if (x && x->number()) return *x->number();
    }
    return def;
}

uint32_t u32Of(double d) {
    if (!std::isfinite(d)) return 0;
    if (d < 0) return uint32_t(int64_t(d));  // -1 written for 0xFFFFFFFF
    return uint32_t(uint64_t(d));
}

// The four floats of one volumeTransform row: {"v": [x, y, z, w], ...} or a plain array.
bool rowOf(const Value* r, float out[4]) {
    if (!r) return false;
    const Value* v = r->type == Value::Type::Array ? r : r->get("v");
    if (!v || v->type != Value::Type::Array || v->arr.size() < 4) return false;
    for (int k = 0; k < 4; ++k) out[k] = float(num(&v->arr[size_t(k)]));
    return true;
}

// ---- atlas root ----

struct RootInfo {
    uint32_t width = 0, depth = 0, numVolumes = 0;
    uint64_t blockBytes = 0;
};

uint64_t blockBytesFor(uint64_t w, uint64_t d, uint64_t n) {
    return kRootBytes + w * w * d * 4 + (2 * w) * (3 * w) * d * 4 + n * (kPackBytes + kTransformBytes);
}

// Checks a 240-byte root. wantVolumes 0 accepts any count >= 1.
bool rootValid(const uint8_t* r, uint32_t wantVolumes, uint32_t maxDepth, bool checkPointers, RootInfo& info) {
    const uint32_t w = rd32(r), d = rd32(r + 4), n = rd32(r + 8);
    if (w < 1 || w > kMaxWidth || d < 1 || d > maxDepth || n < 1) return false;
    if (wantVolumes && n != wantVolumes) return false;
    const uint64_t t0 = uint64_t(w) * w * d * 4, t1 = uint64_t(2 * w) * (3 * w) * d * 4;
    // texture 0 descriptor at 0x10, texture 1 at 0x38: u32 format, u32 flags, u32 bytes, u16 w, h, d
    if (rd32(r + 0x10) != kFormatR11G11B10F || rd32(r + 0x18) != t0 || rd16(r + 0x1C) != w || rd16(r + 0x1E) != w ||
        rd16(r + 0x20) != d)
        return false;
    if (rd32(r + 0x38) != kFormatR8G8B8A8 || rd32(r + 0x40) != t1 || rd16(r + 0x44) != 2 * w || rd16(r + 0x46) != 3 * w ||
        rd16(r + 0x48) != d)
        return false;
    if (checkPointers && (rd64s(r + 0x30) != -2 || rd64s(r + 0x58) != -2 || rd64s(r + 0x60) != -2 || rd64s(r + 0xA8) != -2))
        return false;
    info.width = w;
    info.depth = d;
    info.numVolumes = n;
    info.blockBytes = blockBytesFor(w, d, n);
    return true;
}

// ---- byte sources: a string in memory, or a file read in windows ----

struct Source {
    virtual ~Source() = default;
    virtual uint64_t size() const = 0;
    virtual bool read(uint64_t off, size_t n, uint8_t* out) = 0;
};

struct MemSource : Source {
    const std::string& s;
    explicit MemSource(const std::string& str) : s(str) {}
    uint64_t size() const override { return s.size(); }
    bool read(uint64_t off, size_t n, uint8_t* out) override {
        if (off > s.size() || n > s.size() - off) return false;
        std::memcpy(out, s.data() + off, n);
        return true;
    }
};

struct FileSource : Source {
    std::ifstream f;
    uint64_t total = 0;
    uint64_t size() const override { return total; }
    bool read(uint64_t off, size_t n, uint8_t* out) override {
        if (off > total || n > total - off) return false;
        f.clear();
        f.seekg(std::streamoff(off), std::ios::beg);
        f.read(reinterpret_cast<char*>(out), std::streamsize(n));
        return bool(f);
    }
};

// Calls onHit(absolute offset) for each occurrence of `needle` at or after `from`, in order, until it returns true.
bool findEach(Source& src, uint64_t from, const std::vector<uint8_t>& needle, const std::function<bool(uint64_t)>& onHit,
              uint64_t& hit) {
    const size_t nl = needle.size();
    if (nl == 0 || nl > kChunk) return false;
    std::vector<uint8_t> buf;
    const std::boyer_moore_horspool_searcher searcher(needle.begin(), needle.end());
    for (uint64_t pos = from; pos + nl <= src.size();) {
        const size_t len = size_t(std::min<uint64_t>(kChunk, src.size() - pos));
        buf.resize(len);
        if (!src.read(pos, len, buf.data())) return false;
        for (auto it = buf.begin();;) {
            it = std::search(it, buf.end(), searcher);
            if (it == buf.end()) break;
            const uint64_t abs = pos + uint64_t(it - buf.begin());
            if (onHit(abs)) {
                hit = abs;
                return true;
            }
            ++it;
        }
        if (pos + len >= src.size()) break;
        pos += len - (nl - 1);  // the next window starts where a match could still begin
    }
    return false;
}

// Scans for a root by its signature (research 105 §5.1), at any byte offset at or after `from`.
bool scanForRoot(Source& src, uint64_t from, uint32_t numVolumes, uint64_t& rootOff, RootInfo& info) {
    std::vector<uint8_t> buf;
    for (uint64_t pos = from; pos + kRootBytes <= src.size();) {
        const size_t len = size_t(std::min<uint64_t>(kChunk, src.size() - pos));
        buf.resize(len);
        if (!src.read(pos, len, buf.data())) return false;
        const size_t last = len - kRootBytes;  // candidates r in [0, last] have their whole root in the window
        const uint8_t* b = buf.data();
        for (size_t r = 0; r <= last; ++r) {
            if (rd32(b + r + 8) != numVolumes) continue;
            if (rootValid(b + r, numVolumes, kMaxDepthSignature, true, info)) {
                rootOff = pos + r;
                return true;
            }
        }
        if (pos + len >= src.size()) break;
        pos += last + 1;
    }
    return false;
}

bool extractFrom(Source& src, const std::vector<uint8_t>& rawTable, size_t numVolumes, std::string& block, std::string* err) {
    block.clear();
    if (numVolumes == 0) return fail(err, "no static model volumes");
    if (numVolumes > 0xFFFFFFFFu) return fail(err, "volume count out of range");
    if (!rawTable.empty() && rawTable.size() != numVolumes * kBoxBytes)
        return fail(err, "box table is " + std::to_string(rawTable.size()) + " bytes, expected " +
                             std::to_string(numVolumes * kBoxBytes));
    const uint32_t n = uint32_t(numVolumes);

    uint64_t rootOff = 0;
    RootInfo info;
    bool found = false;
    uint64_t tableEnd = 0;
    if (!rawTable.empty()) {
        const std::vector<uint8_t> needle(rawTable.begin(), rawTable.begin() + std::min(kTableNeedle, rawTable.size()));
        std::vector<uint8_t> whole(rawTable.size());
        uint64_t hit = 0;
        const bool tableFound = findEach(src, 0, needle, [&](uint64_t at) {
            if (!src.read(at, whole.size(), whole.data()) || whole != rawTable) return false;
            tableEnd = at + rawTable.size();
            return true;
        }, hit);
        if (tableFound) {
            uint8_t root[kRootBytes];
            if (src.read(tableEnd, kRootBytes, root) && rootValid(root, n, kMaxDepthAfterTable, true, info)) {
                rootOff = tableEnd;
                found = true;
            } else {
                found = scanForRoot(src, tableEnd, n, rootOff, info);  // something (SMLGppZone) sits in between
            }
        }
    }
    if (!found) found = scanForRoot(src, 0, n, rootOff, info);
    if (!found) return fail(err, "light grid volume atlas root not found (" + std::to_string(numVolumes) + " volumes)");
    if (rootOff + info.blockBytes > src.size()) return fail(err, "light grid volume atlas runs past the end of the zone");
    block.resize(size_t(info.blockBytes));
    if (!src.read(rootOff, block.size(), reinterpret_cast<uint8_t*>(block.data()))) {
        block.clear();
        return fail(err, "cannot read the light grid volume atlas");
    }
    return true;
}

float finiteOr0(float f) { return std::isfinite(f) ? f : 0.0f; }

double clamp01(double v) { return v >= 0 ? (v <= 1 ? v : 1) : 0; }  // NaN -> 0

}  // namespace

void decodeR11G11B10F(uint32_t packed, float outRgb[3]) {
    const auto f = [](uint32_t m, uint32_t e, int mbits) -> float {
        const float frac = float(m) / float(1u << mbits);
        if (e == 0) return frac * std::ldexp(1.0f, -14);
        if (e == 31) return m ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
        return (1.0f + frac) * std::ldexp(1.0f, int(e) - 15);
    };
    outRgb[0] = f(packed & 63, (packed >> 6) & 31, 6);
    outRgb[1] = f((packed >> 11) & 63, (packed >> 17) & 31, 6);
    outRgb[2] = f((packed >> 22) & 31, (packed >> 27) & 31, 5);
}

bool parseStockLgvBoxes(const std::string& trzoneJson, std::vector<StockLgvBox>& out, std::vector<uint8_t>* rawTable,
                        std::string* err) {
    out.clear();
    if (rawTable) rawTable->clear();
    std::string perr;
    const auto vols = parseMember(trzoneJson, "staticModelVolumes", &perr);
    if (!vols) return fail(err, perr.empty() ? "trzone has no gpuLightGrid.staticModelVolumes" : perr);
    const auto countNode = parseMember(trzoneJson, "staticModelVolumeCount", nullptr);
    const bool haveCount = countNode && countNode->number();
    const double count = haveCount ? *countNode->number() : 0;

    std::vector<uint8_t> raw;
    if (vols->isNull()) {
        if (haveCount && count != 0) return fail(err, "staticModelVolumes is null but staticModelVolumeCount is not 0");
        return true;
    }
    if (vols->type != Value::Type::Object) return fail(err, "staticModelVolumes is not an object");
    const Value* stride = vols->get("stride");
    if (stride && num(stride) != double(kBoxBytes))
        return fail(err, "staticModelVolumes stride is " + std::to_string(num(stride)) + ", expected 64");
    const Value* bytes = vols->get("bytes");
    const Value* values = vols->get("values");
    if (bytes && bytes->string()) {
        if (!unhex(*bytes->string(), raw) || raw.size() % kBoxBytes)
            return fail(err, "staticModelVolumes bytes are not hex or not a multiple of 64");
    } else if (values && values->type == Value::Type::Array) {
        raw.reserve(values->arr.size() * kBoxBytes);
        for (size_t i = 0; i < values->arr.size(); ++i) {
            const Value& v = values->arr[i];
            const Value* sz = v.get("m_volumeSize");
            const Value* xf = v.get("volumeTransform");
            float rows[3][4];
            if (!sz || sz->type != Value::Type::Array || sz->arr.size() < 3 || !xf || xf->type != Value::Type::Array ||
                xf->arr.size() < 3 || !rowOf(&xf->arr[0], rows[0]) || !rowOf(&xf->arr[1], rows[1]) || !rowOf(&xf->arr[2], rows[2]))
                return fail(err, "staticModelVolumes value " + std::to_string(i) + " lacks m_volumeSize or volumeTransform");
            put32(raw, u32Of(num(v.get("firstProbe"))));
            for (int k = 0; k < 3; ++k) put16(raw, uint16_t(u32Of(num(&sz->arr[size_t(k)]))));
            put16(raw, uint16_t(u32Of(num(v.get("flags")))));
            put32(raw, u32Of(num(v.get("singleProbe"), -1)));
            for (const auto& r : rows)
                for (float f : r) putF(raw, f);
        }
    } else {
        return fail(err, "staticModelVolumes has neither bytes nor values");
    }

    const size_t n = raw.size() / kBoxBytes;
    const Value* c = vols->get("count");
    if (c && c->number() && *c->number() != double(n))
        return fail(err, "staticModelVolumes count " + std::to_string(*c->number()) + " but " + std::to_string(n) + " records");
    if (haveCount && count != double(n))
        return fail(err, "staticModelVolumeCount " + std::to_string(count) + " but " + std::to_string(n) + " records");

    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = raw.data() + i * kBoxBytes;
        StockLgvBox& b = out[i];
        b.firstProbe = rd32(p);
        for (int k = 0; k < 3; ++k) b.size[k] = rd16(p + 4 + 2 * k);
        b.flags = rd16(p + 10);
        b.singleProbe = rd32(p + 12);
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) b.row[r][k] = double(rdf(p + 16 + 16 * r + 4 * k));
    }
    if (rawTable) *rawTable = std::move(raw);
    return true;
}

bool extractStockLgvBlock(const std::string& decBytes, const std::vector<uint8_t>& rawTable, size_t numVolumes,
                          std::string& block, std::string* err) {
    MemSource src(decBytes);
    return extractFrom(src, rawTable, numVolumes, block, err);
}

bool extractStockLgvBlockFromFile(const std::string& decPath, const std::vector<uint8_t>& rawTable, size_t numVolumes,
                                  std::string& block, std::string* err) {
    block.clear();
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(decPath);
    const uintmax_t sz = fs::file_size(p, ec);
    if (ec) return fail(err, "cannot open " + decPath);
    FileSource src;
    src.total = uint64_t(sz);
    src.f.open(p, std::ios::binary);
    if (!src.f) return fail(err, "cannot open " + decPath);
    return extractFrom(src, rawTable, numVolumes, block, err);
}

bool parseStockLgvBlock(const std::string& block, StockLgvAtlas& out, std::string* err) {
    out = StockLgvAtlas{};
    if (block.size() < kRootBytes) return fail(err, "light grid volume block is shorter than its root");
    const uint8_t* b = reinterpret_cast<const uint8_t*>(block.data());
    RootInfo info;
    if (!rootValid(b, 0, kMaxDepthAfterTable, false, info)) return fail(err, "light grid volume block has a bad root");
    if (info.blockBytes != block.size())
        return fail(err, "light grid volume block is " + std::to_string(block.size()) + " bytes, its root says " +
                             std::to_string(info.blockBytes));
    const size_t w = info.width, d = info.depth, n = info.numVolumes;
    out.width = int(w);
    out.depth = int(d);
    size_t o = kRootBytes;
    out.tex0.resize(w * w * d);
    for (size_t i = 0; i < out.tex0.size(); ++i, o += 4) out.tex0[i] = rd32(b + o);
    out.tex1.resize(6 * w * w * d);
    for (size_t i = 0; i < out.tex1.size(); ++i, o += 4) out.tex1[i] = rd32(b + o);
    out.pack.resize(n);
    for (size_t v = 0; v < n; ++v, o += kPackBytes) {
        for (int k = 0; k < 3; ++k) {
            out.pack[v].uvScale[k] = finiteOr0(rdf(b + o + 4 * k));
            out.pack[v].uvBias[k] = finiteOr0(rdf(b + o + 16 + 4 * k));
        }
    }
    return true;  // the transform params that follow repeat the exported box rows (research 105 §1.3)
}

StockLgvSh sampleStockLgv(const StockLgvAtlas& a, size_t volume, const double coord[3]) {
    StockLgvSh s;
    const size_t w = a.width > 0 ? size_t(a.width) : 0, d = a.depth > 0 ? size_t(a.depth) : 0;
    if (volume >= a.pack.size() || !w || !d || a.tex0.size() != w * w * d || a.tex1.size() != 6 * w * w * d) return s;
    const StockLgvAtlas::Pack& pk = a.pack[volume];
    const size_t dims[3] = {w, w, d};
    size_t i0[3], i1[3];
    double fr[3];
    for (int k = 0; k < 3; ++k) {
        const double uvw = clamp01(coord[k]) * double(pk.uvScale[k]) + double(pk.uvBias[k]);
        const double p = uvw * double(dims[k]) - 0.5;  // texel space: texel centres at integers
        const double fl = std::floor(p);
        fr[k] = p - fl;
        const auto clampIdx = [&](double v) { return size_t(std::clamp(v, 0.0, double(dims[k] - 1))); };
        i0[k] = clampIdx(fl);
        i1[k] = clampIdx(fl + 1);
    }
    double dc[3] = {0, 0, 0}, q[24] = {};
    const size_t w2 = 2 * w, slice1 = w2 * 3 * w;
    for (int dz = 0; dz < 2; ++dz)
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const double wt = (dx ? fr[0] : 1 - fr[0]) * (dy ? fr[1] : 1 - fr[1]) * (dz ? fr[2] : 1 - fr[2]);
                if (wt == 0) continue;
                const size_t x = dx ? i1[0] : i0[0], y = dy ? i1[1] : i0[1], z = dz ? i1[2] : i0[2];
                float rgb[3];
                decodeR11G11B10F(a.tex0[x + w * (y + w * z)], rgb);
                for (int c = 0; c < 3; ++c) dc[c] += wt * (std::isfinite(rgb[c]) ? rgb[c] : 0.0f);
                for (int t = 0; t < 6; ++t) {  // tile t at (W (t & 1), W (t >> 1)) of texture 1
                    const uint32_t px = a.tex1[(x + w * size_t(t & 1)) + w2 * (y + w * size_t(t >> 1)) + slice1 * z];
                    for (int ch = 0; ch < 4; ++ch) q[4 * t + ch] += wt * (double((px >> (8 * ch)) & 0xFF) / 255.0 * 2.0 - 1.0);
                }
            }
    const double sqrt3 = std::sqrt(3.0), sqrt5 = std::sqrt(5.0);
    for (int c = 0; c < 3; ++c) s.dc[c] = float(dc[c]);
    for (int k = 0; k < 24; ++k) {
        const int c = k % 3, j = k / 3;
        const double f = (j == 0 || j == 2 || j == 3) ? sqrt3 : sqrt5;
        s.sh[j][c] = float(q[k] * dc[c] * f);
    }
    return s;
}

void evalStockLgv(const StockLgvSh& s, const double n[3], double outRgb[3]) {
    const double x = n[0], y = n[1], z = n[2];
    for (int c = 0; c < 3; ++c) {
        const auto sh = [&](int j) { return double(s.sh[j][c]); };
        double e = 0.282095 * double(s.dc[c]);
        e += (4.0 / 9.0) * 0.488603 * (sh(0) * z - sh(2) * y - sh(3) * x);
        e += (1.0 / 16.0) * (0.315392 * (3 * z * z - 1) * sh(1) - 1.092548 * x * z * sh(4) - 1.092548 * y * z * sh(5) +
                             0.546274 * (x * x - y * y) * sh(6) + 1.092548 * x * y * sh(7));
        outRgb[c] = e > 0 ? e : 0.0;
    }
}

}  // namespace iw8r
