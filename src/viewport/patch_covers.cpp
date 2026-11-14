#include "patch_covers.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "compile_iw8.h"

namespace iw8r::app {

namespace {

uint64_t mix(uint64_t h, double v) {
    uint64_t b = 0;
    std::memcpy(&b, &v, sizeof(b));
    return (h ^ b) * 1099511628211ull;
}

// p (in the triangle's plane, within the plane tolerance) inside or on the edge of triangle a b c.
bool inTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& n) {
    const double e = 1e-4;
    const double s0 = dot(cross(b - a, p - a), n), s1 = dot(cross(c - b, p - b), n), s2 = dot(cross(a - c, p - c), n);
    const double area2 = dot(cross(b - a, c - a), n);
    if (std::fabs(area2) < 1e-12) return false;
    // Both windings: the patch may face either way.
    return (s0 >= -e && s1 >= -e && s2 >= -e) || (s0 <= e && s1 <= e && s2 <= e);
}

// Area of triangle (a, b, c) inside the convex polygon `poly` (all in the plane with normal n; poly wound either way).
double clippedArea(const Vec3& a, const Vec3& b, const Vec3& c, const Winding& poly, const Vec3& n) {
    // The polygon's own turning sense about n.
    double turn = 0;
    for (size_t k = 0; k < poly.size(); ++k)
        turn += dot(cross(poly[(k + 1) % poly.size()] - poly[k], poly[(k + 2) % poly.size()] - poly[(k + 1) % poly.size()]), n);
    const double sign = turn >= 0 ? 1.0 : -1.0;
    std::vector<Vec3> in{a, b, c}, out;
    for (size_t k = 0; k < poly.size() && in.size() >= 3; ++k) {
        const Vec3& p0 = poly[k];
        const Vec3 edge = poly[(k + 1) % poly.size()] - p0;
        const auto side = [&](const Vec3& p) { return sign * dot(cross(edge, p - p0), n); };  // >= 0: inside this edge
        out.clear();
        for (size_t i = 0; i < in.size(); ++i) {
            const Vec3& u = in[i];
            const Vec3& v = in[(i + 1) % in.size()];
            const double su = side(u), sv = side(v);
            if (su >= 0) out.push_back(u);
            if ((su >= 0) != (sv >= 0)) out.push_back(u + (v - u) * (su / (su - sv)));
        }
        in.swap(out);
    }
    double area = 0;
    for (size_t k = 1; k + 1 < in.size(); ++k) area += 0.5 * std::fabs(dot(cross(in[k] - in[0], in[k + 1] - in[0]), n));
    return area;
}

}  // namespace

uint64_t PatchCovers::find(const std::vector<const PrimGeom*>& items) {
    covers_.clear();
    static const bool off = std::getenv("IW8R_NO_PATCH_COVERS") != nullptr;
    if (off) return 0;
    if (info_.size() > items.size() * 2 + 64) info_.clear();  // items that are gone
    std::vector<const PrimGeom*> flats, toolBrushes;
    for (const PrimGeom* g : items) {
        if (g->kind == PrimGeom::PointEntity || g->volume) continue;
        Info& ci = info_[g];
        if (ci.stamp != g->stamp) {
            ci = Info{};
            ci.stamp = g->stamp;
            if (g->kind == PrimGeom::Patch) {
                // Flat: every vertex in the plane of the first triangle that has an area.
                bool have = false;
                for (const Winding& w : g->polys) {
                    if (w.size() < 3) continue;
                    const Vec3 c = cross(w[1] - w[0], w[2] - w[0]);
                    if (!(length(c) > 1e-9)) continue;
                    ci.n = normalize(c);
                    ci.d = dot(ci.n, w[0]);
                    have = true;
                    break;
                }
                bool flat = have;
                for (size_t i = 0; i < g->polys.size() && flat; ++i)
                    for (const Vec3& v : g->polys[i])
                        if (std::fabs(dot(ci.n, v) - ci.d) > 0.01) {
                            flat = false;
                            break;
                        }
                ci.flat = flat;
            } else {
                for (const std::string& m : g->polyMaterial)
                    if (isIw8ToolMaterial(m)) {
                        ci.tool = true;
                        break;
                    }
            }
        }
        if (ci.flat) flats.push_back(g);
        else if (ci.tool) toolBrushes.push_back(g);
    }
    if (flats.empty() || toolBrushes.empty()) return 0;
    uint64_t h = 1469598103934665603ull;
    for (const PrimGeom* f : flats) {
        const AABB& a = f->bounds;
        bool touches = false;
        for (const PrimGeom* b : toolBrushes) {
            const AABB& o = b->bounds;
            if (a.mins.x <= o.maxs.x + 0.1 && a.maxs.x >= o.mins.x - 0.1 && a.mins.y <= o.maxs.y + 0.1 && a.maxs.y >= o.mins.y - 0.1 &&
                a.mins.z <= o.maxs.z + 0.1 && a.maxs.z >= o.mins.z - 0.1) {
                touches = true;
                break;
            }
        }
        if (!touches) continue;
        const Info& ci = info_[f];
        Cover c;
        c.n = ci.n, c.d = ci.d, c.box = a, c.tris = &f->polys;
        covers_.push_back(c);
        // The patch's shape is part of the hash through its stamp: a reshaped patch is another geometry build.
        for (double v : {ci.n.x, ci.n.y, ci.n.z, ci.d, a.mins.x, a.mins.y, a.mins.z, a.maxs.x, a.maxs.y, a.maxs.z}) h = mix(h, v);
        h = (h ^ f->stamp) * 1099511628211ull;
    }
    return covers_.empty() ? 0 : h;
}

bool PatchCovers::covered(const Winding& face, const Vec3& normal) const {
    if (covers_.empty() || face.size() < 3) return false;
    // The covers in the face's plane.
    std::vector<const Cover*> plane;
    for (const Cover& c : covers_) {
        if (std::fabs(dot(c.n, normal)) < 0.999) continue;
        bool same = true;
        for (const Vec3& v : face)
            if (std::fabs(dot(c.n, v) - c.d) > 0.05) {
                same = false;
                break;
            }
        if (same) plane.push_back(&c);
    }
    if (plane.empty()) return false;
    Vec3 centre{0, 0, 0};
    for (const Vec3& v : face) centre = centre + v;
    centre = centre * (1.0 / double(face.size()));
    // Corners and edge midpoints, pulled 1 % toward the centre (a point exactly on the rim is a matter of rounding),
    // and the centre itself.
    std::vector<Vec3> samples;
    samples.reserve(face.size() * 2 + 1);
    for (size_t k = 0; k < face.size(); ++k) {
        const Vec3 mid = (face[k] + face[(k + 1) % face.size()]) * 0.5;
        samples.push_back(face[k] + (centre - face[k]) * 0.01);
        samples.push_back(mid + (centre - mid) * 0.01);
    }
    samples.push_back(centre);
    for (const Vec3& p : samples) {
        bool in = false;
        for (const Cover* c : plane) {
            if (p.x < c->box.mins.x - 0.1 || p.x > c->box.maxs.x + 0.1 || p.y < c->box.mins.y - 0.1 || p.y > c->box.maxs.y + 0.1 ||
                p.z < c->box.mins.z - 0.1 || p.z > c->box.maxs.z + 0.1)
                continue;
            for (const Winding& t : *c->tris) {
                if (t.size() < 3) continue;
                for (size_t k = 1; k + 1 < t.size() && !in; ++k) in = inTriangle(p, t[0], t[k], t[k + 1], c->n);
                if (in) break;
            }
            if (in) break;
        }
        if (!in) return false;
    }
    // The samples can all be covered while a strip between two patches is not: the patch triangles must also hold the
    // face's whole area. (Patches that lie over each other count twice; a paint mesh does not do that.)
    double faceArea = 0, held = 0;
    for (size_t k = 1; k + 1 < face.size(); ++k) faceArea += 0.5 * std::fabs(dot(cross(face[k] - face[0], face[k + 1] - face[0]), normal));
    if (!(faceArea > 0)) return false;
    for (const Cover* c : plane) {
        for (const Winding& t : *c->tris)
            for (size_t k = 1; k + 1 < t.size(); ++k) held += clippedArea(t[0], t[k], t[k + 1], face, normal);
        if (held >= faceArea * 0.999) return true;
    }
    return false;
}

}  // namespace iw8r::app
