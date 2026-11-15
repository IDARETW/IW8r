import io

# Research 132 G8: an own map's lamps in Game Lighting take the game's light path, as the build gives them (the
# converter writes ComPrimaryLight records that Replay's LocalLighting shades): tube / bulb form factor, the smoothstep
# window, the spot cone, and a shadow map for spots with iw8r_shadows 1 (the only shadowed own lamps in game too).
#   - scene: PreviewLight carries the build's light keys (iw8r_bulb_radius, iw8r_falloff, iw8r_shadows on spots) and
#     the build's 150-degree rule (a wider spot is built as an omni light);
#   - camera: on an own map in Game Lighting the lamps are packed as the game's records (packStockLight; the colour is
#     the calibrated one the view already uses) with shadow cameras, and handed to the renderer's local-light path
#     (tile-binned, shadow slots); the map's own surfaces then shade with that loop instead of the simplified one.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/viewport/scene.h', '''        bool volumetric = false;  // iw8r_volumetric: the lamp lights the volumetric air
    };''', '''        bool volumetric = false;  // iw8r_volumetric: the lamp lights the volumetric air
        // The build's light keys (compile_iw8.cpp kLightKeys; research 132 G8): bulb radius (0 = the game's 1), distance
        // falloff (0 = 1), a shadow map (spots only).
        double bulbRadius = 0, falloff = 0;
        bool shadows = false;
    };''')
patch('src/viewport/scene.cpp', '''        bool spot = cls == "light_spot" || e.value("fov_outer");''', '''        bool spot = cls == "light_spot" || e.value("fov_outer");
        if (const std::string* v = e.value("iw8r_bulb_radius")) L.bulbRadius = std::max(0.0, std::atof(v->c_str()));
        if (const std::string* v = e.value("iw8r_falloff")) L.falloff = std::max(0.0, std::atof(v->c_str()));
        const bool shadowKey = [&] {
            const std::string* v = e.value("iw8r_shadows");
            return v && (*v == "1" || *v == "true");
        }();''')
patch('src/viewport/scene.cpp', '''        if (spot) {
            Vec3 ang;
            if (!e.value("target") && parse3(e.value("angles"), ang)) {''', '''        // The build turns a spot of 150 degrees or more into an omni light (compile_iw8.cpp localPrimaryLights).
        if (spot && e.value("fov_outer") && std::atof(e.value("fov_outer")->c_str()) >= 150.0) spot = false;
        L.shadows = spot && shadowKey;
        if (spot) {
            Vec3 ang;
            if (!e.value("target") && parse3(e.value("angles"), ang)) {''')

CV = 'src/qtapp/camera_view.cpp'
patch(CV, '''        renderer_.setPixelLights(pl);''', '''        renderer_.setPixelLights(pl);
        // An own map in Game Lighting: its lamps through the game's light path (research 132 G8), as built.
        {
            const bool ownGame = game && !gameStockLook() && ctx_->texturedView;
            uint64_t key = 0;
            if (ownGame) {
                key = 1469598103934665603ull;
                const auto h = [&](double v) { key = (key ^ std::hash<double>{}(v)) * 1099511628211ull; };
                for (const auto& L : ctx_->scene.lights) {
                    for (double v : {L.pos.x, L.pos.y, L.pos.z, L.color.x, L.color.y, L.color.z, L.dir.x, L.dir.y, L.dir.z, L.radius, L.cosOuter,
                                     L.cosInner, L.bulbRadius, L.falloff})
                        h(v);
                    h(L.shadows ? 1.0 : 0.0);
                }
            }
            if (key != ownLampsKey_) {
                ownLampsKey_ = key;
                std::vector<StockGameLight> lamps;
                std::vector<StockLightShadow> shadows;
                if (ownGame)
                    for (size_t k = 0; k < ctx_->scene.lights.size() && lamps.size() < 254; ++k) {
                        const auto& L = ctx_->scene.lights[k];
                        const app::SceneRenderer::PixelLight p = pixelLightOf(L, true);  // the calibrated colour
                        StockPrimaryLight sp;
                        sp.index = int(k) + 2;
                        sp.type = L.cosOuter > -1.5 ? 2 : 3;
                        sp.intensity = 1.0f;
                        for (int c = 0; c < 3; ++c) sp.color[c] = p.color[c];
                        const Vec3 back = sp.type == 2 ? Vec3{-L.dir.x, -L.dir.y, -L.dir.z} : Vec3{0, 0, 1};  // toward the lamp
                        sp.dir[0] = float(back.x), sp.dir[1] = float(back.y), sp.dir[2] = float(back.z);
                        sp.origin[0] = float(L.pos.x), sp.origin[1] = float(L.pos.y), sp.origin[2] = float(L.pos.z);
                        sp.radius = float(L.radius);
                        sp.bulbRadius = float(L.bulbRadius);
                        sp.cosHalfFovOuter = sp.type == 2 ? float(L.cosOuter) : 0.0f;
                        sp.cosHalfFovInner = sp.type == 2 ? float(L.cosInner) : 0.0f;
                        sp.distanceFalloff = L.falloff > 0 ? float(L.falloff) : 1.0f;
                        sp.canUseShadowMap = L.shadows;
                        sp.isVolumetric = L.volumetric;
                        StockGameLight g;
                        if (!packStockLight(sp, nullptr, g)) continue;
                        lamps.push_back(g);
                        StockLightShadow sh;
                        if (L.shadows) sh = stockLightShadowCamera(sp, g);
                        sh.primaryIndex = sp.index;
                        sh.cast = L.shadows;
                        shadows.push_back(sh);
                    }
                if (ownGame || ownLampsOn_) renderer_.setStockLocalLights(std::move(lamps), std::move(shadows));
                ownLampsOn_ = ownGame;
            }
        }''')
patch('src/qtapp/camera_view.h', '''    float skyRgbmRange_ = 1.0f;  // the sky faces are RGBM with this range (research 132 G10); 1 = 8-bit faces''',
      '''    float skyRgbmRange_ = 1.0f;  // the sky faces are RGBM with this range (research 132 G10); 1 = 8-bit faces
    // An own map's lamps in the renderer's local-light path (research 132 G8): the lamps' key and whether they are set.
    uint64_t ownLampsKey_ = 0;
    bool ownLampsOn_ = false;''')

R = 'src/viewport/render.cpp'
patch(R, '''        // Local lights (LocalLighting): colour x game intensity / d^2 x w^2 (3 - 2w), w = (1 - d / r) x spot cone;
        // specular as the iw3 style's (F0 .04, gloss .3).
        float3 V = normalize(eyeStart.xyz - i.w);
        int count = int(lightCount.x);''', '''        // Local lights (LocalLighting): colour x game intensity / d^2 x w^2 (3 - 2w), w = (1 - d / r) x spot cone;
        // specular as the iw3 style's (F0 .04, gloss .3).
        float3 V = normalize(eyeStart.xyz - i.w);
        int count = int(lightCount.x);
        if (localInfo.x > 0.5) {
            // The game's light path (research 132 G8): the map's lamps as the build writes them (bulb, window, cone,
            // shadow maps of spots with iw8r_shadows), tile-binned; a stock map's lamps on brushes added to it.
            const uint2 tile = uint2(i.pos.xy) / uint(localInfo.z);
            const uint base = (tile.y * uint(localInfo.y) + tile.x) * 8u;
            float3 dSum = 0, sSum = 0;
            const float gpOwn = 1.0 - sqrt(1.0 - 0.3);  // the build's gloss 0.3
            [loop] for (uint wd = 0; wd < 8u; ++wd) {
                uint bits = lightTiles[base + wd];
                [loop] while (bits != 0u) {
                    const uint b = firstbitlow(bits);
                    bits &= bits - 1u;
                    gameLocalLight(gameLights[wd * 32u + b], i.w, N, V, albedo, float3(0.04, 0.04, 0.04), gpOwn, 1.0,
                                   float4(0, 0, 0, 0), float4(0, 0, 0, 0), dSum, sSum);
                }
            }
            c += dSum + sSum;
            count = 0;  // the simplified loop below would count them twice
        }''')
print('ok')
