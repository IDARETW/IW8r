import io

# Viewport: an own map's volumetric boxes (iw8r_volume) in the volumetric preview. The march adds each box's fog at a
# sample the way the game's scatter pass does (research 127 section A.2): density per unit inside the box, a border of
# `falloff` units with rounded corners, exp(-0.1 x height_fade x height above the box's bottom). A map with boxes and
# no air still runs the march (the vision's table defaults light the boxes: sun 1, ambient 1, g 0.15).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


R = 'src/viewport/render.cpp'
patch(R, '''                                 float4 volP; float4 volQ; float4 volAmb; };''',
      '''                                 float4 volP; float4 volQ; float4 volAmb;
                                 float4 volBoxInfo;      // x: boxes in use (0..8)
                                 float4 volBox[40]; };   // per box 5 rows: centre + density, three axes + half sizes,
                                                         // (1 / falloff, -0.1 x height fade, 0, 0)
// The fog a volumetric box adds at a point (research 127 section A.2): extinction per unit.
float volBoxSigma(float3 P) {
    float sum = 0.0;
    const int n = int(volBoxInfo.x);
    [loop] for (int b = 0; b < n; ++b) {
        const float3 rel = P - volBox[b * 5].xyz;
        const float3 local = float3(dot(rel, volBox[b * 5 + 1].xyz), dot(rel, volBox[b * 5 + 2].xyz), dot(rel, volBox[b * 5 + 3].xyz));
        const float3 halfSize = float3(volBox[b * 5 + 1].w, volBox[b * 5 + 2].w, volBox[b * 5 + 3].w);
        if (any(abs(local) > halfSize)) continue;
        const float invFalloff = volBox[b * 5 + 4].x;
        // 0 in the core, 1 at a face; the corners are rounded.
        const float3 q = max(abs(local) * invFalloff - (halfSize * invFalloff - 1.0), 0.0);
        sum += volBox[b * 5].w * saturate(1.0 - length(q)) * exp(volBox[b * 5 + 4].y * (local.z + halfSize.z));
    }
    return sum;
}''')
patch(R, '''        const float sig = volAlbedo.w * exp(max(P.z - volP.z, 0.0) * volP.w);''',
      '''        const float sig = volAlbedo.w * exp(max(P.z - volP.z, 0.0) * volP.w) + volBoxSigma(P);''')
patch(R, '''        float cb[44] = {};
        const bool volOn''', '''        float cb[208] = {};
        const bool volOn''')
patch(R, '''            cb[40] = game_.sky[0], cb[41] = game_.sky[1], cb[42] = game_.sky[2];
        }''', '''            cb[40] = game_.sky[0], cb[41] = game_.sky[1], cb[42] = game_.sky[2];
            const int boxes = std::clamp(game_.volBoxCount, 0, 8);
            cb[44] = float(boxes);
            std::memcpy(cb + 48, game_.volBoxes, size_t(boxes) * 20 * sizeof(float));
        }''')
H = 'src/viewport/render.h'
patch(H, '''        bool volOwn = false;''', '''        bool volOwn = false;
        // Volumetric boxes (an own map's iw8r_volume, research 127): per box centre + density, the three axes with the
        // half sizes, (1 / falloff, -0.1 x height fade, 0, 0). At most 8 (the nearest to the camera).
        float volBoxes[8][20] = {};
        int volBoxCount = 0;''')

CH = 'src/qtapp/camera_view.h'
patch(CH, '''    GamePostFx ownPostFx_;
''', '''    GamePostFx ownPostFx_;
    // The map's volumetric boxes for the preview: centre, density, axes, half sizes, 1 / falloff, height factor.
    struct OwnVolumeBox {
        Vec3 center, axis[3];
        double half[3] = {0, 0, 0}, density = 0, invFalloff = 0, heightK = 0;
    };
    std::vector<OwnVolumeBox> ownVolumes_;
''')
C = 'src/qtapp/camera_view.cpp'
patch(C, '''            ownFog_ = GameFog{};
            ownFogRefdef_ = GameFogRefdef{};
            ownPostFx_ = GamePostFx{};
''', '''            ownFog_ = GameFog{};
            ownFogRefdef_ = GameFogRefdef{};
            ownPostFx_ = GamePostFx{};
            // Volumetric boxes, read as the build reads them (core compile_iw8: defaults, the falloff cap, height_half).
            ownVolumes_.clear();
            for (const Entity& e : ctx_->doc.map().entities) {
                if (e.classname() != "iw8r_volume") continue;
                const auto three = [&](const char* key, double a, double b, double c) {
                    std::array<double, 3> v{a, b, c};
                    if (const std::string* s = e.value(key)) {
                        double x = 0, y = 0, z = 0;
                        if (std::sscanf(s->c_str(), "%lf %lf %lf", &x, &y, &z) == 3) v = {x, y, z};
                    }
                    return v;
                };
                const auto one = [&](const char* key, double def) {
                    const std::string* s = e.value(key);
                    char* end = nullptr;
                    const double v = s ? std::strtod(s->c_str(), &end) : def;
                    return s && end != s->c_str() && std::isfinite(v) ? v : def;
                };
                const auto mn = three("mins", -128, -128, -128), mx = three("maxs", 128, 128, 128);
                if (!(mx[0] > mn[0] && mx[1] > mn[1] && mx[2] > mn[2])) continue;
                const auto ang = three("angles", 0, 0, 0);
                const auto ax = anglesToAxis(Vec3{ang[0], ang[1], ang[2]});
                OwnVolumeBox box;
                const Vec3 o = e.origin().value_or(Vec3{});
                box.center = o + ax[0] * ((mn[0] + mx[0]) / 2) + ax[1] * ((mn[1] + mx[1]) / 2) + ax[2] * ((mn[2] + mx[2]) / 2);
                for (int k = 0; k < 3; ++k) box.axis[k] = ax[size_t(k)], box.half[k] = (mx[size_t(k)] - mn[size_t(k)]) / 2;
                box.density = std::max(one("density", 0.001), 0.0);
                const double falloff = std::min(std::max(one("falloff", 32), 1e-3), std::min({box.half[0], box.half[1], box.half[2]}));
                box.invFalloff = 1.0 / falloff;
                double fade = std::clamp(one("height_fade", 0), 0.0, 1.0);
                if (const double hh = one("height_half", 0); hh >= 6.931) fade = 6.931 / hh;
                box.heightK = -0.1 * fade;
                if (box.density > 0) ownVolumes_.push_back(box);
            }
''')
patch(C, '''        {
            const GamePostFx& p = postFx;
            if (p.volAir > 0) {  // the volumetric fog (research 109 section 2)''', '''        {
            const GamePostFx& p = postFx;
            // An own map's volumetric boxes (research 127): the 8 nearest to the camera go to the march.
            if (!stock && !ownVolumes_.empty()) {
                std::vector<const OwnVolumeBox*> near;
                for (const OwnVolumeBox& b : ownVolumes_) near.push_back(&b);
                const Vec3 eye = ctx_->cam.pos;
                const auto far2 = [&](const OwnVolumeBox* b) {
                    const Vec3 d = b->center - eye;
                    return d.x * d.x + d.y * d.y + d.z * d.z;
                };
                std::sort(near.begin(), near.end(), [&](const OwnVolumeBox* a, const OwnVolumeBox* b) { return far2(a) < far2(b); });
                g.volBoxCount = int(std::min<size_t>(near.size(), 8));
                for (int i = 0; i < g.volBoxCount; ++i) {
                    const OwnVolumeBox& b = *near[size_t(i)];
                    float* r = g.volBoxes[i];
                    r[0] = float(b.center.x), r[1] = float(b.center.y), r[2] = float(b.center.z), r[3] = float(b.density);
                    for (int k = 0; k < 3; ++k)
                        r[4 + k * 4] = float(b.axis[k].x), r[5 + k * 4] = float(b.axis[k].y), r[6 + k * 4] = float(b.axis[k].z), r[7 + k * 4] = float(b.half[k]);
                    r[16] = float(b.invFalloff), r[17] = float(b.heightK);
                }
            }
            if (p.volAir > 0 || g.volBoxCount > 0) {  // the volumetric fog (research 109 section 2)''')
print('ok')
