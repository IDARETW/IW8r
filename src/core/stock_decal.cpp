#include "stock_decal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace iw8r {
namespace {

float sat(float x) { return std::clamp(x, 0.0f, 1.0f); }

bool parseHex64(const std::string& s, uint64_t& out) {
    if (s.empty() || s.size() > 16) return false;
    uint64_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = v << 4 | uint64_t(d);
    }
    out = v;
    return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t p = s.find(sep, start);
        out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

}  // namespace

StockBlendState decodeStockBlendBits(uint64_t bits) {
    StockBlendState s;
    s.enabled = (bits & 0x3FFFFFull) != 0;
    s.srcRgb = int(bits & 0xF);
    s.dstRgb = int((bits >> 4) & 0xF);
    s.opRgb = int((bits >> 8) & 7);
    s.srcAlpha = int((bits >> 11) & 0xF);
    s.dstAlpha = int((bits >> 15) & 0xF);
    s.opAlpha = int((bits >> 19) & 7);
    s.independent = (bits >> 32) != 0;
    for (int i = 0; i < 8; ++i)
        s.writeMask[size_t(i)] = s.independent ? uint8_t(~(bits >> (32 + 4 * i)) & 0xF) : uint8_t(0xF);
    return s;
}

StockOtherState decodeStockOtherBits(uint64_t bits) {
    StockOtherState s;
    s.cull = int(bits & 3);
    s.wireframe = (bits & 4) != 0;
    s.depthClipDisabled = (bits & 8) != 0;
    s.polygonOffset = int((bits >> 4) & 3);
    s.depthWrite = (bits & 0x200) != 0;
    s.depthEnabled = (bits & 0xE00) != 0;
    s.depthTest = int((bits >> 10) & 3);
    s.stencil = (bits & 0xFFFFFF000ull) != 0;
    return s;
}

StockTechsetTraits parseStockTechsetName(const std::string& name) {
    StockTechsetTraits t;
    const size_t slash = name.find('/');
    if (slash == std::string::npos) return t;
    t.family = name.substr(0, slash);
    const std::vector<std::string> tok = split(name.substr(slash + 1), '_');
    // lit 3 lit <blend> <x> <hexA> <hexB> 0 <n> <n> ...
    if (tok.size() < 10 || tok[0] != "lit" || tok[2] != "lit") return t;
    uint64_t a = 0, b = 0;
    if (!parseHex64(tok[5], a) || !parseHex64(tok[6], b)) return t;
    t.blendToken = tok[3];
    t.blended = t.blendToken == "bad";
    t.polygonOffset = t.blended && (a & 0x10000) != 0;
    t.ssr = (b & 0x2) != 0;  // hexB bit 1: "...2" and the glass "...3" (research 104 section 5)
    t.displacement = (b & 0xF) == 8;
    t.wrapDiffuse = (b & 0x0008000000000000ull) != 0;
    t.transmission = (b & 0x1) != 0;
    t.layers = std::atoi(tok[8].c_str());
    uint64_t c = 0;
    t.uvAnim = tok.size() > 12 && parseHex64(tok[12], c) && (c & 0x800000000ull) != 0;
    t.valid = true;
    return t;
}

StockDecalFamily stockDecalFamilyFromTechset(const std::string& family) {
    if (family == "twc") return StockDecalFamily::WorldVertCol;
    if (family == "tw4") return StockDecalFamily::WorldLayered;
    if (family == "twc4") return StockDecalFamily::WorldLayeredVertCol;
    return StockDecalFamily::World;
}

float stockDecalLayerWeight(StockDecalFamily family, int layer, const std::array<uint8_t, 4>& color) {
    switch (family) {
    case StockDecalFamily::WorldVertCol: return color[3] / 255.0f;
    case StockDecalFamily::WorldLayeredVertCol:
        return layer >= 0 && layer < 4 ? color[size_t(layer)] / 255.0f : 0.0f;
    default: return 1.0f;
    }
}

float stockDecalCoverage(const std::vector<float>& layerOpacity) {
    float a = 0.0f;
    for (size_t i = 0; i < layerOpacity.size(); ++i) {
        const float b = layerOpacity[i];
        if (i == 0 || b >= 1.0f / 255.0f) a = std::max(a, b);
    }
    return a;
}

std::array<float, 3> stockDecalComposite(const std::array<float, 3>& below, const std::array<float, 3>& lit, float b) {
    std::array<float, 3> out{};
    for (size_t i = 0; i < 3; ++i) out[i] = lit[i] * b + below[i] * (1.0f - b);
    return out;
}

bool stockDecalDiscards(const std::array<float, 3>& litTimesB, float b) {
    return std::max({litTimesB[0], litTimesB[1], litTimesB[2], b}) <= 1e-6f;
}

float stockWrapDiffuse(float ndotl, float wrap) {
    const float w = std::max(wrap, 1e-6f);
    const float d = 3.0f / (w * w + 3.0f);
    if (ndotl < w) {
        const float x = std::max(ndotl, -w) + w;
        return sat(x * x * d / (4.0f * w));
    }
    return sat(ndotl * d);
}

float stockDisplacementSceneParam(float heightScale, bool heightAuto) {
    return heightAuto ? heightScale * -0.00390625f : heightScale;
}

float stockDisplacementAmplitude(float lo, float hi, float sceneParam) {
    const float div = sceneParam >= 0.0f ? 1.0f : hi;
    if (div == 0.0f) return 0.0f;
    return lo / div * std::fabs(sceneParam);
}

float stockDisplacementOffset(float height, float amp, float scale, float bias) {
    return amp * (height * scale + bias);
}

}  // namespace iw8r
