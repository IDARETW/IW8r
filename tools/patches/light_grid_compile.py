import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


C = 'src/core/compile_iw8.cpp'
patch(C, '''        LightGridFiles grid;
        std::string gerr;
        if (buildLightGrid(lg, grid, &gerr)) {''', '''        // ROADMAP 5.20: each palette cell's light from the map itself (unless worldspawn iw8r_flat_light_grid 1): the sky
        // where rays escape upward, one bounce of the sun and the sky off what they hit (albedo 0.45, as the bake), as
        // the 56 samples of the converter's model-lighting volume. Indoors props and the viewmodel go dark; under the
        // open sky they take its colour. Units: the bake's (the sun colour x 1 = 255).
        RayBvh gridBvh;
        {
            std::vector<RayBvh::Triangle> tris;
            for (const RenderSurface& s : surfaces) {
                if (lower(s.editorMaterial).find("sky") != std::string::npos) continue;
                for (size_t t = 0; t + 2 < s.idx.size(); t += 3)
                    tris.push_back({s.verts[s.idx[t]].pos, s.verts[s.idx[t + 1]].pos, s.verts[s.idx[t + 2]].pos, 0});
            }
            gridBvh.build(std::move(tris));
        }
        Vec3 gridSky{0.55, 0.65, 0.8};
        double gridSkyI = 0.35;
        if (worldspawn)
            if (const std::string* sk = worldspawn->value("iw8r_bake_sky")) {
                double v[4] = {0, 0, 0, 0};
                if (std::sscanf(sk->c_str(), "%lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3]) == 4) gridSky = Vec3{v[0], v[1], v[2]}, gridSkyI = v[3];
            }
        const bool flatGrid = worldspawn && worldspawn->value("iw8r_flat_light_grid") && *worldspawn->value("iw8r_flat_light_grid") == "1";
        if (!gridBvh.empty() && !flatGrid) {
            const Vec3 sunC{sunColor[0], sunColor[1], sunColor[2]};
            lg.samplesAt = [&gridBvh, sunDir, sunC, gridSky, gridSkyI, reach](const Vec3& p, std::array<std::array<float, 3>, 56>& out) {
                static const std::vector<Vec3> rays = [] {  // 96 directions spread evenly over the sphere
                    std::vector<Vec3> r;
                    const double ga = kPi * (3.0 - std::sqrt(5.0));
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
                    if (!gridBvh.intersect(p, d, 0.5, reach, h)) {
                        if (d.z > 0) rad = gridSky * (gridSkyI / kPi);  // the open sky; below the map, nothing
                    } else {
                        const RayBvh::Triangle& t = gridBvh.triangles()[h.tri];
                        Vec3 n = cross(t.b - t.a, t.c - t.a);
                        const double nl = length(n);
                        if (nl < 1e-12) continue;
                        n = n * (1.0 / nl);
                        if (dot(n, d) > 0) n = n * -1.0;
                        const Vec3 hp = p + d * h.t + n * 0.5;
                        const double ns = dot(n, sunDir);
                        const double eSun = ns > 0 && !gridBvh.occluded(hp, sunDir, 0.1, reach) ? ns : 0.0;
                        const Vec3 e = sunC * eSun + gridSky * (gridSkyI * 0.25 * (1.0 + n.z));  // about half its sky seen
                        rad = e * (0.45 / kPi);
                    }
                    for (int s = 0; s < 56; ++s) {
                        const double c = dot(sdirs[size_t(s)], d);
                        if (c <= 0) continue;
                        acc[s][0] += rad.x * c * w, acc[s][1] += rad.y * c * w, acc[s][2] += rad.z * c * w;
                    }
                }
                for (int s = 0; s < 56; ++s)
                    for (int ch = 0; ch < 3; ++ch) out[size_t(s)][size_t(ch)] = float(255.0 * acc[s][ch]);
            };
        }
        LightGridFiles grid;
        std::string gerr;
        if (buildLightGrid(lg, grid, &gerr)) {''')
patch(C, '''            warn.push_back("light grid: " + std::to_string(grid.entryCount) + " points (" + std::to_string(grid.sunlit) +
                           " sunlit) for viewmodel and model lighting");''', '''            warn.push_back("light grid: " + std::to_string(grid.entryCount) + " points (" + std::to_string(grid.sunlit) +
                           " sunlit), " + std::to_string(grid.colorCount) + " colour(s) " +
                           (lg.samplesAt ? std::string("baked from the map (sky and bounce)") : std::string("(flat ambient)")) +
                           " for viewmodel and model lighting");''')
print('ok')
