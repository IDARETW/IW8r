import io

# Plan 1.9: models in an own map take the light the build's light grid gives them, in View > Game Lighting.
# The build lights every placed model from the light grid at its place (the converter's InstanceAmbient: the 56 colours
# of the point as a 4 x 4 x 4 volume, read along the normal, x 2). The viewport gave every model the map's mean light,
# so a prop in a dark room was as bright as one in the street. Now: the point sampler of the build's grid is one core
# function (light_grid lightGridSamplesAt), the viewport runs it for every model in the background and gives each
# model triangle the light of its normal, read the way the build's shader reads it (modelLightFromSamples).

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


# ---- core: the sampler and the reader
LH = 'src/core/light_grid.h'
patch(LH, '''// Builds the files (validated: sizes and indices follow the converter's reader rules).''', '''// The light of one point as those 56 samples (ROADMAP 5.20), in the grid's 8-bit units (255 = lit by 1.0): the sky
// where a ray escapes upward, and one bounce of the sun and the sky off what a ray hits, in that surface's colour
// (`albedos`[triangle id - 1] for ids above 0, else 45 % grey). `sky` is the sky's colour times its strength. The
// build calls it per palette cell; the viewport per placed model.
void lightGridSamplesAt(const class RayBvh& bvh, const std::vector<Vec3>& albedos, const Vec3& sunToward, const Vec3& sunColour, const Vec3& sky,
                        double reach, const Vec3& point, std::array<std::array<float, 3>, 56>& out);

// The light a surface with normal `n` takes from a point's 56 samples, as the build's world shader reads a placed
// model's light (the converter's InstanceAmbient: the samples as the shell of a 4 x 4 x 4 volume of bytes, read
// trilinearly where the normal leaves the cube, times 2). In lightmap units (1 = lit by 1.0).
Vec3 modelLightFromSamples(const std::array<std::array<float, 3>, 56>& samples, const Vec3& n);

// Builds the files (validated: sizes and indices follow the converter's reader rules).''')
LC = 'src/core/light_grid.cpp'
patch(LC, '''const std::array<Vec3, 56>& lightGridSampleDirections() {''', '''void lightGridSamplesAt(const RayBvh& bvh, const std::vector<Vec3>& albedos, const Vec3& sunToward, const Vec3& sunColour, const Vec3& sky,
                        double reach, const Vec3& p, std::array<std::array<float, 3>, 56>& out) {
    constexpr double kPi = 3.14159265358979323846;
    static const std::vector<Vec3> rays = [] {  // 96 directions spread evenly over the sphere
        std::vector<Vec3> r;
        const double ga = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
        for (int k = 0; k < 96; ++k) {
            const double z = 1.0 - (k + 0.5) * (2.0 / 96.0), rr = std::sqrt(std::max(0.0, 1.0 - z * z));
            r.push_back(Vec3{std::cos(ga * k) * rr, std::sin(ga * k) * rr, z});
        }
        return r;
    }();
    const auto& sdirs = lightGridSampleDirections();
    double acc[56][3] = {};
    const double w = 4.0 * kPi / double(rays.size());
    for (const Vec3& d : rays) {
        Vec3 rad{0, 0, 0};
        RayBvh::Hit h;
        if (!bvh.intersect(p, d, 0.5, reach, h)) {
            if (d.z > 0) rad = sky * (1.0 / kPi);  // the open sky; below the map, nothing
        } else {
            const RayBvh::Triangle& t = bvh.triangles()[h.tri];
            Vec3 n = cross(t.b - t.a, t.c - t.a);
            const double nl = length(n);
            if (nl < 1e-12) continue;
            n = n * (1.0 / nl);
            if (dot(n, d) > 0) n = n * -1.0;
            const Vec3 hp = p + d * h.t + n * 0.5;
            const double ns = dot(n, sunToward);
            const double eSun = ns > 0 && !bvh.occluded(hp, sunToward, 0.1, reach) ? ns : 0.0;
            const Vec3 e = sunColour * eSun + sky * (0.25 * (1.0 + n.z));  // about half its sky seen
            const Vec3 rho = t.id > 0 && t.id <= albedos.size() ? albedos[t.id - 1] : Vec3{0.45, 0.45, 0.45};
            rad = Vec3{e.x * rho.x, e.y * rho.y, e.z * rho.z} * (1.0 / kPi);
        }
        for (int s = 0; s < 56; ++s) {
            const double c = dot(sdirs[size_t(s)], d);
            if (c <= 0) continue;
            acc[s][0] += rad.x * c * w, acc[s][1] += rad.y * c * w, acc[s][2] += rad.z * c * w;
        }
    }
    for (int s = 0; s < 56; ++s)
        for (int ch = 0; ch < 3; ++ch) out[size_t(s)][size_t(ch)] = float(255.0 * acc[s][ch]);
}

Vec3 modelLightFromSamples(const std::array<std::array<float, 3>, 56>& samples, const Vec3& nIn) {
    // Texel (x, y, z) of the 4 x 4 x 4 volume -> its colour (the converter's kModelLightingTexelColors: the shell in
    // sample order; the eight inner texels repeat shell colours and are never reached from the cube's surface).
    static const uint8_t colourOf[64] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 0,
                                         3,  21, 22, 12, 15, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 40, 43, 33, 34, 52, 55, 35,
                                         36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55};
    const double len = length(nIn);
    const Vec3 n = len > 1e-12 ? nIn * (1.0 / len) : Vec3{0, 0, 1};
    const double m = std::max(std::max(std::fabs(n.x), std::fabs(n.y)), std::max(std::fabs(n.z), 1e-6));
    const double c[3] = {n.x / m * 1.5 + 1.5, n.y / m * 1.5 + 1.5, n.z / m * 1.5 + 1.5};
    int i[3];
    double f[3];
    for (int a = 0; a < 3; ++a) i[a] = std::min(int(std::floor(c[a])), 2), f[a] = c[a] - i[a];
    const auto texel = [&](int x, int y, int z, int ch) {
        // A byte of the model lighting image: the sample, cut at 255.
        return std::clamp(double(samples[colourOf[x + 4 * y + 16 * z]][size_t(ch)]), 0.0, 255.0) / 255.0;
    };
    Vec3 out;
    double* o[3] = {&out.x, &out.y, &out.z};
    for (int ch = 0; ch < 3; ++ch) {
        const double x00 = texel(i[0], i[1], i[2], ch) * (1 - f[0]) + texel(i[0] + 1, i[1], i[2], ch) * f[0];
        const double x10 = texel(i[0], i[1] + 1, i[2], ch) * (1 - f[0]) + texel(i[0] + 1, i[1] + 1, i[2], ch) * f[0];
        const double x01 = texel(i[0], i[1], i[2] + 1, ch) * (1 - f[0]) + texel(i[0] + 1, i[1], i[2] + 1, ch) * f[0];
        const double x11 = texel(i[0], i[1] + 1, i[2] + 1, ch) * (1 - f[0]) + texel(i[0] + 1, i[1] + 1, i[2] + 1, ch) * f[0];
        *o[ch] = ((x00 * (1 - f[1]) + x10 * f[1]) * (1 - f[2]) + (x01 * (1 - f[1]) + x11 * f[1]) * f[2]) * 2.0;
    }
    return out;
}

const std::array<Vec3, 56>& lightGridSampleDirections() {''')
s = read(LC)
for inc in ('#include "ray_bvh.h"', '#include <cmath>', '#include <algorithm>'):
    if inc not in s:
        s = s.replace('#include "light_grid.h"', '#include "light_grid.h"\n' + inc, 1)
        assert inc in s
write(LC, s)

# ---- the build uses the shared sampler
K = 'src/core/compile_iw8.cpp'
s = read(K)
nl = '\r\n' if '\r\n' in s else '\n'
a = s.index('            lg.samplesAt = [&gridBvh, &gridAlbedo, sunDir, sunC, gridSky, gridSkyI, reach](const Vec3& p, std::array<std::array<float, 3>, 56>& out) {')
b = s.index('            };', a) + len('            };')
s = s[:a] + ('            lg.samplesAt = [&gridBvh, &gridAlbedo, sunDir, sunC, gridSky, gridSkyI, reach](const Vec3& p, std::array<std::array<float, 3>, 56>& out) {' + nl +
             '                lightGridSamplesAt(gridBvh, gridAlbedo, sunDir, sunC, gridSky * gridSkyI, reach, p, out);' + nl +
             '            };') + s[b:]
write(K, s)

# ---- viewport: the lights per model, and the vertices that carry them
SH = 'src/viewport/scene.h'
patch(SH, '''    std::shared_ptr<const GameLightmap> gameLightmap;''', '''    std::shared_ptr<const GameLightmap> gameLightmap;
    // Game Lighting on an own map: the light of every placed model (by its entity), as the 56 samples the build's
    // light grid holds at its place (core light_grid). A model's triangles carry the light of their normal in the
    // vertex tangent with the lightmap UV (-2, -2), as stock props carry their light grid box. modelLightsRev moves
    // when the table changes (the camera's geometry is then made again).
    std::unordered_map<Id, std::array<std::array<float, 3>, 56>> modelLights;
    uint64_t modelLightsRev = 0;''')
patch(SH, '''void appendTexturedFace(const PrimGeom& g, size_t i, const Vec3& n, const Vec3& tan, uint32_t colour, const GameLightmap* gameLm,
                        const Vec3& centre, std::vector<GpuTexVertex>& out);''', '''// propLight: `tan` is the light of a model's triangle (SceneCache::modelLights), marked by the lightmap UV (-2, -2).
void appendTexturedFace(const PrimGeom& g, size_t i, const Vec3& n, const Vec3& tan, uint32_t colour, const GameLightmap* gameLm,
                        const Vec3& centre, std::vector<GpuTexVertex>& out, bool propLight = false);
// The light of poly `i` of an item when it is a model triangle with a light in `scene.modelLights` (Game Lighting).
bool modelTriangleLight(const class SceneCache& scene, const PrimGeom& g, size_t i, const Vec3& n, Vec3& light);''')
s = read(SH)
for inc in ('#include <unordered_map>', '#include <array>'):
    if inc not in s:
        s = s.replace('#include <', inc + '\n#include <', 1)
write(SH, s)
SC = 'src/viewport/scene.cpp'
patch(SC, '''void appendTexturedFace(const PrimGeom& g, size_t i, const Vec3& n, const Vec3& tan, uint32_t colour, const GameLightmap* gameLm,
                        const Vec3& centre, std::vector<GpuTexVertex>& out) {''', '''bool modelTriangleLight(const SceneCache& scene, const PrimGeom& g, size_t i, const Vec3& n, Vec3& light) {
    if (scene.modelLights.empty() || !scene.gameLookActive() || !g.meshPoly(i)) return false;
    const auto it = scene.modelLights.find(g.ref.entity);
    if (it == scene.modelLights.end()) return false;
    light = modelLightFromSamples(it->second, n);
    return true;
}

void appendTexturedFace(const PrimGeom& g, size_t i, const Vec3& n, const Vec3& tan, uint32_t colour, const GameLightmap* gameLm,
                        const Vec3& centre, std::vector<GpuTexVertex>& out, bool propLight) {''')
patch(SC, '''            if (gameLm) {
                float luv[2];
                if (gameLm->uvFor(w[k], n, centre, luv)) v.lu = luv[0], v.lv = luv[1];
            }
            return v;''', '''            if (gameLm) {
                float luv[2];
                if (gameLm->uvFor(w[k], n, centre, luv)) v.lu = luv[0], v.lv = luv[1];
            }
            if (propLight) v.lu = v.lv = -2.0f;
            return v;''')
patch(SC, '''                appendTexturedFace(*g, i, n, tan, c, gameLm, centre, out);''', '''                Vec3 modelLight;
                const bool lit = modelTriangleLight(scene, *g, i, n, modelLight);
                appendTexturedFace(*g, i, n, lit ? modelLight : tan, c, gameLm, centre, out, lit);''')
s = read(SC)
if '#include "light_grid.h"' not in s:
    s = s.replace('#include "scene.h"', '#include "scene.h"\n#include "light_grid.h"', 1)
    assert '#include "light_grid.h"' in s
write(SC, s)
CG = 'src/viewport/camera_geometry.cpp'
patch(CG, '''            appendTexturedFace(*g, i, n, tan, c, gameLm, centre, dst);''', '''            Vec3 modelLight;
            const bool lit = modelTriangleLight(sc, *g, i, n, modelLight);
            appendTexturedFace(*g, i, n, lit ? modelLight : tan, c, gameLm, centre, dst, lit);''')
patch(CG, '''    key = mixBits(key, scene.selectionTint);''', '''    key = mixBits(key, scene.modelLightsRev);
    key = mixBits(key, scene.selectionTint);''')

# ---- the shader: an own model's light, where stock props read their light grid box
R = 'src/viewport/render.cpp'
patch(R, '''        if (!lmOk && stockRef && propLit(i)) {
            indirect = propLgv * gameSkyScale.y;  // the prop's light grid box (vs_tex_inst), x bakedLightScale
        } else if''', '''        if (!lmOk && stockRef && propLit(i)) {
            indirect = propLgv * gameSkyScale.y;  // the prop's light grid box (vs_tex_inst), x bakedLightScale
        } else if (!lmOk && !stockRef && propLit(i)) {
            // A model of an own map: the light the build's light grid gives it at its place, by the triangle's normal
            // (SceneCache::modelLights; the converter's InstanceAmbient).
            indirect = propLgv;
        } else if''')

# ---- the camera view: the lights are made in the background
CH = 'src/qtapp/camera_view.h'
patch(CH, '''    // A material's bounce colour for the preview bake''', '''    // Game Lighting on an own map: the placed models' light from the build's light grid (SceneCache::modelLights),
    // made again in the background when the geometry, the sun or the sky changed.
    void maybeLightModels();
    void modelLightsDone(std::shared_ptr<const std::unordered_map<Id, std::array<std::array<float, 3>, 56>>> lights, uint64_t key);
    bool modelLighting_ = false;
    uint64_t modelLightKey_ = 0, modelLightRev_ = ~0ull, modelLightSeenRev_ = 0;
    QElapsedTimer modelLightSeenAt_;
    // A material's bounce colour for the preview bake''')
CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''        maybeBakeGameLook();''', '''        maybeBakeGameLook();
        maybeLightModels();''')
patch(CC, '''void CameraView::gameBakeDone(std::shared_ptr<const app::GameLightmap> lm) {''', '''void CameraView::maybeLightModels() {
    const auto clear = [this] {
        if (!ctx_->scene.modelLights.empty()) {
            ctx_->scene.modelLights.clear();
            ++ctx_->scene.modelLightsRev;
            geomDirty_ = true;
            update();
        }
        modelLightKey_ = 0;
        modelLightRev_ = ~0ull;
    };
    if (!gameLookOn() || gameStockLook()) {  // off, or a stock map (its own light grid lights its props)
        clear();
        return;
    }
    if (modelLighting_) return;
    const uint64_t rev = ctx_->doc.revision() * 1000003ull ^ ctx_->scene.generation();
    if (rev == modelLightRev_) return;
    if (rev != modelLightSeenRev_) {
        modelLightSeenRev_ = rev;
        modelLightSeenAt_.restart();
    }
    // Wait until edits settle, as the lightmap bake does; the first lights are made at once.
    if (!ctx_->scene.modelLights.empty() && modelLightSeenAt_.isValid() && modelLightSeenAt_.elapsed() < 600) {
        QTimer::singleShot(650, this, [this] { update(); });
        return;
    }
    modelLightRev_ = rev;
    // The models: every item with model triangles, by its entity, at the middle of its bounds.
    std::vector<std::pair<Id, Vec3>> models;
    {
        std::unordered_set<Id> seen;
        for (const app::PrimGeom* g : ctx_->scene.items())
            if (g->hasMeshPolys() && g->bounds.valid() && seen.insert(g->ref.entity).second)
                models.push_back({g->ref.entity, (g->bounds.mins + g->bounds.maxs) * 0.5});
    }
    if (models.empty()) {
        clear();
        modelLightRev_ = rev;
        return;
    }
    const Vec3 sunDir = ctx_->scene.sunToward, sunColour = ctx_->scene.sunColor, sky = gameSkyOf(ctx_->doc);
    uint64_t h = app::gameBakeGeometryKey(ctx_->scene);
    const auto mixD = [&h](double v) {
        uint64_t b = 0;
        std::memcpy(&b, &v, sizeof(b));
        h ^= b + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h *= 0x100000001b3ull;
    };
    for (const Vec3& v : {sunDir, sunColour, sky}) mixD(v.x), mixD(v.y), mixD(v.z);
    {
        size_t loaded = 0;  // the bounce colours come from the materials' images
        for (const auto& [m, id] : texIds_) loaded += id >= 0 && id != checkerTex_;
        mixD(double(loaded));
    }
    if (h == modelLightKey_) return;
    // The world the build's grid sees: the drawn brush faces and patches (models are not in it), in their colours.
    app::GameBakeInput g = app::gatherGameBake(ctx_->scene, ctx_->scene.drawClassFor, [this](const std::string& m) { return bakeAlbedoFor(m); });
    auto tris = std::make_shared<std::vector<RayBvh::Triangle>>();
    auto albedos = std::make_shared<std::vector<Vec3>>();
    AABB bounds;
    for (const auto& p : g.polys) {
        uint32_t id = 0;
        if (p.albedo.x >= 0) {
            albedos->push_back(p.albedo);
            id = uint32_t(albedos->size());
        }
        for (const Vec3& q : p.pts) bounds.add(q);
        for (size_t k = 1; k + 1 < p.pts.size(); ++k) tris->push_back({p.pts[0], p.pts[k], p.pts[k + 1], id});
    }
    if (tris->empty() || !bounds.valid()) {
        clear();
        modelLightRev_ = rev;
        return;
    }
    const double reach = 2.0 * length(bounds.maxs - bounds.mins) + 1024.0;
    modelLighting_ = true;
    QPointer<CameraView> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, tris, albedos, models = std::move(models), sunDir, sunColour, sky, reach, h]() {
        RayBvh bvh;
        bvh.build(*tris);
        auto out = std::make_shared<std::unordered_map<Id, std::array<std::array<float, 3>, 56>>>();
        for (const auto& [id, at] : models) lightGridSamplesAt(bvh, *albedos, sunDir, sunColour, sky, reach, at, (*out)[id]);
        QMetaObject::invokeMethod(qApp, [self, out, h]() {
            if (self) self->modelLightsDone(out, h);
        }, Qt::QueuedConnection);
    })).detach();
}

void CameraView::modelLightsDone(std::shared_ptr<const std::unordered_map<Id, std::array<std::array<float, 3>, 56>>> lights, uint64_t key) {
    modelLighting_ = false;
    modelLightKey_ = key;
    if (!lights || !gameLookOn() || gameStockLook()) return;
    if (*lights == ctx_->scene.modelLights) return;  // nothing changed for any model: the geometry stays
    ctx_->scene.modelLights = *lights;
    ++ctx_->scene.modelLightsRev;
    geomDirty_ = true;
    update();
}

void CameraView::gameBakeDone(std::shared_ptr<const app::GameLightmap> lm) {''')
s = read(CC)
for inc in ('#include "light_grid.h"', '#include "ray_bvh.h"', '#include <unordered_set>'):
    if inc not in s:
        s = s.replace('#include "custom_materials.h"', '#include "custom_materials.h"\n' + inc, 1)
write(CC, s)
print('ok')
