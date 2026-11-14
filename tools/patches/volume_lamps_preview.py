import io

# Viewport: lamps flagged iw8r_volumetric light the volumetric air in the preview (research 126 section 1.4: the game
# adds a lamp's light to the air only when it is flagged). The march adds, per sample, each flagged lamp's light at
# that point (the same falloff and cone as on surfaces) times the phase and pi, as the sun's term.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/viewport/scene.h', '''        double radius = 400, cosOuter = -2, cosInner = -2;
    };''', '''        double radius = 400, cosOuter = -2, cosInner = -2;
        bool volumetric = false;  // iw8r_volumetric: the lamp lights the volumetric air
    };''')
patch('src/viewport/scene.cpp', '''        if (const std::string* r = e.value("radius")) L.radius = std::max(1.0, std::atof(r->c_str()));
        bool spot = cls == "light_spot" || e.value("fov_outer");''', '''        if (const std::string* r = e.value("radius")) L.radius = std::max(1.0, std::atof(r->c_str()));
        if (const std::string* v = e.value("iw8r_volumetric")) L.volumetric = *v == "1" || *v == "true";
        bool spot = cls == "light_spot" || e.value("fov_outer");''')

R = 'src/viewport/render.cpp'
patch(R, '''                                 float4 volBox[40]; };   // per box 5 rows: centre + density, three axes + half sizes,
                                                         // (1 / falloff, -0.1 x height fade, 0, 0)''',
      '''                                 float4 volBox[40];      // per box 5 rows: centre + density, three axes + half sizes,
                                                         // (1 / falloff, -0.1 x height fade, 0, 0)
                                 float4 volLampInfo;     // x: lamps in use (0..8), y: spot brightness, z: omni brightness
                                 float4 volLamp[24]; };  // per lamp 3 rows, as PixelLight: position + radius, colour + cos
                                                         // outer (below -1.5: omni), axis + cos inner
// The light of the flagged lamps in the air at a point, seen along V (research 126 section 1.4): each lamp's light
// there (the falloff and cone it has on surfaces), the phase toward the eye, pi, and the vision's lamp brightness.
float3 volLampLight(float3 P, float3 V, float g) {
    float3 sum = 0.0;
    const int n = int(volLampInfo.x);
    [loop] for (int k = 0; k < n; ++k) {
        const float3 dl = volLamp[k * 3].xyz - P;
        const float dist = length(dl);
        const float radius = volLamp[k * 3].w;
        if (dist >= radius) continue;
        const float3 l = dl / max(dist, 1e-3);
        const bool omni = volLamp[k * 3 + 1].w < -1.5;
        float cone = 1.0;
        if (!omni)
            cone = saturate((dot(-l, volLamp[k * 3 + 2].xyz) - volLamp[k * 3 + 1].w) / max(1e-4, volLamp[k * 3 + 2].w - volLamp[k * 3 + 1].w));
        const float w = saturate(1.0 - dist / radius) * cone;
        const float cth = dot(V, -l);  // the light travels from the lamp to P, then along -V to the eye
        const float ph = min((1.0 - g * g) / (12.566371 * pow(max(1.0 + g * g + 2.0 * g * cth, 1e-4), 1.5)), 1.0);
        sum += volLamp[k * 3 + 1].rgb * (w * w * (3.0 - 2.0 * w) / max(dist * dist, 1.0)) * ph * 3.141593 * (omni ? volLampInfo.z : volLampInfo.y);
    }
    return sum;
}''')
patch(R, '''        const float3 sun = sunShadow(P) * volQ.x * ph * 3.141593 * volSunRad.rgb * dz;''',
      '''        const float3 sun = (sunShadow(P) * volQ.x * ph * 3.141593 * volSunRad.rgb + volLampLight(P, V, g)) * dz;''')
patch(R, '''        float cb[208] = {};
        const bool volOn''', '''        float cb[308] = {};
        const bool volOn''')
patch(R, '''            std::memcpy(cb + 48, game_.volBoxes, size_t(boxes) * 20 * sizeof(float));
        }''', '''            std::memcpy(cb + 48, game_.volBoxes, size_t(boxes) * 20 * sizeof(float));
            const int lamps = std::clamp(game_.volLampCount, 0, 8);
            cb[208] = float(lamps), cb[209] = game_.volLampBrightness[0], cb[210] = game_.volLampBrightness[1];
            std::memcpy(cb + 212, game_.volLamps, size_t(lamps) * 12 * sizeof(float));
        }''')
patch('src/viewport/render.h', '''        float volBoxes[8][20] = {};
        int volBoxCount = 0;''', '''        float volBoxes[8][20] = {};
        int volBoxCount = 0;
        // Lamps that light the air (iw8r_volumetric): as PixelLight (position + radius, colour + cos outer, axis + cos
        // inner), the 8 nearest; the vision's spot and omni brightness (volumetricSpotBrightness / OmniBrightness).
        float volLamps[8][12] = {};
        int volLampCount = 0;
        float volLampBrightness[2] = {1, 1};''')

G = 'src/core/game_look.'
patch(G + 'h', '''    double volAir = 0, volAbsorption = 0, volAlbedo[3] = {1, 1, 1}, volAmbient = 1, volSun = 1, volAnisotropy = 0;''',
      '''    double volAir = 0, volAbsorption = 0, volAlbedo[3] = {1, 1, 1}, volAmbient = 1, volSun = 1, volAnisotropy = 0;
    double volSpot = 1, volOmni = 1;  // volumetricSpotBrightness / OmniBrightness: flagged lamps in the air''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''            if (p.volAir > 0 || g.volBoxCount > 0) {  // the volumetric fog (research 109 section 2)''',
      '''            // An own map's lamps that light the air (iw8r_volumetric), in the units the surfaces get: the 8 nearest.
            if (!stock && (p.volAir > 0 || g.volBoxCount > 0)) {
                std::vector<std::pair<double, size_t>> order;
                const auto& pl = pixelLightScratch_;
                for (size_t k = 0; k < ctx_->scene.lights.size() && k < pl.size(); ++k)
                    if (ctx_->scene.lights[k].volumetric) {
                        const Vec3 d = ctx_->scene.lights[k].pos - ctx_->cam.pos;
                        order.push_back({d.x * d.x + d.y * d.y + d.z * d.z, k});
                    }
                std::sort(order.begin(), order.end());
                g.volLampCount = int(std::min<size_t>(order.size(), 8));
                for (int i = 0; i < g.volLampCount; ++i) {
                    const auto& l = pl[order[size_t(i)].second];
                    float* r = g.volLamps[i];
                    r[0] = l.pos[0], r[1] = l.pos[1], r[2] = l.pos[2], r[3] = l.radius;
                    r[4] = l.color[0], r[5] = l.color[1], r[6] = l.color[2], r[7] = l.cosOuter;
                    r[8] = l.dir[0], r[9] = l.dir[1], r[10] = l.dir[2], r[11] = l.cosInner;
                }
                g.volLampBrightness[0] = float(p.volSpot), g.volLampBrightness[1] = float(p.volOmni);
            }
            if (p.volAir > 0 || g.volBoxCount > 0) {  // the volumetric fog (research 109 section 2)''')
print('ok')
