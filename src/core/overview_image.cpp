// IW8 Radiant Suite - top-down overview image and PNG writer (overview_image.h).
#include "overview_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "patch_tess.h"
#include "spawn_analysis.h"

namespace iw8r {
namespace {

constexpr int kMinWidth = 16;
constexpr int kMaxSide = 8192;
constexpr int kPatchSubdiv = 4;             // the compile's default (CompileOptions::patchSubdiv)
constexpr double kStepDarken = 8.0;         // height step in units that marks an edge
constexpr double kEdgeFactor = 0.8;         // colour multiplier on edge pixels
constexpr double kSpawnRadiusUnits = 40.0;  // spawn triangle radius in units (at least 4 pixels)
constexpr double kNoSurface = -std::numeric_limits<double>::infinity();
constexpr double kPi = 3.14159265358979323846;

using Rgb = std::array<uint8_t, 3>;
constexpr Rgb kDark{38, 42, 48};            // lowest drawn height
constexpr Rgb kLight{236, 228, 206};        // highest drawn height
constexpr Rgb kBackground{16, 18, 22};
constexpr Rgb kAllies{50, 120, 255};
constexpr Rgb kAxis{235, 50, 45};
constexpr Rgb kOther{60, 210, 80};          // spawns that are neither allies nor axis (FFA, info_player_start)
constexpr Rgb kDomColour{255, 210, 40};
constexpr Rgb kHardpointColour{255, 140, 0};
constexpr Rgb kBombzoneColour{220, 80, 220};
constexpr Rgb kBombColour{255, 255, 255};
constexpr Rgb kInk{0, 0, 0};
constexpr Rgb kText{255, 255, 255};

struct Tri {
    Vec3 a, b, c;  // world points
    Vec3 n;        // unit normal with n.z > 0 (the side seen from above)
};

struct P2 {
    double x = 0, y = 0;
};

// Pixel frame: +X to the right, +Y up. One scale on both axes.
struct Frame {
    double minX = 0, maxY = 0, scale = 1;
    int w = 1, h = 1;
    P2 at(double x, double y) const { return {(x - minX) * scale, (maxY - y) * scale}; }
};

struct Glyph {
    char ch;
    std::array<uint8_t, 7> rows;  // 5-bit rows, bit 4 = left column
};

// 5x7 bitmap font for the overview's labels and markers.
constexpr Glyph kFont[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
};

uint8_t toByte(double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 255.0))); }

std::string lowerCase(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

// ---------------------------------------------------------------------------------------------------------------
// PNG and zlib

uint32_t crcUpdate(uint32_t crc, const uint8_t* p, size_t n) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void putBe32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void appendChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    putBe32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    putBe32(out, crcUpdate(0, out.data() + start, out.size() - start));  // CRC over type and data
}

// zlib stream made of stored (uncompressed) deflate blocks of at most 65535 bytes, then the Adler-32 of the data.
std::vector<uint8_t> zlibStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    out.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    out.push_back(0x78);  // CMF: deflate, 32K window
    out.push_back(0x01);  // FLG: check bits make (CMF * 256 + FLG) a multiple of 31, no dictionary, fastest level
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n == raw.size();
        const uint16_t len = static_cast<uint16_t>(n);
        const uint16_t nlen = static_cast<uint16_t>(~len);
        out.push_back(last ? 1 : 0);  // BFINAL, BTYPE = 00 (stored)
        out.push_back(static_cast<uint8_t>(len));
        out.push_back(static_cast<uint8_t>(len >> 8));
        out.push_back(static_cast<uint8_t>(nlen));
        out.push_back(static_cast<uint8_t>(nlen >> 8));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());

    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    putBe32(out, (b << 16) | a);
    return out;
}

bool writeBytes(const std::string& path, const std::vector<uint8_t>& bytes, std::string* err) {
    const auto fail = [&](std::string m) {
        if (err) *err = std::move(m);
        return false;
    };
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return fail("cannot open " + path + " for writing");
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) return fail("failed to write " + path);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Surfaces (the same drawn set as minimap.cpp: worldspawn, func_group and script_brushmodel)

bool isDrawnEntity(const Entity& e) {
    const std::string cls = lowerCase(e.classname());
    return cls == "worldspawn" || cls == "func_group" || cls == "script_brushmodel";
}

void addBrushTriangles(const Primitive& p, std::vector<Tri>& out) {
    const std::vector<Winding> windings = faceWindingsOf(p);
    for (size_t i = 0; i < windings.size() && i < p.faces.size(); ++i) {
        const Winding& w = windings[i];
        if (w.size() < 3 || isIw8ToolMaterial(p.faces[i].material())) continue;
        const std::optional<Plane> plane = facePlane(p, i);
        if (!plane || plane->n.z <= 0.0) continue;  // walls are edge-on and undersides are hidden
        const Vec3 n = normalize(plane->n);
        for (size_t k = 1; k + 1 < w.size(); ++k) out.push_back({w[0], w[k], w[k + 1], n});
    }
}

void addPatchTriangles(const Primitive& p, std::vector<Tri>& out) {
    if (p.patchMaterial.empty() || isIw8ToolMaterial(p.patchMaterial)) return;
    const PatchMesh mesh = tessellatePrimitive(p, kPatchSubdiv);
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const Vec3 a = mesh.points[mesh.indices[t]];
        const Vec3 b = mesh.points[mesh.indices[t + 1]];
        const Vec3 c = mesh.points[mesh.indices[t + 2]];
        Vec3 n = cross(b - a, c - a);
        if (length(n) <= 1e-12) continue;
        n = normalize(n);
        if (n.z < 0.0) n = -n;  // patches are double sided: show the side that faces up
        if (n.z <= 1e-9) continue;
        out.push_back({a, b, c, n});
    }
}

std::vector<Tri> drawnTriangles(const MapFile& map) {
    std::vector<Tri> tris;
    for (const Entity& e : map.entities) {
        if (!isDrawnEntity(e)) continue;
        for (const EntityItem& item : e.items) {
            const Primitive* p = item.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Brush)
                addBrushTriangles(*p, tris);
            else if (p->kind == PrimKind::Patch)
                addPatchTriangles(*p, tris);
        }
    }
    return tris;
}

Rgb surfaceColour(const Vec3& n, double z, double zLo, double zHi, bool byHeight) {
    double t = 1.0;
    if (byHeight) t = zHi > zLo ? std::clamp((z - zLo) / (zHi - zLo), 0.0, 1.0) : 0.5;
    const double slope = 0.6 + 0.4 * std::max(0.0, dot(n, normalize(Vec3{0.35, 0.25, 0.9})));
    Rgb out{};
    for (size_t c = 0; c < 3; ++c) out[c] = toByte((kDark[c] + (kLight[c] - kDark[c]) * t) * slope);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing primitives on the RGB buffer

double edgeFn(const P2& a, const P2& b, const P2& p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); }

void fillTriangle(std::vector<Rgb>& img, int w, int h, const P2& a, const P2& b, const P2& c, const Rgb& col) {
    const double area = edgeFn(a, b, c);
    if (area == 0.0) return;
    const int x0 = std::clamp(static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))), 0, w - 1);
    const int x1 = std::clamp(static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))), 0, w - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))), 0, h - 1);
    const int y1 = std::clamp(static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))), 0, h - 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const P2 p{x + 0.5, y + 0.5};
            const double e1 = edgeFn(a, b, p), e2 = edgeFn(b, c, p), e3 = edgeFn(c, a, p);
            const bool inside = area > 0 ? (e1 >= 0 && e2 >= 0 && e3 >= 0) : (e1 <= 0 && e2 <= 0 && e3 <= 0);
            if (inside) img[size_t(y) * size_t(w) + size_t(x)] = col;
        }
}

void fillDisc(std::vector<Rgb>& img, int w, int h, const P2& c, double r, const Rgb& col) {
    const int x0 = std::clamp(static_cast<int>(std::floor(c.x - r)), 0, w - 1);
    const int x1 = std::clamp(static_cast<int>(std::ceil(c.x + r)), 0, w - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(c.y - r)), 0, h - 1);
    const int y1 = std::clamp(static_cast<int>(std::ceil(c.y + r)), 0, h - 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double dx = x + 0.5 - c.x, dy = y + 0.5 - c.y;
            if (dx * dx + dy * dy <= r * r) img[size_t(y) * size_t(w) + size_t(x)] = col;
        }
}

const Glyph* findGlyph(char ch) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (const Glyph& g : kFont)
        if (g.ch == upper) return &g;
    return nullptr;
}

// Text centred on (cx, cy) with each font cell scaled by s pixels. Unknown characters take a cell but draw nothing.
void drawText(std::vector<Rgb>& img, int w, int h, const std::string& text, double cx, double cy, int s, const Rgb& col,
              int dx, int dy) {
    if (text.empty()) return;
    const int cell = 6 * s;
    const int textW = static_cast<int>(text.size()) * cell - s;
    const int x0 = static_cast<int>(std::lround(cx - textW / 2.0)) + dx;
    const int y0 = static_cast<int>(std::lround(cy - 3.5 * s)) + dy;
    for (size_t i = 0; i < text.size(); ++i) {
        const Glyph* g = findGlyph(text[i]);
        if (!g) continue;
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 5; ++c) {
                if (!((g->rows[size_t(r)] >> (4 - c)) & 1)) continue;
                for (int py = 0; py < s; ++py)
                    for (int px = 0; px < s; ++px) {
                        const int X = x0 + static_cast<int>(i) * cell + c * s + px;
                        const int Y = y0 + r * s + py;
                        if (X >= 0 && X < w && Y >= 0 && Y < h) img[size_t(Y) * size_t(w) + size_t(X)] = col;
                    }
            }
    }
}

// The letter of a site from its script_label ("_a" -> "A", "1" -> "1"); `fallback` when there is none.
std::string siteGlyph(const std::string& label, char fallback) {
    for (char ch : label) {
        if (ch == '_' || ch == ' ') continue;
        if (std::isalnum(static_cast<unsigned char>(ch))) return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        break;
    }
    return std::string(1, fallback);
}

}  // namespace

std::vector<uint8_t> encodePng(int w, int h, const std::vector<uint8_t>& rgba) {
    if (w < 1 || h < 1 || rgba.size() != size_t(w) * size_t(h) * 4) return {};
    const size_t rowBytes = size_t(w) * 4;
    std::vector<uint8_t> raw;
    raw.reserve((rowBytes + 1) * size_t(h));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);  // filter type None
        raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(size_t(y) * rowBytes),
                   rgba.begin() + static_cast<std::ptrdiff_t>((size_t(y) + 1) * rowBytes));
    }

    std::vector<uint8_t> ihdr;
    putBe32(ihdr, static_cast<uint32_t>(w));
    putBe32(ihdr, static_cast<uint32_t>(h));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // colour type: RGBA
    ihdr.push_back(0);  // compression: deflate
    ihdr.push_back(0);  // filter method 0
    ihdr.push_back(0);  // no interlace

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    appendChunk(out, "IHDR", ihdr);
    appendChunk(out, "IDAT", zlibStored(raw));
    appendChunk(out, "IEND", {});
    return out;
}

bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba, std::string* err) {
    const std::vector<uint8_t> png = encodePng(w, h, rgba);
    if (png.empty()) {
        if (err) *err = "PNG: pixel buffer does not match " + std::to_string(w) + "x" + std::to_string(h);
        return false;
    }
    return writeBytes(path, png, err);
}

bool renderOverview(const MapFile& map, const OverviewOptions& opt, std::vector<uint8_t>& rgba, int& w, int& h,
                    std::string* err) {
    const auto fail = [&](std::string m) {
        if (err) *err = std::move(m);
        return false;
    };

    const std::vector<Tri> tris = drawnTriangles(map);
    const std::vector<SpawnPoint> spawns = collectSpawns(map);
    AABB box;
    double zLo = std::numeric_limits<double>::infinity();
    double zHi = -std::numeric_limits<double>::infinity();
    for (const Tri& t : tris)
        for (const Vec3* v : {&t.a, &t.b, &t.c}) {
            box.add(*v);
            zLo = std::min(zLo, v->z);
            zHi = std::max(zHi, v->z);
        }
    if (!box.valid())
        for (const SpawnPoint& s : spawns) box.add(s.eye);
    if (!box.valid()) return fail("overview: the map has no drawn geometry or spawns");

    // Fit the bounds (at least one unit per axis) with one scale for both axes.
    const double spanX = std::max(box.maxs.x - box.mins.x, 1.0);
    const double spanY = std::max(box.maxs.y - box.mins.y, 1.0);
    Frame f;
    f.minX = box.mins.x;
    f.maxY = box.maxs.y;
    f.scale = opt.unitsPerPixel > 0 ? 1.0 / opt.unitsPerPixel : std::clamp(opt.width, kMinWidth, kMaxSide) / spanX;
    const double longest = std::max(spanX, spanY) * f.scale;
    if (longest > kMaxSide) f.scale *= kMaxSide / longest;  // keep both sides within 8192 pixels
    f.w = std::max(1, static_cast<int>(std::ceil(spanX * f.scale - 1e-6)));
    f.h = std::max(1, static_cast<int>(std::ceil(spanY * f.scale - 1e-6)));
    w = f.w;
    h = f.h;

    const size_t npx = size_t(w) * size_t(h);
    std::vector<double> depth(npx, kNoSurface);
    std::vector<Rgb> img(npx, kBackground);

    // Top faces: nearest surface per pixel (pixel centres, barycentric weights as in minimap.cpp).
    for (const Tri& t : tris) {
        const P2 A = f.at(t.a.x, t.a.y), B = f.at(t.b.x, t.b.y), C = f.at(t.c.x, t.c.y);
        const double area = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        if (std::abs(area) < 1e-12) continue;
        const int x0 = std::clamp(static_cast<int>(std::floor(std::min({A.x, B.x, C.x}))), 0, w - 1);
        const int x1 = std::clamp(static_cast<int>(std::ceil(std::max({A.x, B.x, C.x}))), 0, w - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(std::min({A.y, B.y, C.y}))), 0, h - 1);
        const int y1 = std::clamp(static_cast<int>(std::ceil(std::max({A.y, B.y, C.y}))), 0, h - 1);
        for (int py = y0; py <= y1; ++py)
            for (int px = x0; px <= x1; ++px) {
                const double sx = px + 0.5, sy = py + 0.5;
                const double wA = ((B.x - sx) * (C.y - sy) - (B.y - sy) * (C.x - sx)) / area;
                const double wB = ((C.x - sx) * (A.y - sy) - (C.y - sy) * (A.x - sx)) / area;
                const double wC = 1.0 - wA - wB;
                const double eps = -1e-9;
                if (wA < eps || wB < eps || wC < eps) continue;
                const double z = wA * t.a.z + wB * t.b.z + wC * t.c.z;
                const size_t index = size_t(py) * size_t(w) + size_t(px);
                if (z <= depth[index]) continue;
                depth[index] = z;
                img[index] = surfaceColour(t.n, z, zLo, zHi, opt.shadeByHeight);
            }
    }

    // Edge darkening: a covered pixel with a covered 4-neighbour more than kStepDarken units away in height is an edge.
    {
        const int dxs[4] = {1, -1, 0, 0}, dys[4] = {0, 0, 1, -1};
        std::vector<Rgb> shaded = img;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t idx = size_t(y) * size_t(w) + size_t(x);
                if (depth[idx] == kNoSurface) continue;
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dxs[k], ny = y + dys[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const double nd = depth[size_t(ny) * size_t(w) + size_t(nx)];
                    if (nd == kNoSurface || std::abs(nd - depth[idx]) <= kStepDarken) continue;
                    for (size_t c = 0; c < 3; ++c) shaded[idx][c] = toByte(shaded[idx][c] * kEdgeFactor);
                    break;
                }
            }
        img.swap(shaded);
    }

    // Grid (white at a quarter opacity), only when it is at least 3 pixels apart.
    const double gs = opt.gridStep;
    if (opt.drawGrid && gs > 0 && gs * f.scale >= 3.0) {
        const double x0 = f.minX, x1 = f.minX + w / f.scale;
        const double y0 = f.maxY - h / f.scale, y1 = f.maxY;
        const auto blend = [](Rgb& c) {
            for (size_t i = 0; i < 3; ++i) c[i] = toByte(c[i] + (255.0 - c[i]) * 0.25);
        };
        for (long long k = static_cast<long long>(std::ceil(x0 / gs)); k * gs <= x1; ++k) {
            const int col = static_cast<int>(std::floor((k * gs - f.minX) * f.scale));
            if (col < 0 || col >= w) continue;
            for (int y = 0; y < h; ++y) blend(img[size_t(y) * size_t(w) + size_t(col)]);
        }
        for (long long k = static_cast<long long>(std::ceil(y0 / gs)); k * gs <= y1; ++k) {
            const int row = static_cast<int>(std::floor((f.maxY - k * gs) * f.scale));
            if (row < 0 || row >= h) continue;
            for (int x = 0; x < w; ++x) blend(img[size_t(row) * size_t(w) + size_t(x)]);
        }
    }

    // Spawns: a triangle at the XY point pointing along the yaw (world +X is the right of the image at yaw 0).
    if (opt.drawSpawns) {
        for (const SpawnPoint& s : spawns) {
            const Rgb col = s.team == 1 ? kAllies : (s.team == 2 ? kAxis : kOther);
            const P2 c = f.at(s.eye.x, s.eye.y);
            const double r = std::max(4.0, kSpawnRadiusUnits * f.scale);
            const double rad = s.yaw * kPi / 180.0;
            const P2 d{std::cos(rad), -std::sin(rad)};  // pixel space: Y points down
            const P2 p{-d.y, d.x};
            const P2 tip{c.x + d.x * r, c.y + d.y * r};
            const P2 left{c.x - 0.5 * r * d.x + 0.6 * r * p.x, c.y - 0.5 * r * d.y + 0.6 * r * p.y};
            const P2 right{c.x - 0.5 * r * d.x - 0.6 * r * p.x, c.y - 0.5 * r * d.y - 0.6 * r * p.y};
            fillTriangle(img, w, h, tip, left, right, col);
        }
    }

    // Objectives: discs with a letter. DOM flags ("dom ..."), Hardpoint zones and S&D sites and bomb.
    if (opt.drawObjectives) {
        for (const Entity& e : map.entities) {
            const std::string* go = e.value("script_gameobjectname");
            if (!go) continue;
            const std::string kind = lowerCase(*go);
            std::string label;
            if (const std::string* l = e.value("script_label")) label = *l;
            std::string glyph;
            Rgb col{};
            if (kind == "dom" || startsWith(kind, "dom ")) {
                glyph = siteGlyph(label, 'D');
                col = kDomColour;
            } else if (kind == "hardpoint") {
                glyph = siteGlyph(label, 'H');
                col = kHardpointColour;
            } else if (kind == "bombzone") {
                glyph = siteGlyph(label, 'S');
                col = kBombzoneColour;
            } else if (kind == "sd") {
                glyph = "!";
                col = kBombColour;
            } else {
                continue;
            }
            Vec3 at;
            if (const std::optional<Vec3> o = e.origin()) {
                at = *o;
            } else {
                AABB b;
                for (const EntityItem& item : e.items)
                    if (const Primitive* p = item.prim()) b.add(p->bounds());
                if (!b.valid()) continue;
                at = b.center();
            }
            const P2 c = f.at(at.x, at.y);
            const double r = std::max(6.0, w / 96.0);
            fillDisc(img, w, h, c, r, col);
            drawText(img, w, h, glyph, c.x, c.y, std::max(1, static_cast<int>(r * 1.1 / 7.0)), kInk, 0, 0);
        }
    }

    // Labels: text centred on the XY centre of each box, with a dark shadow.
    const int labelScale = std::max(1, w / 512);
    for (const auto& [text, labelBox] : opt.labels) {
        if (!labelBox.valid() || text.empty()) continue;
        const P2 c = f.at(labelBox.center().x, labelBox.center().y);
        drawText(img, w, h, text, c.x, c.y, labelScale, kInk, labelScale, labelScale);
        drawText(img, w, h, text, c.x, c.y, labelScale, kText, 0, 0);
    }

    rgba.assign(npx * 4, 255);
    for (size_t i = 0; i < npx; ++i) {
        rgba[i * 4 + 0] = img[i][0];
        rgba[i * 4 + 1] = img[i][1];
        rgba[i * 4 + 2] = img[i][2];
    }
    return true;
}

size_t exportOverviewPng(const MapFile& map, const OverviewOptions& opt, const std::string& path, std::string* err) {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!renderOverview(map, opt, rgba, w, h, err)) return 0;
    const std::vector<uint8_t> png = encodePng(w, h, rgba);
    if (png.empty()) {
        if (err) *err = "PNG encoding failed";
        return 0;
    }
    if (!writeBytes(path, png, err)) return 0;
    return png.size();
}

}  // namespace iw8r
