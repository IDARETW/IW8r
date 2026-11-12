// Roadmap 5.11 / 5.21: the ray BVH and the lightmap baker.
#include "lightmap_bake.h"
#include "ray_bvh.h"
#include "test_harness.h"

#include <cmath>

using namespace iw8r;

namespace {
// An axis-aligned box as 12 triangles (outward winding does not matter: triangles are two-sided).
void addBox(std::vector<RayBvh::Triangle>& out, const Vec3& lo, const Vec3& hi) {
    const Vec3 p[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                       {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    const int f[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 3, 7, 4}};
    for (const auto& q : f) {
        out.push_back({p[q[0]], p[q[1]], p[q[2]], 0});
        out.push_back({p[q[0]], p[q[2]], p[q[3]], 0});
    }
}
// A floor quad as a bake surface (normal +z).
BakeSurface floorQuad(double size) {
    BakeSurface s;
    const Vec3 n{0, 0, 1}, t{1, 0, 0};
    s.verts = {{{-size, -size, 0}, n, t}, {{size, -size, 0}, n, t}, {{size, size, 0}, n, t}, {{-size, size, 0}, n, t}};
    s.idx = {0, 1, 2, 0, 2, 3};
    return s;
}
}  // namespace

TEST(ray_bvh_hits_and_occlusion) {
    std::vector<RayBvh::Triangle> tris;
    for (int i = 0; i < 50; ++i) addBox(tris, Vec3(i * 100.0, 0, 0), Vec3(i * 100.0 + 50, 50, 50));
    RayBvh bvh;
    bvh.build(tris);
    RayBvh::Hit hit;
    CHECK(bvh.intersect(Vec3(-100, 25, 25), Vec3(1, 0, 0), 0, 1e6, hit) && std::fabs(hit.t - 100) < 1e-6);
    CHECK(bvh.intersect(Vec3(1025, 25, 200), Vec3(0, 0, -1), 0, 1e6, hit) && std::fabs(hit.t - 150) < 1e-6);
    CHECK(!bvh.intersect(Vec3(75, 25, 200), Vec3(0, 0, -1), 0, 1e6, hit));  // the gap between two boxes
    CHECK(bvh.occluded(Vec3(-100, 25, 25), Vec3(1, 0, 0), 0, 1e6) && !bvh.occluded(Vec3(-100, 25, 25), Vec3(1, 0, 0), 0, 99));
    CHECK(!bvh.occluded(Vec3(0, 200, 25), Vec3(1, 0, 0), 0, 1e6));
}

TEST(lightmap_bake_floor_sun_and_shadow) {
    BakeScene scene;
    scene.surfaces.push_back(floorQuad(256));
    // A blocker above the floor's +x half: a box from x 0..256, high above, casting a shadow straight down.
    addBox(scene.extraOccluders, Vec3(0, -300, 200), Vec3(300, 300, 220));
    scene.sunDir = Vec3(0, 0, 1);
    scene.sunColor = Vec3(1, 0.9, 0.8);
    scene.sunIntensity = 2.0;
    scene.skyIntensity = 0.0;  // direct light only: the numbers are exact
    BakeSettings settings;
    settings.texelSize = 16;
    settings.skyRays = 8;
    settings.threads = 2;
    const BakeResult r = bakeLightmap(scene, settings);
    CHECK(r.ok && r.width > 0 && r.width == r.height && r.charts == 1 && r.texelsLit > 900);
    if (!r.ok) return;
    // Every vertex has a UV inside the atlas.
    bool inside = true;
    for (const auto& uv : r.uv[0]) inside &= uv[0] >= 0 && uv[0] <= 1 && uv[1] >= 0 && uv[1] <= 1;
    CHECK(inside && r.uv[0].size() == 4);
    // The texel under the floor point (x, y) from the vertex UVs (bilinear over the quad).
    const auto texelAt = [&](double x, double y) {
        const double s = (x + 256) / 512, t = (y + 256) / 512;
        const auto& uv = r.uv[0];
        const double u = uv[0][0] * (1 - s) * (1 - t) + uv[1][0] * s * (1 - t) + uv[2][0] * s * t + uv[3][0] * (1 - s) * t;
        const double v = uv[0][1] * (1 - s) * (1 - t) + uv[1][1] * s * (1 - t) + uv[2][1] * s * t + uv[3][1] * (1 - s) * t;
        return size_t(int(v * r.height)) * r.width + size_t(int(u * r.width));
    };
    const size_t lit = texelAt(-128, 0), shadowed = texelAt(128, 0);
    // Lit: the sun straight down on a flat floor: directional = sun colour * intensity, direction (0, 0, 1).
    CHECK(std::fabs(r.directional[lit][0] - 2.0f) < 1e-3 && std::fabs(r.directional[lit][2] - 1.6f) < 1e-3);
    CHECK(std::fabs(r.direction[lit][2] - 1.0f) < 1e-4);
    // Shadowed: no direct light.
    CHECK(r.directional[shadowed][0] < 1e-6);
    // Encoded and decoded as the converter does: C = A + B D.z with f = lum(A) / lum(C).
    const EncodedLightmap e = encodeLightmapPair(r);
    CHECK(e.primary.size() == size_t(r.width) * r.height && e.secondary.size() == size_t(r.width) * r.height * 8);
    float c[3], f, d[3];
    decodeLightmapTexel(e, r.width, r.height, int(lit % r.width), int(lit / r.width), c, f, d);
    CHECK(std::fabs(c[0] - 2.0f) < 0.05f && std::fabs(c[2] - 1.6f) < 0.05f && f < 0.02f && d[2] > 0.99f);
}

TEST(lightmap_bake_sky_and_bounce) {
    BakeScene scene;
    scene.surfaces.push_back(floorQuad(128));
    scene.sunIntensity = 0;
    scene.skyColor = Vec3(0.5, 0.6, 0.7);
    scene.skyIntensity = 1.0;
    BakeSettings settings;
    settings.skyRays = 64;
    settings.threads = 1;
    BakeResult open = bakeLightmap(scene, settings);
    CHECK(open.ok);
    if (!open.ok) return;
    // An open floor sees the whole sky: ambient = the sky irradiance.
    size_t i = 0;
    while (i < open.covered.size() && !open.covered[i]) ++i;
    CHECK(i < open.covered.size() && std::fabs(open.ambient[i][0] - 0.5f) < 1e-3 && std::fabs(open.ambient[i][2] - 0.7f) < 1e-3);
    // A roof over the floor: the sky is blocked (only the bounce of the sky remains, much darker).
    addBox(scene.extraOccluders, Vec3(-1000, -1000, 64), Vec3(1000, 1000, 80));
    BakeResult roofed = bakeLightmap(scene, settings);
    CHECK(roofed.ok && roofed.ambient[i][0] < 0.5f * 0.5f);
}

TEST(lightmap_bake_charts_split_vertices) {
    // Two faces sharing a vertex but at 90 degrees: two charts; the shared vertex is split.
    BakeSurface s;
    const Vec3 up{0, 0, 1}, side{1, 0, 0}, t{0, 1, 0};
    s.verts = {{{0, 0, 0}, up, t}, {{0, 64, 0}, up, t}, {{-64, 32, 0}, up, t}, {{0, 32, 64}, side, t}};
    s.idx = {0, 1, 2, 0, 3, 1};
    BakeScene scene;
    scene.surfaces.push_back(s);
    scene.sunIntensity = 1;
    BakeSettings settings;
    settings.skyRays = 4;
    settings.threads = 1;
    const BakeResult r = bakeLightmap(scene, settings);
    CHECK(r.ok && r.charts == 2 && r.surfaces[0].verts.size() == 6 && r.uv[0].size() == 6);
}
