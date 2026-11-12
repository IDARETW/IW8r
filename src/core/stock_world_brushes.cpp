#include "stock_world_brushes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

#include "brush_ops.h"
#include "tex_projection.h"
#include "uv_unwrap.h"

namespace iw8r {
namespace {

bool isSky(const std::string& name) {
    std::string n = name;
    if (n.size() >= 3 && (n[0] == 't' || n[0] == 'T') && (n[1] == 'w' || n[1] == 'W') && n[2] == '/') n.erase(0, 3);
    return n.size() >= 3 && std::tolower(static_cast<unsigned char>(n[0])) == 's' &&
           std::tolower(static_cast<unsigned char>(n[1])) == 'k' && std::tolower(static_cast<unsigned char>(n[2])) == 'y';
}

std::string num(double v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6g", std::fabs(v) < 1e-9 ? 0.0 : v);
    return buf;
}

std::string suffixFor(const std::string& material, const TexParams& tp) {
    return " " + material + " " + num(tp.width) + " " + num(tp.height) + " " + num(tp.xoff) + " " + num(tp.yoff) + " " + num(tp.rot) +
           " 0 lightmap_gray 16384 16384 0 0 0 0";
}

}  // namespace

StockBrushResult stockWorldToBrushes(const StockWorld& w, const AABB& region, double thickness, size_t maxBrushes,
                                     const std::string& eol) {
    StockBrushResult r;
    if (!region.valid()) return r;
    thickness = std::max(0.5, thickness);
    const std::string caulk = suffixFor("caulk", TexParams{64, 64, 0, 0, 0});
    std::set<std::string> mats;
    for (const ImportedMesh& m : w.meshes) {
        if (isSky(m.material)) continue;
        const bool hasUv = m.uv.size() == m.pos.size();
        for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
            const uint32_t ia = m.idx[i], ib = m.idx[i + 1], ic = m.idx[i + 2];
            if (ia >= m.pos.size() || ib >= m.pos.size() || ic >= m.pos.size()) continue;
            const Vec3 a = m.pos[ia], b = m.pos[ib], c = m.pos[ic];
            if (!region.contains(a) && !region.contains(b) && !region.contains(c)) continue;
            ++r.triangles;
            if (r.brushes.size() >= maxBrushes) {
                r.capped = true;
                continue;
            }
            const Vec3 cr = cross(b - a, c - a);
            const double area2 = length(cr);
            if (area2 < 1.0) {  // under half a square unit
                ++r.skipped;
                continue;
            }
            const Vec3 n = cr * (1.0 / area2);
            // Top: the triangle, with a projection fitted to its texture coordinates (stock v runs top-down, the
            // ImportedMesh convention bottom-up).
            TexParams tp;
            if (hasUv) {
                const auto& ua = m.uv[ia];
                const auto& ub = m.uv[ib];
                const auto& uc = m.uv[ic];
                tp = fitTexParams(n, {a, b, c}, {Uv{ua[0], 1 - ua[1]}, Uv{ub[0], 1 - ub[1]}, Uv{uc[0], 1 - uc[1]}}, TexParams{});
            }
            ConvexBrush brush;
            brush.push_back({Plane{n, dot(n, a)}, suffixFor(m.material.empty() ? std::string("caulk") : m.material, tp)});
            brush.push_back({Plane{n * -1.0, -dot(n, a) + thickness}, caulk});
            const Vec3 centre = (a + b + c) * (1.0 / 3.0) - n * (thickness * 0.5);
            const Vec3 corners[3] = {a, b, c};
            bool ok = true;
            for (int e = 0; e < 3 && ok; ++e) {
                const Vec3 p = corners[e], q = corners[(e + 1) % 3];
                Vec3 s = cross(q - p, n);
                const double len = length(s);
                if (len < 1e-6) {
                    ok = false;
                    break;
                }
                s = s * (1.0 / len);
                if (dot(s, centre - p) > 0) s = s * -1.0;  // outward
                brush.push_back({Plane{s, dot(s, p)}, caulk});
            }
            std::optional<Primitive> prim = ok ? makeBrushPrimitive(brush, eol) : std::nullopt;
            if (!prim) {
                ++r.skipped;
                continue;
            }
            r.brushes.push_back(std::move(*prim));
            if (!m.material.empty()) mats.insert(m.material);
        }
    }
    r.materials.assign(mats.begin(), mats.end());
    return r;
}

}  // namespace iw8r
