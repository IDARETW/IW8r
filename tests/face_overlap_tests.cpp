// No two drawn triangles of a map lie in one plane, face the same way and overlap: that is what z-fights. Checked on
// what the editor and the build draw (core face_edges edgeBrushMeshes: coplanar covers and hidden faces cut away), for
// the sample room always and for the map named by IW8R_TEST_OVERLAP_MAP when set (a real map; skipped otherwise).
#include <algorithm>
#include <array>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "face_edges.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

struct Tri {
    Vec3 a, b, c;
    size_t brush = 0;
};

double cross2(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

// Area of the overlap of two triangles given in 2D.
double overlapArea(const double A[3][2], const double B[3][2]) {
    std::vector<std::pair<double, double>> poly{{A[0][0], A[0][1]}, {A[1][0], A[1][1]}, {A[2][0], A[2][1]}};
    double sign = cross2(B[1][0] - B[0][0], B[1][1] - B[0][1], B[2][0] - B[0][0], B[2][1] - B[0][1]) >= 0 ? 1.0 : -1.0;
    for (int k = 0; k < 3 && !poly.empty(); ++k) {
        const double ax = B[k][0], ay = B[k][1], bx = B[(k + 1) % 3][0], by = B[(k + 1) % 3][1];
        std::vector<std::pair<double, double>> out;
        for (size_t j = 0; j < poly.size(); ++j) {
            const auto p = poly[j], q = poly[(j + 1) % poly.size()];
            const double sp = sign * cross2(bx - ax, by - ay, p.first - ax, p.second - ay);
            const double sq = sign * cross2(bx - ax, by - ay, q.first - ax, q.second - ay);
            if (sp >= 0) out.push_back(p);
            if ((sp >= 0) != (sq >= 0)) {
                const double t = sp / (sp - sq);
                out.push_back({p.first + (q.first - p.first) * t, p.second + (q.second - p.second) * t});
            }
        }
        poly.swap(out);
    }
    double area = 0;
    for (size_t j = 0; j < poly.size(); ++j) {
        const auto p = poly[j], q = poly[(j + 1) % poly.size()];
        area += cross2(p.first, p.second, q.first, q.second);
    }
    return std::fabs(area) * 0.5;
}

struct OverlapReport {
    size_t brushes = 0, triangles = 0, pairs = 0;
    double area = 0;
    std::string worst;
    std::vector<std::pair<double, std::string>> list;  // every pair: its area, and where and between what
    std::vector<std::string> materials;                 // per brush: its first drawn material
};

// Every world brush of the map as the editor makes it (all drawn materials opaque), cut by edgeBrushMeshes; then the
// overlap of every two triangles of different brushes that share a plane and a facing.
OverlapReport overlapsOf(const MapFile& map) {
    OverlapReport rep;
    std::vector<EdgeBrush> brushes;
    for (const Entity& e : map.entities) {
        if (e.classname() != "worldspawn") continue;
        for (const EntityItem& item : e.items) {
            const Primitive* p = item.prim();
            if (!p || p->kind != PrimKind::Brush) continue;
            EdgeBrush b;
            b.wins = faceWindingsOf(*p);
            bool ok = b.wins.size() == p->faces.size();
            for (size_t f = 0; ok && f < p->faces.size(); ++f) {
                const auto pl = facePlane(*p, f);
                if (!pl) {
                    ok = false;
                    break;
                }
                b.planes.push_back(*pl);
                b.drawn.push_back(!isIw8ToolMaterial(p->faces[f].material()));
            }
            if (!ok) continue;
            finishEdgeBrush(b);
            bool anyDrawn = false, allKnown = true;
            for (size_t f = 0; f < b.drawn.size(); ++f) {
                anyDrawn = anyDrawn || b.drawn[f];
                if (!b.drawn[f]) {
                    std::string low = p->faces[f].material();
                    for (char& ch : low) ch = char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
                    allKnown = allKnown && (low.find("caulk") != std::string::npos || low.find("nodraw") != std::string::npos);
                }
            }
            b.opaque = anyDrawn && allKnown;
            {
                std::string first;
                for (size_t f = 0; f < b.drawn.size() && first.empty(); ++f)
                    if (b.drawn[f]) first = p->faces[f].material();
                rep.materials.push_back(first);
            }
            brushes.push_back(std::move(b));
        }
    }
    rep.brushes = brushes.size();
    const auto near = edgeNeighbours(brushes);
    // Triangles by plane: normal to 1 / 500, distance to 1 / 50 of a unit.
    std::map<std::array<long long, 4>, std::vector<Tri>> planes;
    for (size_t i = 0; i < brushes.size(); ++i) {
        const auto meshes = edgeBrushMeshes(brushes, i, near[i], kEdgeRange, true, &near);
        for (size_t f = 0; f < meshes.size() && f < brushes[i].planes.size(); ++f) {
            if (f < brushes[i].drawn.size() && !brushes[i].drawn[f]) continue;  // tool faces are not drawn
            const EdgeFaceMesh& m = meshes[f];
            const Vec3 n = normalize(brushes[i].planes[f].n);
            for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                Tri tr{m.verts[m.idx[t]].pos, m.verts[m.idx[t + 1]].pos, m.verts[m.idx[t + 2]].pos, i};
                const double d = dot(n, tr.a);
                planes[{llround(n.x * 500), llround(n.y * 500), llround(n.z * 500), llround(d * 50)}].push_back(tr);
                ++rep.triangles;
            }
        }
    }
    double worstArea = 0;
    for (const auto& [key, tris] : planes) {
        if (tris.size() < 2) continue;
        const Vec3 n = normalize(Vec3{double(key[0]), double(key[1]), double(key[2])});
        Vec3 u, v;
        edgeFaceBasis(n, u, v);
        struct P {
            double q[3][2], lo[2], hi[2];
        };
        std::vector<P> proj(tris.size());
        for (size_t k = 0; k < tris.size(); ++k) {
            const Vec3 pts[3] = {tris[k].a, tris[k].b, tris[k].c};
            for (int c = 0; c < 3; ++c) proj[k].q[c][0] = dot(pts[c], u), proj[k].q[c][1] = dot(pts[c], v);
            for (int a = 0; a < 2; ++a) {
                proj[k].lo[a] = std::min({proj[k].q[0][a], proj[k].q[1][a], proj[k].q[2][a]});
                proj[k].hi[a] = std::max({proj[k].q[0][a], proj[k].q[1][a], proj[k].q[2][a]});
            }
        }
        // Sweep along the first axis.
        std::vector<size_t> order(tris.size());
        for (size_t k = 0; k < order.size(); ++k) order[k] = k;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return proj[a].lo[0] < proj[b].lo[0]; });
        for (size_t x = 0; x < order.size(); ++x) {
            const size_t a = order[x];
            for (size_t y = x + 1; y < order.size(); ++y) {
                const size_t b = order[y];
                if (proj[b].lo[0] >= proj[a].hi[0]) break;
                if (tris[a].brush == tris[b].brush) continue;
                if (proj[b].lo[1] >= proj[a].hi[1] || proj[a].lo[1] >= proj[b].hi[1]) continue;
                const double area = overlapArea(proj[a].q, proj[b].q);
                if (area < 0.01) continue;
                ++rep.pairs;
                rep.area += area;
                {
                    const Vec3 c = (tris[a].a + tris[a].b + tris[a].c) * (1.0 / 3.0);
                    char text[300];
                    std::snprintf(text, sizeof text, "%.1f at (%.0f %.0f %.0f) n (%.0f %.0f %.0f): %zu %s / %zu %s", area, c.x, c.y, c.z, n.x, n.y,
                                  n.z, tris[a].brush, rep.materials[tris[a].brush].c_str(), tris[b].brush, rep.materials[tris[b].brush].c_str());
                    rep.list.push_back({area, text});
                }
                if (area > worstArea) {
                    worstArea = area;
                    const Vec3 c = (tris[a].a + tris[a].b + tris[a].c) * (1.0 / 3.0);
                    char text[200];
                    std::snprintf(text, sizeof text, "%.1f square units at (%.0f %.0f %.0f), normal (%.2f %.2f %.2f), brushes %zu and %zu", area, c.x,
                                  c.y, c.z, n.x, n.y, n.z, tris[a].brush, tris[b].brush);
                    rep.worst = text;
                }
            }
        }
    }
    return rep;
}

// tests/data/iw8_sample_room.map, looked for upward from the working directory.
std::string sampleRoomPath() {
    std::error_code ec;
    for (std::filesystem::path p = std::filesystem::current_path(ec); !p.empty(); p = p.parent_path()) {
        const std::filesystem::path cand = p / "tests" / "data" / "iw8_sample_room.map";
        if (std::filesystem::exists(cand, ec)) return cand.string();
        if (p == p.parent_path()) break;
    }
    return {};
}

OverlapReport overlapsOfFile(const std::string& path, bool& read) {
    std::string text;
    read = readFile(path, text);
    if (!read) return {};
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    read = pr.map.has_value();
    return read ? overlapsOf(*pr.map) : OverlapReport{};
}

}  // namespace

TEST(face_overlap_sample_room_draws_every_plane_once) {
    bool read = false;
    const OverlapReport rep = overlapsOfFile(sampleRoomPath(), read);
    CHECK(read && rep.brushes > 0 && rep.triangles > 0);
    if (rep.pairs) std::printf("  sample room: %zu overlapping pairs, %.1f square units; worst %s\n", rep.pairs, rep.area, rep.worst.c_str());
    CHECK(rep.pairs == 0);
}

TEST(face_overlap_real_map_draws_every_plane_once) {
    const char* path = std::getenv("IW8R_TEST_OVERLAP_MAP");
    if (!path || !*path) return;
    bool read = false;
    const OverlapReport rep = overlapsOfFile(path, read);
    CHECK(read);
    std::printf("  overlap check of %s: %zu brushes, %zu triangles, %zu overlapping pairs, %.1f square units%s%s\n", path, rep.brushes, rep.triangles,
                rep.pairs, rep.area, rep.pairs ? "; worst " : "", rep.worst.c_str());
    auto list = rep.list;
    std::sort(list.begin(), list.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    for (size_t k = 0; k < list.size() && k < 40; ++k) std::printf("    %s\n", list[k].second.c_str());
    CHECK(rep.pairs == 0);
}

// The brushes around the one place of Cinder Rail where two roof slabs' undersides were both drawn (2026-11-14): two
// slabs that overlap by 8 units, the walls and pilasters under them, and what touches them.
TEST(face_overlap_roof_slabs_over_pilasters) {
    struct Box {
        double v[6];
    };
    const Box boxes[] = {{{-64, -224, 0, 64, 224, 144}},  {{-64, 224, 0, 64, 344, 144}},   {{48, 344, 16, 160, 360, 320}},
                         {{-48, 344, 16, 48, 360, 200}},  {{-80, 344, 144, -48, 360, 200}}, {{-52, 338, 16, -44, 344, 144}},
                         {{-180, 338, 144, -44, 344, 150}}, {{-68, -224, 0, 68, -212, 144}}, {{-68, -64, 0, 68, -52, 144}},
                         {{-68, 96, 0, 68, 108, 144}},    {{-64, 220, 0, -52, 348, 144}},  {{-68, -228, 144, 68, 228, 152}},
                         {{-68, 220, 144, 68, 348, 152}}};
    const size_t count = sizeof boxes / sizeof boxes[0];
    // With every brush, and with each one left out in turn (which one matters is printed when a case fails).
    for (size_t leftOut = count; leftOut-- > 0 || leftOut == size_t(-1);) {
        std::string text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
        for (size_t k = 0; k < count; ++k) {
            if (k == leftOut) continue;
            const Box& b = boxes[k];
            text += makeBrushPrimitive(makeBox({{b.v[0], b.v[1], b.v[2]}, {b.v[3], b.v[4], b.v[5]}}, " twc/wall 128 128 0 0 0 0"), "\n")->text();
        }
        text += "}\n";
        Id next = 1;
        ParseResult pr = parseMap(text, next);
        CHECK(pr.map.has_value());
        if (!pr.map) return;
        const OverlapReport rep = overlapsOf(*pr.map);
        if (rep.pairs)
            std::printf("  roof slabs, brush %d left out: %zu overlapping pairs, %.1f square units; worst %s\n", leftOut == size_t(-1) ? -1 : int(leftOut),
                        rep.pairs, rep.area, rep.worst.c_str());
        CHECK(rep.pairs == 0);
        if (leftOut == size_t(-1)) break;
    }
}
