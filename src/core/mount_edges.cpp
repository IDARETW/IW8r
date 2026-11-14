#include "mount_edges.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>
#include <unordered_map>

namespace iw8r {

namespace {

constexpr double kOnPlane = 0.02;   // an end point this close to a plane lies on it
constexpr double kInside = 0.1;     // a probe this deep behind every plane of a solid is inside it
constexpr double kUpFace = 0.7;     // a face with normal z above this is a top face
constexpr double kWallFace = 0.26;  // a face with |normal z| below this is a wall (within 15 degrees of upright)
constexpr double kCellSize = 256;   // the grid the solids are sorted into for the probes

bool inside(const MountSolid& s, const Vec3& p) {
    if (!s.bounds.contains(p)) return false;
    for (const Plane& pl : s.planes)
        if (dot(pl.n, p) - pl.d > -kInside) return false;
    return true;
}

// The solids by grid cell (x, y), for point probes.
class SolidGrid {
public:
    explicit SolidGrid(const std::vector<MountSolid>& solids) : solids_(solids) {
        for (size_t i = 0; i < solids.size(); ++i) {
            const AABB& b = solids[i].bounds;
            if (!b.valid()) continue;
            for (long long x = cell(b.mins.x); x <= cell(b.maxs.x); ++x)
                for (long long y = cell(b.mins.y); y <= cell(b.maxs.y); ++y) cells_[key(x, y)].push_back(uint32_t(i));
        }
    }
    // Is `p` inside a solid other than `self`?
    bool solid(const Vec3& p, size_t self) const {
        const auto it = cells_.find(key(cell(p.x), cell(p.y)));
        if (it == cells_.end()) return false;
        for (const uint32_t i : it->second)
            if (i != self && inside(solids_[i], p)) return true;
        return false;
    }

private:
    static long long cell(double v) { return static_cast<long long>(std::floor(v / kCellSize)); }
    static uint64_t key(long long x, long long y) { return (uint64_t(uint32_t(x)) << 32) | uint32_t(y); }
    const std::vector<MountSolid>& solids_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells_;
};

// One line in space with its two faces: the open parts found on it, as intervals along `dir`.
struct Line {
    Vec3 dir, origin;    // origin: the line's point nearest the world origin
    Vec3 first, second;  // the faces' outward normals (a ledge: top, wall)
    bool ledge = false;
    bool model = false;  // every part comes from a placed model's hull
    std::vector<std::pair<double, double>> parts;
};

using LineKey = std::tuple<long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, bool>;

long long q(double v, double scale) { return std::llround(v * scale); }

}  // namespace

std::vector<MapEdge> findMountEdges(const std::vector<MountSolid>& solids, const MountEdgeOptions& opt, MountEdgeStats* stats) {
    MountEdgeStats st;
    const SolidGrid grid(solids);
    const double step = std::max(1.0, opt.step);
    std::map<LineKey, Line> lines;

    for (size_t si = 0; si < solids.size(); ++si) {
        const MountSolid& s = solids[si];
        if (!s.source || s.planes.size() < 4) continue;
        const std::vector<Winding> wins = buildBrushWindings(s.planes);
        for (size_t i = 0; i < wins.size(); ++i) {
            const Winding& w = wins[i];
            for (size_t k = 0; k < w.size(); ++k) {
                const Vec3 a = w[k], b = w[(k + 1) % w.size()];
                const double len = length(b - a);
                if (len < 1.0) continue;
                // The other face at this edge: both end points on its plane. Each pair of faces once.
                size_t j = wins.size();
                for (size_t c = 0; c < s.planes.size() && c < wins.size(); ++c) {
                    if (c == i || wins[c].empty()) continue;
                    const Plane& pl = s.planes[c];
                    if (std::fabs(dot(pl.n, a) - pl.d) < kOnPlane && std::fabs(dot(pl.n, b) - pl.d) < kOnPlane &&
                        std::fabs(dot(pl.n, s.planes[i].n)) < 0.999) {
                        j = c;
                        break;
                    }
                }
                if (j >= wins.size() || j < i) continue;
                Vec3 n1 = s.planes[i].n, n2 = s.planes[j].n;
                Vec3 dir = (b - a) * (1.0 / len);
                bool ledge = false;
                if (std::fabs(dir.z) > 0.966 && std::fabs(n1.z) < kWallFace && std::fabs(n2.z) < kWallFace) {
                    // A wall corner. Corners that are nearly flat or needle sharp are no cover.
                    const double c = dot(n1, n2);
                    if (c > (s.model ? 0.5 : 0.71) || c < -0.71) continue;
                } else if (std::fabs(dir.z) < kWallFace && ((n1.z > kUpFace && std::fabs(n2.z) < kWallFace) || (n2.z > kUpFace && std::fabs(n1.z) < kWallFace))) {
                    ledge = true;
                    if (n2.z > kUpFace) std::swap(n1, n2);  // first: the top face
                } else {
                    continue;
                }
                // The open parts: samples along the edge, each probed in front of both faces and at the corner.
                const Vec3 out = normalize(n1 + n2);
                const int count = std::max(1, int(std::ceil(len / step - 1e-9)));
                const double piece = len / count;
                std::vector<std::pair<double, double>> parts;
                for (int t = 0; t < count; ++t) {
                    const Vec3 p = a + dir * ((t + 0.5) * piece);
                    bool open = !grid.solid(p + out * 3.0, si) && !grid.solid(p + n1 * 3.0 - n2 * 1.5, si) && !grid.solid(p + n2 * 3.0 - n1 * 1.5, si);
                    if (open && ledge) {
                        const Vec3 up{0, 0, 1};
                        // Room for the weapon over the edge, and room to stand in front of the wall below it.
                        open = !grid.solid(p + up * 20.0 + n2 * 2.0, si) && !grid.solid(p + up * 20.0 - n2 * 2.0, si) &&
                               !grid.solid(p + n2 * 16.0 - up * 8.0, si);
                    } else if (open) {
                        open = !grid.solid(p + out * 16.0, si);
                    }
                    if (!open) continue;
                    const double from = t * piece, to = (t + 1) * piece;
                    if (!parts.empty() && std::fabs(parts.back().second - from) < 1e-6) parts.back().second = to;
                    else parts.push_back({from, to});
                }
                if (parts.empty()) {
                    ++st.hidden;
                    continue;
                }
                // The line's canonical direction: its largest part positive, so both ways of walking it meet.
                Vec3 d = dir;
                const double ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
                const double lead = az >= ax && az >= ay ? d.z : (ax >= ay ? d.x : d.y);
                const bool flip = lead < 0;
                if (flip) d = d * -1.0;
                if (!ledge) {
                    // Wall corners: the two walls in a fixed order.
                    const auto lower = [](const Vec3& u, const Vec3& v) { return std::make_tuple(q(u.x, 1000), q(u.y, 1000), q(u.z, 1000)) < std::make_tuple(q(v.x, 1000), q(v.y, 1000), q(v.z, 1000)); };
                    if (lower(n2, n1)) std::swap(n1, n2);
                }
                const Vec3 origin = a - d * dot(a, d);
                const LineKey key{q(d.x, 1000), q(d.y, 1000), q(d.z, 1000), q(origin.x, 8), q(origin.y, 8), q(origin.z, 8),
                                  q(n1.x, 1000), q(n1.y, 1000), q(n1.z, 1000), q(n2.x, 1000), q(n2.y, 1000), q(n2.z, 1000), ledge};
                Line& line = lines[key];
                if (line.parts.empty()) line.dir = d, line.origin = origin, line.first = n1, line.second = n2, line.ledge = ledge, line.model = s.model;
                line.model = line.model && s.model;
                const double base = dot(a, d);
                for (const auto& [from, to] : parts) {
                    const double u = base + (flip ? -to : from), v = base + (flip ? -from : to);
                    line.parts.push_back({u, v});
                }
            }
        }
    }

    struct Found {
        MapEdge edge;
        double length;
        bool ledge, model;
    };
    std::vector<Found> found;
    for (auto& [key, line] : lines) {
        std::sort(line.parts.begin(), line.parts.end());
        std::vector<std::pair<double, double>> joined;
        for (const auto& part : line.parts) {
            if (!joined.empty() && part.first <= joined.back().second + 0.5) joined.back().second = std::max(joined.back().second, part.second);
            else joined.push_back(part);
        }
        for (const auto& [from, to] : joined) {
            if (to - from < (line.ledge ? opt.minLedge : opt.minCorner) - 1e-6) {
                ++st.short_;
                continue;
            }
            const Vec3 a = line.origin + line.dir * from, b = line.origin + line.dir * to;
            const double av[3] = {a.x, a.y, a.z}, bv[3] = {b.x, b.y, b.z};
            const double n1[3] = {line.first.x, line.first.y, line.first.z}, n2[3] = {line.second.x, line.second.y, line.second.z};
            MapEdge e;
            // A ledge keeps its top face as the adjacent face (stock: angle 0); a corner takes the faces as they fit.
            if (!makeMapEdge(av, bv, n1, n2, kEdgeFlagMount, e, line.ledge)) continue;
            found.push_back({e, to - from, line.ledge, line.model});
        }
    }
    if (found.size() > opt.maxEdges) {
        std::stable_sort(found.begin(), found.end(), [](const Found& x, const Found& y) { return x.length > y.length; });
        st.dropped = found.size() - opt.maxEdges;
        found.resize(opt.maxEdges);
    }
    std::vector<MapEdge> out;
    out.reserve(found.size());
    for (const Found& f : found) {
        (f.ledge ? st.ledges : st.corners)++;
        if (f.model) ++st.fromModels;
        out.push_back(f.edge);
    }
    if (stats) *stats = st;
    return out;
}

std::string mountEdgesJson(const std::vector<MapEdge>& edges) {
    std::string json = "[";
    char buf[320];
    for (size_t i = 0; i < edges.size(); ++i) {
        const MapEdge& e = edges[i];
        std::snprintf(buf, sizeof buf, "%s[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u]", i ? "," : "", double(e.a[0]), double(e.a[1]), double(e.a[2]),
                      double(e.b[0]), double(e.b[1]), double(e.b[2]), double(e.adjacentNormal), unsigned(e.openAngle), unsigned(e.flags));
        json += buf;
    }
    return json + "]";
}

}  // namespace iw8r
