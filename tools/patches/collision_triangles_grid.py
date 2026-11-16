import io

# CollisionWorld: addTriangle (a thin convex facet with its bounds taken straight from the corners, so a stock map's
# hundreds of thousands of world triangles add quickly) and a uniform x/y grid over the static solids once there are
# many, so a trace tests the cells it crosses instead of every solid. Movers' solids are always tested. Owner checklist
# 2026-11-15 (C3 / D2): walking a stock map fell back to noclip because its world gave no collision at all.

P = 'E:/IW8-Radiant/src/core/collision_world.cpp'
H = 'E:/IW8-Radiant/src/core/collision_world.h'


def rw(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw(H, [
('''    // Contents mask the player collides with (solid | player clip).
    static constexpr uint32_t kPlayerMask = 0x1u | 0x10000u;
''', '''    // Adds one triangle as a thin facet (its plane, a back plane `thickness` behind it and the three edge planes) that
    // blocks the player: a stock map's world surfaces in the play preview. Bounds come from the corners (no windings),
    // so many triangles add quickly. False for a degenerate triangle.
    bool addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, double thickness = 1.0, uint32_t contents = 1);

    // Contents mask the player collides with (solid | player clip).
    static constexpr uint32_t kPlayerMask = 0x1u | 0x10000u;
'''),
('''    std::vector<Mover> movers_;
    void addBrushesOf(const Entity& e, int patchSubdiv);''', '''    std::vector<Mover> movers_;
    void addBrushesOf(const Entity& e, int patchSubdiv);
    // The broad phase over many solids: x/y cells of kGridCell units holding the static solids that overlap them,
    // built on the first trace after a change (mutable: traces are const). Movers' solids are tested every trace.
    static constexpr double kGridCell = 256.0;
    static constexpr size_t kGridFrom = 2048;  // solids before the grid is used
    mutable std::unordered_map<int64_t, std::vector<uint32_t>> grid_;
    mutable std::vector<uint32_t> gridStamp_;
    mutable uint32_t gridTick_ = 0;
    mutable size_t gridSolids_ = 0;  // solids_.size() when the grid was built
    mutable std::vector<uint32_t> moverSolidList_;
    void buildGrid() const;'''),
])
s = io.open(H, encoding='utf-8', newline='').read()
if '#include <unordered_map>' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('#pragma once' + nl, '#pragma once' + nl + nl + '#include <cstdint>' + nl + '#include <unordered_map>' + nl, 1)
    io.open(H, 'w', encoding='utf-8', newline='').write(s)

rw(P, [
('''void CollisionWorld::build(const MapFile& map, int patchSubdiv) { build(map, patchSubdiv, {}); }''',
 '''bool CollisionWorld::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, double thickness, uint32_t contents) {
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

void CollisionWorld::build(const MapFile& map, int patchSubdiv) { build(map, patchSubdiv, {}); }'''),
('''    swept.add(end + maxs);
    for (size_t i = 0; i < solids_.size(); ++i) {
        const Solid& s = solids_[i];
        bool overlap = true;
        for (int a = 0; a < 3; ++a) overlap &= swept.maxs[a] >= s.bounds.mins[a] - 1 && swept.mins[a] <= s.bounds.maxs[a] + 1;
        if (!overlap) continue;
        traceThrough(s, int(i), start, end, mins, maxs, tr, capsuleRadius);
        if (tr.allSolid) break;
    }''', '''    swept.add(end + maxs);
    const auto test = [&](size_t i) {
        const Solid& s = solids_[i];
        bool overlap = true;
        for (int a = 0; a < 3; ++a) overlap &= swept.maxs[a] >= s.bounds.mins[a] - 1 && swept.mins[a] <= s.bounds.maxs[a] + 1;
        if (overlap) traceThrough(s, int(i), start, end, mins, maxs, tr, capsuleRadius);
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
    }'''),
])
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '#include <cmath>' not in s:
    s = s.replace('#include "patch_tess.h"' + nl, '#include "patch_tess.h"' + nl + nl + '#include <algorithm>' + nl + '#include <cmath>' + nl, 1)
    io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
