import io

# Plan 1.11, per-face lightmap scale: the lightmap bake gives a face the texel density its lightmap fields ask for
# (L1 / L2, the Surface panel's "Lightmap scale", 16384 = the default: 16384 / 1024 = the bake's 16 units a texel; half
# the scale = twice the texels). The editor makes the lightmap and its UVs itself, so the converter needs no change:
# faces of another density become charts of their own with their own texel size. The viewport's preview bake does the
# same.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


# ---- core: the density of a face from its lightmap fields
patch('src/core/lightmap_fields.h', '''// Reads the lightmap fields of brush face `face`.''', '''// How many times finer than the default a face's lightmap is, from the lightmap fields in its suffix text:
// 16384 / sqrt(L1 x L2), kept inside 1/8 .. 8. 1 when the suffix has no lightmap fields or they are not positive.
double lightmapDensityOfSuffix(const std::string& suffix);

// Reads the lightmap fields of brush face `face`.''')
patch('src/core/lightmap_fields.cpp', '''std::optional<LightmapParams> lightmapParamsOfFace(const Primitive& p, size_t face) {''', '''double lightmapDensityOfSuffix(const std::string& suffix) {
    const auto lp = readSuffix(suffix);
    if (!lp || !(lp->sx > 0) || !(lp->sy > 0)) return 1.0;
    return std::clamp(16384.0 / std::sqrt(lp->sx * lp->sy), 0.125, 8.0);
}

std::optional<LightmapParams> lightmapParamsOfFace(const Primitive& p, size_t face) {''')
s = read('src/core/lightmap_fields.cpp')
for inc in ('#include <algorithm>', '#include <cmath>'):
    if inc not in s:
        s = s.replace('#include "lightmap_fields.h"', '#include "lightmap_fields.h"\n' + inc, 1)
        assert inc in s
write('src/core/lightmap_fields.cpp', s)

# ---- core: the bake
H = 'src/core/lightmap_bake.h'
patch(H, '''    Vec3 albedo{-1, -1, -1};
    std::vector<Vec3> triAlbedo;
};''', '''    Vec3 albedo{-1, -1, -1};
    std::vector<Vec3> triAlbedo;
    // The lightmap density of each triangle against BakeSettings::texelSize (2 = texels half as wide; a face's
    // lightmap scale, core lightmap_fields lightmapDensityOfSuffix). Empty or not one per triangle: 1 everywhere.
    // Triangles of different densities are never in one chart.
    std::vector<float> triDensity;
};''')
C = 'src/core/lightmap_bake.cpp'
patch(C, '''    Vec3 normal, u, v;           // projection basis
    double minU = 0, minV = 0, maxU = 0, maxV = 0;  // in texels (projected / texelSize)''', '''    Vec3 normal, u, v;           // projection basis
    double density = 1;          // texels per texelSize (BakeSurface::triDensity of its triangles)
    double minU = 0, minV = 0, maxU = 0, maxV = 0;  // in texels (projected x density / texelSize)''')
patch(C, '''        std::vector<int> chartOf(triCount, -1);
        for (size_t t = 0; t < triCount; ++t) {
            if (chartOf[t] >= 0 || length(fn[t]) < 0.5) continue;
            Chart c;
            c.surface = si;
            c.normal = fn[t];''', '''        std::vector<int> chartOf(triCount, -1);
        const bool densities = s.triDensity.size() == triCount;
        const auto densityOf = [&](size_t t) { return densities && s.triDensity[t] > 0 ? std::clamp(double(s.triDensity[t]), 0.125, 8.0) : 1.0; };
        for (size_t t = 0; t < triCount; ++t) {
            if (chartOf[t] >= 0 || length(fn[t]) < 0.5) continue;
            Chart c;
            c.surface = si;
            c.normal = fn[t];
            c.density = densityOf(t);''')
patch(C, '''                    if (chartOf[n] < 0 && length(fn[n]) > 0.5 && dot(fn[n], c.normal) >= settings.chartNormalDot) {''',
      '''                    if (chartOf[n] < 0 && length(fn[n]) > 0.5 && dot(fn[n], c.normal) >= settings.chartNormalDot &&
                        std::fabs(densityOf(n) - c.density) < 1e-6) {''')
patch(C, '''                    const double pu = dot(p, c.u) / texel, pv = dot(p, c.v) / texel;''',
      '''                    const double pu = dot(p, c.u) * c.density / texel, pv = dot(p, c.v) * c.density / texel;''')
patch(C, '''                const double tu = c.x + settings.padding + (dot(p, c.u) / texel - c.minU);
                const double tv = c.y + settings.padding + (dot(p, c.v) / texel - c.minV);''',
      '''                const double tu = c.x + settings.padding + (dot(p, c.u) * c.density / texel - c.minU);
                const double tv = c.y + settings.padding + (dot(p, c.v) * c.density / texel - c.minV);''')
patch(C, '''                px[e] = c.x + settings.padding + (dot(vtx[e]->pos, c.u) / texel - c.minU);
                py[e] = c.y + settings.padding + (dot(vtx[e]->pos, c.v) / texel - c.minV);''',
      '''                px[e] = c.x + settings.padding + (dot(vtx[e]->pos, c.u) * c.density / texel - c.minU);
                py[e] = c.y + settings.padding + (dot(vtx[e]->pos, c.v) * c.density / texel - c.minV);''')

# ---- the build
K = 'src/core/compile_iw8.cpp'
patch(K, '''    bool hasColor = false;  // 8.11: painted patch vertex colour (research 57); white when absent
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
};

// One brush face (a fan) or one patch triangle, with indices local to its own vertex list.
struct Chunk {
    size_t entity = 0;
    size_t group = 0;  // brush or patch primitive ordinal
    std::string editorMaterial;''', '''    bool hasColor = false;  // 8.11: painted patch vertex colour (research 57); white when absent
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
};

// One brush face (a fan) or one patch triangle, with indices local to its own vertex list.
struct Chunk {
    size_t entity = 0;
    size_t group = 0;  // brush or patch primitive ordinal
    float lmDensity = 1;  // the face's lightmap density (core lightmap_fields; 1 = the bake's texel size)
    std::string editorMaterial;''')
patch(K, '''struct RenderSurface {
    std::string editorMaterial;
    std::vector<RenderVertex> verts;
    std::vector<uint32_t> idx;
};''', '''struct RenderSurface {
    std::string editorMaterial;
    std::vector<RenderVertex> verts;
    std::vector<uint32_t> idx;
    std::vector<float> triDensity;  // per triangle: the lightmap density of the face it came from
};''')
patch(K, '''        Chunk c;
        c.entity = entity;
        c.group = group;
        c.editorMaterial = mat;
        if (haveMeshes && fi < edgeMeshes.size()) {''', '''        Chunk c;
        c.entity = entity;
        c.group = group;
        c.editorMaterial = mat;
        c.lmDensity = float(lightmapDensityOfSuffix(cb[fi].suffix));
        if (haveMeshes && fi < edgeMeshes.size()) {''')
patch(K, '''        s.verts.insert(s.verts.end(), c.verts.begin(), c.verts.end());
        for (uint32_t i : c.idx) s.idx.push_back(base + i);''', '''        s.verts.insert(s.verts.end(), c.verts.begin(), c.verts.end());
        for (uint32_t i : c.idx) s.idx.push_back(base + i);
        s.triDensity.insert(s.triDensity.end(), c.idx.size() / 3, c.lmDensity);''')
patch(K, '''            b.albedo = bounceOf(s.editorMaterial);''', '''            b.albedo = bounceOf(s.editorMaterial);
            b.triDensity = s.triDensity;
            for (const float d : s.triDensity) scaledTriangles += std::fabs(d - 1.0f) > 1e-6f;''')
patch(K, '''        BakeScene scene;
        for (const RenderSurface& s : surfaces) {
            BakeSurface b;''', '''        BakeScene scene;
        size_t scaledTriangles = 0;  // triangles of faces with a lightmap scale of their own
        for (const RenderSurface& s : surfaces) {
            BakeSurface b;''')
patch(K, '''" light(s), bounce colours of " +''', '''" light(s), " + std::to_string(scaledTriangles) + " triangle(s) with their own lightmap scale, bounce colours of " +''')
s = read(K)
if '#include "lightmap_fields.h"' not in s:
    s = s.replace('#include "light_grid.h"', '#include "light_grid.h"\n#include "lightmap_fields.h"', 1)
    assert '#include "lightmap_fields.h"' in s
write(K, s)

# ---- the viewport's preview bake
patch('src/viewport/scene.h', '''    std::vector<float> polyRepeatsPerUnit;''', '''    std::vector<float> polyRepeatsPerUnit;
    // The lightmap density of each brush face (core lightmap_fields lightmapDensityOfSuffix); empty: 1 everywhere.
    std::vector<float> polyLmDensity;''')
patch('src/viewport/scene.cpp', '''            g.polyRepeatsPerUnit.push_back(float(faceDensity(p, i, 1, 1, 0).texturePxPerUnit));''',
      '''            g.polyRepeatsPerUnit.push_back(float(faceDensity(p, i, 1, 1, 0).texturePxPerUnit));
            g.polyLmDensity.resize(g.polys.size() - 1, 1.0f);
            g.polyLmDensity.push_back(float(lightmapDensityOfSuffix(p.faces[i].suffix)));''')
s = read('src/viewport/scene.cpp')
if '#include "lightmap_fields.h"' not in s:
    s = s.replace('#include "light_grid.h"', '#include "light_grid.h"\n#include "lightmap_fields.h"', 1)
    assert '#include "lightmap_fields.h"' in s
write('src/viewport/scene.cpp', s)
patch('src/viewport/game_lightmap.h', '''        Vec3 albedo{-1, -1, -1};  // the material's bounce colour (BakeSurface::albedo); negative: the bake's grey''',
      '''        Vec3 albedo{-1, -1, -1};  // the material's bounce colour (BakeSurface::albedo); negative: the bake's grey
        float density = 1;        // the face's lightmap density (BakeSurface::triDensity)''')
G = 'src/viewport/game_lightmap.cpp'
patch(G, '''                if (albedoFor) p.albedo = albedoFor(*mat);''', '''                if (albedoFor) p.albedo = albedoFor(*mat);
                if (i < g->polyLmDensity.size()) p.density = g->polyLmDensity[i];''')
patch(G, '''            s.triAlbedo.push_back(p.albedo);''', '''            s.triAlbedo.push_back(p.albedo);
            s.triDensity.push_back(p.density);''')
print('ok')
