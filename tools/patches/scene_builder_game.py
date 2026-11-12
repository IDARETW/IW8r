import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/viewport/scene.cpp', [
 ('#include "brush_flags.h"\n', '#include "brush_flags.h"\n#include "game_lightmap.h"\n'),
 ('''            Vec3 sunLit;  // the sun's part of lit (the shadow map removes it where the sun is blocked)
            if (scene.litPreview) {''', '''            Vec3 sunLit;  // the sun's part of lit (the shadow map removes it where the sun is blocked)
            // View > Game Lighting: the renderer lights textured faces; colours carry only tints (camera_geometry.cpp).
            const bool litMode = scene.litPreview && !scene.gameLook;
            const bool encodedMode = scene.litEncoded() && !scene.gameLook;
            const GameLightmap* gameLm = scene.gameLook ? scene.gameLightmap.get() : nullptr;
            if (litMode) {'''),
 ('''            auto litColor = [&](uint32_t base) {
                if (!scene.litPreview) return scaleColor(base, shade);
                if (!scene.litEncoded()) {''', '''            auto litColor = [&](uint32_t base) {
                if (!litMode) return scaleColor(base, shade);
                if (!encodedMode) {'''),
 ('''            if (texId >= 0) {
                // Brush entities keep a hint of their class colour; selection tints red.
                uint32_t c = litColor(''', '''            if (texId >= 0) {
                if (scene.gameLook) shade = 1.0;
                // Brush entities keep a hint of their class colour; selection tints red.
                uint32_t c = litColor('''),
 ('''                auto tv = [&](size_t k) {
                    GpuTexVertex v{float(w[k].x), float(w[k].y), float(w[k].z), uv[k][0], uv[k][1], c};
                    v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
                    v.tx = float(tan.x), v.ty = float(tan.y), v.tz = float(tan.z);
                    return v;
                };''', '''                Vec3 centre;
                if (gameLm) {
                    for (const auto& p : w) centre += p;
                    centre = centre * (1.0 / double(std::max<size_t>(1, w.size())));
                }
                auto tv = [&](size_t k) {
                    GpuTexVertex v{float(w[k].x), float(w[k].y), float(w[k].z), uv[k][0], uv[k][1], c};
                    v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
                    v.tx = float(tan.x), v.ty = float(tan.y), v.tz = float(tan.z);
                    if (gameLm) {
                        float luv[2];
                        if (gameLm->uvFor(w[k], n, centre, luv)) v.lu = luv[0], v.lv = luv[1];
                    }
                    return v;
                };'''),
 ('''                if (&dst == &noCast && scene.litEncoded()) c = (c & 0x00ffffffu) | 0x80000000u;''',
  '''                if (&dst == &noCast && encodedMode) c = (c & 0x00ffffffu) | 0x80000000u;'''),
])
print('ok')
