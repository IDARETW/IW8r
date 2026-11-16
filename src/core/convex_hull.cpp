// IW8 Radiant - convex collision for custom models (convex_hull.h).
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

using Tri = std::array<Vec3, 3>;

// The triangles' surface on the plane of the two largest box axes (ax the larger), rasterised by samples across each
// triangle to a grid of `n` cells along ax: the occupied cells, each triangle's first cell, the 4-connected groups.
struct PieceRaster {
    int ax = 0, ay = 1, nx = 1, ny = 1;
    double cell = 1;
    std::vector<char> occ;      // ny rows of nx cells
    std::vector<int> group;     // per cell, -1 when empty
    int groups = 0;
    std::vector<int> triCell;   // per triangle
};

// 4-connected groups of the occupied cells whose column (cx) or row (cy) lies in [lo, hi) along `axis` (0 = columns):
// the summed area of the groups' bounding rectangles, in cells. Fills `group` when given.
double groupBoxArea(const PieceRaster& r, int axis, int lo, int hi, std::vector<int>* group = nullptr, int* count = nullptr) {
    std::vector<int> g(r.occ.size(), -1);
    const auto inside = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= r.nx || y >= r.ny) return false;
        const int k = axis == 0 ? x : y;
        return k >= lo && k < hi && r.occ[size_t(y) * size_t(r.nx) + size_t(x)];
    };
    double area = 0;
    int groups = 0;
    for (int y = 0; y < r.ny; ++y)
        for (int x = 0; x < r.nx; ++x) {
            const size_t c = size_t(y) * size_t(r.nx) + size_t(x);
            if (!inside(x, y) || g[c] >= 0) continue;
            int x0 = x, x1 = x, y0 = y, y1 = y;
            std::vector<std::pair<int, int>> stack{{x, y}};
            g[c] = groups;
            while (!stack.empty()) {
                const auto [qx, qy] = stack.back();
                stack.pop_back();
                x0 = std::min(x0, qx), x1 = std::max(x1, qx), y0 = std::min(y0, qy), y1 = std::max(y1, qy);
                const int nb[4][2] = {{qx + 1, qy}, {qx - 1, qy}, {qx, qy + 1}, {qx, qy - 1}};
                for (const auto& e : nb) {
                    if (!inside(e[0], e[1])) continue;
                    const size_t q = size_t(e[1]) * size_t(r.nx) + size_t(e[0]);
                    if (g[q] >= 0) continue;
                    g[q] = groups;
                    stack.push_back({e[0], e[1]});
                }
            }
            area += double(x1 - x0 + 1) * double(y1 - y0 + 1);
            ++groups;
        }
    if (group) *group = std::move(g);
    if (count) *count = groups;
    return area;
}

PieceRaster rasterTris(const std::vector<Tri>& tris, const AABB& box, int n) {
    PieceRaster r;
    const double ext[3] = {box.maxs.x - box.mins.x, box.maxs.y - box.mins.y, box.maxs.z - box.mins.z};
    int order[3] = {0, 1, 2};
    std::stable_sort(order, order + 3, [&](int a, int b) { return ext[a] > ext[b]; });
    r.ax = order[0], r.ay = order[1];
    r.cell = std::max(ext[r.ax], 1e-6) / n;
    r.nx = std::max(1, int(std::floor(ext[r.ax] / r.cell)) + 1);
    r.ny = std::max(1, int(std::floor(ext[r.ay] / r.cell)) + 1);
    r.occ.assign(size_t(r.nx) * size_t(r.ny), 0);
    r.triCell.assign(tris.size(), 0);
    const auto cellAt = [&](const Vec3& p) {
        const int cx = std::clamp(int((p[r.ax] - box.mins[r.ax]) / r.cell), 0, r.nx - 1);
        const int cy = std::clamp(int((p[r.ay] - box.mins[r.ay]) / r.cell), 0, r.ny - 1);
        return cy * r.nx + cx;
    };
    for (size_t t = 0; t < tris.size(); ++t) {
        const Tri& T = tris[t];
        const double edge = std::max({length(T[1] - T[0]), length(T[2] - T[1]), length(T[0] - T[2])});
        const int k = std::clamp(int(std::ceil(edge / (0.5 * r.cell))), 1, 48);
        r.triCell[t] = cellAt((T[0] + T[1] + T[2]) * (1.0 / 3.0));
        for (int i = 0; i <= k; ++i)
            for (int j = 0; j <= k - i; ++j) {
                const double u = double(i) / k, v = double(j) / k;
                r.occ[size_t(cellAt(T[0] + (T[1] - T[0]) * u + (T[2] - T[0]) * v))] = 1;
            }
        r.occ[size_t(r.triCell[t])] = 1;
    }
    groupBoxArea(r, 0, 0, r.nx, &r.group, &r.groups);
    return r;
}

void pieceFromTris(const std::vector<Tri>& tris, std::vector<Piece>& out) {
    Piece pc;
    std::set<std::array<long long, 3>> seen;
    for (const Tri& T : tris)
        for (const Vec3& v : T) {
            pc.box.add(v);
            const std::array<long long, 3> key{std::llround(v.x * 1000.0), std::llround(v.y * 1000.0), std::llround(v.z * 1000.0)};
            if (seen.insert(key).second) pc.pts.push_back(v);
        }
    if (!pc.pts.empty()) out.push_back(std::move(pc));
}

// Splits a notched part (a door frame, an arch, an L of walls) into pieces whose hulls leave the notch free.
void splitConcave(std::vector<Tri> tris, int depth, double minSize, std::vector<Piece>& out) {
    AABB box;
    for (const Tri& T : tris)
        for (const Vec3& v : T) box.add(v);
    const double big = std::max({box.maxs.x - box.mins.x, box.maxs.y - box.mins.y, box.maxs.z - box.mins.z});
    if (depth <= 0 || tris.size() < 4 || !(big >= minSize)) return pieceFromTris(tris, out);
    const PieceRaster r = rasterTris(tris, box, 24);
    if (r.groups > 1) {  // apart on this plane: each group on its own
        std::vector<std::vector<Tri>> parts(size_t(r.groups));
        for (size_t t = 0; t < tris.size(); ++t) {
            const int g = r.group[size_t(r.triCell[t])];
            parts[size_t(std::max(0, g))].push_back(tris[t]);
        }
        size_t nonEmpty = 0;
        for (const auto& p : parts) nonEmpty += p.empty() ? 0 : 1;
        // Only when the groups part the triangles: a thin diagonal triangle rasterises to cells that touch only at
        // their corners (separate 4-connected groups) while every triangle's centre lies in one of them, and the same
        // triangles came back here at the same depth until the stack ran out (walking Hideout, 2026-11-15).
        if (nonEmpty > 1) {
            for (auto& p : parts)
                if (!p.empty()) splitConcave(std::move(p), depth, minSize, out);
            return;
        }
    }
    size_t occupied = 0;
    for (char c : r.occ) occupied += c ? 1 : 0;
    const double whole = groupBoxArea(r, 0, 0, r.nx);
    if (whole <= 0 || 1.0 - double(occupied) / whole < 0.35) return pieceFromTris(tris, out);
    // The cut along either axis that leaves the least bounding area.
    int bestAxis = -1, bestAt = 0;
    double bestCost = 0.8 * whole;
    for (int axis = 0; axis < 2; ++axis) {
        const int cells = axis == 0 ? r.nx : r.ny;
        for (int at = 1; at < cells; ++at) {
            const double cost = groupBoxArea(r, axis, 0, at) + groupBoxArea(r, axis, at, cells);
            if (cost < bestCost) bestCost = cost, bestAxis = axis, bestAt = at;
        }
    }
    if (bestAxis < 0) return pieceFromTris(tris, out);
    const int world = bestAxis == 0 ? r.ax : r.ay;
    const double cut = box.mins[world] + bestAt * r.cell;
    std::vector<Tri> lo, hi;
    for (const Tri& T : tris) ((T[0][world] + T[1][world] + T[2][world]) / 3.0 < cut ? lo : hi).push_back(T);
    if (lo.empty() || hi.empty()) return pieceFromTris(tris, out);
    splitConcave(std::move(lo), depth - 1, minSize, out);
    splitConcave(std::move(hi), depth - 1, minSize, out);
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

    const auto planesOf = [&](const std::vector<Vec3>& normals) {
        std::vector<Plane> planes;
        for (const Vec3& s : normals) {
            const Vec3 nn = normalize(s);
            if (!(length(nn) > 0.5)) continue;
            double d = -std::numeric_limits<double>::infinity();
            for (const Vec3& p : points) d = std::max(d, dot(nn, p));  // keeps every input point inside
            planes.push_back(Plane{nn, d});
        }
        return planes;
    };
    // The planes must close the solid. Faces of (almost) no area lose their direction in the area-weighted sums, and a
    // long thin part (a wire, a bracket rod) can end up with two or four planes: a slab through the whole map (owner's
    // Hideout walk 2026-11-15: 150 barbed wire brackets). Then every face keeps its own plane; still open: no hull.
    std::vector<Plane> planes = planesOf(sums);
    const Poly solid = solidOf(planes);
    if (planes.size() >= 4 && !solid.empty() && boundedSolid(solid)) return planes;
    std::vector<Vec3> each;
    for (const RawFace& rf : raw) {
        bool dup = false;
        for (const Vec3& n : each) dup = dup || dot(n, rf.n) > 1.0 - 1e-12;
        if (!dup) each.push_back(rf.n);
    }
    planes = planesOf(each);
    const Poly solid2 = solidOf(planes);
    if (planes.size() >= 4 && !solid2.empty() && boundedSolid(solid2)) return planes;
    return {};
}

bool convexSolidBounds(const std::vector<Plane>& planes, AABB& out) {
    out = AABB{};
    if (planes.size() < 4) return false;
    const Poly solid = solidOf(planes);
    if (solid.empty() || !boundedSolid(solid)) return false;
    for (const PolyFace& f : solid)
        for (const Vec3& v : f.v) out.add(v);
    return out.valid();
}

void thickenHull(std::vector<Plane>& planes, const std::vector<Vec3>& points, double minWidth) {
    if (points.empty() || !(minWidth > 0)) return;
    for (Plane& pl : planes) {
        double lo = std::numeric_limits<double>::infinity();
        for (const Vec3& p : points) lo = std::min(lo, dot(pl.n, p));
        const double width = pl.d - lo;
        if (width < minWidth) pl.d += 0.5 * (minWidth - width);
    }
}

bool convexSolidWithin(const std::vector<Plane>& planes, const AABB& points) {
    if (!points.valid()) return false;
    AABB b;
    if (!convexSolidBounds(planes, b)) return false;
    const Vec3 s = points.maxs - points.mins;
    const double pad = 2.0 + 0.25 * std::max({s.x, s.y, s.z});
    for (int a = 0; a < 3; ++a)
        if (b.mins[a] < points.mins[a] - pad || b.maxs[a] > points.maxs[a] + pad) return false;
    return true;
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
    // Parts smaller than a twentieth of the model are not cut (the model's own units: map units or a file's metres).
    AABB all;
    for (const ImportedMesh& mesh : m.meshes)
        for (const Vec3& p : mesh.pos) all.add(p);
    const double minSize = all.valid() ? 0.05 * std::max({all.maxs.x - all.mins.x, all.maxs.y - all.mins.y, all.maxs.z - all.mins.z}) : 0.0;
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
        std::vector<std::array<size_t, 3>> welded;  // the triangles on the welded points
        for (size_t t = 0; t + 2 < mesh.idx.size(); t += 3) {
            const uint32_t i0 = mesh.idx[t], i1 = mesh.idx[t + 1], i2 = mesh.idx[t + 2];
            if (i0 >= mesh.pos.size() || i1 >= mesh.pos.size() || i2 >= mesh.pos.size()) continue;
            const size_t a = remap[i0], b = remap[i1], c = remap[i2];
            used[a] = used[b] = used[c] = 1;
            unite(a, b);
            unite(a, c);
            welded.push_back({a, b, c});
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
        // Each part's triangles; a notched part (a door frame, an arch) is split so its hulls leave the opening free.
        std::vector<std::vector<Tri>> partTris(local.size());
        for (const auto& w : welded)
            if (const auto it = pieceOf.find(find(w[0])); it != pieceOf.end())
                partTris[it->second].push_back(Tri{wp[w[0]], wp[w[1]], wp[w[2]]});
        for (size_t i = 0; i < local.size(); ++i) {
            if (partTris[i].empty()) {
                pieces.push_back(std::move(local[i]));
                continue;
            }
            splitConcave(std::move(partTris[i]), 4, minSize, pieces);
        }
    }

    const auto byVolume = [](const Piece& a, const Piece& b) { return boxVolume(a.box) > boxVolume(b.box); };
    std::stable_sort(pieces.begin(), pieces.end(), byVolume);

    // Too many pieces: the smallest merges into the piece whose bounds grow least (summed extents, so flat pieces count),
    // the nearer bounds deciding a tie.
    const auto extentSum = [](const AABB& b) { return (b.maxs.x - b.mins.x) + (b.maxs.y - b.mins.y) + (b.maxs.z - b.mins.z); };
    while (pieces.size() > cap) {
        Piece small = std::move(pieces.back());
        pieces.pop_back();
        size_t best = 0;
        double bestGrow = std::numeric_limits<double>::infinity(), bestGap = bestGrow;
        for (size_t i = 0; i < pieces.size(); ++i) {
            AABB u = pieces[i].box;
            u.add(small.box);
            const double grow = extentSum(u) - extentSum(pieces[i].box), g = boxGap(small.box, pieces[i].box);
            if (grow < bestGrow - 1e-9 || (grow < bestGrow + 1e-9 && g < bestGap)) bestGrow = grow, bestGap = g, best = i;
        }
        Piece& into = pieces[best];
        into.pts.insert(into.pts.end(), small.pts.begin(), small.pts.end());
        into.box.add(small.box);
    }
    std::stable_sort(pieces.begin(), pieces.end(), byVolume);

    std::vector<std::vector<Plane>> out;
    for (const Piece& pc : pieces) {
        std::vector<Plane> planes = convexHullPlanes(pc.pts);
        if (maxPlanesPerPiece >= 4 && !planes.empty()) planes = simplifyHull(planes, maxPlanesPerPiece);
        // A sliver is made half a unit thick, and a hull that is open or reaches past its points (a slab through the
        // map, owner's Hideout walk 2026-11-15) collides as the box below instead.
        thickenHull(planes, pc.pts, 0.5);
        if (!planes.empty() && !convexSolidWithin(planes, pc.box)) planes.clear();
        if (planes.empty() && pc.box.valid()) {
            // No hull (flat, or the points defeat the hull): the piece collides as its box, a little thickened when flat.
            AABB b = pc.box;
            for (int a = 0; a < 3; ++a)
                if (b.maxs[a] - b.mins[a] < 0.5) b.mins[a] -= 0.25, b.maxs[a] += 0.25;
            planes = {Plane{Vec3{1, 0, 0}, b.maxs.x}, Plane{Vec3{-1, 0, 0}, -b.mins.x}, Plane{Vec3{0, 1, 0}, b.maxs.y},
                      Plane{Vec3{0, -1, 0}, -b.mins.y}, Plane{Vec3{0, 0, 1}, b.maxs.z}, Plane{Vec3{0, 0, -1}, -b.mins.z}};
        }
        if (planes.empty()) continue;
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
