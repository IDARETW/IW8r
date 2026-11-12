// IW8 Radiant Suite - decal tool (see decal_tools.h).
#include "decal_tools.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>

#include "brush_ops.h"
#include "iw8_contents.h"

namespace iw8r {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCos45 = 0.70710678118654752440;  // steeper faces than 45 degrees are not painted
constexpr double kAreaEps = 1e-9;
constexpr double kPointEps = 1e-9;

using UV = std::array<double, 2>;

// In-plane frame of a decal: u and v with cross(u, v) == n, so a grid built from u (columns) and v (rows) faces +n.
struct Basis {
    Vec3 u, v, n;
};

Basis decalBasis(const Vec3& unitNormal, double rotationDeg) {
    // Reference direction for the rotation-0 frame: Y for normals that are not close to Y, otherwise X. With a +Z
    // normal this gives u = +X and v = +Y.
    const Vec3 helper = std::fabs(unitNormal.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 u0 = normalize(cross(helper, unitNormal));
    const Vec3 v0 = cross(unitNormal, u0);
    const double a = rotationDeg * kPi / 180.0;
    const Vec3 u = u0 * std::cos(a) + v0 * std::sin(a);
    return {u, cross(unitNormal, u), unitNormal};
}

bool finite(double v) { return std::isfinite(v); }
bool finite(const Vec3& v) { return finite(v.x) && finite(v.y) && finite(v.z); }

// Nothing a decal can be built from is accepted when it is NaN, infinite, zero-sized or unnamed.
bool validArgs(const Vec3& hit, const Vec3& normal, double width, double height, double rotationDeg, double offset,
               const std::string& material) {
    return finite(hit) && finite(normal) && length(normal) > 1e-12 && finite(width) && width > 0 && finite(height) &&
           height > 0 && finite(rotationDeg) && finite(offset) && !material.empty();
}

// Lower-case copy used for the tool-texture test.
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimWs(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Tool textures that are not drawn or not visible surfaces: a decal on them would never show.
bool toolMaterial(const std::string& material) {
    const std::string m = lower(material);
    for (const char* word : {"nodraw", "caulk", "clip", "hint", "skip", "sky", "trigger", "water"})
        if (m.find(word) != std::string::npos) return true;
    return false;
}

// Sutherland-Hodgman: keeps the part of the convex `subject` inside the convex CCW `clip` (uv space).
std::vector<UV> clipConvex(std::vector<UV> subject, const std::vector<UV>& clip) {
    for (size_t i = 0; i < clip.size() && !subject.empty(); ++i) {
        const UV a = clip[i];
        const UV b = clip[(i + 1) % clip.size()];
        auto side = [&](const UV& p) { return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]); };
        std::vector<UV> out;
        for (size_t k = 0; k < subject.size(); ++k) {
            const UV& p = subject[k];
            const UV& q = subject[(k + 1) % subject.size()];
            const double sp = side(p), sq = side(q);
            if (sp >= 0) out.push_back(p);
            if ((sp >= 0) != (sq >= 0)) {
                const double t = sp / (sp - sq);
                out.push_back({p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t});
            }
        }
        subject = std::move(out);
    }
    return subject;
}

double signedArea(const std::vector<UV>& poly) {
    double a = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const UV& p = poly[i];
        const UV& q = poly[(i + 1) % poly.size()];
        a += p[0] * q[1] - q[0] * p[1];
    }
    return 0.5 * a;
}

// Drops consecutive duplicate points (a clip through a vertex can produce them).
std::vector<UV> dedupe(const std::vector<UV>& poly) {
    std::vector<UV> out;
    for (const UV& p : poly) {
        if (!out.empty() && std::fabs(out.back()[0] - p[0]) < kPointEps && std::fabs(out.back()[1] - p[1]) < kPointEps)
            continue;
        out.push_back(p);
    }
    while (out.size() > 1 && std::fabs(out.front()[0] - out.back()[0]) < kPointEps &&
           std::fabs(out.front()[1] - out.back()[1]) < kPointEps)
        out.pop_back();
    return out;
}

// Appends the patches for one convex CCW polygon (3D points on one plane, facing the polygon's normal).
// A quad keeps its corners; each fan triangle becomes a 2 x 2 patch with the third corner repeated.
void appendPolygonPatches(std::vector<PatchGrid>& out, const std::vector<Vec3>& p, const std::vector<UV>& st,
                          const std::string& material) {
    auto quad = [&](size_t a, size_t b, size_t c, size_t d) {
        // Grid order (row 0: a b, row 1: d c) keeps the cyclic corner order a -> b -> c -> d.
        PatchGrid g;
        g.rows = 2;
        g.cols = 2;
        g.keyword = "mesh";
        g.material = material;
        const size_t idx[4] = {a, b, d, c};
        for (size_t k : idx) {
            g.pos.push_back(p[k]);
            g.st.push_back(st[k]);
        }
        out.push_back(std::move(g));
    };
    if (p.size() == 4) {
        quad(0, 1, 2, 3);
        return;
    }
    for (size_t i = 1; i + 1 < p.size(); ++i) quad(0, i, i + 1, i + 1);
}

// The planar decal is the fallback when no conforming piece is found.
std::vector<PatchGrid> planarOnly(std::optional<PatchGrid> g) {
    std::vector<PatchGrid> out;
    if (g) out.push_back(std::move(*g));
    return out;
}

}  // namespace

std::optional<PatchGrid> makeDecalPatch(const Vec3& hitPoint, const Vec3& surfaceNormal, double width, double height,
                                        double rotationDeg, double offset, const std::string& material) {
    if (!validArgs(hitPoint, surfaceNormal, width, height, rotationDeg, offset, material)) return std::nullopt;
    const Basis b = decalBasis(normalize(surfaceNormal), rotationDeg);
    const Vec3 centre = hitPoint + b.n * offset;

    PatchGrid g;
    g.rows = 2;
    g.cols = 2;
    g.keyword = "mesh";
    g.material = material;
    g.pos.resize(4);
    g.st.resize(4);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            const double su = col ? 0.5 : -0.5;  // along u (columns)
            const double sv = row ? 0.5 : -0.5;  // along v (rows)
            const size_t k = static_cast<size_t>(row * 2 + col);
            g.pos[k] = centre + b.u * (su * width) + b.v * (sv * height);
            g.st[k] = {static_cast<double>(col), static_cast<double>(row)};
        }
    }
    return g;
}

std::vector<PatchGrid> makeConformingDecal(const MapFile& map, const Vec3& hitPoint, const Vec3& normal, double width,
                                           double height, double rotationDeg, double offset,
                                           const std::string& material) {
    const std::optional<PatchGrid> planar = makeDecalPatch(hitPoint, normal, width, height, rotationDeg, offset, material);
    if (!planar) return {};

    const Basis b = decalBasis(normalize(normal), rotationDeg);
    const double halfW = width * 0.5, halfH = height * 0.5;
    const double depth = std::max(width, height);
    // Bounding box of everything the decal can reach: the rectangle's circumradius plus the depth band.
    const double radius = std::sqrt(halfW * halfW + halfH * halfH) + depth;
    AABB query;
    query.add(hitPoint - Vec3{radius, radius, radius});
    query.add(hitPoint + Vec3{radius, radius, radius});

    // The decal rectangle in uv, counter-clockwise about b.n.
    const std::vector<UV> rect = {{-halfW, -halfH}, {halfW, -halfH}, {halfW, halfH}, {-halfW, halfH}};

    std::vector<PatchGrid> out;
    for (const Entity& e : map.entities) {
        if (!e.isWorldspawn()) continue;
        for (const EntityItem& it : e.items) {
            const Primitive* p = it.prim();
            if (!p || p->kind != PrimKind::Brush) continue;
            const AABB pb = p->bounds();
            if (!pb.valid() || pb.mins.x > query.maxs.x || pb.maxs.x < query.mins.x || pb.mins.y > query.maxs.y ||
                pb.maxs.y < query.mins.y || pb.mins.z > query.maxs.z || pb.maxs.z < query.mins.z)
                continue;
            if (!brushContents(*p).collides) continue;  // triggers, hints, nonColliding brushes

            const std::vector<Winding> windings = faceWindingsOf(*p);
            for (size_t f = 0; f < windings.size() && f < p->faces.size(); ++f) {
                const Winding& w = windings[f];
                if (w.size() < 3 || toolMaterial(p->faces[f].material())) continue;
                const std::optional<Plane> plane = facePlane(*p, f);
                if (!plane) continue;
                if (dot(plane->n, b.n) < kCos45) continue;  // steep faces (walls) are not painted

                double lo = 1e300, hi = -1e300;
                for (const Vec3& q : w) {
                    const double h = dot(q - hitPoint, b.n);
                    lo = std::min(lo, h);
                    hi = std::max(hi, h);
                }
                if (hi < -depth || lo > depth) continue;  // outside the depth band

                // Projection of the face polygon onto the decal plane (orthographic, along the normal).
                std::vector<UV> poly;
                poly.reserve(w.size());
                for (const Vec3& q : w) poly.push_back({dot(q - hitPoint, b.u), dot(q - hitPoint, b.v)});
                const double area = signedArea(poly);
                if (std::fabs(area) < kAreaEps) continue;
                if (area < 0) std::reverse(poly.begin(), poly.end());

                const std::vector<UV> clipped = dedupe(clipConvex(rect, poly));
                if (clipped.size() < 3 || std::fabs(signedArea(clipped)) < kAreaEps) continue;

                // Lift each clipped point onto the face plane, then offset it along that face's normal.
                std::vector<Vec3> pts;
                std::vector<UV> st;
                pts.reserve(clipped.size());
                st.reserve(clipped.size());
                const double denom = dot(plane->n, b.n);  // >= cos 45 degrees, never zero
                for (const UV& q : clipped) {
                    const Vec3 base = hitPoint + b.u * q[0] + b.v * q[1];
                    const double t = (plane->d - dot(plane->n, base)) / denom;
                    pts.push_back(base + b.n * t + plane->n * offset);
                    st.push_back({(q[0] + halfW) / width, (q[1] + halfH) / height});
                }
                appendPolygonPatches(out, pts, st, material);
            }
        }
    }
    if (out.empty()) return planarOnly(planar);
    return out;
}

bool placeDecal(Document& doc, const std::vector<PatchGrid>& patches, bool nonColliding) {
    Entity* ws = doc.worldspawn();
    if (!ws || patches.empty()) return false;

    std::vector<Document::PrimEdit> edits;
    for (const PatchGrid& g : patches) {
        Primitive p = makePatchPrimitive(g, doc.eol(), "");
        if (p.kind != PrimKind::Patch) continue;
        if (nonColliding) {
            // The body line goes right after the material line: the parser takes the first token-only line of the
            // patch body as the material, and a `contents ...;` line has spaces, so it is never read as one.
            size_t at = p.lines.size();
            for (size_t i = 0; i < p.lines.size(); ++i) {
                if (trimWs(p.lines[i].text) == p.patchMaterial) {
                    at = i + 1;
                    break;
                }
            }
            if (at >= p.lines.size()) continue;  // no material line: cannot place this patch safely
            p.lines.insert(p.lines.begin() + static_cast<std::ptrdiff_t>(at), TextLine{"  contents nonColliding;", doc.eol()});
            reparsePrimitive(p);
            if (p.kind != PrimKind::Patch) continue;
            p.dirty = true;
        }
        edits.push_back({ws->id, kNoId, std::move(p)});
    }
    if (edits.empty()) return false;
    return doc.editPrimitives(edits, "Place Decal", true);
}

}  // namespace iw8r
