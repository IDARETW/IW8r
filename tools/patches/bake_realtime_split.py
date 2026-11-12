import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/lightmap_bake.h', [
 ('''    double cosInner = 1, cosOuter = 0.7;
};''', '''    double cosInner = 1, cosOuter = 0.7;
    // false: the game draws this light in real time (a converted primary light), so the lightmap keeps only its
    // bounce; true: its direct light is baked too.
    bool direct = true;
};'''),
 ('''    double sunIntensity = 0;                    // 0: no sun''',
  '''    double sunIntensity = 0;                    // 0: no sun
    // false: the game draws the sun in real time (the converter's world shader adds visibility x N.L x sun on top of
    // the lightmap), so the lightmap keeps only its bounce and the sun visibility.
    bool sunDirect = true;'''),
 ('''    std::vector<std::array<float, 3>> ambient, directional, direction;
    std::vector<uint8_t> covered;''', '''    std::vector<std::array<float, 3>> ambient, directional, direction;
    std::vector<uint8_t> covered;
    // Per texel: the sun's visibility (1 lit, 0 shadowed; the IW3 primary-light mask the converter's shader falls back
    // on where its baked sun depth does not reach).
    std::vector<float> sunVisibility;'''),
 ('''//   primary: width * height bytes (255: no IW3 sun mask);''',
  '''//   primary: width * height bytes, the sun visibility (255 lit; all 255 when the result has none);'''),
])

patch('src/core/lightmap_bake.cpp', [
 ('''    r.direction.assign(n, {0, 0, 1});
    std::vector<size_t> work;''', '''    r.direction.assign(n, {0, 0, 1});
    r.sunVisibility.assign(n, 1.0f);
    std::vector<size_t> work;'''),
 ('''                if (sun) {
                    const double ndl = dot(N, sunDir);
                    if (ndl > 0 && !bvh.occluded(P, sunDir, 0.05, 1e6)) {
                        const Vec3 e = sunE * ndl;
                        E = E + e;
                        dirSum = dirSum + sunDir * lumV(e);
                    }
                }
                for (const BakeLight& l : scene.lights) {''', '''                if (sun) {
                    const double ndl = dot(N, sunDir);
                    const bool visible = ndl > 0 && !bvh.occluded(P, sunDir, 0.05, 1e6);
                    r.sunVisibility[i] = visible ? 1.0f : 0.0f;
                    if (visible && scene.sunDirect) {
                        const Vec3 e = sunE * ndl;
                        E = E + e;
                        dirSum = dirSum + sunDir * lumV(e);
                    }
                }
                for (const BakeLight& l : scene.lights) {
                    if (!l.direct) continue;'''),
 ('''                    Vec3 Eh = skyE * 0.5;
                    if (sun) {
                        const double ndl = dot(Nh, sunDir);
                        if (ndl > 0 && !bvh.occluded(H, sunDir, 0.05, 1e6)) Eh = Eh + sunE * ndl;
                    }''', '''                    Vec3 Eh = skyE * 0.5;
                    if (sun) {
                        const double ndl = dot(Nh, sunDir);
                        if (ndl > 0 && !bvh.occluded(H, sunDir, 0.05, 1e6)) Eh = Eh + sunE * ndl;
                    }
                    // The light entities' direct light at the bounce point (real-time lights bounce too).
                    for (const BakeLight& l : scene.lights) {
                        const Vec3 d = l.pos - H;
                        const double dist = length(d);
                        if (dist >= l.radius || dist < 1e-3) continue;
                        const Vec3 L = d * (1.0 / dist);
                        const double ndl = dot(Nh, L);
                        if (ndl <= 0) continue;
                        double att = 1.0 - dist / l.radius;
                        att *= att;
                        if (l.spot)
                            att *= std::clamp((dot(L * -1.0, l.dir) - l.cosOuter) / std::max(1e-4, l.cosInner - l.cosOuter), 0.0, 1.0);
                        if (att <= 0 || bvh.occluded(H, L, 0.05, dist - 0.5)) continue;
                        Eh = Eh + l.color * (l.intensity * att * ndl);
                    }'''),
 ('''                std::array<float, 3> a{0, 0, 0}, b{0, 0, 0}, d{0, 0, 0};
                int count = 0;''', '''                std::array<float, 3> a{0, 0, 0}, b{0, 0, 0}, d{0, 0, 0};
                float vis = 0;
                int count = 0;'''),
 ('''                        d[c] += r.direction[j][c];
                    }
                    ++count;''', '''                        d[c] += r.direction[j][c];
                    }
                    vis += r.sunVisibility[j];
                    ++count;'''),
 ('''                    d[c] /= count;
                }''', '''                    d[c] /= count;
                }
                r.sunVisibility[i] = vis / count;'''),
 ('''    e.primary.assign(n, 255);''', '''    e.primary.assign(n, 255);
    for (size_t i = 0; i < n && i < r.sunVisibility.size(); ++i)
        e.primary[i] = uint8_t(std::clamp(std::lround(255.0 * r.sunVisibility[i]), 0L, 255L));'''),
])

patch('tests/lightmap_bake_tests.cpp', [
 ('''    CHECK(std::fabs(c[0] - 2.0f) < 0.05f && std::fabs(c[2] - 1.6f) < 0.05f && f < 0.02f && d[2] > 0.99f);
}''', '''    CHECK(std::fabs(c[0] - 2.0f) < 0.05f && std::fabs(c[2] - 1.6f) < 0.05f && f < 0.02f && d[2] > 0.99f);
    // The sun visibility: the primary byte (the IW3 mask the converter's shader reads).
    CHECK(r.sunVisibility[lit] == 1.0f && r.sunVisibility[shadowed] == 0.0f && e.primary[lit] == 255 && e.primary[shadowed] == 0);
    // A real-time sun (the game adds it on top of the lightmap): no direct sun in the lightmap, the visibility stays.
    scene.sunDirect = false;
    const BakeResult rt = bakeLightmap(scene, settings);
    CHECK(rt.ok && rt.directional[lit][0] < 1e-6 && rt.sunVisibility[lit] == 1.0f && rt.sunVisibility[shadowed] == 0.0f);
}'''),
 ('''    CHECK(roofed.ok && roofed.ambient[i][0] < 0.5f * 0.5f);
}''', '''    CHECK(roofed.ok && roofed.ambient[i][0] < 0.5f * 0.5f);
    // A real-time light under the roof: not in the directional layer, but its bounce off the roof reaches the floor.
    scene.skyIntensity = 0;
    BakeLight l;
    l.pos = Vec3(0, 0, 32);
    l.radius = 400;
    l.intensity = 4;
    l.direct = false;
    scene.lights.push_back(l);
    BakeResult bounced = bakeLightmap(scene, settings);
    CHECK(bounced.ok && bounced.directional[i][0] < 1e-6 && bounced.ambient[i][0] > 0.01f);
}'''),
])
print('ok')
