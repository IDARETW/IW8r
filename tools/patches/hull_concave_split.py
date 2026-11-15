import io

# Map-agent report 2026-11-14 ("Door frame collision fills the opening"): a placed door frame is one connected mesh,
# so its one convex hull spanned the doorway and players stopped at the frame. Each connected part now keeps its
# triangles. A part whose surface leaves a large notch (more than 35 % of its extent empty on the plane of its two
# largest axes; the triangles rasterised by sampling onto a 24-cell grid) is cut where the cut leaves the least empty
# space: every grid line along both axes is tried, the cost of a cut being the summed bounding areas of the connected
# occupied groups on its two sides. The triangles go to a side by their centroid, and each side is split again into its
# connected parts and cut again, a few levels deep. A door frame becomes its two jambs and its header and the doorway
# stays open; a box, a crate or a wall stays one piece (its surface fills its extent).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


C = 'src/core/convex_hull.cpp'
patch(C, '''double boxGap(const AABB& a, const AABB& b) {''', '''using Tri = std::array<Vec3, 3>;

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
void splitConcave(std::vector<Tri> tris, int depth, std::vector<Piece>& out) {
    AABB box;
    for (const Tri& T : tris)
        for (const Vec3& v : T) box.add(v);
    const double big = std::max({box.maxs.x - box.mins.x, box.maxs.y - box.mins.y, box.maxs.z - box.mins.z});
    if (depth <= 0 || tris.size() < 4 || !(big >= 16.0)) return pieceFromTris(tris, out);
    const PieceRaster r = rasterTris(tris, box, 24);
    if (r.groups > 1) {  // apart on this plane: each group on its own
        std::vector<std::vector<Tri>> parts(size_t(r.groups));
        for (size_t t = 0; t < tris.size(); ++t) {
            const int g = r.group[size_t(r.triCell[t])];
            parts[size_t(std::max(0, g))].push_back(tris[t]);
        }
        for (auto& p : parts)
            if (!p.empty()) splitConcave(std::move(p), depth, out);
        return;
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
    splitConcave(std::move(lo), depth - 1, out);
    splitConcave(std::move(hi), depth - 1, out);
}

double boxGap(const AABB& a, const AABB& b) {''')
# the components keep their triangles
patch(C, '''        std::vector<char> used(wp.size(), 0);
        for (size_t t = 0; t + 2 < mesh.idx.size(); t += 3) {
            const uint32_t i0 = mesh.idx[t], i1 = mesh.idx[t + 1], i2 = mesh.idx[t + 2];
            if (i0 >= mesh.pos.size() || i1 >= mesh.pos.size() || i2 >= mesh.pos.size()) continue;
            const size_t a = remap[i0], b = remap[i1], c = remap[i2];
            used[a] = used[b] = used[c] = 1;
            unite(a, b);
            unite(a, c);
        }''', '''        std::vector<char> used(wp.size(), 0);
        std::vector<std::array<size_t, 3>> welded;  // the triangles on the welded points
        for (size_t t = 0; t + 2 < mesh.idx.size(); t += 3) {
            const uint32_t i0 = mesh.idx[t], i1 = mesh.idx[t + 1], i2 = mesh.idx[t + 2];
            if (i0 >= mesh.pos.size() || i1 >= mesh.pos.size() || i2 >= mesh.pos.size()) continue;
            const size_t a = remap[i0], b = remap[i1], c = remap[i2];
            used[a] = used[b] = used[c] = 1;
            unite(a, b);
            unite(a, c);
            welded.push_back({a, b, c});
        }''')
patch(C, '''        for (Piece& pc : local) pieces.push_back(std::move(pc));
    }''', '''        // Each part's triangles; a notched part (a door frame, an arch) is split so its hulls leave the opening free.
        std::vector<std::vector<Tri>> partTris(local.size());
        for (const auto& w : welded)
            if (const auto it = pieceOf.find(find(w[0])); it != pieceOf.end())
                partTris[it->second].push_back(Tri{wp[w[0]], wp[w[1]], wp[w[2]]});
        for (size_t i = 0; i < local.size(); ++i) {
            if (partTris[i].empty()) {
                pieces.push_back(std::move(local[i]));
                continue;
            }
            splitConcave(std::move(partTris[i]), 4, pieces);
        }
    }''')
# Over the budget the smallest piece merges into the piece whose box grows least (summed extents; a flat piece counts),
# no longer the nearest box: a door's header trim joins the header, not a jamb, and the doorway stays open.
patch(C, '''    // Too many pieces: the smallest merges into the piece with the nearest bounds.
    while (pieces.size() > cap) {
        Piece small = std::move(pieces.back());
        pieces.pop_back();
        size_t best = 0;
        double bestGap = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < pieces.size(); ++i) {
            const double g = boxGap(small.box, pieces[i].box);
            if (g < bestGap) bestGap = g, best = i;
        }''', '''    // Too many pieces: the smallest merges into the piece whose bounds grow least (summed extents, so flat pieces count),
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
        }''')
print('ok')
