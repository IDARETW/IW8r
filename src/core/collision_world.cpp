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
    // An open hull (a physics piece or model hull missing its caps) reaches the winding clip box (+-262144): it would
    // be a slab through the whole map that the player stands inside (stuck in Hideout's play preview). IW maps stay
    // within +-131072, so such a solid is dropped.
    for (int a = 0; a < 3; ++a)
        if (s.bounds.maxs[a] > 200000.0 || s.bounds.mins[a] < -200000.0) return;
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

bool CollisionWorld::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, double thickness, uint32_t contents) {
    const Vec3 cr = cross(b - a, c - a);
    if (length(cr) < 1e-6) return false;
    const Vec3 n = normalize(cr);
    Solid s;
    s.contents = contents;
    s.patch = true;
    s.planes.reserve(11);
    s.planes.push_back({n, dot(n, a)});
    s.planes.push_back({n * -1.0, -dot(n, a) + thickness});
    const Vec3 tri[3] = {a, b, c};
    for (int k = 0; k < 3; ++k) {
        const Vec3& p0 = tri[k];
        const Vec3& p1 = tri[(k + 1) % 3];
        Vec3 en = normalize(cross(p1 - p0, n));
        if (dot(en, tri[(k + 2) % 3] - p0) > 0) en = en * -1.0;  // outward from the triangle
        s.planes.push_back({en, dot(en, p0)});
    }
    for (const Vec3& p : tri) s.bounds.add(p), s.bounds.add(p - n * thickness);
    for (int ax = 0; ax < 3; ++ax) {  // axial bevels, as addSolid adds
        Vec3 v;
        v[ax] = 1;
        s.planes.push_back({v, s.bounds.maxs[ax]});
        v[ax] = -1;
        s.planes.push_back({v, -s.bounds.mins[ax]});
    }
    bounds_.add(s.bounds);
    solids_.push_back(std::move(s));
    return true;
}

void CollisionWorld::buildGrid() const {
    grid_.clear();
    moverSolidList_.clear();
    std::vector<char> moving(solids_.size(), 0);
    for (const Mover& m : movers_)
        for (const size_t i : m.solids)
            if (i < moving.size()) moving[i] = 1, moverSolidList_.push_back(uint32_t(i));
    const auto key = [](int64_t x, int64_t y) { return (x << 32) ^ (y & 0xffffffffll); };
    for (size_t i = 0; i < solids_.size(); ++i) {
        if (moving[i]) continue;
        const AABB& b = solids_[i].bounds;
        const int64_t x0 = int64_t(std::floor((b.mins.x - 1) / kGridCell)), x1 = int64_t(std::floor((b.maxs.x + 1) / kGridCell));
        const int64_t y0 = int64_t(std::floor((b.mins.y - 1) / kGridCell)), y1 = int64_t(std::floor((b.maxs.y + 1) / kGridCell));
        for (int64_t x = x0; x <= x1; ++x)
            for (int64_t y = y0; y <= y1; ++y) grid_[key(x, y)].push_back(uint32_t(i));
    }
    gridStamp_.assign(solids_.size(), 0);
    gridTick_ = 0;
    gridSolids_ = solids_.size();
}

void CollisionWorld::build(const MapFile& map, int patchSubdiv) { build(map, patchSubdiv, {}); }

void CollisionWorld::build(const MapFile& map, int patchSubdiv, const std::function<bool(const Entity&)>& skip) {
    solids_.clear();
    movers_.clear();
    gridSolids_ = ~size_t(0);  // the grid is built again at the next trace
    bounds_ = AABB{};
    for (const auto& e : map.entities) {
        if (!blocksPlayerClass(e.classname())) continue;  // triggers and other brush entities do not block
        if (skip && skip(e)) continue;
        addBrushesOf(e, patchSubdiv);
    }
}

int CollisionWorld::addMover(const Entity& e) {
    const size_t first = solids_.size();
    addBrushesOf(e, 4);
    if (solids_.size() == first) return -1;
    Mover m;
    for (size_t i = first; i < solids_.size(); ++i) {
        m.solids.push_back(i);
        m.planes.push_back(solids_[i].planes);
        m.bounds.push_back(solids_[i].bounds);
        m.all.add(solids_[i].bounds);
    }
    movers_.push_back(std::move(m));
    return int(movers_.size()) - 1;
}

AABB CollisionWorld::moverBounds(int group) const {
    return group >= 0 && size_t(group) < movers_.size() ? movers_[size_t(group)].all : AABB{};
}

void CollisionWorld::setMoverPose(int group, const double R[3][3], const Vec3& pivot, const Vec3& move) {
    if (group < 0 || size_t(group) >= movers_.size()) return;
    const Mover& m = movers_[size_t(group)];
    const auto rot = [&](const Vec3& v) {
        return Vec3{R[0][0] * v.x + R[0][1] * v.y + R[0][2] * v.z, R[1][0] * v.x + R[1][1] * v.y + R[1][2] * v.z,
                    R[2][0] * v.x + R[2][1] * v.y + R[2][2] * v.z};
    };
    for (size_t k = 0; k < m.solids.size(); ++k) {
        Solid& s = solids_[m.solids[k]];
        for (size_t p = 0; p < s.planes.size() && p < m.planes[k].size(); ++p) {
            const Plane& base = m.planes[k][p];
            const Vec3 n = rot(base.n);
            const Vec3 onPlane = pivot + rot(base.n * base.d - pivot) + move;
            s.planes[p] = {n, dot(n, onPlane)};
        }
        AABB b;
        const AABB& rb = m.bounds[k];
        for (int c = 0; c < 8; ++c) {
            const Vec3 corner{c & 1 ? rb.maxs.x : rb.mins.x, c & 2 ? rb.maxs.y : rb.mins.y, c & 4 ? rb.maxs.z : rb.mins.z};
            b.add(pivot + rot(corner - pivot) + move);
        }
        s.bounds = b;
        bounds_.add(b);
    }
}

void CollisionWorld::addBrushesOf(const Entity& e, int patchSubdiv) {
    {
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
                                  const Vec3& maxs, Trace& tr, double capsuleRadius, bool prism) const {
    double enterFrac = -1.0, leaveFrac = 1.0;
    bool startOut = false, getOut = false;
    const Plane* clipPlane = nullptr;
    for (const Plane& pl : s.planes) {
        // Expand the plane by the box: the box corner that leads into the plane.
        Vec3 ofs;
        if (prism) {
            // Octagonal prism support point: the corner (r sin t, r cos t), t = k * 45 degrees, lowest along n, on the
            // bottom face for an upward-facing plane, the top face otherwise. The solids' axial bevels give the flat
            // faces their contacts (a flat bottom resting on a ridge or a corner).
            constexpr double h = 0.70710678118654752440;
            static constexpr double kSin[8] = {0, h, 1, h, 0, -h, -1, -h}, kCos[8] = {1, h, 0, -h, -1, -h, 0, h};
            const double cx = (mins.x + maxs.x) * 0.5, cy = (mins.y + maxs.y) * 0.5, r = (maxs.x - mins.x) * 0.5;
            int best = 0;
            double lowest = 1e300;
            for (int k = 0; k < 8; ++k) {
                const double v = pl.n.x * kSin[k] + pl.n.y * kCos[k];
                if (v < lowest) lowest = v, best = k;
            }
            ofs = Vec3{cx + r * kSin[best], cy + r * kCos[best], pl.n.z > 0 ? mins.z : maxs.z};
        } else if (capsuleRadius > 0) {
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

CollisionWorld::Trace CollisionWorld::traceCylinder(const Vec3& start, const Vec3& end, double r, double h) const {
    return traceShape(start, end, Vec3{-r, -r, 0}, Vec3{r, r, std::max(h, 0.0)}, 0, true);
}

CollisionWorld::Trace CollisionWorld::traceShape(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                                                 double capsuleRadius, bool prism) const {
    Trace tr;
    // Swept bounds for the broad phase.
    AABB swept;
    swept.add(start + mins);
    swept.add(start + maxs);
    swept.add(end + mins);
    swept.add(end + maxs);
    const auto test = [&](size_t i) {
        const Solid& s = solids_[i];
        bool overlap = true;
        for (int a = 0; a < 3; ++a) overlap &= swept.maxs[a] >= s.bounds.mins[a] - 1 && swept.mins[a] <= s.bounds.maxs[a] + 1;
        if (overlap) traceThrough(s, int(i), start, end, mins, maxs, tr, capsuleRadius, prism);
    };
    // Many solids (a stock map's world): the grid cells the sweep crosses, each solid once; movers every time.
    const int64_t x0 = int64_t(std::floor((swept.mins.x - 1) / kGridCell)), x1 = int64_t(std::floor((swept.maxs.x + 1) / kGridCell));
    const int64_t y0 = int64_t(std::floor((swept.mins.y - 1) / kGridCell)), y1 = int64_t(std::floor((swept.maxs.y + 1) / kGridCell));
    if (solids_.size() >= kGridFrom && (x1 - x0 + 1) * (y1 - y0 + 1) <= 4096) {
        if (gridSolids_ != solids_.size() || gridStamp_.size() != solids_.size()) buildGrid();
        if (++gridTick_ == 0) std::fill(gridStamp_.begin(), gridStamp_.end(), 0u), gridTick_ = 1;
        const auto key = [](int64_t x, int64_t y) { return (x << 32) ^ (y & 0xffffffffll); };
        for (int64_t x = x0; x <= x1 && !tr.allSolid; ++x)
            for (int64_t y = y0; y <= y1 && !tr.allSolid; ++y) {
                const auto it = grid_.find(key(x, y));
                if (it == grid_.end()) continue;
                for (const uint32_t i : it->second) {
                    if (gridStamp_[i] == gridTick_) continue;
                    gridStamp_[i] = gridTick_;
                    test(i);
                    if (tr.allSolid) break;
                }
            }
        for (const uint32_t i : moverSolidList_) {
            if (tr.allSolid) break;
            test(i);
        }
    } else {
        for (size_t i = 0; i < solids_.size() && !tr.allSolid; ++i) test(i);
    }
    tr.end = start + (end - start) * tr.fraction;
    return tr;
}

}  // namespace iw8r
