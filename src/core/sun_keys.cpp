// Sun and shadow worldspawn keys (roadmap 5.16). Evidence for each row: docs/research/39_sun_shadows.md.
#include "sun_keys.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace iw8r {
namespace {

// Splits on whitespace and parses every token as a finite number. False when a token is not a number or the count is
// wrong (count 0 = any count, at least one).
bool parseNumbers(const std::string& text, size_t count, std::vector<double>& out) {
    out.clear();
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) {
        char* end = nullptr;
        errno = 0;
        const double v = std::strtod(tok.c_str(), &end);
        if (end == tok.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(v)) return false;
        out.push_back(v);
    }
    if (count == 0) return !out.empty();
    return out.size() == count;
}

bool parseBool(const std::string& text, bool& out) {
    if (text == "0" || text == "false") {
        out = false;
        return true;
    }
    if (text == "1" || text == "true") {
        out = true;
        return true;
    }
    return false;
}

bool parseInt(const std::string& text, long long& out) {
    if (text.empty()) return false;
    size_t i = (text[0] == '-' || text[0] == '+') ? 1 : 0;
    if (i >= text.size()) return false;
    for (size_t k = i; k < text.size(); ++k)
        if (text[k] < '0' || text[k] > '9') return false;
    out = std::strtoll(text.c_str(), nullptr, 10);
    return true;
}

const char* typeName(SunValueType t) {
    switch (t) {
        case SunValueType::Vec3: return "three numbers";
        case SunValueType::Float: return "one number";
        case SunValueType::Bool: return "0, 1, true or false";
        case SunValueType::Int: return "a whole number";
    }
    return "";
}

void checkBounds(const SunKeyDef& def, const std::vector<double>& nums, std::vector<std::string>& out,
                 const std::string& shown) {
    for (size_t i = 0; i < nums.size(); ++i) {
        if (def.key == "sundirection" && i != 0) continue;  // pitch only
        if (def.min && nums[i] < *def.min) {
            out.push_back(def.key + " '" + shown + "' is below " + std::to_string(static_cast<long long>(*def.min)) +
                          ": out of range");
            return;
        }
        if (def.max && nums[i] > *def.max) {
            out.push_back(def.key + " '" + shown + "' is above " + std::to_string(static_cast<long long>(*def.max)) +
                          ": out of range");
            return;
        }
    }
}

}  // namespace

const std::vector<SunKeyDef>& sunKeyDefs() {
    static const std::vector<SunKeyDef> defs = {
        {"sundirection", SunValueType::Vec3, "-20.261 -15.4 0", -90.0, 90.0,
         "Sun pitch yaw roll in degrees. Negative pitch = sun above the horizon. Bounds apply to pitch. The compiler uses "
         "-50 45 0 when absent or not parsed. Written by the Sun and Sky dialog.",
         true},
        {"suncolor", SunValueType::Vec3, "1 0.9764 0.945", 0.0, 255.0,
         "Sun colour r g b, linear 0 to 1. If any value is above 1 the compiler reads all three as 0 to 255 and divides "
         "by 255. Written by the Sun and Sky dialog.",
         true},
        {"sunlight", SunValueType::Float, "27.118", 0.0, std::nullopt,
         "Sun intensity. Read by the IW3 converter, but the compiled sun light's luminance replaces it for editor maps, "
         "and the editor does not write this key. Upper bound not verified.",
         false},
        {"csm_enable", SunValueType::Bool, "1", 0.0, 1.0,
         "Cascaded shadow maps enable. Stock maps write it; the converter does not read it and the sun shadow bake "
         "runs regardless.",
         false},
        {"csm_max_resolution", SunValueType::Int, "256", std::nullopt, std::nullopt,
         "Cascaded shadow map max resolution. Stock maps write it; no converter reads it. No range verified.", false},
        {"csm_sample_size", SunValueType::Int, "2", std::nullopt, std::nullopt,
         "Cascaded shadow map sample size. The converter bakes at a fixed 2 units per texel and ignores this key. No "
         "range verified.",
         false},
        {"_csm_bound_center", SunValueType::Vec3, "", std::nullopt, std::nullopt,
         "Cascaded shadow map bounds centre. Not on the mp_shipment stock worldspawn; seen on another stock map (not "
         "in the cache). Not read by the converter.",
         false},
        {"_csm_bound_halfsize", SunValueType::Vec3, "", 0.0, std::nullopt,
         "Cascaded shadow map bounds half size. Not on the mp_shipment stock worldspawn. Not read by the converter. "
         "Components must not be negative.",
         false},
    };
    return defs;
}

std::vector<std::string> checkSunKeys(const Entity& worldspawn) {
    std::vector<std::string> out;
    std::vector<double> nums;
    for (const SunKeyDef& def : sunKeyDefs()) {
        const std::string* value = worldspawn.value(def.key);
        if (!value) continue;
        const std::string& shown = *value;

        if (!def.passedThrough)
            out.push_back(def.key + " is not written into the build: the editor passes only sundirection and suncolor, "
                                    "so this value has no effect");

        switch (def.type) {
            case SunValueType::Vec3:
                if (!parseNumbers(shown, 3, nums)) {
                    out.push_back(def.key + " '" + shown + "' is not three numbers: " +
                                  (def.key == "sundirection" ? "the compiler uses -50 45 0"
                                                             : "the compiler uses its default"));
                    break;
                }
                checkBounds(def, nums, out, shown);
                if (def.key == "sundirection" && nums[0] > 0.0)
                    out.push_back("sundirection '" + shown + "' has positive pitch: the sun is below the horizon");
                if (def.key == "suncolor" && (nums[0] > 1.0 || nums[1] > 1.0 || nums[2] > 1.0))
                    out.push_back("suncolor '" + shown + "' is above 1: the compiler reads all three as 0 to 255");
                break;
            case SunValueType::Float:
                if (!parseNumbers(shown, 1, nums)) {
                    out.push_back(def.key + " '" + shown + "' is not " + typeName(def.type));
                    break;
                }
                checkBounds(def, nums, out, shown);
                break;
            case SunValueType::Bool: {
                bool b = false;
                if (!parseBool(shown, b)) out.push_back(def.key + " '" + shown + "' is not " + typeName(def.type));
                else {
                    nums.assign(1, b ? 1.0 : 0.0);
                    checkBounds(def, nums, out, shown);
                }
                break;
            }
            case SunValueType::Int: {
                long long n = 0;
                if (!parseInt(shown, n)) out.push_back(def.key + " '" + shown + "' is not " + typeName(def.type));
                else {
                    nums.assign(1, static_cast<double>(n));
                    checkBounds(def, nums, out, shown);
                }
                break;
            }
        }
    }
    return out;
}

}  // namespace iw8r
