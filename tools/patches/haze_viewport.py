import io

# Viewport: an own map's haze (worldspawn iw8r_haze and the fog / veil / volumetric fields of iw8r_vision) is drawn
# by the same Game Lighting code that draws a stock map's vision: the game's fog (research 111), the volumetric march
# and the veil (research 109). Before, these were fed from stock visions only.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/game_look.h', '''GamePostFx gamePostFxFromVision(const std::string& visionText);
''', '''GamePostFx gamePostFxFromVision(const std::string& visionText);
// An own map's fog, veil and volumetric fields as the vision text the readers above take: `overridesText` is
// "name=value; ..." (worldspawn iw8r_vision, with the iw8r_haze preset under it: core atmosphere), names or tokens;
// fields of other groups are left out. "" when it names none.
std::string hazeVisionFromOverrides(const std::string& overridesText);
''')
patch('src/core/game_look.cpp', '''GamePostFx gamePostFxFromVision(const std::string& text) {''', '''std::string hazeVisionFromOverrides(const std::string& overridesText) {
    // The converter's names (x64-zt replay_vision_names.inc) for the fog, veil and volumetric fields.
    static const std::pair<const char*, const char*> kNames[] = {
        {"fogDensity", "MSTOLQOTKN"}, {"fogStartDist", "RPNOKKL"}, {"fogColorIntensity", "NTPTLNNQQL"}, {"fogColor", "NMLMMNQLSQ"},
        {"fogMaxOpacity", "TRPROKTLS"}, {"fogSunDensity", "MPNPSMMTTT"}, {"fogSunColorIntensity", "MLORTTKOLN"},
        {"fogSunColor", "MRTOMMTTQM"}, {"fogSunBeginFadeAngle", "NKLTMMOOLP"}, {"fogSunEndFadeAngle", "NONQQTPNLT"},
        {"fogSunDir", "RLNQONPKS"}, {"fogSkyIntensity", "NORQKLTPTR"}, {"fogSkyMinAngle", "LOROLOTNMP"},
        {"fogSkyMaxAngle", "SPQNRNKQR"}, {"fogAtmosphericScattering", "MKKLQTSKSO"}, {"fogHalfPlaneDistance", "SNNNRLTML"},
        {"fogMaxDistance", "NRTMTRQTRQ"}, {"fogSkyDistance", "NLQKSSPMKP"}, {"fogExtinctionDesaturation", "MKQSQRPPQR"},
        {"fogInscatteringEv", "LNOQTMMLSP"}, {"fogRayleighAlbedo", "LNMRMNLNTT"}, {"fogMieAlbedo", "LRKPNSNORL"},
        {"fogMieAnisotropy", "MPRLMQNQO"}, {"fogMieBlendStrength", "NPMPRRLNPO"}, {"fogHeightFogBaseHeight", "NOSTLRKPQ"},
        {"fogHeightFogHalfPlaneDistance", "POKSTQPPQ"}, {"fogHeightFogMaximumOffset", "LOLKSRNSMR"},
        {"fogHeightFogAdditionalFog", "NKOQRKQRMT"}, {"veilStrength", "MMQLTMKTPK"}, {"volumetricAirDensity", "NSKNLMKQSR"},
        {"volumetricAbsorption", "MQMPKLKK"}, {"volumetricAlbedo", "NKKRQROOQK"}, {"volumetricSunBrightness", "LKNNNKTPTM"},
        {"volumetricSpotBrightness", "MQSQOLLMRM"}, {"volumetricOmniBrightness", "RRKMLNPKP"},
        {"volumetricAmbientBrightness", "MQRSTPRORQ"}, {"volumetricHeightFogBaseHeight", "NPKPLTKRNS"},
        {"volumetricHeightFogHalfPlaneDistance", "MLPOMNQPPK"}, {"volumetricSunAnisotropy", "MTNSRNMTKN"},
    };
    std::string out;
    size_t at = 0;
    while (at <= overridesText.size()) {
        size_t end = overridesText.find(';', at);
        if (end == std::string::npos) end = overridesText.size();
        const std::string item = overridesText.substr(at, end - at);
        at = end + 1;
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const auto trim = [](std::string s) {
            const size_t b = s.find_first_not_of(" \\t"), e = s.find_last_not_of(" \\t");
            return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
        };
        const std::string name = trim(item.substr(0, eq)), value = trim(item.substr(eq + 1));
        if (name.empty() || value.empty()) continue;
        for (const auto& [dev, token] : kNames)
            if (name == dev || name == token) out += std::string(token) + " \\"" + value + "\\"\\n";
    }
    return out;
}

GamePostFx gamePostFxFromVision(const std::string& text) {''')

H = 'src/qtapp/camera_view.h'
patch(H, '''    GamePostFx stockPostFx_;  // the stock map's veil and vignette (research 109)
''', '''    GamePostFx stockPostFx_;  // the stock map's veil and vignette (research 109)
    // An own map's haze (worldspawn iw8r_haze under iw8r_vision, research 126), read the way a stock vision is.
    GameFog ownFog_;
    GameFogRefdef ownFogRefdef_{};
    GamePostFx ownPostFx_;
''')
C = 'src/qtapp/camera_view.cpp'
patch(C, '''            if (const Entity* ws = ctx_->doc.worldspawn()) {
                atmo_ = readAtmosphere(*ws);
                if (auto r = readWind(*ws)) wind_ = *r;
            }
''', '''            ownFog_ = GameFog{};
            ownFogRefdef_ = GameFogRefdef{};
            ownPostFx_ = GamePostFx{};
            if (const Entity* ws = ctx_->doc.worldspawn()) {
                atmo_ = readAtmosphere(*ws);
                if (auto r = readWind(*ws)) wind_ = *r;
                // The haze as the build will write it (core compile_iw8: the preset under the user's iw8r_vision, the
                // fog's brightness from the map's sun, the ground layers at the spawn points' height).
                std::string hazeText;
                if (atmo_.haze) {
                    std::vector<double> heights;
                    for (const Entity& e : ctx_->doc.map().entities)
                        if (const std::string* cn = e.value("classname"); cn && cn->find("spawn") != std::string::npos)
                            if (auto o = e.origin()) heights.push_back(o->z);
                    double groundZ = 0;
                    if (!heights.empty()) {
                        std::nth_element(heights.begin(), heights.begin() + long(heights.size() / 2), heights.end());
                        groundZ = heights[heights.size() / 2];
                    }
                    const Vec3 sc = ctx_->scene.sunColor;
                    hazeText = hazeVisionText(*atmo_.haze, 0.2126 * sc.x + 0.7152 * sc.y + 0.0722 * sc.z, gameSunToward(), std::round(groundZ));
                }
                if (const std::string vision = hazeVisionFromOverrides(mergeVisionText(hazeText, atmo_.vision)); !vision.empty()) {
                    ownFog_ = gameFogFromVision(vision);
                    ownFogRefdef_ = gameFogRefdef(gameFogVisionFromText(vision));
                    ownPostFx_ = gamePostFxFromVision(vision);
                }
            }
''')
patch(C, '''        const ExpFog* fog = ctx_->fogPreview && atmo_.fog ? &*atmo_.fog : nullptr;''',
      '''        // (Left out with iw8r_haze, as in the build: the game's own fog replaces it.)
        const ExpFog* fog = ctx_->fogPreview && atmo_.fog && !atmo_.haze ? &*atmo_.fog : nullptr;''')
patch(C, '''        if (stock) {  // the stock map's bloom veil and vignette (research 109)
            g.veil = float(stockPostFx_.veil);
            g.grain = float(stockPostFx_.grain);
        }''', '''        // The vision's post effects and fog: a stock map's own vision, an own map's haze (research 126).
        const GamePostFx& postFx = stock ? stockPostFx_ : ownPostFx_;
        const GameFog& gameFog = stock ? stockFog_ : ownFog_;
        g.veil = float(postFx.veil);  // the bloom veil and vignette (research 109)
        if (stock) g.grain = float(stockPostFx_.grain);''')
patch(C, '''        if (stock) {
            const GamePostFx& p = stockPostFx_;
            if (p.volAir > 0) {  // the volumetric fog (research 109 section 2)''', '''        {
            const GamePostFx& p = postFx;
            if (p.volAir > 0) {  // the volumetric fog (research 109 section 2)''')
patch(C, '''            gameVignetteConstants(stockPostFx_, aspect, 1.0, g.vignette);
        }
        if (stock && stockFog_.on) {
            const GameFog& f = stockFog_;''', '''            gameVignetteConstants(postFx, aspect, 1.0, g.vignette);
        }
        if (gameFog.on) {
            const GameFog& f = gameFog;''')
patch(C, '''            const GameFogConstants k = gameFogConstants(stockFogRefdef_, float(ctx_->cam.pos.z));''',
      '''            const GameFogConstants k = gameFogConstants(stock ? stockFogRefdef_ : ownFogRefdef_, float(ctx_->cam.pos.z));''')
print('ok')
