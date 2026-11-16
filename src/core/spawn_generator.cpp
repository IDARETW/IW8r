// IW8 Radiant - generated starting spawns from a map's walkable geometry (see spawn_generator.h).
#include "spawn_generator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kInf = std::numeric_limits<double>::infinity();
// Team starts of one side keep this 3D spacing (the spawn_analysis guideline).
constexpr double kTeamStartSpacing = 128.0;
// The sky trace starts this far above area.maxs.z; surfaces above that are not seen.
constexpr double kSkyMargin = 64.0;
// Floors of one column closer than this in Z are the same surface seen through two brushes.
constexpr double kFloorMergeEps = 0.5;
// Tolerance for the vertical column test.
constexpr double kColumnEps = 1e-6;

struct Floor {
    double z = 0;
    Vec3 normal;
};

// The top surface of a convex solid over the vertical line (x, y). False when the line misses the solid.
// A solid's top is the z where its upward-facing planes bind, so every storey is found without tracing through brushes
// (a trace that starts inside a brush does not report where it leaves).
bool columnTop(const CollisionWorld::Solid& s, double x, double y, double& zTop, Vec3& normal) {
    double zLo = -kInf, zHi = kInf;
    bool hasTop = false;
    for (const Plane& p : s.planes) {
        const double rhs = p.d - p.n.x * x - p.n.y * y;  // the solid needs p.n.z * z <= rhs
        if (std::fabs(p.n.z) < 1e-9) {
            if (rhs < -kColumnEps) return false;  // the column is outside this vertical plane
            continue;
        }
        const double bound = rhs / p.n.z;
        if (p.n.z > 0) {
            if (bound < zHi) {
                zHi = bound;
                normal = p.n;
                hasTop = true;
            }
        } else if (bound > zLo) {
            zLo = bound;
        }
    }
    if (!hasTop || zLo > zHi) return false;
    zTop = zHi;
    normal = normalize(normal);
    return true;
}

// Cell index range [a, b] (clamped to [0, n-1]) whose centres can fall inside [lo, hi] along one axis.
// Cell i has its centre at origin + (i + 0.5) * step.
void cellRange(double lo, double hi, double origin, double step, int n, int& a, int& b) {
    if (step <= 0) {
        a = b = 0;
        return;
    }
    a = int(std::clamp(std::floor((lo - origin) / step - 0.5), 0.0, double(n - 1)));
    b = int(std::clamp(std::ceil((hi - origin) / step - 0.5), 0.0, double(n - 1)));
}

double dist3(const Vec3& a, const Vec3& b) { return length(a - b); }

std::string intText(double v) { return std::to_string(int(std::lround(v))); }

// Numbers for the entity text: rounded to 1/1000, no trailing zeros, no negative zero.
std::string numText(double v) {
    double r = std::round(v * 1000.0) / 1000.0;
    if (r == 0) r = 0;
    return formatNumber(r);
}

}  // namespace

SpawnGenResult generateSpawns(const CollisionWorld& world, const AABB& area, const SpawnGenOptions& opt) {
    SpawnGenResult res;
    if (!area.valid()) {
        res.warnings.push_back("spawn generation: the area is empty");
        return res;
    }
    const double cell = opt.cellSize > 0 ? opt.cellSize : 64.0;
    const double r = std::max(0.0, opt.playerRadius);
    const double h = opt.playerHeight;
    const double cosLimit = std::cos(std::clamp(opt.maxFloorSlopeDeg, 0.0, 90.0) * kDegToRad);
    const double top = area.maxs.z + kSkyMargin;

    // Sample grid: cell centres spread evenly over the area.
    const double width = area.maxs.x - area.mins.x, depth = area.maxs.y - area.mins.y;
    const int nx = std::max(1, int(std::ceil(width / cell - 1e-9)));
    const int ny = std::max(1, int(std::ceil(depth / cell - 1e-9)));
    const double sx = width / nx, sy = depth / ny;
    auto cellX = [&](int i) { return area.mins.x + (i + 0.5) * sx; };
    auto cellY = [&](int j) { return area.mins.y + (j + 0.5) * sy; };

    // Floors per column: the top of every solid that covers the column and lies below the sky trace start.
    std::vector<std::vector<Floor>> columns(size_t(nx) * size_t(ny));
    for (const CollisionWorld::Solid& s : world.solids()) {
        if (s.bounds.maxs.x < area.mins.x || s.bounds.mins.x > area.maxs.x || s.bounds.maxs.y < area.mins.y ||
            s.bounds.mins.y > area.maxs.y)
            continue;
        int i0 = 0, i1 = 0, j0 = 0, j1 = 0;
        cellRange(s.bounds.mins.x, s.bounds.maxs.x, area.mins.x, sx, nx, i0, i1);
        cellRange(s.bounds.mins.y, s.bounds.maxs.y, area.mins.y, sy, ny, j0, j1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                double zTop = 0;
                Vec3 n;
                if (columnTop(s, cellX(i), cellY(j), zTop, n) && zTop <= top)
                    columns[size_t(j) * size_t(nx) + size_t(i)].push_back({zTop, n});
            }
    }

    // Walkable points: a walkable floor with the player box (radius, height) clear above it. The box starts 1 unit
    // above the floor, so the floor itself does not count as an obstacle.
    std::vector<Vec3> cands;
    const Vec3 headMins{-r, -r, 1.0}, headMaxs{r, r, h};
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            std::vector<Floor>& fl = columns[size_t(j) * size_t(nx) + size_t(i)];
            std::sort(fl.begin(), fl.end(), [](const Floor& a, const Floor& b) { return a.z < b.z; });
            double last = -kInf;
            for (const Floor& f : fl) {
                if (f.z - last < kFloorMergeEps) continue;
                last = f.z;
                if (f.normal.z < cosLimit) continue;
                const Vec3 p{cellX(i), cellY(j), f.z};
                if (world.traceBox(p, p, headMins, headMaxs).startSolid) continue;
                cands.push_back(p);
            }
        }
    res.candidates = cands.size();
    if (cands.empty()) {
        res.warnings.push_back("no walkable spawn points in the area (needs " + intText(h) + " units of headroom, " +
                               intText(r) + " units of side clearance, floors within " +
                               intText(opt.maxFloorSlopeDeg) + " degrees of level)");
        return res;
    }

    AABB bounds;
    for (const Vec3& p : cands) bounds.add(p);
    const Vec3 mid = bounds.center();
    auto yawToward = [&](const Vec3& p) {
        double deg = std::atan2(mid.y - p.y, mid.x - p.x) / kDegToRad;
        if (deg < 0) deg += 360.0;
        return deg;
    };

    // Free-for-all points: farthest-point sampling, starting from the candidate nearest the area centre.
    std::vector<size_t> ffa;
    if (opt.ffaCount > 0) {
        const double ax = area.center().x, ay = area.center().y;
        size_t start = 0;
        double best = kInf;
        for (size_t k = 0; k < cands.size(); ++k) {
            const double d = std::hypot(cands[k].x - ax, cands[k].y - ay);
            if (d < best) {
                best = d;
                start = k;
            }
        }
        std::vector<double> nearest(cands.size(), kInf);  // distance to the nearest chosen point
        std::vector<char> used(cands.size(), 0);
        auto take = [&](size_t k) {
            used[k] = 1;
            ffa.push_back(k);
            for (size_t m = 0; m < cands.size(); ++m) nearest[m] = std::min(nearest[m], dist3(cands[m], cands[k]));
        };
        take(start);
        while (int(ffa.size()) < opt.ffaCount) {
            size_t next = cands.size();
            double far = -1;
            for (size_t m = 0; m < cands.size(); ++m)
                if (!used[m] && nearest[m] > far) {
                    far = nearest[m];
                    next = m;
                }
            if (next == cands.size() || far < opt.minSpacing) break;
            take(next);
        }
        if (int(ffa.size()) < opt.ffaCount)
            res.warnings.push_back("only " + std::to_string(ffa.size()) + " walkable points far enough apart (" +
                                   std::to_string(opt.ffaCount) + " asked)");
    }

    // Team starts: split along the longer side of the candidates; allies low, axis high.
    const char* sideNames[2] = {"allies", "axis"};
    std::vector<size_t> picked[2];  // 0 allies, 1 axis
    if (opt.teamStartsPerSide > 0) {
        const bool alongX = bounds.maxs.x - bounds.mins.x >= bounds.maxs.y - bounds.mins.y;
        const double lo = alongX ? bounds.mins.x : bounds.mins.y;
        const double hi = alongX ? bounds.maxs.x : bounds.maxs.y;
        const double split = 0.5 * (lo + hi);
        std::vector<size_t> side[2];
        for (size_t k = 0; k < cands.size(); ++k) {
            const double c = alongX ? cands[k].x : cands[k].y;
            side[c < split ? 0 : 1].push_back(k);
        }
        for (int s = 0; s < 2; ++s) {
            // The far end of this side: its outer edge along the split axis, centred across it.
            const double fx = alongX ? (s == 0 ? lo : hi) : mid.x;
            const double fy = alongX ? mid.y : (s == 0 ? lo : hi);
            std::vector<size_t>& group = side[s];
            if (group.empty()) {
                res.warnings.push_back(std::string("no walkable points on the ") + sideNames[s] + " half of the area");
                continue;
            }
            std::sort(group.begin(), group.end(), [&](size_t a, size_t b) {
                const double da = std::hypot(cands[a].x - fx, cands[a].y - fy);
                const double db = std::hypot(cands[b].x - fx, cands[b].y - fy);
                return da != db ? da < db : a < b;
            });
            for (size_t k : group) {
                if (int(picked[s].size()) >= opt.teamStartsPerSide) break;
                bool ok = true;
                for (int o = 0; o < 2 && ok; ++o)
                    for (size_t q : picked[o])
                        if (dist3(cands[k], cands[q]) < kTeamStartSpacing) {
                            ok = false;
                            break;
                        }
                if (ok) picked[s].push_back(k);
            }
            if (int(picked[s].size()) < opt.teamStartsPerSide)
                res.warnings.push_back("only " + std::to_string(picked[s].size()) + " " + sideNames[s] +
                                       " start points far enough apart (" + std::to_string(opt.teamStartsPerSide) +
                                       " asked)");
        }
    }

    // Output: the FFA set for dm, tdm and dom, then the team starts for tdm and dom.
    for (const char* cls : {"mp_dm_spawn", "mp_tdm_spawn", "mp_dom_spawn"})
        for (size_t k : ffa) res.spawns.push_back({cls, cands[k], yawToward(cands[k])});
    for (const char* mode : {"tdm", "dom"})
        for (int s = 0; s < 2; ++s)
            for (size_t k : picked[s])
                res.spawns.push_back({std::string("mp_") + mode + "_spawn_" + sideNames[s] + "_start", cands[k],
                                      yawToward(cands[k])});
    return res;
}

std::string spawnEntityText(const GeneratedSpawn& s) {
    return "{\n\"classname\" \"" + s.classname + "\"\n\"origin\" \"" + numText(s.origin.x) + " " + numText(s.origin.y) +
           " " + numText(s.origin.z) + "\"\n\"angles\" \"0 " + numText(s.yaw) + " 0\"\n}\n";
}

}  // namespace iw8r
