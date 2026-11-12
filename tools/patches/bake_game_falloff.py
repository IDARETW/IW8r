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
 ('''struct BakeLight {
    Vec3 pos;
    Vec3 color{1, 1, 1};
    double intensity = 1, radius = 256;''', '''// A light entity in the game's units (core game_look): colour x intensity is the IW3 colour (the _color and
// intensity keys); the converter turns the radius and a reference distance into the game light's intensity, which
// falls off as 1 / d^2 inside a smooth window to the radius.
struct BakeLight {
    Vec3 pos;
    Vec3 color{1, 1, 1};
    double intensity = 1, radius = 256;
    double reference = 0;  // the converter's reference distance; 0: radius / 2 (omni) or the spot axis's first hit'''),
])

hit = '''                    const double a = lightAtt(li, dist, L);
                    if (a <= 0 || bvh.occluded(P, L, 0.05, dist - 0.5)) continue;
                    const Vec3 e = l.color * (l.intensity * a * ndl);'''
patch('src/core/lightmap_bake.cpp', [
 ('#include "lightmap_bake.h"\n', '#include "lightmap_bake.h"\n\n#include "game_look.h"\n'),
 ('''    const Vec3 skyE = scene.skyColor * scene.skyIntensity;''', '''    const Vec3 skyE = scene.skyColor * scene.skyIntensity;
    // The game intensity of each light (convert.cpp): the reference is radius / 2, or for a spot the distance along its
    // axis to the first surface (at most radius / 2).
    std::vector<double> gameIntensity;
    for (const BakeLight& l : scene.lights) {
        double ref = l.reference;
        if (ref <= 0) {
            ref = l.radius * 0.5;
            RayBvh::Hit h;
            if (l.spot && bvh.intersect(l.pos, safeNormalize(l.dir, Vec3{0, 0, -1}), 1.0, ref, h)) ref = h.t;
        }
        gameIntensity.push_back(gameLightIntensity(l.radius, ref));
    }
    // A light's attenuation at distance dist toward L (unit, surface to light): the game falloff and the spot cone.
    const auto lightAtt = [&](size_t li, double dist, const Vec3& L) {
        const BakeLight& l = scene.lights[li];
        double spot = 1.0;
        if (l.spot) spot = std::clamp((dot(L * -1.0, l.dir) - l.cosOuter) / std::max(1e-4, l.cosInner - l.cosOuter), 0.0, 1.0);
        return gameLightAttenuation(gameIntensity[li], l.radius, dist, spot);
    };'''),
 ('''                for (const BakeLight& l : scene.lights) {
                    if (!l.direct) continue;''', '''                for (size_t li = 0; li < scene.lights.size(); ++li) {
                    const BakeLight& l = scene.lights[li];
                    if (!l.direct) continue;'''),
 ('''                    double att = 1.0 - dist / l.radius;
                    att *= att;
                    if (l.spot) {
                        const double cs = dot(L * -1.0, l.dir);
                        att *= std::clamp((cs - l.cosOuter) / std::max(1e-4, l.cosInner - l.cosOuter), 0.0, 1.0);
                    }
                    if (att <= 0 || bvh.occluded(P, L, 0.05, dist - 0.5)) continue;
                    const Vec3 e = l.color * (l.intensity * att * ndl);''', hit),
 ('''                    for (const BakeLight& l : scene.lights) {
                        const Vec3 d = l.pos - H;''', '''                    for (size_t li = 0; li < scene.lights.size(); ++li) {
                        const BakeLight& l = scene.lights[li];
                        const Vec3 d = l.pos - H;'''),
 ('''                        double att = 1.0 - dist / l.radius;
                        att *= att;
                        if (l.spot)
                            att *= std::clamp((dot(L * -1.0, l.dir) - l.cosOuter) / std::max(1e-4, l.cosInner - l.cosOuter), 0.0, 1.0);
                        if (att <= 0 || bvh.occluded(H, L, 0.05, dist - 0.5)) continue;
                        Eh = Eh + l.color * (l.intensity * att * ndl);''', '''                        const double a = lightAtt(li, dist, L);
                        if (a <= 0 || bvh.occluded(H, L, 0.05, dist - 0.5)) continue;
                        Eh = Eh + l.color * (l.intensity * a * ndl);'''),
])
print('ok')
