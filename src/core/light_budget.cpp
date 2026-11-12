// IW8 Radiant Suite - light budget (roadmap 5.15). See light_budget.h.
#include "light_budget.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>

namespace iw8r {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Same helpers as compile_iw8.cpp (its copies sit in an anonymous namespace, so they are repeated here).
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool parseNumber(const std::string& s, double& out) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool parseVec3Text(const std::string& s, double out[3]) {
    const auto tok = splitTokens(s);
    if (tok.size() < 3) return false;
    for (size_t k = 0; k < 3; ++k)
        if (!parseNumber(tok[k], out[k])) return false;
    return true;
}

bool isLightClass(const std::string& lcls) { return lcls == "light" || lcls == "light_spot" || lcls == "light_omni"; }

// The compiler keeps brush-less entities as points, and skips worldspawn and func_group (compile_iw8.cpp:928-938).
bool hasPrimitive(const Entity& e) {
    for (const auto& it : e.items)
        if (it.isPrimitive()) return true;
    return false;
}

}  // namespace

std::vector<LightInfo> collectLights(const MapFile& map) {
    // targetname -> origin of the first entity that has it (compile_iw8.cpp localPrimaryLights, targets map).
    std::map<std::string, Vec3> targets;
    for (const Entity& e : map.entities) {
        const std::string* tn = e.value("targetname");
        const auto o = e.origin();
        if (tn && o && !tn->empty()) targets.emplace(*tn, *o);
    }

    std::vector<LightInfo> out;
    for (const Entity& e : map.entities) {
        if (hasPrimitive(e)) continue;
        const std::string lcls = lower(e.classname());
        if (!isLightClass(lcls)) continue;

        LightInfo L;
        L.entity = e.id;
        L.classname = lcls;
        L.origin = e.origin().value_or(Vec3{});

        // Radius: default 400; a non-positive or unparsed value falls back to 400 (compile_iw8.cpp:628-633).
        if (const std::string* rs = e.value("radius")) {
            double v = 0;
            if (parseNumber(*rs, v) && v > 0) L.radius = v;
        }

        // Aim at the target entity when it exists (compile_iw8.cpp:636-641).
        std::optional<Vec3> aimAt;
        if (const std::string* tk = e.value("target"); tk && !tk->empty()) {
            const auto found = targets.find(*tk);
            if (found != targets.end()) aimAt = found->second - L.origin;
        }
        // A `light` with a target or an fov key is a spot; otherwise an omni (compile_iw8.cpp:642-643).
        const bool hasFov = e.value("fov_outer") || e.value("fov_inner");
        L.spot = lcls == "light_spot" || (lcls == "light" && (aimAt || hasFov));

        if (L.spot) {
            // Emission axis: towards the target, else from angles (pitch positive down) (compile_iw8.cpp:645-656).
            Vec3 emission{1, 0, 0};
            if (aimAt && length(*aimAt) > 1e-6) {
                emission = normalize(*aimAt);
            } else {
                double a[3] = {0, 0, 0};
                if (const std::string* as = e.value("angles"); as && !parseVec3Text(*as, a)) a[0] = a[1] = a[2] = 0;
                const double p = a[0] * kPi / 180.0, y = a[1] * kPi / 180.0;
                emission = {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
            }
            L.dir = emission;

            // Outer cone: full angle, default 90, must be positive; clamped to 1..170 degrees (compile_iw8.cpp:657-669).
            // fov_inner only shapes the falloff in the compiler, so it is not stored here.
            double outer = 90.0;
            if (const std::string* fo = e.value("fov_outer")) {
                double v = 0;
                if (parseNumber(*fo, v) && v > 0) outer = v;
            }
            outer = std::clamp(outer, 1.0, 170.0);
            L.cosOuter = std::cos(outer * 0.5 * kPi / 180.0);  // compile_iw8.cpp:674
        }

        // "shadows" is not a compiler key: the compiler always writes can_use_shadow_map false. It is read here so the
        // shadow-overlap rule has an input once shadows are written.
        if (const std::string* sh = e.value("shadows")) {
            const std::string v = lower(*sh);
            L.shadows = v == "1" || v == "true" || v == "yes";
        }
        out.push_back(L);
    }
    return out;
}

bool lightReaches(const LightInfo& light, const Vec3& p) {
    if (!(light.radius > 0)) return false;
    const Vec3 d = p - light.origin;
    const double dist = length(d);
    if (dist > light.radius) return false;
    if (!light.spot) return true;
    if (dist < 1e-9) return true;  // at the light's own origin, inside any cone
    const double axis = length(light.dir);
    if (axis < 1e-9) return false;  // a spot with no aim lights nothing
    return dot(d, light.dir) / (dist * axis) >= light.cosOuter;
}

std::vector<int> lightOverlap(const std::vector<LightInfo>& lights, const AABB& bounds, int cellsX, int cellsY,
                              double z) {
    if (cellsX <= 0 || cellsY <= 0 || !bounds.valid()) return {};
    const double sx = (bounds.maxs.x - bounds.mins.x) / cellsX;
    const double sy = (bounds.maxs.y - bounds.mins.y) / cellsY;
    std::vector<int> counts(static_cast<size_t>(cellsX) * static_cast<size_t>(cellsY), 0);
    for (int row = 0; row < cellsY; ++row) {
        const double cy = bounds.mins.y + (row + 0.5) * sy;
        for (int col = 0; col < cellsX; ++col) {
            const Vec3 centre{bounds.mins.x + (col + 0.5) * sx, cy, z};
            int n = 0;
            for (const LightInfo& L : lights) n += lightReaches(L, centre);
            counts[static_cast<size_t>(row) * cellsX + col] = n;
        }
    }
    return counts;
}

std::vector<LightBudgetIssue> checkLightBudget(const std::vector<LightInfo>& lights) {
    namespace lb = lightbudget;
    std::vector<LightBudgetIssue> out;

    if (lights.size() > lb::kMaxPrimaryLocalLights) {
        out.push_back({kNoId, true,
                       std::to_string(lights.size()) + " light entities; the compiler keeps " +
                           std::to_string(lb::kMaxPrimaryLocalLights) +
                           " (the primary-light table is 256 entries with the empty and sun slots). The rest are left out"});
    }

    for (size_t i = 0; i < lights.size(); ++i) {
        const LightInfo& L = lights[i];
        const std::string id = L.classname + " " + std::to_string(L.entity);
        if (!(L.radius > 0)) {
            out.push_back({L.entity, true, id + ": radius must be above 0; this light lights nothing"});
        } else if (L.radius > lb::kHugeRadius) {
            out.push_back({L.entity, false,
                           id + ": radius " + std::to_string(static_cast<long long>(L.radius)) + " is above " +
                               std::to_string(static_cast<long long>(lb::kHugeRadius)) + " (check for a typo)"});
        }

        if (L.spot) {
            const double c = std::clamp(L.cosOuter, -1.0, 1.0);
            const double fullDeg = 2.0 * std::acos(c) * 180.0 / kPi;
            if (!(fullDeg >= lb::kMinSpotConeDeg)) {
                out.push_back({L.entity, true,
                               id + ": spot cone is " + std::to_string(fullDeg) + " degrees (minimum " +
                                   std::to_string(static_cast<int>(lb::kMinSpotConeDeg)) + "); it lights almost nothing"});
            }
        }

        // Overlap at this light's origin; a light that lights nothing does not add to a point's count.
        if (!(L.radius > 0)) continue;
        int n = 0, shadowed = 0;
        for (const LightInfo& other : lights) {
            if (!lightReaches(other, L.origin)) continue;
            ++n;
            shadowed += other.shadows;
        }
        if (n > lb::kMaxOverlap) {
            out.push_back({L.entity, false,
                           id + ": " + std::to_string(n) + " lights overlap at this light (guideline " +
                               std::to_string(lb::kMaxOverlap) + ")"});
        }
        if (shadowed > lb::kMaxShadowOverlap) {
            out.push_back({L.entity, false,
                           id + ": " + std::to_string(shadowed) + " shadow-casting lights overlap at this light (guideline " +
                               std::to_string(lb::kMaxShadowOverlap) + ")"});
        }
    }
    return out;
}

}  // namespace iw8r
