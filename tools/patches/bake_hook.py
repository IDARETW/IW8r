import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/compile_iw8.cpp', [
 ('''    std::string surfJson;
    for (size_t si = 0; si < surfaces.size(); ++si) {
        const RenderSurface& s = surfaces[si];''', '''    // 5.11 (research 94 / 95): baked lightmaps, when the map asks for them (worldspawn iw8r_bake_lightmaps 1). The
    // surfaces get lightmap UVs (vertices split between charts), and the converter turns the extraction's lightmap pair
    // (IW7 layout, constant ambient) into the game's three native planes.
    std::vector<std::vector<std::array<float, 2>>> lightmapUv;
    std::vector<bool> lightmapped;
    std::string lightmapKeys;
    if (worldspawn && worldspawn->value("iw8r_bake_lightmaps") && *worldspawn->value("iw8r_bake_lightmaps") == "1") {
        BakeScene scene;
        for (const RenderSurface& s : surfaces) {
            BakeSurface b;
            const std::string m = lower(s.editorMaterial);
            b.lit = m.find("sky") == std::string::npos;
            for (const RenderVertex& v : s.verts) b.verts.push_back({v.pos, v.normal, v.tangent});
            b.idx = s.idx;
            scene.surfaces.push_back(std::move(b));
        }
        scene.sunDir = sunDir;
        scene.sunColor = Vec3{sunColor[0], sunColor[1], sunColor[2]};
        scene.sunIntensity = 1.0;
        if (const std::string* sl = worldspawn->value("sunlight")) {
            double v = 0;
            if (parseNumber(*sl, v) && v >= 0) scene.sunIntensity = v;
        }
        // The sky: worldspawn iw8r_bake_sky "r g b intensity" (default a pale blue at 0.35 of the sun).
        scene.skyIntensity = 0.35 * scene.sunIntensity;
        if (const std::string* sk = worldspawn->value("iw8r_bake_sky")) {
            double v[4] = {0, 0, 0, 0};
            if (std::sscanf(sk->c_str(), "%lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3]) == 4) {
                scene.skyColor = Vec3{v[0], v[1], v[2]};
                scene.skyIntensity = v[3];
            } else {
                warn.push_back("worldspawn iw8r_bake_sky '" + *sk + "' is not 'r g b intensity': the default sky is used");
            }
        }
        // Light entities: origin, colour x intensity, radius; a spot by its target or angles and fov (as the comworld).
        for (const Entity* pe : points) {
            const std::string lcls = lower(pe->classname());
            if (!isLightClass(lcls)) continue;
            BakeLight l;
            l.pos = pe->origin().value_or(Vec3{});
            double c[3] = {1, 1, 1};
            const std::string* cs = pe->value("_color");
            if (!cs) cs = pe->value("color");
            if (cs && parseVec3Text(*cs, c) && std::max({c[0], c[1], c[2]}) > 1.0)
                for (double& x : c) x /= 255.0;
            l.color = Vec3{std::max(0.0, c[0]), std::max(0.0, c[1]), std::max(0.0, c[2])};
            if (const std::string* is = pe->value("intensity")) {
                double v = 0;
                if (parseNumber(*is, v) && v >= 0) l.intensity = v;
            }
            l.radius = kDefaultLightRadius;
            if (const std::string* rs = pe->value("radius")) {
                double v = 0;
                if (parseNumber(*rs, v) && v > 0) l.radius = v;
            }
            l.spot = lcls == "light_spot";
            if (l.spot) {
                double a[3] = {0, 0, 0};
                if (const std::string* as = pe->value("angles")) parseVec3Text(*as, a);
                l.dir = sunDirection(-a[0], a[1]) * -1.0;  // the forward of pitch / yaw (pitch down positive)
                l.dir = Vec3{std::cos(a[0] * kPi / 180) * std::cos(a[1] * kPi / 180), std::cos(a[0] * kPi / 180) * std::sin(a[1] * kPi / 180),
                             -std::sin(a[0] * kPi / 180)};
                double outer = kDefaultFovOuter, inner = kDefaultFovInner;
                if (const std::string* fo = pe->value("fov_outer")) parseNumber(*fo, outer);
                if (const std::string* fi = pe->value("fov_inner")) parseNumber(*fi, inner);
                l.cosOuter = std::cos(std::clamp(outer, 1.0, 179.0) * 0.5 * kPi / 180);
                l.cosInner = std::cos(std::clamp(std::min(inner, outer), 0.5, 179.0) * 0.5 * kPi / 180);
            }
            scene.lights.push_back(l);
        }
        BakeSettings settings;
        if (const std::string* ts = worldspawn->value("iw8r_lightmap_texel")) {
            double v = 0;
            if (parseNumber(*ts, v) && v >= 1) settings.texelSize = v;
        }
        if (const std::string* rs = worldspawn->value("iw8r_bake_rays")) {
            double v = 0;
            if (parseNumber(*rs, v) && v >= 1) settings.skyRays = int(std::min(v, 1024.0));
        }
        const auto started = std::chrono::steady_clock::now();
        const BakeResult baked = bakeLightmap(scene, settings);
        if (!baked.ok) {
            warn.push_back("lightmap bake failed: " + baked.error + "; the map is built without lightmaps");
        } else {
            // The surfaces take the baked vertex split; their other attributes come from the source vertex.
            for (size_t si = 0; si < surfaces.size(); ++si) {
                RenderSurface& s = surfaces[si];
                std::vector<RenderVertex> verts;
                verts.reserve(baked.sourceVertex[si].size());
                for (const uint32_t src : baked.sourceVertex[si]) verts.push_back(s.verts[src]);
                s.verts = std::move(verts);
                s.idx = baked.surfaces[si].idx;
            }
            lightmapUv = baked.uv;
            for (const BakeSurface& b : scene.surfaces) lightmapped.push_back(b.lit);
            const EncodedLightmap enc = encodeLightmapPair(baked);
            const std::string stem = "maps/mp/" + id + ".lightmap0";
            customFiles.push_back({root / "extracted" / (stem + "_primary.bin"), std::string(enc.primary.begin(), enc.primary.end())});
            customFiles.push_back({root / "extracted" / (stem + "_secondary.bin"), std::string(enc.secondary.begin(), enc.secondary.end())});
            lightmapKeys = ",\\"lightmaps\\":[[{\\"format\\":50,\\"width\\":" + std::to_string(baked.width) + ",\\"height\\":" +
                           std::to_string(baked.height) + ",\\"file\\":" + quote(stem + "_primary.bin") + "},{\\"format\\":21,\\"width\\":" +
                           std::to_string(baked.width) + ",\\"height\\":" + std::to_string(baked.height * 2) + ",\\"file\\":" +
                           quote(stem + "_secondary.bin") + "}]],\\"lightmap_encoding\\":{\\"log_scale\\":" + num(enc.logScale) +
                           ",\\"maximum\\":" + num(enc.maximum) + ",\\"constant_ambient\\":true}";
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            warn.push_back("lightmap baked: " + std::to_string(baked.width) + " x " + std::to_string(baked.height) + ", " +
                           std::to_string(baked.charts) + " chart(s), " + std::to_string(baked.texelsLit) + " texels at " +
                           num(baked.texelSize) + " units, " + std::to_string(scene.lights.size()) + " light(s), " +
                           num(std::round(seconds * 10) / 10) + " s (research 94 / 95; in game not verified)");
        }
    }
    std::string surfJson;
    for (size_t si = 0; si < surfaces.size(); ++si) {
        const RenderSurface& s = surfaces[si];'''),
 ('''                        ",\\"binormal_sign\\":1,\\"uv\\":[" + num(v.u) + "," + num(v.v) + "],\\"lightmap_uv\\":[0,0]" +''',
  '''                        ",\\"binormal_sign\\":1,\\"uv\\":[" + num(v.u) + "," + num(v.v) + "],\\"lightmap_uv\\":" +
                        (si < lightmapUv.size() && vi < lightmapUv[si].size() && lightmapped[si]
                             ? "[" + num(lightmapUv[si][vi][0]) + "," + num(lightmapUv[si][vi][1]) + "]"
                             : std::string("[0,0]")) +'''),
 ('''        ",\\"reflection_probes\\":[" + probeJson + "]" + modelsKey + gradingKey + volumesKey + decalsKey +''',
  '''        ",\\"reflection_probes\\":[" + probeJson + "]" + modelsKey + gradingKey + volumesKey + decalsKey + lightmapKeys +'''),
])
s = io.open('src/core/compile_iw8.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
for inc in ('#include "lightmap_bake.h"', '#include <chrono>'):
    if inc not in s:
        s = s.replace('#include "stock_loot.h"' + nl, '#include "stock_loot.h"' + nl + inc + nl, 1)
        assert inc in s
io.open('src/core/compile_iw8.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
