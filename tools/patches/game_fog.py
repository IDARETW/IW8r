import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---------------- core
patch('src/core/game_look.h', [
 ('''// sRGB texture value to linear (the game samples colour maps as sRGB).''', '''// A stock map's atmosphere from its vision (vision/<map>.vision; field tokens from the x64-zt converter's
// replay_vision_names.inc): distance fog that halves every halfPlane units past start, scaled by a height falloff
// (halves every heightHalf units above heightBase), up to maxOpacity; the inscattered light is
// 0.001 x 2^(inscatterEv - 2.84) / (4 pi) x fogColor (calibrated 2026-11-12 on mp_m_speedball: the distant haze in the
// owner's screenshot is about 7 scene units, this gives 7.2), tinted by the Rayleigh albedo and, toward the sun, by the
// Mie albedo with a Henyey-Greenstein lobe (mieBlend, mieAnisotropy). The sky takes the fog of skyDistance, times
// skyIntensity. Surfaces lose saturation by `desaturation` x the fog amount.
struct GameFog {
    bool on = false;
    double start = 0, halfPlane = 5000, maxOpacity = 1, maxDistance = 1e9, skyDistance = 40000, skyIntensity = 0.5;
    double heightBase = 0, heightHalf = 1e9, desaturation = 0, inscatterEv = 18;
    double color[3] = {1, 1, 1}, rayleigh[3] = {1, 1, 1}, mie[3] = {1, 1, 1};
    double mieAnisotropy = 0.4, mieBlend = 0;
};
// Reads the vision text (`TOKEN "value"` lines). `on` only when the atmospheric fog fields are there.
GameFog gameFogFromVision(const std::string& visionText);
// The inscattered radiance scale, in scene light units.
double gameFogInscatter(const GameFog& f);

// sRGB texture value to linear (the game samples colour maps as sRGB).'''),
])
patch('src/core/game_look.cpp', [
 ('''double srgbToLinear(double c) {''', '''GameFog gameFogFromVision(const std::string& text) {
    GameFog f;
    std::map<std::string, std::string> v;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t q1 = line.find('"'), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::string key = line.substr(0, q1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\\t')) key.pop_back();
        while (!key.empty() && (key.front() == ' ' || key.front() == '\\t')) key.erase(key.begin());
        v[key] = line.substr(q1 + 1, q2 - q1 - 1);
    }
    const auto num = [&](const char* tok, double& out) {
        const auto it = v.find(tok);
        if (it == v.end() || it->second.empty()) return false;
        try {
            out = std::stod(it->second);
        } catch (...) {
            return false;
        }
        return true;
    };
    const auto vec = [&](const char* tok, double out[3]) {
        const auto it = v.find(tok);
        if (it == v.end()) return false;
        double a[3];
        if (std::sscanf(it->second.c_str(), "%lf %lf %lf", &a[0], &a[1], &a[2]) != 3) return false;
        for (int k = 0; k < 3; ++k) out[k] = a[k];
        return true;
    };
    double scatter = 0;
    const bool haveHalf = num("SNNNRLTML", f.halfPlane);           // fogHalfPlaneDistance
    num("MKKLQTSKSO", scatter);                                    // fogAtmosphericScattering
    num("RPNOKKL", f.start);                                       // fogStartDist
    num("TRPROKTLS", f.maxOpacity);                                // fogMaxOpacity
    num("NRTMTRQTRQ", f.maxDistance);                              // fogMaxDistance
    num("NLQKSSPMKP", f.skyDistance);                              // fogSkyDistance
    num("NORQKLTPTR", f.skyIntensity);                             // fogSkyIntensity
    num("NOSTLRKPQ", f.heightBase);                                // fogHeightFogBaseHeight
    num("POKSTQPPQ", f.heightHalf);                                // fogHeightFogHalfPlaneDistance
    num("MKQSQRPPQR", f.desaturation);                             // fogExtinctionDesaturation
    num("LNOQTMMLSP", f.inscatterEv);                              // fogInscatteringEv
    vec("NMLMMNQLSQ", f.color);                                    // fogColor
    vec("LNMRMNLNTT", f.rayleigh);                                 // fogRayleighAlbedo
    vec("LRKPNSNORL", f.mie);                                      // fogMieAlbedo
    num("MPRLMQNQO", f.mieAnisotropy);                             // fogMieAnisotropy
    num("NPMPRRLNPO", f.mieBlend);                                 // fogMieBlendStrength
    f.on = haveHalf && scatter > 0 && f.halfPlane > 0 && f.maxOpacity > 0;
    if (f.heightHalf <= 0) f.heightHalf = 1e9;
    return f;
}

double gameFogInscatter(const GameFog& f) { return 0.001 * std::exp2(f.inscatterEv - 2.84) / (4.0 * 3.14159265358979); }

double srgbToLinear(double c) {'''),
])
s = io.open('src/core/game_look.cpp', encoding='utf-8', newline='').read()
for inc in ('#include <map>', '#include <cstdio>'):
    if inc not in s:
        s = s.replace('#include <cmath>', '#include <cmath>\n' + inc, 1)
io.open('src/core/game_look.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('tests/game_look_tests.cpp', [
 ('''TEST(game_look_local_lights) {''', '''TEST(game_look_fog_from_vision) {
    // mp_m_speedball's vision lines (tokens of the converter's replay_vision_names.inc).
    const std::string text = "SNNNRLTML \\"2550\\"\\nMKKLQTSKSO \\"1\\"\\nRPNOKKL \\"1000\\"\\nTRPROKTLS \\"1\\"\\n"
                             "LNOQTMMLSP \\"19.3\\"\\nLNMRMNLNTT \\"0.9176471 0.9568627 1\\"\\nPOKSTQPPQ \\"5000\\"\\n"
                             "NPMPRRLNPO \\"0.3\\"\\nMPRLMQNQO \\"0.7\\"\\n";
    const GameFog f = gameFogFromVision(text);
    CHECK(f.on && f.halfPlane == 2550 && f.start == 1000 && f.heightHalf == 5000 && std::fabs(f.rayleigh[0] - 0.9176471) < 1e-9);
    CHECK(std::fabs(gameFogInscatter(f) - 7.2) < 0.2);  // the haze of the owner's screenshot, about 7 scene units
    CHECK(!gameFogFromVision("RPNOKKL \\"1000\\"\\n").on);
}

TEST(game_look_local_lights) {'''),
])

# ---------------- renderer
patch('src/viewport/render.h', [
 ('''        bool skyIsStock = false;  // sky = the stock map's mean baked light: unlit surfaces take the probes at that level''',
  '''        bool skyIsStock = false;  // sky = the stock map's mean baked light: unlit surfaces take the probes at that level
        // The stock map's atmosphere (core GameFog): fog0 = start, half distance, max opacity, on; fog1 = inscattered
        // radiance (rgb, fog colour x scale), Mie blend; fog2 = Rayleigh albedo, Mie anisotropy; fog3 = Mie albedo,
        // desaturation; fog4 = height base, height half distance, sky distance, sky intensity; fog5.x = max distance;
        // skyScale: the sky texture (LDR) to scene light.
        float fog[6][4] = {};
        float skyScale = 1.0f;'''),
])
patch('src/viewport/render.cpp', [
 ('''cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; };''',
  '''cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; float4 gameFog[6]; float4 gameSkyScale; };
// The stock map's atmosphere (GameLook::fog): distance fog with a height falloff, Rayleigh / Mie inscattering, extinction
// desaturation. `sky`: the sky takes the fog of the sky distance times the sky intensity.
float3 gameFogApply(float3 c, float3 w, bool sky) {
    if (gameFog[0].w < 0.5) return c;
    float3 v = w - eyeStart.xyz;
    float d = length(v);
    float3 dir = v / max(d, 1e-3);
    if (sky) d = gameFog[4].z;
    d = min(d, gameFog[5].x);
    float dd = max(d - gameFog[0].x, 0.0);
    float a = (eyeStart.z - gameFog[4].x) / gameFog[4].y;
    float b = a + dir.z * d / gameFog[4].y;
    float hf = abs(b - a) > 1e-3 ? (exp2(-a) - exp2(-b)) / ((b - a) * 0.693147) : exp2(-a);
    float amount = min(1.0 - exp2(-dd / gameFog[0].y * max(hf, 0.0)), gameFog[0].z);
    if (sky) amount *= gameFog[4].w;
    float g = gameFog[2].w, mu = dot(dir, gameSun.xyz);
    float hg = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * mu, 1e-4), 1.5);
    float3 inscatter = gameFog[1].rgb * lerp(gameFog[2].rgb, gameFog[3].rgb * hg, gameFog[1].w);
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(c, l.xxx, gameFog[3].w * amount);
    return lerp(c, inscatter, amount);
}'''),
 ('''        if (!stockRef && i.col.a < 0.5) return float4(gamePost(albedo), 1.0);  // sky box: unlit''',
  '''        if (!stockRef && i.col.a < 0.5) return float4(gamePost(gameFogApply(albedo * gameSkyScale.x, i.w, true)), 1.0);  // sky box'''),
 ('''        if (batchFlags2.x > 0.5) c += srgbToLinear3(emi0.Sample(samp0, i.uv).rgb);
        c = applyFog(c, i.w);
        c = gamePost(c);''', '''        if (batchFlags2.x > 0.5) c += srgbToLinear3(emi0.Sample(samp0, i.uv).rgb);
        c = gameFog[0].w > 0.5 ? gameFogApply(c, i.w, false) : applyFog(c, i.w);
        c = gamePost(c);'''),
 ('''            bd.ByteWidth = 64;  // game0, sun toward, sun colour, unbaked light''',
  '''            bd.ByteWidth = 176;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale'''),
 ('''    const float g[16] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, envBrdfSrv_ ? 1.0f : 0.0f,
                         game_.sunToward[0], game_.sunToward[1], game_.sunToward[2], 0,
                         game_.sunColor[0], game_.sunColor[1], game_.sunColor[2], 0,
                         game_.sky[0], game_.sky[1], game_.sky[2], game_.skyIsStock ? 1.0f : 0.0f};''',
  '''    float g[44] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, envBrdfSrv_ ? 1.0f : 0.0f,
                   game_.sunToward[0], game_.sunToward[1], game_.sunToward[2], 0,
                   game_.sunColor[0], game_.sunColor[1], game_.sunColor[2], 0,
                   game_.sky[0], game_.sky[1], game_.sky[2], game_.skyIsStock ? 1.0f : 0.0f};
    for (int r = 0; r < 6; ++r)
        for (int k = 0; k < 4; ++k) g[16 + 4 * r + k] = game_.fog[r][k];
    g[40] = game_.skyScale;'''),
])

# ---------------- camera: the stock vision's fog
patch('src/qtapp/camera_view.h', [
 ('''    // The stock map's own sun (its ComWorld sun light: intensity, linear colour, direction toward the sun).''',
  '''    // The stock map's atmosphere (its vision text; core gameFogFromVision).
    void setGameStockVision(const std::string& visionText) {
        stockFog_ = gameFogFromVision(visionText);
        update();
    }
    // The stock map's own sun (its ComWorld sun light: intensity, linear colour, direction toward the sun).'''),
 ('''    bool stockSunKnown_ = false;''', '''    bool stockSunKnown_ = false;
    GameFog stockFog_;'''),
 ('''        if (zone != gameStockMap_) gameBakeRev_ = ~0ull, stockSunKnown_ = false;  // the preview bake follows that map's sun''',
  '''        if (zone != gameStockMap_) gameBakeRev_ = ~0ull, stockSunKnown_ = false, stockFog_ = GameFog{};  // the bake follows the sun'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''        if (stock && stockMeanLight_[0] + stockMeanLight_[1] + stockMeanLight_[2] > 0) {''', '''        if (stock && stockFog_.on) {
            const GameFog& f = stockFog_;
            const double s = gameFogInscatter(f);
            const float fog[6][4] = {{float(f.start), float(f.halfPlane), float(f.maxOpacity), 1.0f},
                                     {float(f.color[0] * s), float(f.color[1] * s), float(f.color[2] * s), float(f.mieBlend)},
                                     {float(f.rayleigh[0]), float(f.rayleigh[1]), float(f.rayleigh[2]), float(f.mieAnisotropy)},
                                     {float(f.mie[0]), float(f.mie[1]), float(f.mie[2]), float(f.desaturation)},
                                     {float(f.heightBase), float(f.heightHalf), float(f.skyDistance), float(f.skyIntensity)},
                                     {float(f.maxDistance), 0, 0, 0}};
            std::memcpy(g.fog, fog, sizeof(fog));
            // The sky texture is an 8-bit copy of the HDR stock sky: a mid-grey texel stands for the haze's light.
            g.skyScale = float(2.0 * s);
        }
        if (stock && stockMeanLight_[0] + stockMeanLight_[1] + stockMeanLight_[2] > 0) {'''),
])

# ---------------- main window: vision with the sun; the stock sky from the world's sky material
patch('src/qtapp/main_window.cpp', [
 ('''        double intensity = 0;
        Vec3 color{1, 1, 1}, toward{0, 0, 1};
        QFile f(json);''', '''        // The vision (fog, atmosphere) beside it: vision/<zone>.vision as a rawfile.
        const QString visDir = QFileInfo(dir).absolutePath() + "/vision";
        const auto findVision = [&] {
            for (QDirIterator it(visDir, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                const QString f = it.next();
                if (f.contains("/rawfile/vision/")) return f;
            }
            return QString();
        };
        QString vision = findVision();
        if (vision.isEmpty() && QFileInfo::exists(acts) && QFileInfo::exists(ff)) {
            QProcess p;
            p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "rawfile", "-n",
                           "vision/" + zone + ".vision", "-l", "-o", visDir, ff});
            p.waitForFinished(120000);
            vision = findVision();
        }
        std::string visionText;
        if (QFile vf(vision); !vision.isEmpty() && vf.open(QIODevice::ReadOnly)) visionText = vf.readAll().toStdString();
        double intensity = 0;
        Vec3 color{1, 1, 1}, toward{0, 0, 1};
        QFile f(json);'''),
 ('''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward] {
            if (!self || self->referenceZone_ != zone || intensity <= 0) return;
            self->camera_->setGameStockSun(intensity, color, toward);
        }, Qt::QueuedConnection);''', '''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText] {
            if (!self || self->referenceZone_ != zone) return;
            if (!visionText.empty()) self->camera_->setGameStockVision(visionText);
            if (intensity > 0) self->camera_->setGameStockSun(intensity, color, toward);
        }, Qt::QueuedConnection);'''),
 ('''                for (const char* key : {"skyname", "sky"})
                    if (const std::string* v = e.value(key); v && !v->empty()) {
                        const QString f = skyCache->cachedDds(qs(*v));
                        if (f.isEmpty()) skyCache->request(qs(*v));
                        return f;
                    }
                break;
            }
            return {};''', '''                for (const char* key : {"skyname", "sky"})
                    if (const std::string* v = e.value(key); v && !v->empty()) {
                        const QString f = skyCache->cachedDds(qs(*v));
                        if (f.isEmpty()) skyCache->request(qs(*v));
                        return f;
                    }
                break;
            }
            // A stock map's document names no sky: its world's own sky material (tw/sky_<map>) does.
            if (stockWorld_)
                for (const auto& m : stockWorld_->meshes) {
                    const QString mat = qs(m.material);
                    if (!mat.contains("sky", Qt::CaseInsensitive)) continue;
                    const QString f = skyCache->cachedDds(mat);
                    if (f.isEmpty()) skyCache->request(mat);
                    return f;
                }
            return {};'''),
])
print('ok')
