import io

# Plan 1.11, the lightmap bake: bounce light takes the colour of the surface it bounces off (the average of the
# material's colour image, where the host knows it; 45 % grey otherwise), and the bounces after the first are
# estimated from how enclosed a texel is (the geometric series of a room's inter-reflection). The build and the
# viewport's preview bake use the same core code, so both change together.

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


H = 'src/core/lightmap_bake.h'
patch(H, '''    bool lit = true;            // false: no lightmap (sky, tool surfaces); still casts shadows
};''', '''    bool lit = true;            // false: no lightmap (sky, tool surfaces); still casts shadows
    // What the surface reflects of the light that bounces off it (linear, 0..1 per channel): the average of its
    // material's colour image. A negative x: not known, BakeSettings::bounceAlbedo grey is used. triAlbedo, when it has
    // one entry per triangle, gives it per triangle instead (the viewport bakes every polygon in one surface).
    Vec3 albedo{-1, -1, -1};
    std::vector<Vec3> triAlbedo;
};

// The bounce reflectance of a material from its colour image: the average of the sRGB bytes in linear light, kept
// inside 0.02 .. 0.9 (nothing reflects all light, and nothing none). `rgba` holds `texels` RGBA8 pixels; `step` reads
// every step-th pixel (1 = all).
Vec3 bakeAlbedoFromRgba(const uint8_t* rgba, size_t texels, size_t step = 1);''')
patch(H, '''    double bounceAlbedo = 0.45; // the bounce surface reflectance''', '''    double bounceAlbedo = 0.45; // the bounce surface reflectance where a surface does not give its own
    // Bounces after the first, estimated: the first bounce times 1 / (1 - albedo x enclosure), where enclosure is the
    // share of a texel's rays that hit geometry (0 under the open sky, 1 in a closed room) and albedo the mean of what
    // they hit. False: one bounce only.
    bool laterBounces = true;''')
C = 'src/core/lightmap_bake.cpp'
patch(C, '''    std::vector<RayBvh::Triangle> tris = scene.extraOccluders;
    for (const BakeSurface& s : r.surfaces)
        for (size_t t = 0; t + 2 < s.idx.size(); t += 3) tris.push_back({s.verts[s.idx[t]].pos, s.verts[s.idx[t + 1]].pos, s.verts[s.idx[t + 2]].pos, 0});''',
      '''    // A triangle's id: 0 for the default bounce reflectance, else 1 + its entry in `albedos` (the surface's own, or the
    // triangle's own where the surface gives one per triangle; the split surfaces keep the input's triangle order).
    std::vector<RayBvh::Triangle> tris = scene.extraOccluders;
    for (RayBvh::Triangle& t : tris) t.id = 0;
    std::vector<Vec3> albedos;
    const auto albedoId = [&](const Vec3& a) {
        albedos.push_back(Vec3{std::clamp(a.x, 0.0, 0.95), std::clamp(a.y, 0.0, 0.95), std::clamp(a.z, 0.0, 0.95)});
        return uint32_t(albedos.size());
    };
    for (size_t si = 0; si < r.surfaces.size(); ++si) {
        const BakeSurface& s = r.surfaces[si];
        const BakeSurface* in = si < scene.surfaces.size() ? &scene.surfaces[si] : nullptr;
        const bool perTri = in && !in->triAlbedo.empty() && in->triAlbedo.size() * 3 == in->idx.size() && in->idx.size() == s.idx.size();
        const uint32_t whole = in && !perTri && in->albedo.x >= 0 ? albedoId(in->albedo) : 0;
        for (size_t t = 0; t + 2 < s.idx.size(); t += 3) {
            uint32_t id = whole;
            if (perTri && in->triAlbedo[t / 3].x >= 0) id = albedoId(in->triAlbedo[t / 3]);
            tris.push_back({s.verts[s.idx[t]].pos, s.verts[s.idx[t + 1]].pos, s.verts[s.idx[t + 2]].pos, id});
        }
    }''')
patch(C, '''                const int rays = std::max(1, settings.skyRays);
                const uint32_t seed = hash3(uint32_t(i), uint32_t(i >> 32), 7u);''', '''                const int rays = std::max(1, settings.skyRays);
                Vec3 sky{0, 0, 0}, bounce{0, 0, 0}, hitAlbedo{0, 0, 0};
                int hits = 0;
                const uint32_t seed = hash3(uint32_t(i), uint32_t(i >> 32), 7u);''')
patch(C, '''                    if (!bvh.intersect(P, dir, 0.05, 1e6, hit)) {
                        A = A + skyE;
                        continue;
                    }''', '''                    if (!bvh.intersect(P, dir, 0.05, 1e6, hit)) {
                        sky = sky + skyE;
                        continue;
                    }''')
patch(C, '''                    A = A + Eh * settings.bounceAlbedo;
                }
                A = A * (1.0 / rays);''', '''                    // The light leaves the surface in the surface's own colour.
                    const Vec3 rho = ht.id > 0 && ht.id <= albedos.size() ? albedos[ht.id - 1]
                                                                          : Vec3{settings.bounceAlbedo, settings.bounceAlbedo, settings.bounceAlbedo};
                    bounce = bounce + Vec3{Eh.x * rho.x, Eh.y * rho.y, Eh.z * rho.z};
                    hitAlbedo = hitAlbedo + rho;
                    ++hits;
                }
                if (settings.laterBounces && hits > 0) {
                    // The bounces after the first: each sends on albedo x enclosure of what the one before it brought.
                    const double enclosure = double(hits) / double(rays);
                    const Vec3 mean = hitAlbedo * (1.0 / hits);
                    bounce = Vec3{bounce.x / (1.0 - std::min(0.9, mean.x * enclosure)), bounce.y / (1.0 - std::min(0.9, mean.y * enclosure)),
                                  bounce.z / (1.0 - std::min(0.9, mean.z * enclosure))};
                }
                A = (sky + bounce) * (1.0 / rays);''')
patch(C, '''BakeResult bakeLightmap(''', '''Vec3 bakeAlbedoFromRgba(const uint8_t* rgba, size_t texels, size_t step) {
    if (!rgba || texels == 0) return Vec3{-1, -1, -1};
    const auto linear = [](uint8_t b) {
        const double c = b / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    double sum[3] = {0, 0, 0};
    size_t n = 0;
    for (size_t k = 0; k < texels; k += std::max<size_t>(1, step), ++n)
        for (int c = 0; c < 3; ++c) sum[c] += linear(rgba[k * 4 + size_t(c)]);
    if (n == 0) return Vec3{-1, -1, -1};
    return Vec3{std::clamp(sum[0] / double(n), 0.02, 0.9), std::clamp(sum[1] / double(n), 0.02, 0.9), std::clamp(sum[2] / double(n), 0.02, 0.9)};
}

BakeResult bakeLightmap(''')

# ---- the build: each surface's material gives its albedo
K = 'src/core/compile_iw8.cpp'
patch(K, '''        BakeScene scene;
        for (const RenderSurface& s : surfaces) {
            BakeSurface b;
            const std::string m = lower(s.editorMaterial);
            b.lit = m.find("sky") == std::string::npos;''', '''        BakeScene scene;
        // The bounce colour of a material: the average of its colour image (library materials: own ones, and the stock
        // ones the editor put there with their textures). Not known: the bake's grey.
        std::map<std::string, Vec3> bounceColour;
        const auto bounceOf = [&](const std::string& material) {
            auto it = bounceColour.find(material);
            if (it != bounceColour.end()) return it->second;
            Vec3 a{-1, -1, -1};
            if (const CustomMaterial* cm = opt.library ? opt.library->find(material) : nullptr; cm && !cm->colorImage.empty()) {
                int w = 0, h = 0;
                std::vector<uint8_t> px;
                std::string ierr;
                if (decodeImageFile(cm->colorImage, w, h, px, &ierr) && w > 0 && h > 0) {
                    a = bakeAlbedoFromRgba(px.data(), size_t(w) * size_t(h), std::max<size_t>(1, size_t(w) * size_t(h) / 4096));
                    if (cm->hasTint() && a.x >= 0)
                        a = Vec3{std::min(0.9, a.x * cm->tint[0]), std::min(0.9, a.y * cm->tint[1]), std::min(0.9, a.z * cm->tint[2])};
                }
            }
            return bounceColour.emplace(material, a).first->second;
        };
        for (const RenderSurface& s : surfaces) {
            BakeSurface b;
            const std::string m = lower(s.editorMaterial);
            b.lit = m.find("sky") == std::string::npos;
            b.albedo = bounceOf(s.editorMaterial);''')

# ---- the viewport's preview bake: each polygon's material
GH = 'src/viewport/game_lightmap.h'
patch(GH, '''        std::vector<Vec3> pts;
        Vec3 normal, centroid;
    };''', '''        std::vector<Vec3> pts;
        Vec3 normal, centroid;
        Vec3 albedo{-1, -1, -1};  // the material's bounce colour (BakeSurface::albedo); negative: the bake's grey
    };''')
patch(GH, '''GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor);''',
      '''// albedoFor: a material's bounce colour (negative x or no function: the bake's grey).
GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor,
                             const std::function<Vec3(const std::string&)>& albedoFor = {});''')
G = 'src/viewport/game_lightmap.cpp'
patch(G, '''GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor) {''',
      '''GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor,
                             const std::function<Vec3(const std::string&)>& albedoFor) {''')
patch(G, '''                p.centroid = c * (1.0 / double(w.size()));
                in.polys.push_back(std::move(p));''', '''                p.centroid = c * (1.0 / double(w.size()));
                if (albedoFor) p.albedo = albedoFor(*mat);
                in.polys.push_back(std::move(p));''')
patch(G, '''        for (size_t k = 1; k + 1 < p.pts.size(); ++k) {
            s.idx.push_back(base);
            s.idx.push_back(base + uint32_t(k));
            s.idx.push_back(base + uint32_t(k + 1));
        }''', '''        for (size_t k = 1; k + 1 < p.pts.size(); ++k) {
            s.idx.push_back(base);
            s.idx.push_back(base + uint32_t(k));
            s.idx.push_back(base + uint32_t(k + 1));
            s.triAlbedo.push_back(p.albedo);
        }''')

CH = 'src/qtapp/camera_view.h'
patch(CH, '''    MaskLookup specLookup_;''', '''    // A material's bounce colour for the preview bake (the average of its colour image), kept per image file.
    Vec3 bakeAlbedoFor(const std::string& material);
    std::unordered_map<std::string, Vec3> bakeAlbedo_;
    MaskLookup specLookup_;''')
CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''        app::GameBakeInput g = app::gatherGameBake(ctx_->scene, ctx_->scene.drawClassFor);''',
      '''        app::GameBakeInput g = app::gatherGameBake(ctx_->scene, ctx_->scene.drawClassFor, [this](const std::string& m) { return bakeAlbedoFor(m); });''')
patch(CC, '''        mixD(in.scene.ambientAt ? 1.0 : 0.0);''', '''        mixD(in.scene.ambientAt ? 1.0 : 0.0);
        // The bounce colours come from the materials' images: bake again when more of them have arrived.
        {
            size_t loaded = 0;
            for (const auto& [m, id] : texIds_) loaded += id >= 0 && id != checkerTex_;
            mixD(double(loaded));
        }''')
patch(CC, '''int CameraView::specFor(const std::string& material) {''', '''Vec3 CameraView::bakeAlbedoFor(const std::string& material) {
    const Vec3 unknown{-1, -1, -1};
    if (material.empty() || !imageLookup_) return unknown;
    const std::optional<QString> path = imageLookup_(material);
    if (!path || path->isEmpty()) return unknown;  // not exported yet, or no image
    const std::string file = path->toStdString();
    if (const auto it = bakeAlbedo_.find(file); it != bakeAlbedo_.end()) return it->second;
    Vec3 a = unknown;
    if (path->endsWith(".dds", Qt::CaseInsensitive)) {
        app::Rgba8Image img;  // a small level is enough for an average
        if (app::loadDdsPreview(file, 32, img) && img.width > 0 && img.height > 0)
            a = bakeAlbedoFromRgba(img.pixels.data(), size_t(img.width) * size_t(img.height));
    } else {
        int w = 0, h = 0;
        std::vector<uint8_t> px;
        std::string err;
        if (decodeImageFile(file, w, h, px, &err) && w > 0 && h > 0)
            a = bakeAlbedoFromRgba(px.data(), size_t(w) * size_t(h), std::max<size_t>(1, size_t(w) * size_t(h) / 4096));
    }
    return bakeAlbedo_[file] = a;
}

int CameraView::specFor(const std::string& material) {''')
s = read(CC)
if '#include "lightmap_bake.h"' not in s:
    s = s.replace('#include "custom_materials.h"', '#include "custom_materials.h"\n#include "lightmap_bake.h"', 1)
write(CC, s)
print('ok')
