import io

# Map-agent report (EDITOR_ISSUES.md, 2026-11-11/14): building a map with tm/building_awning_rusted_metal_corrugated_128
# crashed the editor (std::out_of_range from quickHullFaces via convexHullPlanes / convexPieces, compile_iw8.cpp:1837).
# The corrugated sheet's points are nearly coplanar: rounding made a visible-face region whose horizon did not close, a
# directed edge lost its twin and owner.at() threw. Now: a missing twin ends that attempt (false), the points are
# welded and the hull tried again with a looser tolerance, and a piece whose hull still fails collides as its box.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


C = 'src/core/convex_hull.cpp'
patch(C, '''            for (int k = 0; k < 3; ++k) {
                const int a = F(cur).v[k], b = F(cur).v[(k + 1) % 3];
                const int nb = owner.at(std::pair<int, int>{b, a});
                if (!vis.count(nb) && dot(F(nb).n, ap) - F(nb).d > eps) {''', '''            for (int k = 0; k < 3; ++k) {
                const int a = F(cur).v[k], b = F(cur).v[(k + 1) % 3];
                const auto twin = owner.find(std::pair<int, int>{b, a});
                if (twin == owner.end()) return false;  // rounding broke the surface: the caller retries or falls back
                const int nb = twin->second;
                if (!vis.count(nb) && dot(F(nb).n, ap) - F(nb).d > eps) {''')
patch(C, '''        for (int cur : vis)
            for (int k = 0; k < 3; ++k) {
                const int a = F(cur).v[k], b = F(cur).v[(k + 1) % 3];
                if (!vis.count(owner.at(std::pair<int, int>{b, a}))) horizon.push_back({a, b});
            }''', '''        for (int cur : vis)
            for (int k = 0; k < 3; ++k) {
                const int a = F(cur).v[k], b = F(cur).v[(k + 1) % 3];
                const auto twin = owner.find(std::pair<int, int>{b, a});
                if (twin == owner.end()) return false;
                if (!vis.count(twin->second)) horizon.push_back({a, b});
            }''')
patch(C, '''    std::vector<RawFace> raw;
    if (!quickHullFaces(points, 1e-7 * diag, raw)) return {};''', '''    std::vector<RawFace> raw;
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
    }''')
patch(C, '''        std::vector<Plane> planes = convexHullPlanes(pc.pts);
        if (planes.empty()) continue;''', '''        std::vector<Plane> planes = convexHullPlanes(pc.pts);
        if (planes.empty() && pc.box.valid()) {
            // No hull (flat, or the points defeat the hull): the piece collides as its box, a little thickened when flat.
            AABB b = pc.box;
            for (int a = 0; a < 3; ++a)
                if (b.maxs[a] - b.mins[a] < 0.5) b.mins[a] -= 0.25, b.maxs[a] += 0.25;
            planes = {Plane{Vec3{1, 0, 0}, b.maxs.x}, Plane{Vec3{-1, 0, 0}, -b.mins.x}, Plane{Vec3{0, 1, 0}, b.maxs.y},
                      Plane{Vec3{0, -1, 0}, -b.mins.y}, Plane{Vec3{0, 0, 1}, b.maxs.z}, Plane{Vec3{0, 0, -1}, -b.mins.z}};
        }
        if (planes.empty()) continue;''')
print('ok')
