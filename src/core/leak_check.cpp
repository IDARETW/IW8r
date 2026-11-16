// IW8 Radiant - voxel flood-fill leak check (see leak_check.h).
#include "leak_check.h"

#include <cmath>
#include <cstdint>

#include "iw8_contents.h"

namespace iw8r {
namespace {

constexpr uint8_t kRoot = 7;  // dir marker of a cell on the grid boundary (outside seed)

// Grid of cells. Cell (i,j,k) is centred at origin + (i+0.5, j+0.5, k+0.5) * cell.
struct Grid {
    Vec3 origin;
    double cell = 16;
    size_t nx = 0, ny = 0, nz = 0;

    size_t index(size_t i, size_t j, size_t k) const { return (k * ny + j) * nx + i; }
    Vec3 centre(size_t idx) const {
        const size_t i = idx % nx, j = (idx / nx) % ny, k = idx / (nx * ny);
        return {origin.x + (static_cast<double>(i) + 0.5) * cell, origin.y + (static_cast<double>(j) + 0.5) * cell,
                origin.z + (static_cast<double>(k) + 0.5) * cell};
    }
};

// Directions 0:+x 1:-x 2:+y 3:-y 4:+z 5:-z; the opposite of d is d ^ 1.
bool neighbour(const Grid& g, size_t idx, int d, size_t& out) {
    long long i = static_cast<long long>(idx % g.nx);
    long long j = static_cast<long long>((idx / g.nx) % g.ny);
    long long k = static_cast<long long>(idx / (g.nx * g.ny));
    switch (d) {
    case 0: ++i; break;
    case 1: --i; break;
    case 2: ++j; break;
    case 3: --j; break;
    case 4: ++k; break;
    default: --k; break;
    }
    if (i < 0 || j < 0 || k < 0 || i >= static_cast<long long>(g.nx) || j >= static_cast<long long>(g.ny) ||
        k >= static_cast<long long>(g.nz))
        return false;
    out = g.index(static_cast<size_t>(i), static_cast<size_t>(j), static_cast<size_t>(k));
    return true;
}

bool insideBrush(const ConvexBrush& b, const Vec3& p) {
    for (const BrushFaceSpec& f : b)
        if (dot(f.plane.n, p) - f.plane.d > 1e-9) return false;
    return true;
}

}  // namespace

bool isBrushEntityClass(const std::string& classname) { return classname == "worldspawn" || classname == "func_group"; }

std::vector<SolidBrush> collidingBrushes(const MapFile& map, bool includeDetail) {
    std::vector<SolidBrush> out;
    for (size_t e = 0; e < map.entities.size(); ++e) {
        const Entity& ent = map.entities[e];
        if (!isBrushEntityClass(ent.classname())) continue;
        for (const EntityItem& item : ent.items) {
            const Primitive* p = item.prim();
            if (!p || p->kind != PrimKind::Brush) continue;
            if (!brushContents(*p).collides) continue;
            const bool detail = brushIsDetail(*p);
            if (detail && !includeDetail) continue;
            ConvexBrush cb = brushFromPrimitive(*p);
            if (!isValidBrush(cb)) continue;
            SolidBrush s;
            s.entity = e;
            s.bounds = brushBounds(cb);
            s.planes = std::move(cb);
            s.detail = detail;
            out.push_back(std::move(s));
        }
    }
    return out;
}

LeakResult checkLeak(const MapFile& map, double cell, size_t maxCells) {
    LeakResult r;
    r.sealed = false;
    if (!(cell > 0) || !std::isfinite(cell)) {
        r.error = "cell size must be a positive finite number";
        return r;
    }
    const std::vector<SolidBrush> brushes = collidingBrushes(map, false);
    AABB bounds;
    for (const SolidBrush& b : brushes)
        if (b.bounds.valid()) bounds.add(b.bounds);
    if (brushes.empty() || !bounds.valid()) {
        r.error = "no sealing brushes in worldspawn or func_group";
        return r;
    }

    // Grid: brush bounds padded by two cells on every side.
    Grid g;
    g.cell = cell;
    g.origin = bounds.mins - Vec3{2 * cell, 2 * cell, 2 * cell};
    const Vec3 span = bounds.maxs - bounds.mins + Vec3{4 * cell, 4 * cell, 4 * cell};
    size_t dims[3];
    for (int a = 0; a < 3; ++a) {
        const double c = std::ceil(span[a] / cell);
        dims[a] = c < 1 ? 1 : static_cast<size_t>(c);
    }
    const double total = static_cast<double>(dims[0]) * static_cast<double>(dims[1]) * static_cast<double>(dims[2]);
    if (!(total <= static_cast<double>(maxCells))) {
        r.error = "grid of " + std::to_string(static_cast<unsigned long long>(total)) + " cells exceeds the limit of " +
                  std::to_string(maxCells) + " (use a larger cell size)";
        return r;
    }
    g.nx = dims[0];
    g.ny = dims[1];
    g.nz = dims[2];
    const size_t n = g.nx * g.ny * g.nz;
    r.cells = n;

    // Solid cells: centre inside a sealing brush. Only the cells in each brush's bounding box are tested.
    std::vector<uint8_t> solid(n, 0);
    for (const SolidBrush& b : brushes) {
        size_t lo[3], hi[3];
        bool empty = false;
        for (int a = 0; a < 3 && !empty; ++a) {
            double kmin = std::ceil((b.bounds.mins[a] - g.origin[a]) / cell - 0.5);
            double kmax = std::floor((b.bounds.maxs[a] - g.origin[a]) / cell - 0.5);
            if (kmin < 0) kmin = 0;
            if (kmax > static_cast<double>(dims[a]) - 1) kmax = static_cast<double>(dims[a]) - 1;
            if (kmin > kmax) {
                empty = true;
                break;
            }
            lo[a] = static_cast<size_t>(kmin);
            hi[a] = static_cast<size_t>(kmax);
        }
        if (empty) continue;
        for (size_t k = lo[2]; k <= hi[2]; ++k)
            for (size_t j = lo[1]; j <= hi[1]; ++j)
                for (size_t i = lo[0]; i <= hi[0]; ++i) {
                    const size_t idx = g.index(i, j, k);
                    if (solid[idx]) continue;
                    if (insideBrush(b.planes, g.centre(idx))) {
                        solid[idx] = 1;
                        ++r.solidCells;
                    }
                }
    }

    // Outside flood fill from every free boundary cell. dir[c] = d + 1 where d is the step from the parent
    // to c; kRoot marks a seed. The step from c back to its parent is d ^ 1.
    std::vector<uint8_t> dir(n, 0);
    std::vector<uint32_t> queue;
    for (size_t k = 0; k < g.nz; ++k)
        for (size_t j = 0; j < g.ny; ++j)
            for (size_t i = 0; i < g.nx; ++i) {
                if (!(i == 0 || j == 0 || k == 0 || i == g.nx - 1 || j == g.ny - 1 || k == g.nz - 1)) continue;
                const size_t idx = g.index(i, j, k);
                if (solid[idx]) continue;
                dir[idx] = kRoot;
                queue.push_back(static_cast<uint32_t>(idx));
            }
    for (size_t head = 0; head < queue.size(); ++head) {
        const size_t cur = queue[head];
        for (int d = 0; d < 6; ++d) {
            size_t nb;
            if (!neighbour(g, cur, d, nb)) continue;
            if (solid[nb] || dir[nb]) continue;
            dir[nb] = static_cast<uint8_t>(d + 1);
            queue.push_back(static_cast<uint32_t>(nb));
        }
    }

    // Point entities: the first one in an outside cell is the leak.
    for (size_t e = 0; e < map.entities.size(); ++e) {
        const Entity& ent = map.entities[e];
        const std::string cls = ent.classname();
        if (isBrushEntityClass(cls)) continue;
        const std::optional<Vec3> o = ent.origin();
        if (!o) continue;
        size_t c[3];
        bool inGrid = true;
        for (int a = 0; a < 3 && inGrid; ++a) {
            const double f = std::floor(((*o)[a] - g.origin[a]) / cell);
            if (f < 0 || f >= static_cast<double>(dims[a])) {
                inGrid = false;
                break;
            }
            c[a] = static_cast<size_t>(f);
        }
        if (!inGrid) continue;
        const size_t idx = g.index(c[0], c[1], c[2]);
        if (solid[idx] || !dir[idx]) continue;

        // Leak: origin, the cell centres down the BFS tree to the seed, then one cell beyond the grid.
        r.leakingEntity = e;
        r.classname = cls;
        r.path.push_back(*o);
        size_t cur = idx;
        while (true) {
            r.path.push_back(g.centre(cur));
            if (dir[cur] == kRoot) break;
            size_t parent;
            neighbour(g, cur, (dir[cur] - 1) ^ 1, parent);
            cur = parent;
        }
        const size_t si = cur % g.nx, sj = (cur / g.nx) % g.ny, sk = cur / (g.nx * g.ny);
        Vec3 out{0, 0, 0};
        if (si == 0) out.x = -1;
        else if (si == g.nx - 1) out.x = 1;
        else if (sj == 0) out.y = -1;
        else if (sj == g.ny - 1) out.y = 1;
        else if (sk == 0) out.z = -1;
        else out.z = 1;
        r.path.push_back(r.path.back() + out * cell);
        r.sealed = false;
        r.error.clear();
        return r;
    }

    r.sealed = true;
    return r;
}

}  // namespace iw8r
