#include "techset_info.h"

#include <vector>

namespace iw8r {

namespace {

std::vector<std::string> splitUnderscore(const std::string& s) {
    std::vector<std::string> tok;
    size_t start = 0;
    for (;;) {
        const size_t us = s.find('_', start);
        tok.push_back(s.substr(start, us == std::string::npos ? std::string::npos : us - start));
        if (us == std::string::npos) break;
        start = us + 1;
    }
    return tok;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

}  // namespace

TechsetInfo parseTechset(const std::string& techset) {
    TechsetInfo info;
    const size_t slash = techset.rfind('/');
    std::string leaf = slash == std::string::npos ? techset : techset.substr(slash + 1);
    if (!leaf.empty() && leaf[0] == '#') leaf.erase(0, 1);
    if (leaf.empty()) return info;
    if (leaf == ",null" || startsWith(leaf, ",null")) info.family = TechsetFamily::Null;
    else if (startsWith(leaf, ",cust_") || startsWith(leaf, "cust_")) info.family = TechsetFamily::Ui2d;
    else if (startsWith(leaf, "lit_")) info.family = TechsetFamily::Lit;
    else if (startsWith(leaf, "unlit_")) info.family = TechsetFamily::UnlitEffect;
    else if (startsWith(leaf, "dist_")) info.family = TechsetFamily::Distortion;
    else if (startsWith(leaf, "card_")) info.family = TechsetFamily::LightProxyCard;
    else if (startsWith(leaf, "sky_")) info.family = TechsetFamily::Sky;
    const std::vector<std::string> tok = splitUnderscore(leaf);
    for (size_t i = 0; i + 1 < tok.size(); ++i) {
        const std::string& t = tok[i];
        const std::string& next = tok[i + 1];
        if (next.size() != 3 || next[0] != 't') continue;  // the table token: ta0, ta1, tca, tom
        if (t != "rpl" && t != "alt" && t != "bad" && t != "bar") continue;
        info.blendToken = t;
        info.tableToken = next;
        break;
    }
    info.blend = materialBlendFromTechset(techset);
    info.glass = info.blendToken == "bar";
    return info;
}

const char* techsetFamilyName(TechsetFamily f) {
    switch (f) {
        case TechsetFamily::Lit: return "lit";
        case TechsetFamily::UnlitEffect: return "unlit effect";
        case TechsetFamily::Distortion: return "distortion";
        case TechsetFamily::LightProxyCard: return "lighting proxy card";
        case TechsetFamily::Sky: return "sky";
        case TechsetFamily::Ui2d: return "2D";
        case TechsetFamily::Null: return "info carrier";
        default: return "unknown";
    }
}

MaterialDraw materialDrawFor(const std::string& material, const std::string& techset, int64_t surfaceFlags) {
    const TechsetInfo ts = parseTechset(techset);
    if (ts.family == TechsetFamily::Null || ts.family == TechsetFamily::LightProxyCard || ts.family == TechsetFamily::Distortion)
        return MaterialDraw::Hidden;
    if (surfaceFlags >= 0) return (uint64_t(surfaceFlags) & kIw8SurfNoDraw) ? MaterialDraw::ShadowOnly : MaterialDraw::Draw;
    // Older cache entries without flags: every shadow-only stock material in the survey is named *shadowcaster* or
    // *shadow_caster* (or is a tool material the editor already handles).
    if (material.find("shadowcaster") != std::string::npos || material.find("shadow_caster") != std::string::npos)
        return MaterialDraw::ShadowOnly;
    return MaterialDraw::Draw;
}

const char* materialDrawName(MaterialDraw d) {
    switch (d) {
        case MaterialDraw::ShadowOnly: return "shadow only";
        case MaterialDraw::Hidden: return "hidden";
        default: return "drawn";
    }
}

}  // namespace iw8r
