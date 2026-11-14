// IW8 Radiant Suite - top-down minimap render and DDS writer (see minimap.h and docs/research/27_minimap.md).
#include "minimap.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <utility>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "patch_tess.h"

namespace iw8r {
namespace {

// Albedo of every surface before shading (warm grey).
constexpr double kAlbedo[3] = {168.0, 160.0, 140.0};
constexpr int kMinSize = 4;
constexpr int kMaxSize = 8192;
constexpr int kPatchSubdiv = 4;  // the compile's default (CompileOptions::patchSubdiv)

struct Tri {
    Vec3 a, b, c;  // world points
    Vec3 n;        // unit normal with n.z >= 0 (the side seen from above)
};

std::string lowerCase(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

int normalizeSize(int size) {
    const int clamped = std::clamp(size, kMinSize, kMaxSize);
    int power = 1;
    while (power < clamped) power <<= 1;
    return power;
}

Vec3 lightDirection() { return normalize(Vec3{0.35, 0.25, 0.9}); }

// Entities whose surfaces are drawn: worldspawn and func_group (world geometry) and script_brushmodel.
// Triggers and other brush classes are not drawn (compile_iw8.cpp, the entity loop).
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
        // Walls are edge-on from above and undersides are hidden: only the up-facing faces are rasterised.
        if (!plane || plane->n.z <= 0.0) continue;
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
        // Patches are double sided: show whichever side faces up.
        if (n.z < 0.0) n = -n;
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

// The IW3 minimap region as the converter reads it (games/iw8/dump/maps.cpp iw3CompassBounds): exactly two
// script_origin entities with targetname minimap_corner. The Replay-frame corners are their XY box.
std::optional<AABB> cornerRegion(const MapFile& map) {
    std::vector<Vec3> corners;
    for (const Entity& e : map.entities) {
        if (lowerCase(e.classname()) != "script_origin") continue;
        const std::string* target = e.value("targetname");
        if (!target || lowerCase(*target) != "minimap_corner") continue;
        const std::optional<Vec3> origin = e.origin();
        if (!origin) return std::nullopt;
        corners.push_back(*origin);
    }
    if (corners.size() != 2) return std::nullopt;
    AABB box;
    box.add(corners[0]);
    box.add(corners[1]);
    if (!(box.maxs.x > box.mins.x) || !(box.maxs.y > box.mins.y)) return std::nullopt;
    return box;
}

// The converter squares the region about its centre to the longer span (convert.cpp:2226-2245).
AABB squareAbout(const AABB& region) {
    const Vec3 c = region.center();
    const double half = std::max(region.maxs.x - region.mins.x, region.maxs.y - region.mins.y) * 0.5;
    AABB out;
    out.mins = {c.x - half, c.y - half, region.mins.z};
    out.maxs = {c.x + half, c.y + half, region.maxs.z};
    return out;
}

AABB geometryBox(const std::vector<Tri>& tris) {
    AABB box;
    for (const Tri& t : tris) {
        box.add(t.a);
        box.add(t.b);
        box.add(t.c);
    }
    return box;
}

std::array<uint8_t, 3> shade(const Vec3& n, double z, double zLo, double zHi) {
    const double lit = std::max(0.0, dot(n, lightDirection()));
    const double normalTerm = 0.55 + 0.45 * lit;
    const double t = zHi > zLo ? std::clamp((z - zLo) / (zHi - zLo), 0.0, 1.0) : 1.0;
    const double heightTerm = 0.75 + 0.25 * t;
    std::array<uint8_t, 3> out{};
    for (int c = 0; c < 3; ++c)
        out[c] = static_cast<uint8_t>(std::lround(std::clamp(kAlbedo[c] * normalTerm * heightTerm, 0.0, 255.0)));
    return out;
}

void putU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

}  // namespace

int minimapNorthQuarter(double yaw, bool* exact) {
    const double turns = yaw / 90.0;
    const double nearest = std::round(turns);
    if (exact) *exact = std::isfinite(yaw) && std::abs(turns - nearest) < 1e-4;
    if (!std::isfinite(yaw)) return 1;
    return int(((static_cast<long long>(nearest) % 4) + 4) % 4);
}

double mapNorthYaw(const MapFile& map) {
    for (const Entity& e : map.entities) {
        if (!e.isWorldspawn()) continue;
        if (const std::string* v = e.value("northyaw"); v && !v->empty()) {
            char* end = nullptr;
            const double yaw = std::strtod(v->c_str(), &end);
            if (end != v->c_str() && std::isfinite(yaw)) return yaw;
        }
        break;
    }
    return 90.0;
}

int minimapQuarterForBuild(const MapFile& map, std::string* warning) {
    bool exact = true;
    const double yaw = mapNorthYaw(map);
    const int quarter = minimapNorthQuarter(yaw, &exact);
    if (exact) return quarter;
    if (warning) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "northyaw %.6g is not a quarter turn: the minimap is drawn with +Y up, and the compass's north will not "
                                       "point along it (use 0, 90, 180 or 270)", yaw);
        *warning = buf;
    }
    return 1;
}

void minimapPixel(const AABB& rect, int size, int quarter, double x, double y, double& column, double& row) {
    const double span = rect.maxs.x - rect.mins.x;
    const double scale = span > 0 ? size / span : 0.0;
    switch (((quarter % 4) + 4) % 4) {
    case 1: column = (x - rect.mins.x) * scale, row = (rect.maxs.y - y) * scale; break;
    case 2: column = (y - rect.mins.y) * scale, row = (x - rect.mins.x) * scale; break;
    case 3: column = (rect.maxs.x - x) * scale, row = (y - rect.mins.y) * scale; break;
    default: column = (rect.maxs.y - y) * scale, row = (rect.maxs.x - x) * scale; break;
    }
}

MinimapImage renderMinimap(const MapFile& map, int size, int northQuarter) {
    MinimapImage img;
    img.northQuarter = ((northQuarter % 4) + 4) % 4;
    img.width = img.height = normalizeSize(size);
    img.rgba.assign(size_t(img.width) * size_t(img.height) * 4, 0);

    const std::vector<Tri> tris = drawnTriangles(map);
    const std::optional<AABB> corners = cornerRegion(map);
    AABB rect;
    if (corners)
        rect = squareAbout(*corners);
    else if (!tris.empty())
        rect = squareAbout(geometryBox(tris));
    if (tris.empty()) {
        img.worldRect = rect;
        return img;
    }

    double zLo = std::numeric_limits<double>::infinity();
    double zHi = -std::numeric_limits<double>::infinity();
    for (const Tri& t : tris)
        for (const Vec3* v : {&t.a, &t.b, &t.c}) {
            zLo = std::min(zLo, v->z);
            zHi = std::max(zHi, v->z);
        }
    rect.mins.z = zLo;
    rect.maxs.z = zHi;
    img.worldRect = rect;

    const double span = rect.maxs.x - rect.mins.x;
    if (!(span > 0.0)) return img;
    const int n = img.width;

    // North up (minimap.h): quarter 0 gives column = (maxY - y) * scale, row = (maxX - x) * scale.
    const auto toPixel = [&](const Vec3& p) {
        double column = 0, row = 0;
        minimapPixel(rect, n, img.northQuarter, p.x, p.y, column, row);
        return Vec3{column, row, p.z};
    };

    std::vector<double> depth(size_t(n) * size_t(n), -std::numeric_limits<double>::infinity());
    for (const Tri& t : tris) {
        const Vec3 A = toPixel(t.a), B = toPixel(t.b), C = toPixel(t.c);
        const double area = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        if (std::abs(area) < 1e-12) continue;
        const int x0 = std::clamp(static_cast<int>(std::floor(std::min({A.x, B.x, C.x}))), 0, n - 1);
        const int x1 = std::clamp(static_cast<int>(std::ceil(std::max({A.x, B.x, C.x}))), 0, n - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(std::min({A.y, B.y, C.y}))), 0, n - 1);
        const int y1 = std::clamp(static_cast<int>(std::ceil(std::max({A.y, B.y, C.y}))), 0, n - 1);
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                const double sx = px + 0.5, sy = py + 0.5;
                const double wA = ((B.x - sx) * (C.y - sy) - (B.y - sy) * (C.x - sx)) / area;
                const double wB = ((C.x - sx) * (A.y - sy) - (C.y - sy) * (A.x - sx)) / area;
                const double wC = 1.0 - wA - wB;
                const double eps = -1e-9;
                if (wA < eps || wB < eps || wC < eps) continue;
                const double z = wA * t.a.z + wB * t.b.z + wC * t.c.z;
                const size_t index = size_t(py) * size_t(n) + size_t(px);
                if (z <= depth[index]) continue;
                depth[index] = z;
                const std::array<uint8_t, 3> colour = shade(t.n, z, zLo, zHi);
                uint8_t* out = &img.rgba[index * 4];
                out[0] = colour[0];
                out[1] = colour[1];
                out[2] = colour[2];
                out[3] = 255;
            }
        }
    }
    return img;
}

namespace {

// BC3 (DXT5) block encoder. Alpha: 8-value palette from the block's min/max alpha, 3-bit indices.
// Colour: 4-colour palette between the two RGB565 endpoints, which are the opaque texels of lowest and highest
// luminance (a min/max box in luminance). Transparent texels take no part in the endpoint choice.
struct Texel {
    uint8_t r, g, b, a;
};

uint16_t packRgb565(int r, int g, int b) {
    return static_cast<uint16_t>((((r * 31 + 127) / 255) << 11) | (((g * 63 + 127) / 255) << 5) | ((b * 31 + 127) / 255));
}

std::array<int, 3> expandRgb565(uint16_t c) {
    const int r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
    return {(r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)};
}

void encodeBc3Block(const std::array<Texel, 16>& px, uint8_t* out) {
    // Alpha endpoints: a0 > a1 selects the 8-value palette.
    int amin = 255, amax = 0;
    for (const Texel& t : px) {
        amin = std::min(amin, int(t.a));
        amax = std::max(amax, int(t.a));
    }
    int a0 = amax, a1 = amin;
    if (a0 == a1) {
        if (a0 < 255) ++a0;
        else --a1;
    }
    int alphaPal[8];
    alphaPal[0] = a0;
    alphaPal[1] = a1;
    for (int k = 2; k < 8; ++k) alphaPal[k] = ((8 - k) * a0 + (k - 1) * a1 + 3) / 7;
    uint64_t alphaBits = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0, bestErr = 1 << 30;
        for (int k = 0; k < 8; ++k) {
            const int err = std::abs(alphaPal[k] - int(px[i].a));
            if (err < bestErr) {
                bestErr = err;
                best = k;
            }
        }
        alphaBits |= uint64_t(best) << (3 * i);
    }
    out[0] = static_cast<uint8_t>(a0);
    out[1] = static_cast<uint8_t>(a1);
    for (int i = 0; i < 6; ++i) out[2 + i] = static_cast<uint8_t>(alphaBits >> (8 * i));

    // Colour endpoints: opaque texels of lowest and highest luminance.
    bool anyOpaque = false;
    Texel lo{0, 0, 0, 0}, hi{0, 0, 0, 0};
    int loLum = 1 << 30, hiLum = -1;
    for (const Texel& t : px) {
        if (t.a == 0) continue;
        const int lum = t.r * 299 + t.g * 587 + t.b * 114;
        if (!anyOpaque || lum < loLum) {
            loLum = lum;
            lo = t;
        }
        if (!anyOpaque || lum > hiLum) {
            hiLum = lum;
            hi = t;
        }
        anyOpaque = true;
    }
    uint16_t c0 = packRgb565(hi.r, hi.g, hi.b);
    uint16_t c1 = packRgb565(lo.r, lo.g, lo.b);
    if (c0 < c1) std::swap(c0, c1);
    if (c0 == c1) {
        // The 4-colour mode needs c0 > c1; a flat block keeps its colour within one step.
        if (c0 < 0xFFFF) ++c0;
        else --c1;
    }
    const std::array<int, 3> e0 = expandRgb565(c0), e1 = expandRgb565(c1);
    std::array<int, 3> pal[4];
    pal[0] = e0;
    pal[1] = e1;
    for (int c = 0; c < 3; ++c) {
        pal[2][c] = (2 * e0[c] + e1[c]) / 3;
        pal[3][c] = (e0[c] + 2 * e1[c]) / 3;
    }
    uint32_t colourBits = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0, bestErr = 1 << 30;
        for (int k = 0; k < 4; ++k) {
            const int dr = pal[k][0] - px[i].r, dg = pal[k][1] - px[i].g, db = pal[k][2] - px[i].b;
            const int err = dr * dr + dg * dg + db * db;
            if (err < bestErr) {
                bestErr = err;
                best = k;
            }
        }
        colourBits |= uint32_t(best) << (2 * i);
    }
    out[8] = static_cast<uint8_t>(c0);
    out[9] = static_cast<uint8_t>(c0 >> 8);
    out[10] = static_cast<uint8_t>(c1);
    out[11] = static_cast<uint8_t>(c1 >> 8);
    for (int i = 0; i < 4; ++i) out[12 + i] = static_cast<uint8_t>(colourBits >> (8 * i));
}

// Encodes one mip level (RGBA, top row first) into BC3 blocks, row-major. Partial blocks repeat the edge texels.
void encodeBc3Level(const std::vector<uint8_t>& rgba, int w, int h, std::vector<uint8_t>& out) {
    const int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            std::array<Texel, 16> px{};
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) {
                    const int x = std::min(bx * 4 + i, w - 1), y = std::min(by * 4 + j, h - 1);
                    const size_t idx = (size_t(y) * size_t(w) + size_t(x)) * 4;
                    px[size_t(j) * 4 + size_t(i)] = {rgba[idx], rgba[idx + 1], rgba[idx + 2], rgba[idx + 3]};
                }
            const size_t at = out.size();
            out.resize(at + 16);
            encodeBc3Block(px, out.data() + at);
        }
}

}  // namespace

bool writeMinimapDds(const MinimapImage& img, const std::string& ddsPath, std::string* err) {
    std::vector<uint8_t> file;
    if (!encodeMinimapDds(img, file, err)) return false;
    std::ofstream out(ddsPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err) *err = "cannot open " + ddsPath + " for writing";
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out) {
        if (err) *err = "failed to write " + ddsPath;
        return false;
    }
    return true;
}

bool encodeMinimapDds(const MinimapImage& img, std::vector<uint8_t>& file, std::string* err) {
    const auto fail = [&](std::string message) {
        if (err) *err = std::move(message);
        return false;
    };
    const int w = img.width, h = img.height;
    if (w <= 0 || w != h || (w & (w - 1)) != 0) return fail("minimap image must be square with a power-of-two side");
    if (img.rgba.size() != size_t(w) * size_t(h) * 4) return fail("minimap pixel buffer does not match its size");

    int levels = 0;
    for (int s = w; s >= 1; s >>= 1) ++levels;  // floor(log2(w)) + 1

    // Mip chain (RGBA, not premultiplied), each level box-filtered from the one above with alpha weighting, then
    // BC3-encoded. Levels below 4x4 take one padded block.
    std::vector<uint8_t> body;
    std::vector<uint8_t> cur = img.rgba;
    int lw = w, lh = h;
    for (int level = 0; level < levels; ++level) {
        encodeBc3Level(cur, lw, lh, body);
        if (level + 1 == levels) break;
        const int nw = lw / 2, nh = lh / 2;
        std::vector<uint8_t> next(size_t(nw) * size_t(nh) * 4, 0);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                double sumA = 0, sumR = 0, sumG = 0, sumB = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const size_t idx = (size_t(2 * y + dy) * size_t(lw) + size_t(2 * x + dx)) * 4;
                        const double a = cur[idx + 3];
                        sumA += a;
                        sumR += cur[idx] * a;
                        sumG += cur[idx + 1] * a;
                        sumB += cur[idx + 2] * a;
                    }
                uint8_t* out = &next[(size_t(y) * size_t(nw) + size_t(x)) * 4];
                if (sumA > 0) {
                    out[0] = static_cast<uint8_t>(std::lround(sumR / sumA));
                    out[1] = static_cast<uint8_t>(std::lround(sumG / sumA));
                    out[2] = static_cast<uint8_t>(std::lround(sumB / sumA));
                }
                out[3] = static_cast<uint8_t>(std::lround(sumA / 4.0));
            }
        cur.swap(next);
        lw = nw;
        lh = nh;
    }

    // DDS_HEADER (124 bytes) after the "DDS " magic, 128 bytes in total. Legacy FourCC DXT5.
    const uint32_t topBytes = static_cast<uint32_t>(std::max(1, (w + 3) / 4) * std::max(1, (h + 3) / 4) * 16);
    file.assign(128 + body.size(), 0);
    std::memcpy(file.data(), "DDS ", 4);
    putU32(file, 4, 124);
    putU32(file, 8, 0x000A1007);                  // DDSD_CAPS | HEIGHT | WIDTH | PIXELFORMAT | MIPMAPCOUNT | LINEARSIZE
    putU32(file, 12, static_cast<uint32_t>(h));
    putU32(file, 16, static_cast<uint32_t>(w));
    putU32(file, 20, topBytes);                   // linear size of the top level
    putU32(file, 24, 0);                          // depth
    putU32(file, 28, static_cast<uint32_t>(levels));
    putU32(file, 76, 32);                         // DDS_PIXELFORMAT size
    putU32(file, 80, 0x4);                        // DDPF_FOURCC
    putU32(file, 84, 0x35545844);                 // 'DXT5'
    putU32(file, 108, 0x401008);                  // DDSCAPS_TEXTURE | DDSCAPS_MIPMAP | DDSCAPS_COMPLEX
    std::memcpy(file.data() + 128, body.data(), body.size());
    return true;
}

}  // namespace iw8r
