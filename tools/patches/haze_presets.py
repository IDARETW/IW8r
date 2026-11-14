import io

# Editor core: the game's own fog, its sky blend and the volumetric air as a preset with an amount (research 126):
# worldspawn iw8r_haze "<preset> [amount]". The build turns it into vision overrides (the user's iw8r_vision keys win),
# leaves the baked iw8r_fog out, and lamps get iw8r_volumetric (they light the air only when flagged).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/atmosphere.h', '''struct Atmosphere {
    std::optional<ExpFog> fog;''', '''// The game's own fog and volumetric air (research 126): a preset and how much of it. The game fogs every opaque
// pixel and the sky itself (distance and height fog with a sun-side glow, the fog's blend into the sky) and fills the
// air with volumetric fog that the sun lights and shadows (light shafts) and flagged lamps glow in.
struct Haze {
    std::string preset;   // an id of hazePresets()
    double amount = 1.0;  // 0.1 .. 4: scales the densities (1 = the preset as designed)
};
struct HazePreset {
    const char* id;
    const char* label;
    const char* about;
};
const std::vector<HazePreset>& hazePresets();

struct Atmosphere {
    std::optional<Haze> haze;  // iw8r_haze: replaces `fog` in the build
    std::optional<ExpFog> fog;''')
patch('src/core/atmosphere.h', '''// Parses kVisionKey text into the overrides JSON object text ("" when empty or malformed; `err` says why).
std::string visionOverridesJson(const std::string& text, std::string* err = nullptr);''',
      '''// Parses kVisionKey text into the overrides JSON object text ("" when empty or malformed; `err` says why).
std::string visionOverridesJson(const std::string& text, std::string* err = nullptr);
// `base` with every field that `over` names replaced by over's value, over's other fields added (both are kVisionKey
// text; an item that is not name=value is dropped from `base` and kept in `over` for the parser to report).
std::string mergeVisionText(const std::string& base, const std::string& over);

// The game's own fog and volumetric air: "<preset> [amount]" (kHazeKey). It needs the x64-zt fork that leaves the
// baked fog out when the vision turns the atmospheric fog on.
inline constexpr const char* kHazeKey = "iw8r_haze";
std::optional<Haze> parseHaze(const std::string& v);  // nullopt: empty, an unknown preset or a bad amount
std::string formatHaze(const Haze& h);
// The haze as kVisionKey text. `sunLuminance`: the map sun's luminance in the build's light units (the fog's
// brightness is absolute in the game, so it is shifted from the stock maps' sun level to this one); `sunDir`: unit,
// toward the sun; `groundZ`: where the ground layers sit. "" for an unknown preset.
std::string hazeVisionText(const Haze& h, double sunLuminance, const Vec3& sunDir, double groundZ);''')

patch('src/core/atmosphere.cpp', '''Atmosphere readAtmosphere(const Entity& worldspawn, std::string* warn) {
    Atmosphere a;''', '''namespace {
// One haze preset: the fields of research 126's table, in the stock maps' units (fog brightness as the EV a map
// whose sun has the stock level would use).
struct HazeValues {
    HazePreset info;
    double start, half, maxDistance, skyDistance, desaturation, ev;
    double rayleigh[3], mie[3], mieAnisotropy, mieBlend, skyMin, skyMax;
    double heightHalf, heightExtra;
    double veil, air, absorption, albedo[3], sun, spot, omni, ambient, airHalf, airAnisotropy;
};
// The sun level the stock EVs belong to (mp_rust's sun intensity; stock suns are 8.6 to 30.7).
constexpr double kStockSun = 11.7;

const std::vector<HazeValues>& hazeTable() {
    static const std::vector<HazeValues> t = {
        {{"desert", "Desert haze", "Bright warm dust: the low sky fills with haze, the sun lights the air (shafts through gaps)"},
         400, 5000, 1000000, 800000, 0.8, 20.6, {0.56, 0.50, 0.42}, {0.70, 0.64, 0.55}, 0.6, 0.3, 45, 80,
         250, 0.04, 0.09, 0.012, 0, {1, 0.94, 0.84}, 0.4, 1, 1, 0.6, 600, 0.75},
        {{"clear", "Clear day", "Thin blue distance haze and a little lit air: a crisp sunny day"},
         800, 14000, 1000000, 600000, 0.3, 19.6, {0.55, 0.70, 0.95}, {0.95, 0.92, 0.85}, 0.55, 0.35, 60, 86,
         1200, 0.01, 0.06, 0.004, 0, {1, 0.97, 0.92}, 0.5, 1, 1, 0.4, 1500, 0.7},
        {{"overcast", "Overcast mist", "Grey-blue mist under a covered sky: soft, the air lit by the sky more than the sun"},
         300, 6000, 600000, 300000, 0.5, 19.2, {0.62, 0.72, 0.82}, {0.78, 0.82, 0.86}, 0.25, 0.5, 0, 80,
         600, 0.03, 0.08, 0.008, 0, {0.95, 0.97, 1}, 0.08, 1, 1, 0.9, 900, 0.6},
        {{"dusk", "Golden dusk", "Warm low-sun air: orange haze toward the sun, strong shafts, lamps glow"},
         400, 7000, 1000000, 500000, 0.4, 18.6, {0.75, 0.52, 0.40}, {1, 0.72, 0.45}, 0.7, 0.55, 30, 82,
         400, 0.03, 0.11, 0.010, 0, {1, 0.86, 0.68}, 0.8, 1, 1, 0.4, 700, 0.8},
        {{"fog", "Dense fog", "Thick pale fog: the far end of a street fades out, the whole sky is fogged"},
         100, 1400, 400000, 200000, 0.7, 19.4, {0.70, 0.74, 0.78}, {0.82, 0.84, 0.86}, 0.3, 0.4, -30, 60,
         300, 0.2, 0.12, 0.030, 0, {0.96, 0.98, 1}, 0.15, 1, 1, 1, 500, 0.5},
    };
    return t;
}
}  // namespace

const std::vector<HazePreset>& hazePresets() {
    static const std::vector<HazePreset> list = [] {
        std::vector<HazePreset> l;
        for (const HazeValues& v : hazeTable()) l.push_back(v.info);
        return l;
    }();
    return list;
}

std::optional<Haze> parseHaze(const std::string& v) {
    std::istringstream in(v);
    Haze h;
    if (!(in >> h.preset)) return std::nullopt;
    bool known = false;
    for (const HazePreset& p : hazePresets()) known |= h.preset == p.id;
    if (!known) return std::nullopt;
    std::string rest;
    if (in >> rest) {
        char* end = nullptr;
        h.amount = std::strtod(rest.c_str(), &end);
        if (end == rest.c_str() || *end || !std::isfinite(h.amount) || h.amount < 0.1 || h.amount > 4.0) return std::nullopt;
        if (in >> rest) return std::nullopt;
    }
    return h;
}

std::string formatHaze(const Haze& h) { return h.amount == 1.0 ? h.preset : h.preset + " " + num(h.amount); }

std::string hazeVisionText(const Haze& h, double sunLuminance, const Vec3& sunDir, double groundZ) {
    const HazeValues* v = nullptr;
    for (const HazeValues& t : hazeTable())
        if (h.preset == t.info.id) v = &t;
    if (!v) return {};
    const double k = std::clamp(h.amount, 0.1, 4.0);
    // The fog's in-scatter is an absolute brightness (EV) in the game; the air's light terms scale with the sun and
    // the light grid on their own. A map whose sun is weaker than the stock level gets the EV lowered by the same
    // ratio, so the haze is as bright against its sunlit ground as on a stock map.
    const double ev = v->ev - std::log2(kStockSun / std::clamp(sunLuminance, 0.01, 100.0));
    const auto v3 = [](const double c[3]) { return num(c[0]) + " " + num(c[1]) + " " + num(c[2]); };
    std::string o;
    const auto add = [&](const char* name, const std::string& value) { o += std::string(o.empty() ? "" : "; ") + name + "=" + value; };
    add("fogAtmosphericScattering", "1");
    add("fogStartDist", num(v->start));
    add("fogHalfPlaneDistance", num(v->half / k));
    add("fogMaxDistance", num(v->maxDistance));
    add("fogSkyDistance", num(v->skyDistance));
    add("fogExtinctionDesaturation", num(v->desaturation));
    add("fogInscatteringEv", num(ev));
    add("fogRayleighAlbedo", v3(v->rayleigh));
    add("fogMieAlbedo", v3(v->mie));
    add("fogMieAnisotropy", num(v->mieAnisotropy));
    add("fogMieBlendStrength", num(v->mieBlend));
    add("fogSkyMinAngle", num(v->skyMin));
    add("fogSkyMaxAngle", num(v->skyMax));
    add("fogSunDir", num(sunDir.x) + " " + num(sunDir.y) + " " + num(sunDir.z));
    add("fogHeightFogBaseHeight", num(groundZ));
    add("fogHeightFogHalfPlaneDistance", num(v->heightHalf));
    add("fogHeightFogMaximumOffset", "0");
    add("fogHeightFogAdditionalFog", num(std::min(v->heightExtra * k, 1.0)));
    add("veilStrength", num(v->veil));
    add("volumetricAirDensity", num(v->air * k));
    add("volumetricAbsorption", num(v->absorption * k));
    add("volumetricAlbedo", v3(v->albedo));
    add("volumetricSunBrightness", num(v->sun));
    add("volumetricSpotBrightness", num(v->spot));
    add("volumetricOmniBrightness", num(v->omni));
    add("volumetricAmbientBrightness", num(v->ambient));
    add("volumetricHeightFogBaseHeight", num(groundZ));
    add("volumetricHeightFogHalfPlaneDistance", num(v->airHalf));
    add("volumetricSunAnisotropy", num(v->airAnisotropy));
    return o;
}

std::string mergeVisionText(const std::string& base, const std::string& over) {
    const auto items = [](const std::string& text) {
        std::vector<std::pair<std::string, std::string>> out;  // name ("" when the item has no '='), the trimmed item
        size_t at = 0;
        while (at <= text.size()) {
            size_t end = text.find(';', at);
            if (end == std::string::npos) end = text.size();
            std::string item = text.substr(at, end - at);
            at = end + 1;
            const size_t b = item.find_first_not_of(" \\t"), e = item.find_last_not_of(" \\t");
            if (b == std::string::npos) continue;
            item = item.substr(b, e - b + 1);
            const size_t eq = item.find('=');
            std::string name = eq == std::string::npos ? std::string() : item.substr(0, eq);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\\t')) name.pop_back();
            out.push_back({name, item});
        }
        return out;
    };
    const auto ov = items(over);
    std::string out;
    for (const auto& [name, item] : items(base)) {
        bool replaced = name.empty();
        for (const auto& o : ov) replaced |= !o.first.empty() && o.first == name;
        if (!replaced) out += std::string(out.empty() ? "" : "; ") + item;
    }
    for (const auto& o : ov) out += std::string(out.empty() ? "" : "; ") + o.second;
    return out;
}

Atmosphere readAtmosphere(const Entity& worldspawn, std::string* warn) {
    Atmosphere a;
    if (const std::string* v = worldspawn.value(kHazeKey); v && !v->empty()) {
        a.haze = parseHaze(*v);
        if (!a.haze && warn) {
            std::string ids;
            for (const HazePreset& p : hazePresets()) ids += std::string(ids.empty() ? "" : ", ") + p.id;
            *warn += std::string(kHazeKey) + " '" + *v + "' is not \\"<preset> [amount 0.1 to 4]\\" (presets: " + ids + "); no haze\\n";
        }
    }''')
print('ok')
