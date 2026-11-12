#include "material_blend.h"

#include <vector>

namespace iw8r {

MaterialBlend materialBlendFromTechset(const std::string& techset) {
    // Leaf name after the prefix folder ("tmo/lit_3_lit_rpl_ta1_..."), split on '_'.
    const size_t slash = techset.rfind('/');
    const std::string leaf = slash == std::string::npos ? techset : techset.substr(slash + 1);
    std::vector<std::string> tok;
    size_t start = 0;
    while (start <= leaf.size()) {
        const size_t us = leaf.find('_', start);
        tok.push_back(leaf.substr(start, us == std::string::npos ? std::string::npos : us - start));
        if (us == std::string::npos) break;
        start = us + 1;
    }
    for (size_t i = 0; i + 1 < tok.size(); ++i) {
        const std::string& t = tok[i];
        const std::string& next = tok[i + 1];
        if (next.size() < 2 || next[0] != 't') continue;  // the blend token is followed by the table token (ta0 / ta1 / tca)
        if (t == "rpl") return MaterialBlend::Opaque;
        if (t == "alt") return MaterialBlend::AlphaTest;
        if (t == "bad") return next == "tca" && leaf.rfind("unlit", 0) == 0 ? MaterialBlend::Additive : MaterialBlend::Blend;
        if (t == "bar") return MaterialBlend::Blend;  // glass: blended with reveal (research 66)
    }
    return MaterialBlend::Opaque;
}

const char* materialBlendName(MaterialBlend b) {
    switch (b) {
        case MaterialBlend::AlphaTest: return "alpha test";
        case MaterialBlend::Blend: return "blend";
        case MaterialBlend::Additive: return "additive";
        default: return "opaque";
    }
}

}  // namespace iw8r
