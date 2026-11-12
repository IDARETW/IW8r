#include "collision_world.h"

#include <algorithm>
#include <cmath>

#include "brush_ops.h"
#include "iw8_contents.h"
#include "patch_tess.h"

namespace iw8r {

namespace {

constexpr double kDistEpsilon = 0.03125;  // Quake/CoD trace epsilon: keep the box this far from surfaces

std::string lowerStr(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool blocksPlayerClass(const std::string& cls) {
    const std::string l = lowerStr(cls);
    return l == "worldspawn" || l == "func_group" || l == "script_brushmodel" || l.empty();
}

}  // namespace

void CollisionWorld::addSolid(std::vector<Plane> planes, uint32_t contents, bool patch, bool ladder) {
    Solid s;
    s.contents = contents;
    s.patch = patch;
    s.ladder = ladder;
    // Bounds from the windings; axial bevel planes make box sweeps against slanted faces behave (Quake adds them too).
    for (const Winding& w : buildBrushWindings(planes))
        for (const Vec3& v : w) s.bounds.add(v);
    if (!s.bounds.valid()) return;
    for (int a = 0; a < 3; ++a) {
        Vec3 n;
        n[a] = 1;
        planes.push_back({n, s.bounds.maxs[a]});
        n[a] = -1;
        planes.push_back({n, -s.bounds.mins[a]});
    }
    s.planes = std::move(planes);
    bounds_.add(s.bounds);
    solids_.push_back(std::move(s));
}

void CollisionWorld::build(const MapFile& map, int patchSubdiv) {
    solids_.clear();
    bounds_ = AABB{};
    for (const auto& e : map.entities) {
        if (!blocksPlayerClass(e.classname())) continue;  // triggers and other brush entities do not block
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Brush) {
                const BrushContents bc = brushContents(*p);
                if (!bc.collides || !(bc.contents & kPlayerMask)) continue;
                const ConvexBrush cb = brushFromPrimitive(*p);
                if (!isValidBrush(cb)) continue;
                std::vector<Plane> planes;
                for (const auto& f : cb) planes.push_back(f.plane);
                addSolid(std::move(planes), bc.contents, false, bc.ladder);
            } else if (p->kind == PrimKind::Patch) {
                const std::string text = p->text();
                if (text.find("nonColliding") != std::string::npos) continue;
                const std::string mat = lowerStr(p->patchMaterial);
                if (mat.find("trigger") != std::string::npos || mat.find("hint") != std::string::npos) continue;
                // Each tessellated triangle becomes a thin convex facet (front, back and three edge planes).
                const PatchMesh mesh = tessellatePrimitive(*p, patchSubdiv);
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    const Vec3& a = mesh.points[mesh.indices[i]];
                    const Vec3& b = mesh.points[mesh.indices[i + 1]];
                    const Vec3& c = mesh.points[mesh.indices[i + 2]];
                    const Vec3 cr = cross(b - a, c - a);
                    if (length(cr) < 1e-6) continue;
                    const Vec3 n = normalize(cr);
                    std::vector<Plane> planes;
                    planes.push_back({n, dot(n, a)});
                    planes.push_back({n * -1.0, -dot(n, a) + 1.0});  // 1 unit thick behind the surface
                    const Vec3 tri[3] = {a, b, c};
                    for (int k = 0; k < 3; ++k) {
                        const Vec3& p0 = tri[k];
                        const Vec3& p1 = tri[(k + 1) % 3];
                        Vec3 en = normalize(cross(p1 - p0, n));
                        const Vec3& other = tri[(k + 2) % 3];
                        if (dot(en, other - p0) > 0) en = en * -1.0;  // outward from the triangle
                        planes.push_back({en, dot(en, p0)});
                    }
                    addSolid(std::move(planes), 1u, true);
                }
            }
        }
    }
}

void CollisionWorld::traceThrough(const Solid& s, int index, const Vec3& start, const Vec3& end, const Vec3& mins,
                                  const Vec3& maxs, Trace& tr, double capsuleRadius) const {
    double enterFrac = -1.0, leaveFrac = 1.0;
    bool startOut = false, getOut = false;
    const Plane* clipPlane = nullptr;
    for (const Plane& pl : s.planes) {
        // Expand the plane by the box: the box corner that leads into the plane.
        Vec3 ofs;
        if (capsuleRadius > 0) {
            // Capsule support point: the segment end lower along n, pulled back by the radius.
            const double cx = (mins.x + maxs.x) * 0.5, cy = (mins.y + maxs.y) * 0.5;
            const Vec3 bottom{cx, cy, mins.z + capsuleRadius}, top{cx, cy, maxs.z - capsuleRadius};
            ofs = (pl.n.z > 0 ? bottom : top) - pl.n * capsuleRadius;
        } else {
            for (int a = 0; a < 3; ++a) ofs[a] = pl.n[a] < 0 ? maxs[a] : mins[a];
        }
        const double dist = pl.d - dot(ofs, pl.n);
        const double d1 = dot(start, pl.n) - dist;
        const double d2 = dot(end, pl.n) - dist;
        if (d2 > 0) getOut = true;
        if (d1 > 0) startOut = true;
        if (d1 > 0 && (d2 >= kDistEpsilon || d2 >= d1)) return;  // entirely in front of this plane: no contact
        if (d1 <= 0 && d2 <= 0) continue;
        if (d1 > d2) {
            const double f = std::max(0.0, (d1 - kDistEpsilon) / (d1 - d2));
            if (f > enterFrac) {
                enterFrac = f;
                clipPlane = &pl;
            }
        } else {
            const double f = std::min(1.0, (d1 + kDistEpsilon) / (d1 - d2));
            if (f < leaveFrac) leaveFrac = f;
        }
    }
    if (!startOut) {
        tr.startSolid = true;
        if (!getOut) {
            tr.allSolid = true;
            tr.fraction = 0;
            tr.solid = index;
        }
        return;
    }
    if (enterFrac < leaveFrac && enterFrac > -1 && enterFrac < tr.fraction && clipPlane) {
        tr.fraction = std::max(0.0, enterFrac);
        tr.plane = *clipPlane;
        tr.solid = index;
    }
}

CollisionWorld::Trace CollisionWorld::traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) const {
    return traceShape(start, end, mins, maxs, 0);
}

CollisionWorld::Trace CollisionWorld::traceCapsule(const Vec3& start, const Vec3& end, double r, double h) const {
    return traceShape(start, end, Vec3{-r, -r, 0}, Vec3{r, r, std::max(h, 2 * r)}, r);
}

CollisionWorld::Trace CollisionWorld::traceShape(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                                                 double capsuleRadius) const {
    Trace tr;
    // Swept bounds for the broad phase.
    AABB swept;
    swept.add(start + mins);
    swept.add(start + maxs);
    swept.add(end + mins);
    swept.add(end + maxs);
    for (size_t i = 0; i < solids_.size(); ++i) {
        const Solid& s = solids_[i];
        bool overlap = true;
        for (int a = 0; a < 3; ++a) overlap &= swept.maxs[a] >= s.bounds.mins[a] - 1 && swept.mins[a] <= s.bounds.maxs[a] + 1;
        if (!overlap) continue;
        traceThrough(s, int(i), start, end, mins, maxs, tr, capsuleRadius);
        if (tr.allSolid) break;
    }
    tr.end = start + (end - start) * tr.fraction;
    return tr;
}

}  // namespace iw8r
