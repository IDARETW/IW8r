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
 ('''    std::vector<float> sunVisibility;''', '''    std::vector<float> sunVisibility;
    // Per texel: the direction of `direction` in world space (the viewport shades in world space).
    std::vector<std::array<float, 3>> directionWorld;'''),
])
patch('src/core/lightmap_bake.cpp', [
 ('''    r.sunVisibility.assign(n, 1.0f);''', '''    r.sunVisibility.assign(n, 1.0f);
    r.directionWorld.assign(n, {0, 0, 1});'''),
 ('''                r.direction[i] = {float(sx * dz), float(sy * dz), float(dz)};''',
  '''                r.direction[i] = {float(sx * dz), float(sy * dz), float(dz)};
                const Vec3 dw = safeNormalize(T * (sx * dz) + B * (sy * dz) + N * dz, N);
                r.directionWorld[i] = {float(dw.x), float(dw.y), float(dw.z)};'''),
 ('''                float vis = 0;
                int count = 0;''', '''                float vis = 0;
                std::array<float, 3> dwSum{0, 0, 0};
                int count = 0;'''),
 ('''                    vis += r.sunVisibility[j];''', '''                    vis += r.sunVisibility[j];
                    for (int c = 0; c < 3; ++c) dwSum[c] += r.directionWorld[j][c];'''),
 ('''                r.sunVisibility[i] = vis / count;''', '''                r.sunVisibility[i] = vis / count;
                const double dwl = std::sqrt(dwSum[0] * dwSum[0] + dwSum[1] * dwSum[1] + dwSum[2] * dwSum[2]);
                if (dwl > 1e-6) r.directionWorld[i] = {float(dwSum[0] / dwl), float(dwSum[1] / dwl), float(dwSum[2] / dwl)};'''),
])
patch('tests/lightmap_bake_tests.cpp', [
 ('''    CHECK(std::fabs(r.direction[lit][2] - 1.0f) < 1e-4);''', '''    CHECK(std::fabs(r.direction[lit][2] - 1.0f) < 1e-4 && std::fabs(r.directionWorld[lit][2] - 1.0f) < 1e-4);'''),
])
print('ok')
