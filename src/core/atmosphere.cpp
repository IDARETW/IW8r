#include "atmosphere.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace iw8r {

namespace {

std::optional<std::vector<double>> numbers(const std::string& v, size_t count) {
    std::istringstream in(v);
    std::vector<double> out;
    std::string tok;
    while (in >> tok) {
        char* end = nullptr;
        const double d = std::strtod(tok.c_str(), &end);
        if (!end || *end != '\0' || !std::isfinite(d)) return std::nullopt;
        out.push_back(d);
    }
    if (out.size() != count) return std::nullopt;
    return out;
}

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

std::string num(double v) { return formatNumber(v); }
std::string vec(const Vec3& v) { return num(v.x) + " " + num(v.y) + " " + num(v.z); }

}  // namespace

bool FilmGrade::isIdentity() const {
    return contrast == 1 && brightness == 0 && desaturation == 0 && !invert && lightTint.x == 1 && lightTint.y == 1 &&
           lightTint.z == 1 && darkTint.x == 1 && darkTint.y == 1 && darkTint.z == 1;
}

std::optional<ExpFog> parseFog(const std::string& v) {
    const auto n = numbers(v, 5);
    if (!n) return std::nullopt;
    ExpFog f;
    f.start = (*n)[0];
    f.halfway = (*n)[1];
    f.color = Vec3{(*n)[2], (*n)[3], (*n)[4]};
    if (f.start < 0 || f.halfway <= 0) return std::nullopt;  // the converter rejects these calls (V:242-248)
    return f;
}

std::optional<FilmGrade> parseFilm(const std::string& v) {
    const auto n = numbers(v, 10);
    if (!n) return std::nullopt;
    FilmGrade f;
    f.contrast = (*n)[0];
    f.brightness = (*n)[1];
    f.desaturation = (*n)[2];
    f.invert = (*n)[3] != 0;
    f.lightTint = Vec3{(*n)[4], (*n)[5], (*n)[6]};
    f.darkTint = Vec3{(*n)[7], (*n)[8], (*n)[9]};
    return f;
}

std::string formatFog(const ExpFog& f) { return num(f.start) + " " + num(f.halfway) + " " + vec(f.color); }

std::string formatFilm(const FilmGrade& f) {
    return num(f.contrast) + " " + num(f.brightness) + " " + num(f.desaturation) + " " + (f.invert ? "1" : "0") + " " +
           vec(f.lightTint) + " " + vec(f.darkTint);
}

namespace {
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
            const size_t b = item.find_first_not_of(" \t"), e = item.find_last_not_of(" \t");
            if (b == std::string::npos) continue;
            item = item.substr(b, e - b + 1);
            const size_t eq = item.find('=');
            std::string name = eq == std::string::npos ? std::string() : item.substr(0, eq);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
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
            *warn += std::string(kHazeKey) + " '" + *v + "' is not \"<preset> [amount 0.1 to 4]\" (presets: " + ids + "); no haze\n";
        }
    }
    if (const std::string* v = worldspawn.value(kFogKey); v && !v->empty()) {
        a.fog = parseFog(*v);
        if (!a.fog && warn) *warn += std::string(kFogKey) + " '" + *v + "' is not \"start halfway r g b\" (start >= 0, halfway > 0); fog ignored\n";
    }
    if (const std::string* v = worldspawn.value(kFilmKey); v && !v->empty()) {
        a.film = parseFilm(*v);
        if (!a.film && warn) *warn += std::string(kFilmKey) + " '" + *v + "' needs 10 numbers; film ignored\n";
    }
    if (const std::string* v = worldspawn.value(kLutKey)) a.lut = *v;
    if (const std::string* v = worldspawn.value(kVisionKey)) a.vision = *v;
    return a;
}

std::string fogGsc(const ExpFog& f) {
    const double start = std::clamp(f.start, 0.0, 20000.0);
    const double halfway = std::clamp(f.halfway, 1.0, 70000.0);
    return "// Generated by IW8 Radiant from worldspawn iw8r_fog.\nmain()\n{\n    setExpFog(" + num(start) + ", " + num(halfway) +
           ", " + num(clamp01(f.color.x)) + ", " + num(clamp01(f.color.y)) + ", " + num(clamp01(f.color.z)) + ");\n}\n";
}

std::string filmVision(const FilmGrade& f) {
    return "r_filmenable \"1\"\nr_filmcontrast \"" + num(f.contrast) + "\"\nr_filmbrightness \"" + num(f.brightness) +
           "\"\nr_filmdesaturation \"" + num(f.desaturation) + "\"\nr_filminvert \"" + (f.invert ? "1" : "0") +
           "\"\nr_filmlighttint \"" + vec(f.lightTint) + "\"\nr_filmdarktint \"" + vec(f.darkTint) + "\"\n";
}

bool parseCubeLut(const std::string& text, int& size, std::vector<uint8_t>& rgba, std::string* err) {
    size = 0;
    rgba.clear();
    double dmin[3] = {0, 0, 0}, dmax[3] = {1, 1, 1};
    std::istringstream in(text);
    std::string line;
    std::vector<double> values;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream ls(line.substr(first));
        std::string word;
        ls >> word;
        if (word == "LUT_3D_SIZE") {
            ls >> size;
            continue;
        }
        if (word == "DOMAIN_MIN" || word == "DOMAIN_MAX") {
            double* d = word == "DOMAIN_MIN" ? dmin : dmax;
            ls >> d[0] >> d[1] >> d[2];
            continue;
        }
        if (word == "TITLE" || word == "LUT_1D_SIZE" || word == "LUT_3D_INPUT_RANGE" || word == "LUT_1D_INPUT_RANGE") {
            if (word == "LUT_1D_SIZE") {
                if (err) *err = "1D LUTs are not supported (need LUT_3D_SIZE)";
                return false;
            }
            continue;
        }
        std::istringstream vs(line.substr(first));
        double r, g, b;
        if (!(vs >> r >> g >> b)) {
            if (err) *err = "unexpected line: " + line.substr(first, 60);
            return false;
        }
        values.push_back(r), values.push_back(g), values.push_back(b);
    }
    if (size < 2 || size > 256) {
        if (err) *err = "LUT_3D_SIZE missing or outside 2..256";
        return false;
    }
    const size_t n = size_t(size) * size * size;
    if (values.size() != n * 3) {
        if (err) *err = "expected " + std::to_string(n) + " rows, found " + std::to_string(values.size() / 3);
        return false;
    }
    rgba.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
            const double range = dmax[c] - dmin[c];
            const double v = range > 0 ? (values[i * 3 + c] - dmin[c]) / range : values[i * 3 + c];
            rgba[i * 4 + c] = uint8_t(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
        }
        rgba[i * 4 + 3] = 255;
    }
    return true;
}

std::string visionOverridesJson(const std::string& text, std::string* err) {
    std::string out;
    size_t count = 0, at = 0;
    while (at <= text.size()) {
        size_t end = text.find(';', at);
        if (end == std::string::npos) end = text.size();
        std::string item = text.substr(at, end - at);
        at = end + 1;
        const size_t b = item.find_first_not_of(" \t"), e = item.find_last_not_of(" \t");
        if (b == std::string::npos) continue;
        item = item.substr(b, e - b + 1);
        const size_t eq = item.find('=');
        const std::string name = eq == std::string::npos ? std::string() : item.substr(0, eq);
        std::istringstream vs(eq == std::string::npos ? std::string() : item.substr(eq + 1));
        std::vector<double> v;
        double d;
        while (vs >> d) v.push_back(d);
        bool okName = !name.empty();
        for (char c : name) okName &= std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        if (!okName || !(v.size() == 1 || v.size() == 3) || !vs.eof()) {
            if (err) *err = "vision override '" + item + "' is not name=number or name=x y z";
            return {};
        }
        out += std::string(count++ ? "," : "") + "\"" + name + "\":" +
               (v.size() == 1 ? num(v[0]) : "[" + num(v[0]) + "," + num(v[1]) + "," + num(v[2]) + "]");
    }
    return count ? "{" + out + "}" : std::string();
}

}  // namespace iw8r
