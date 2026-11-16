// IW8 Radiant - roads, paths and rivers along splines that conform to the terrain (roadmap 8.13).
// See road_tools.h for the terrain layout and the meaning of each argument.
#include "road_tools.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>

#include "document.h"
#include "map_text.h"
#include "patch_ops2.h"

namespace iw8r {

namespace {

constexpr int kMaxCrossSegments = 64;

bool finite(double v) { return std::isfinite(v); }

bool validGrid(const PatchGrid& g) {
    return g.rows >= 2 && g.cols >= 2 && g.pos.size() == static_cast<size_t>(g.rows) * static_cast<size_t>(g.cols);
}

size_t at(const PatchGrid& g, int r, int c) {
    return static_cast<size_t>(r) * static_cast<size_t>(g.cols) + static_cast<size_t>(c);
}

bool validMaterial(const std::string& m) {
    if (m.empty()) return false;
    for (char ch : m)
        if (ch == '"' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') return false;
    return true;
}

bool finitePath(const std::vector<Vec3>& path) {
    for (const Vec3& p : path)
        if (!finite(p.x) || !finite(p.y) || !finite(p.z)) return false;
    return true;
}

// Column x values and row y values when the control grid is a regular lattice (every point sits on its column's x
// and its row's y, and both axes strictly increase). False for any other grid.
bool regularAxes(const PatchGrid& g, std::vector<double>& xs, std::vector<double>& ys) {
    xs.assign(static_cast<size_t>(g.cols), 0.0);
    ys.assign(static_cast<size_t>(g.rows), 0.0);
    for (int c = 0; c < g.cols; ++c) xs[static_cast<size_t>(c)] = g.pos[at(g, 0, c)].x;
    for (int r = 0; r < g.rows; ++r) ys[static_cast<size_t>(r)] = g.pos[at(g, r, 0)].y;
    const double ext = std::max({std::fabs(xs.back() - xs.front()), std::fabs(ys.back() - ys.front()), 1.0});
    const double tol = 1e-6 * ext;
    for (int r = 0; r < g.rows; ++r)
        for (int c = 0; c < g.cols; ++c) {
            const Vec3& p = g.pos[at(g, r, c)];
            if (std::fabs(p.x - xs[static_cast<size_t>(c)]) > tol || std::fabs(p.y - ys[static_cast<size_t>(r)]) > tol)
                return false;
        }
    for (size_t c = 1; c < xs.size(); ++c)
        if (!(xs[c] > xs[c - 1])) return false;
    for (size_t r = 1; r < ys.size(); ++r)
        if (!(ys[r] > ys[r - 1])) return false;
    return true;
}

// Bilinear height of a regular lattice. False when (x, y) is outside the grid.
bool bilinear(const PatchGrid& g, const std::vector<double>& xs, const std::vector<double>& ys, double x, double y,
              double& z) {
    if (x < xs.front() || x > xs.back() || y < ys.front() || y > ys.back()) return false;
    size_t c = static_cast<size_t>(std::upper_bound(xs.begin(), xs.end(), x) - xs.begin());
    c = c == 0 ? 0 : c - 1;
    if (c > xs.size() - 2) c = xs.size() - 2;
    size_t r = static_cast<size_t>(std::upper_bound(ys.begin(), ys.end(), y) - ys.begin());
    r = r == 0 ? 0 : r - 1;
    if (r > ys.size() - 2) r = ys.size() - 2;
    const double tx = (x - xs[c]) / (xs[c + 1] - xs[c]);
    const double ty = (y - ys[r]) / (ys[r + 1] - ys[r]);
    const int ri = static_cast<int>(r), ci = static_cast<int>(c);
    const double z00 = g.pos[at(g, ri, ci)].z, z01 = g.pos[at(g, ri, ci + 1)].z;
    const double z10 = g.pos[at(g, ri + 1, ci)].z, z11 = g.pos[at(g, ri + 1, ci + 1)].z;
    z = (1 - ty) * ((1 - tx) * z00 + tx * z01) + ty * ((1 - tx) * z10 + tx * z11);
    return true;
}

// Barycentric test of (x, y) in the XY triangle (a, b, c); on a hit, z is the interpolated height.
bool inTriangle(const Vec3& a, const Vec3& b, const Vec3& c, double x, double y, double& z) {
    const double det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::fabs(det) < 1e-12) return false;
    const double l1 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / det;
    const double l2 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / det;
    const double l3 = 1.0 - l1 - l2;
    const double eps = 1e-9;
    if (l1 < -eps || l2 < -eps || l3 < -eps) return false;
    z = l1 * a.z + l2 * b.z + l3 * c.z;
    return true;
}

// Height from the tessellation triangles of any control grid (cell (r, c) = triangles (a, b, c) and (b, d, c)).
bool triangleHeight(const PatchGrid& g, double x, double y, double& z) {
    for (int r = 0; r + 1 < g.rows; ++r)
        for (int c = 0; c + 1 < g.cols; ++c) {
            const Vec3& a = g.pos[at(g, r, c)];
            const Vec3& b = g.pos[at(g, r, c + 1)];
            const Vec3& cc = g.pos[at(g, r + 1, c)];
            const Vec3& d = g.pos[at(g, r + 1, c + 1)];
            if (inTriangle(a, b, cc, x, y, z)) return true;
            if (inTriangle(b, d, cc, x, y, z)) return true;
        }
    return false;
}

// Height of one grid under (x, y).
bool gridHeight(const PatchGrid& g, double x, double y, double& z) {
    std::vector<double> xs, ys;
    if (regularAxes(g, xs, ys)) return bilinear(g, xs, ys, x, y, z);
    return triangleHeight(g, x, y, z);
}

// Closest point of the polyline to (x, y) in XY: its distance and the path height there.
void nearestOnPath(const std::vector<Vec3>& path, double x, double y, double& dist, double& z) {
    dist = 1e300;
    z = path.front().z;
    for (size_t s = 0; s + 1 < path.size(); ++s) {
        const Vec3& a = path[s];
        const Vec3& b = path[s + 1];
        const double ex = b.x - a.x, ey = b.y - a.y;
        const double len2 = ex * ex + ey * ey;
        double t = len2 > 0 ? ((x - a.x) * ex + (y - a.y) * ey) / len2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double qx = a.x + ex * t, qy = a.y + ey * t;
        const double d = std::hypot(x - qx, y - qy);
        if (d < dist) {
            dist = d;
            z = a.z + (b.z - a.z) * t;
        }
    }
}

// Terrain candidate: a "mesh" patch with a non-zero XY extent (a vertical wall has none, so it is skipped).
struct Candidate {
    Id entity = kNoId;
    Id item = kNoId;
    PatchGrid grid;
};

bool hasXYExtent(const PatchGrid& g) {
    double x0 = g.pos[0].x, x1 = x0, y0 = g.pos[0].y, y1 = y0;
    for (const Vec3& p : g.pos) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
    }
    return x1 > x0 && y1 > y0;
}

}  // namespace

double terrainHeightAt(const std::vector<PatchGrid>& terrains, double x, double y, bool* hit) {
    if (hit) *hit = false;
    if (!finite(x) || !finite(y)) return 0.0;
    for (const PatchGrid& g : terrains) {
        if (!validGrid(g)) continue;
        double z = 0.0;
        if (gridHeight(g, x, y, z)) {
            if (hit) *hit = true;
            return z;
        }
    }
    return 0.0;
}

std::optional<PatchGrid> roadStrip(const std::vector<Vec3>& path, double width, int crossSegments,
                                   const std::vector<PatchGrid>& terrains, double lift, const std::string& material,
                                   double uvLength) {
    if (path.size() < 2 || crossSegments < 1 || crossSegments > kMaxCrossSegments) return std::nullopt;
    if (!finite(width) || width <= 0 || !finite(lift) || !finite(uvLength) || uvLength <= 0) return std::nullopt;
    if (!validMaterial(material) || !finitePath(path)) return std::nullopt;

    const size_t n = path.size();
    // Horizontal direction at each path point: the normalised sum of the neighbouring segment directions.
    std::vector<Vec3> segDir(n - 1);
    for (size_t s = 0; s + 1 < n; ++s) {
        const Vec3 d{path[s + 1].x - path[s].x, path[s + 1].y - path[s].y, 0.0};
        segDir[s] = length(d) > 0 ? normalize(d) : Vec3{};
    }
    std::vector<Vec3> dir(n);
    for (size_t i = 0; i < n; ++i) {
        Vec3 sum{};
        if (i > 0) sum += segDir[i - 1];
        if (i + 1 < n) sum += segDir[i];
        dir[i] = length(sum) > 0 ? normalize(sum) : Vec3{};
    }
    // Points without a direction (a reversal or a repeated point) take the last good one, or the first good one.
    size_t first = n;
    for (size_t i = 0; i < n && first == n; ++i)
        if (length(dir[i]) > 0) first = i;
    if (first == n) return std::nullopt;  // every point is on one spot
    Vec3 last = dir[first];
    for (size_t i = 0; i < n; ++i) {
        if (length(dir[i]) > 0) last = dir[i];
        else dir[i] = last;
    }

    // Centreline heights (terrain + lift) and the 3D distance along them, which drives t.
    std::vector<double> dist(n, 0.0);
    std::vector<double> centreZ(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        bool hit = false;
        const double h = terrainHeightAt(terrains, path[i].x, path[i].y, &hit);
        centreZ[i] = (hit ? h : path[i].z) + lift;
    }
    for (size_t i = 1; i < n; ++i) {
        const Vec3 d{path[i].x - path[i - 1].x, path[i].y - path[i - 1].y, centreZ[i] - centreZ[i - 1]};
        dist[i] = dist[i - 1] + length(d);
    }

    PatchGrid g;
    g.rows = static_cast<int>(n);
    g.cols = crossSegments + 1;
    g.material = material;
    g.keyword = "mesh";
    g.pos.assign(n * static_cast<size_t>(g.cols), Vec3{});
    g.st.assign(g.pos.size(), {0.0, 0.0});
    for (size_t i = 0; i < n; ++i) {
        const Vec3 right{-dir[i].y, dir[i].x, 0.0};
        for (int j = 0; j < g.cols; ++j) {
            const double s = static_cast<double>(j) / crossSegments;
            const double off = (s - 0.5) * width;
            const double x = path[i].x + right.x * off;
            const double y = path[i].y + right.y * off;
            bool hit = false;
            const double h = terrainHeightAt(terrains, x, y, &hit);
            const size_t k = i * static_cast<size_t>(g.cols) + static_cast<size_t>(j);
            g.pos[k] = {x, y, (hit ? h : path[i].z) + lift};
            g.st[k] = {s, dist[i] / uvLength};
        }
    }
    return g;
}

bool carveTerrain(std::vector<PatchGrid>& terrains, const std::vector<Vec3>& path, double width, double falloff,
                  double depth) {
    if (path.size() < 2 || !finitePath(path)) return false;
    if (!finite(width) || width <= 0 || !finite(falloff) || falloff < 0 || !finite(depth)) return false;
    const double half = width * 0.5;
    bool changed = false;
    for (PatchGrid& g : terrains) {
        if (!validGrid(g)) continue;
        for (Vec3& p : g.pos) {
            double d = 0.0, roadZ = 0.0;
            nearestOnPath(path, p.x, p.y, d, roadZ);
            double w = 0.0;
            if (d <= half) {
                w = 1.0;
            } else if (falloff > 0 && d < half + falloff) {
                const double t = (d - half) / falloff;
                w = 1.0 - t * t * (3.0 - 2.0 * t);  // smoothstep, 1 at the road edge, 0 at the band's outer edge
            } else {
                continue;
            }
            const double target = roadZ - depth;
            const double nz = w >= 1.0 ? target : p.z + (target - p.z) * w;
            if (nz != p.z) {
                p.z = nz;
                changed = true;
            }
        }
    }
    return changed;
}

bool addRoadFromSelection(Document& doc, double width, const RoadOptions& opts) {
    // Path: origins of the selected point entities, in entity order.
    std::vector<Vec3> path;
    std::set<Id> seen;
    for (const SelRef& r : doc.selection()) {
        if (r.item != kNoId || !seen.insert(r.entity).second) continue;
        if (const Entity* e = doc.findEntity(r.entity))
            if (auto o = e->origin()) path.push_back(*o);
    }
    if (path.size() < 2) return false;
    Entity* ws = doc.worldspawn();
    if (!ws) return false;

    // Terrains: every mesh patch with an XY extent, or only the selected ones when a patch is selected.
    bool anySelectedPatch = false;
    for (const Entity& e : doc.map().entities)
        for (const EntityItem& it : e.items) {
            const Primitive* p = it.prim();
            if (p && p->kind == PrimKind::Patch && doc.isSelected(SelRef{e.id, it.id})) anySelectedPatch = true;
        }
    std::vector<Candidate> cands;
    for (const Entity& e : doc.map().entities)
        for (const EntityItem& it : e.items) {
            const Primitive* p = it.prim();
            if (!p || p->kind != PrimKind::Patch) continue;
            if (anySelectedPatch && !doc.isSelected(SelRef{e.id, it.id})) continue;
            auto g = patchGrid(*p);
            if (!g || g->keyword != "mesh" || !validGrid(*g) || !hasXYExtent(*g)) continue;
            cands.push_back({e.id, it.id, std::move(*g)});
        }
    if (cands.empty()) return false;

    std::vector<PatchGrid> terrains;
    terrains.reserve(cands.size());
    for (const Candidate& c : cands) terrains.push_back(c.grid);

    std::vector<Document::PrimEdit> edits;
    if (opts.carve) {
        std::vector<PatchGrid> carved = terrains;
        carveTerrain(carved, path, width, opts.falloff, opts.depth);
        for (size_t i = 0; i < cands.size(); ++i) {
            if (carved[i].pos == terrains[i].pos) continue;
            const EntityItem* item = doc.findItem(cands[i].entity, cands[i].item);
            if (!item || !item->prim()) continue;
            Primitive np = *item->prim();
            for (size_t k = 0; k < carved[i].pos.size(); ++k) {
                const double dz = carved[i].pos[k].z - terrains[i].pos[k].z;
                if (dz != 0) movePatchVerts(np, {k}, Vec3{0, 0, dz});
            }
            edits.push_back({cands[i].entity, cands[i].item, std::move(np)});
        }
        terrains = std::move(carved);  // the road is draped onto the carved ground
    }

    auto road = roadStrip(path, width, opts.crossSegments, terrains, opts.lift, opts.material, opts.uvLength);
    if (!road) return false;
    Primitive roadPrim = makePatchPrimitive(*road, doc.eol(), "");
    if (roadPrim.kind != PrimKind::Patch) return false;
    edits.push_back({ws->id, kNoId, std::move(roadPrim)});
    return doc.editPrimitives(edits, "Add Road");
}

}  // namespace iw8r
