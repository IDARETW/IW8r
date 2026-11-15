// IW8 Radiant Suite - convex collision for custom models (convex_hull.h).
#include "convex_hull.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <utility>

#include "brush_ops.h"
#include "prefab.h"

namespace iw8r {
namespace {

constexpr double kHullPi = 3.14159265358979323846;
constexpr double kHullBoxHalf = 1e6;   // box that bounds the solid while simplifying (unbounded sets touch it)
constexpr double kWeldUnits = 0.01;    // vertex weld grid for convexPieces

// ------------------------------------------------------------------ quickhull

struct HullFace {
    int v[3] = {0, 0, 0};
    Vec3 n;                     // unit outward normal
    double d = 0;               // dot(n, x) on the face
    double area = 0;
    bool alive = true;
    std::vector<int> outside;   // conflict list: points above this face
};

struct RawFace {
    Vec3 n;       // unit outward normal
    double area;  // triangle area
};

// 3D quickhull (Barber, Dobkin, Huhdanpaa). One triangle per hull face into `out`. False when the points are
// degenerate at tolerance eps (all coplanar, collinear or coincident).
bool quickHullFaces(const std::vector<Vec3>& pts, double eps, std::vector<RawFace>& out) {
    const int n = int(pts.size());
    AABB box;
    for (const Vec3& p : pts) box.add(p);

    // Initial simplex: extremes along the widest axis, then the points farthest from that line and from that plane.
    int axis = 0;
    for (int a = 1; a < 3; ++a)
        if (box.maxs[a] - box.mins[a] > box.maxs[axis] - box.mins[axis]) axis = a;
    int i0 = 0, i1 = 0;
    for (int i = 0; i < n; ++i) {
        if (pts[size_t(i)][axis] < pts[size_t(i0)][axis]) i0 = i;
        if (pts[size_t(i)][axis] > pts[size_t(i1)][axis]) i1 = i;
    }
    const Vec3 e = pts[size_t(i1)] - pts[size_t(i0)];
    const double le = length(e);
    if (!(le > eps)) return false;
    int i2 = -1;
    double best = eps;
    for (int i = 0; i < n; ++i) {
        const double dd = length(cross(pts[size_t(i)] - pts[size_t(i0)], e)) / le;
        if (dd > best) best = dd, i2 = i;
    }
    if (i2 < 0) return false;  // collinear
    const Vec3 nrm = normalize(cross(pts[size_t(i1)] - pts[size_t(i0)], pts[size_t(i2)] - pts[size_t(i0)]));
    int i3 = -1;
    best = eps;
    for (int i = 0; i < n; ++i) {
        const double dd = std::fabs(dot(nrm, pts[size_t(i)] - pts[size_t(i0)]));
        if (dd > best) best = dd, i3 = i;
    }
    if (i3 < 0) return false;  // coplanar
    const int simplex[4] = {i0, i1, i2, i3};

    std::vector<HullFace> faces;
    std::map<std::pair<int, int>, int> owner;  // directed edge -> face that holds it
    const auto F = [&faces](int i) -> HullFace& { return faces[size_t(i)]; };
    const auto addFace = [&](int a, int b, int c) -> int {
        HullFace f;
        f.v[0] = a, f.v[1] = b, f.v[2] = c;
        const Vec3 cr = cross(pts[size_t(b)] - pts[size_t(a)], pts[size_t(c)] - pts[size_t(a)]);
        f.area = 0.5 * length(cr);
        f.n = normalize(cr);
        f.d = dot(f.n, pts[size_t(a)]);
        const int idx = int(faces.size());
        faces.push_back(f);
        owner[{a, b}] = idx;
        owner[{b, c}] = idx;
        owner[{c, a}] = idx;
        return idx;
    };

    // Tetrahedron faces, each oriented so that the fourth vertex is on its inner side.
    const int tet[4][4] = {{0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 3, 1}, {1, 2, 3, 0}};
    for (const auto& t : tet) {
        const int a = simplex[t[0]], b = simplex[t[1]], c = simplex[t[2]], opp = simplex[t[3]];
        const Vec3 cr = cross(pts[size_t(b)] - pts[size_t(a)], pts[size_t(c)] - pts[size_t(a)]);
        if (dot(cr, pts[size_t(opp)] - pts[size_t(a)]) > 0)
            addFace(a, c, b);
        else
            addFace(a, b, c);
    }

    // Conflict lists: each remaining point goes to the face it is farthest above.
    std::vector<char> inSimplex(size_t(n), 0);
    for (int s : simplex) inSimplex[size_t(s)] = 1;
    for (int i = 0; i < n; ++i) {
        if (inSimplex[size_t(i)]) continue;
        int bestF = -1;
        double bestD = eps;
        for (int f = 0; f < 4; ++f) {
            const double dd = dot(F(f).n, pts[size_t(i)]) - F(f).d;
            if (dd > bestD) bestD = dd, bestF = f;
        }
        if (bestF >= 0) F(bestF).outside.push_back(i);
    }

    std::vector<int> work;
    for (int f = 0; f < 4; ++f)
        if (!F(f).outside.empty()) work.push_back(f);
    while (!work.empty()) {
        const int f = work.back();
        work.pop_back();
        if (!F(f).alive || F(f).outside.empty()) continue;

        // Apex: the point farthest above f. It joins the hull.
        int apex = -1;
        double far = -1;
        for (int q : F(f).outside) {
            const double dd = dot(F(f).n, pts[size_t(q)]) - F(f).d;
            if (dd > far) far = dd, apex = q;
        }
        const Vec3 ap = pts[size_t(apex)];

        // Visible faces: those above the apex, connected to f across edges.
        std::set<int> vis{f};
        std::vector<int> stack{f};
        while (!stack.empty()) {
            const int cur = stack.back();
            stack.pop_back();
            for (int k = 0; k < 3; ++k) {
                const int a = F(cur).v[k], b = F(cur).v[(k + 1) % 3];
                const auto twin = owner.find(std::pair<int, int>{b, a});
                if (twin == owner.end()) return false;  // rounding broke the surface: the caller retries or falls back
                const int nb = twin->second;
                if (!vis.count(nb) && dot(F(nb).n, ap) - F(nb).d > eps) {
                    vis.insert(nb);
                    stack.push_back(nb);
                }
            }
        }

        // Horizon: directed edges of visible faces whose neighbour across the edge is not visible.
        std::vector<std::pair<int, int>> horizon;
        for (int cur : vis)
            for (int k = 0; k < 3; ++k) {
                const int a = F(cur).v[k], b = F(cur).v[(k + 1) % 3];
                const auto twin = owner.find(std::pair<int, int>{b, a});
                if (twin == owner.end()) return false;
                if (!vis.count(twin->second)) horizon.push_back({a, b});
            }

        // Remove the visible faces; their conflict points are reassigned below.
        std::vector<int> pool;
        for (int cur : vis) {
            for (int q : F(cur).outside)
                if (q != apex) pool.push_back(q);
            F(cur).outside.clear();
            F(cur).alive = false;
            for (int k = 0; k < 3; ++k) owner.erase(std::pair<int, int>{F(cur).v[k], F(cur).v[(k + 1) % 3]});
        }

        // New faces: one per horizon edge, with the apex.
        std::vector<int> created;
        for (const auto& h : horizon) created.push_back(addFace(h.first, h.second, apex));

        for (int q : pool) {
            int bestF = -1;
            double bestD = eps;
            for (int nf : created) {
                const double dd = dot(F(nf).n, pts[size_t(q)]) - F(nf).d;
                if (dd > bestD) bestD = dd, bestF = nf;
            }
            if (bestF >= 0) F(bestF).outside.push_back(q);
        }
        for (int nf : created)
            if (!F(nf).outside.empty()) work.push_back(nf);
    }

    out.clear();
    for (const HullFace& f : faces)
        if (f.alive) out.push_back({f.n, f.area});
    return !out.empty();
}

// ------------------------------------------------------------------ polytope clipping (simplifyHull)

struct PolyFace {
    Vec3 n;                  // outward unit normal
    double d = 0;            // dot(n, x) on the face
    int src = -1;            // index of the source plane; -2 for the bounding box; -1 for a cut cap
    std::vector<Vec3> v;     // convex polygon, cyclic order
};
using Poly = std::vector<PolyFace>;

double polyArea(const PolyFace& f) {
    Vec3 s;
    for (size_t i = 1; i + 1 < f.v.size(); ++i) s += cross(f.v[i] - f.v[0], f.v[i + 1] - f.v[0]);
    return 0.5 * length(s);
}

// Cube of half size h centred on the origin, six faces tagged -2.
Poly boxPoly(double h) {
    Poly p;
    for (int a = 0; a < 3; ++a)
        for (int s = -1; s <= 1; s += 2) {
            PolyFace f;
            f.n[a] = double(s);
            f.d = h;
            f.src = -2;
            const int u = (a + 1) % 3, w = (a + 2) % 3;
            const double cu[4] = {-1, 1, 1, -1}, cw[4] = {-1, -1, 1, 1};
            for (int k = 0; k < 4; ++k) {
                Vec3 q;
                q[a] = s * h;
                q[u] = cu[k] * h;
                q[w] = cw[k] * h;
                f.v.push_back(q);
            }
            p.push_back(std::move(f));
        }
    return p;
}

// Keeps dot(keep.n, x) <= keep.d. The cut face (src = src) is built from the intersection points.
Poly clipPoly(const Poly& in, const Plane& keep, int src) {
    Poly out;
    std::vector<Vec3> cut;
    for (const PolyFace& f : in) {
        PolyFace g;
        g.n = f.n;
        g.d = f.d;
        g.src = f.src;
        const size_t m = f.v.size();
        for (size_t i = 0; i < m; ++i) {
            const Vec3& a = f.v[i];
            const Vec3& b = f.v[(i + 1) % m];
            const double sa = dot(keep.n, a) - keep.d, sb = dot(keep.n, b) - keep.d;
            const bool ina = sa <= 0, inb = sb <= 0;
            if (ina) g.v.push_back(a);
            if (ina != inb) {
                const Vec3 p = a + (b - a) * (sa / (sa - sb));
                g.v.push_back(p);
                cut.push_back(p);
            }
        }
        if (g.v.size() >= 3) out.push_back(std::move(g));
    }

    // Cap: the distinct cut points, ordered by angle in the cut plane.
    std::vector<Vec3> pts;
    for (const Vec3& p : cut) {
        bool dup = false;
        for (const Vec3& q : pts)
            if (length(p - q) < 1e-7) {
                dup = true;
                break;
            }
        if (!dup) pts.push_back(p);
    }
    if (pts.size() >= 3) {
        Vec3 c;
        for (const Vec3& p : pts) c += p;
        c = c * (1.0 / double(pts.size()));
        const Vec3 helper = std::fabs(keep.n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        const Vec3 u = normalize(cross(keep.n, helper));
        const Vec3 w = cross(keep.n, u);
        std::sort(pts.begin(), pts.end(), [&](const Vec3& p, const Vec3& q) {
            return std::atan2(dot(p - c, w), dot(p - c, u)) < std::atan2(dot(q - c, w), dot(q - c, u));
        });
        PolyFace cap;
        cap.n = keep.n;
        cap.d = keep.d;
        cap.src = src;
        cap.v = pts;
        if (polyArea(cap) > 0) out.push_back(std::move(cap));
    }
    return out;
}

// The solid dot(n, x) <= d for every plane, clipped to the bounding box. Empty when nothing is left.
Poly solidOf(const std::vector<Plane>& planes) {
    Poly p = boxPoly(kHullBoxHalf);
    for (size_t i = 0; i < planes.size() && !p.empty(); ++i) p = clipPoly(p, planes[i], int(i));
    return p;
}

// False when the solid reaches the bounding box, i.e. it is unbounded.
bool boundedSolid(const Poly& p) {
    for (const PolyFace& f : p)
        if (f.src == -2 && polyArea(f) > 1e-6) return false;
    return true;
}

// ------------------------------------------------------------------ convexPieces helpers

struct Piece {
    std::vector<Vec3> pts;
    AABB box;
};

double boxVolume(const AABB& b) {
    if (!b.valid()) return 0;
    const Vec3 s = b.maxs - b.mins;
    return s.x * s.y * s.z;
}

double boxGap(const AABB& a, const AABB& b) {
    double sum = 0;
    for (int i = 0; i < 3; ++i) {
        const double g = std::max(0.0, std::max(a.mins[i] - b.maxs[i], b.mins[i] - a.maxs[i]));
        sum += g * g;
    }
    return std::sqrt(sum);
}

}  // namespace

// ------------------------------------------------------------------ public API

std::vector<Plane> convexHullPlanes(const std::vector<Vec3>& points, double mergeAngleDeg) {
    if (points.size() < 4) return {};
    AABB box;
    for (const Vec3& p : points) box.add(p);
    const double diag = length(box.maxs - box.mins);
    if (!std::isfinite(diag) || !(diag > 0)) return {};
    std::vector<RawFace> raw;
    if (!quickHullFaces(points, 1e-7 * diag, raw)) {
        // Nearly coplanar or duplicated points (a corrugated sheet) can defeat the first attempt: weld the points to a
        // grid of 1e-5 of the size and try again with a looser tolerance.
        const double cell = 1e-5 * diag;
        std::set<std::array<long long, 3>> seen;
        std::vector<Vec3> welded;
        for (const Vec3& p : points) {
            const std::array<long long, 3> k{std::llround(p.x / cell), std::llround(p.y / cell), std::llround(p.z / cell)};
            if (seen.insert(k).second) welded.push_back(Vec3{double(k[0]) * cell, double(k[1]) * cell, double(k[2]) * cell});
        }
        raw.clear();
        if (welded.size() < 4 || !quickHullFaces(welded, 1e-5 * diag, raw)) return {};
    }

    // Merge faces whose normals are within the angle: greedy clusters of area-weighted normal sums.
    const double cosT = std::cos(std::max(0.0, mergeAngleDeg) * kHullPi / 180.0);
    std::vector<Vec3> sums;
    for (const RawFace& rf : raw) {
        bool merged = false;
        for (Vec3& s : sums) {
            if (dot(normalize(s), rf.n) >= cosT) {
                s += rf.n * rf.area;
                merged = true;
                break;
            }
        }
        if (!merged) sums.push_back(rf.n * rf.area);
    }

    std::vector<Plane> planes;
    for (const Vec3& s : sums) {
        const Vec3 nn = normalize(s);
        if (length(nn) < 0.5) continue;
        double d = -std::numeric_limits<double>::infinity();
        for (const Vec3& p : points) d = std::max(d, dot(nn, p));  // keeps every input point inside
        planes.push_back(Plane{nn, d});
    }
    return planes;
}

std::vector<Plane> simplifyHull(const std::vector<Plane>& planes, size_t maxPlanes) {
    const size_t limit = std::max<size_t>(maxPlanes, 4);
    std::vector<Plane> cur = planes;
    while (cur.size() > limit) {
        const Poly solid = solidOf(cur);
        if (solid.empty() || !boundedSolid(solid)) break;

        // Area of every plane's face on the solid; a plane with no face is redundant and goes first.
        std::vector<double> area(cur.size(), 0.0);
        for (const PolyFace& f : solid)
            if (f.src >= 0) area[size_t(f.src)] += polyArea(f);
        std::vector<size_t> order(cur.size());
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return area[a] < area[b]; });

        // Remove a quarter of the excess (at least one) at once; halve the batch while the result is unbounded.
        bool removed = false;
        for (size_t r = std::max<size_t>(1, (cur.size() - limit) / 4); r >= 1 && !removed; r /= 2) {
            std::vector<char> drop(cur.size(), 0);
            for (size_t k = 0; k < r; ++k) drop[order[k]] = 1;
            std::vector<Plane> trial;
            for (size_t i = 0; i < cur.size(); ++i)
                if (!drop[i]) trial.push_back(cur[i]);
            const Poly ts = solidOf(trial);
            if (!ts.empty() && boundedSolid(ts)) {
                cur = std::move(trial);
                removed = true;
            }
        }
        if (!removed) break;
    }
    return cur;
}

std::vector<std::vector<Plane>> convexPieces(const ImportedModel& m, int maxPieces, size_t maxPlanesPerPiece) {
    const size_t cap = size_t(std::max(1, maxPieces));
    std::vector<Piece> pieces;
    for (const ImportedMesh& mesh : m.meshes) {
        // Weld positions on a grid of kWeldUnits.
        std::map<std::array<long long, 3>, size_t> cells;
        std::vector<Vec3> wp;
        std::vector<size_t> remap(mesh.pos.size());
        for (size_t i = 0; i < mesh.pos.size(); ++i) {
            const Vec3& p = mesh.pos[i];
            const std::array<long long, 3> key{std::llround(p.x / kWeldUnits), std::llround(p.y / kWeldUnits),
                                               std::llround(p.z / kWeldUnits)};
            auto it = cells.find(key);
            if (it == cells.end()) it = cells.emplace(key, wp.size()).first, wp.push_back(p);
            remap[i] = it->second;
        }

        // Connected components: union-find over the triangles.
        std::vector<size_t> parent(wp.size());
        std::iota(parent.begin(), parent.end(), size_t(0));
        const auto find = [&parent](size_t x) -> size_t {
            while (parent[x] != x) x = parent[x] = parent[parent[x]];
            return x;
        };
        const auto unite = [&](size_t x, size_t y) {
            x = find(x), y = find(y);
            if (x != y) parent[x] = y;
        };
        std::vector<char> used(wp.size(), 0);
        for (size_t t = 0; t + 2 < mesh.idx.size(); t += 3) {
            const uint32_t i0 = mesh.idx[t], i1 = mesh.idx[t + 1], i2 = mesh.idx[t + 2];
            if (i0 >= mesh.pos.size() || i1 >= mesh.pos.size() || i2 >= mesh.pos.size()) continue;
            const size_t a = remap[i0], b = remap[i1], c = remap[i2];
            used[a] = used[b] = used[c] = 1;
            unite(a, b);
            unite(a, c);
        }
        std::map<size_t, size_t> pieceOf;  // root -> index in local
        std::vector<Piece> local;
        for (size_t i = 0; i < wp.size(); ++i) {
            if (!used[i]) continue;
            const size_t root = find(i);
            auto it = pieceOf.find(root);
            if (it == pieceOf.end()) it = pieceOf.emplace(root, local.size()).first, local.emplace_back();
            Piece& pc = local[it->second];
            pc.pts.push_back(wp[i]);
            pc.box.add(wp[i]);
        }
        for (Piece& pc : local) pieces.push_back(std::move(pc));
    }

    const auto byVolume = [](const Piece& a, const Piece& b) { return boxVolume(a.box) > boxVolume(b.box); };
    std::stable_sort(pieces.begin(), pieces.end(), byVolume);

    // Too many pieces: the smallest merges into the piece with the nearest bounds.
    while (pieces.size() > cap) {
        Piece small = std::move(pieces.back());
        pieces.pop_back();
        size_t best = 0;
        double bestGap = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < pieces.size(); ++i) {
            const double g = boxGap(small.box, pieces[i].box);
            if (g < bestGap) bestGap = g, best = i;
        }
        Piece& into = pieces[best];
        into.pts.insert(into.pts.end(), small.pts.begin(), small.pts.end());
        into.box.add(small.box);
    }
    std::stable_sort(pieces.begin(), pieces.end(), byVolume);

    std::vector<std::vector<Plane>> out;
    for (const Piece& pc : pieces) {
        std::vector<Plane> planes = convexHullPlanes(pc.pts);
        if (planes.empty() && pc.box.valid()) {
            // No hull (flat, or the points defeat the hull): the piece collides as its box, a little thickened when flat.
            AABB b = pc.box;
            for (int a = 0; a < 3; ++a)
                if (b.maxs[a] - b.mins[a] < 0.5) b.mins[a] -= 0.25, b.maxs[a] += 0.25;
            planes = {Plane{Vec3{1, 0, 0}, b.maxs.x}, Plane{Vec3{-1, 0, 0}, -b.mins.x}, Plane{Vec3{0, 1, 0}, b.maxs.y},
                      Plane{Vec3{0, -1, 0}, -b.mins.y}, Plane{Vec3{0, 0, 1}, b.maxs.z}, Plane{Vec3{0, 0, -1}, -b.mins.z}};
        }
        if (planes.empty()) continue;
        if (maxPlanesPerPiece >= 4) planes = simplifyHull(planes, maxPlanesPerPiece);
        out.push_back(std::move(planes));
    }
    return out;
}

std::vector<Primitive> hullBrushes(const std::vector<std::vector<Plane>>& pieces, const Vec3& origin,
                                   const Vec3& anglesPitchYawRoll, double scale, const std::string& material,
                                   const std::string& eol) {
    std::vector<Primitive> out;
    if (!(scale > 0)) return out;
    const Mat3 rot = anglesToMatrix(anglesPitchYawRoll);
    const std::string tool = material.empty() ? std::string("clip") : material;
    const std::string suffix = " " + tool + " 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
    for (const auto& piece : pieces) {
        // x' = origin + scale * R x. A plane dot(n, x) = d becomes dot(R n, x') = dot(R n, origin) + scale * d.
        ConvexBrush b;
        for (const Plane& pl : piece) {
            const Vec3 n = rot * pl.n;
            b.push_back(BrushFaceSpec{Plane{n, dot(n, origin) + scale * pl.d}, suffix});
        }
        if (!isValidBrush(b)) continue;
        if (auto prim = makeBrushPrimitive(b, eol)) out.push_back(std::move(*prim));
    }
    return out;
}

}  // namespace iw8r
