#include "stock_layered_material.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace iw8r {
namespace {

uint32_t u32At(const std::vector<uint8_t>& b, size_t off) {
    return uint32_t(b[off]) | uint32_t(b[off + 1]) << 8 | uint32_t(b[off + 2]) << 16 | uint32_t(b[off + 3]) << 24;
}

// IEEE binary16 to float (normal, subnormal, zero; inf / NaN pass through as large values, never seen here).
float half(uint16_t h) {
    const int sign = h >> 15, exp = (h >> 10) & 31, man = h & 1023;
    float v;
    if (exp == 0) v = std::ldexp(float(man), -24);
    else if (exp == 31) v = man ? std::nanf("") : INFINITY;
    else v = std::ldexp(float(1024 + man), exp - 25);
    return sign ? -v : v;
}

float sat(float x) { return std::clamp(x, 0.0f, 1.0f); }

}  // namespace

bool decodeStockLayeredMaterial(const std::vector<uint8_t>& psData, int layerCount, StockLayeredMaterial& out,
                                std::string* err) {
    out = StockLayeredMaterial();
    if (layerCount < 1 || layerCount > 8) {
        if (err) *err = "layer count " + std::to_string(layerCount) + " outside 1..8";
        return false;
    }
    const size_t need = 16 * size_t(1 + 3 * layerCount);
    if (psData.size() < need) {
        if (err) *err = "psData has " + std::to_string(psData.size()) + " bytes, " + std::to_string(need) + " needed";
        return false;
    }
    out.layers = layerCount;
    std::memcpy(&out.row0x, psData.data(), 4);
    out.reflectionProbes = (u32At(psData, 4) & 1) != 0;
    for (int l = 0; l < layerCount; ++l) {
        const size_t flagsRow = 16 * size_t(1 + 3 * l), tintRow = flagsRow + 16, specRow = flagsRow + 32;
        StockLayerConstants c;
        c.flags = u32At(psData, flagsRow);
        c.colorMode = int((c.flags >> 7) & 3);
        c.normalMode = int((c.flags >> 9) & 3);
        const uint32_t rot = u32At(psData, flagsRow + 4), scale = u32At(psData, flagsRow + 8), off = u32At(psData, flagsRow + 12);
        c.uvCos = half(uint16_t(rot & 0xFFFF));
        c.uvSin = half(uint16_t(rot >> 16));
        c.uvScale = {half(uint16_t(scale & 0xFFFF)), half(uint16_t(scale >> 16))};
        c.uvOffset = {half(uint16_t(off & 0xFFFF)), half(uint16_t(off >> 16))};
        const uint32_t tint = u32At(psData, tintRow);
        c.tint = {float(tint & 255) / 255.0f, float((tint >> 8) & 255) / 255.0f, float((tint >> 16) & 255) / 255.0f};
        c.sharpness = float(tint >> 24) / 255.0f;
        const uint32_t spec = u32At(psData, specRow), gloss = u32At(psData, specRow + 4);
        c.specScale = float((spec & 255) + 1) / 256.0f;
        c.specBias = float((spec >> 8) & 255) / 256.0f;
        c.glossScale = float((gloss & 255) + 1) / 256.0f;
        c.glossBias = float((gloss >> 8) & 255) / 256.0f;
        c.normalStrength = float(((gloss >> 16) & 255) + 1) / 256.0f;
        out.layer.push_back(c);
    }
    return true;
}

bool decodeStockLayeredMaterialHex(const std::string& hex, int layerCount, StockLayeredMaterial& out, std::string* err) {
    if (hex.size() % 2) {
        if (err) *err = "psData hex has an odd length";
        return false;
    }
    std::vector<uint8_t> b(hex.size() / 2);
    const auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < b.size(); ++i) {
        const int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            if (err) *err = "psData is not hex";
            return false;
        }
        b[i] = uint8_t(hi << 4 | lo);
    }
    return decodeStockLayeredMaterial(b, layerCount, out, err);
}

std::array<float, 2> stockLayerUv(const StockLayerConstants& c, float u, float v) {
    const float su = u * c.uvScale[0], sv = v * c.uvScale[1];
    return {c.uvCos * su + c.uvSin * sv + c.uvOffset[0], -c.uvSin * su + c.uvCos * sv + c.uvOffset[1]};
}

float stockLayerBlend(const StockLayerConstants& c, float weight, float alpha, float reveal) {
    const float w = sat(weight);
    float b = w;
    if (c.flags & kStockLayerReveal) {
        const float k = c.sharpness;
        const float lo = sat(1.0f - w - k * std::sqrt(w));
        const float hi = sat(1.0f - w + k * std::sqrt(1.0f - w));
        // hi == lo (k = 0 or w = 1): a step at lo, as the shader's reciprocal of 0 gives.
        b = hi > lo ? sat((reveal - lo) / (hi - lo)) : (reveal > lo ? 1.0f : 0.0f);
    }
    if (c.flags & kStockLayerAlphaMask) b *= alpha;
    return b;
}

std::array<float, 3> stockLayerBlendColor(const StockLayerConstants& c, float blend, const std::array<float, 3>& below,
                                          const std::array<float, 3>& layer) {
    std::array<float, 3> out = below;
    if (blend < 1.0f / 255.0f) return out;  // the shader skips the layer below one step of 8 bits
    for (int i = 0; i < 3; ++i) {
        if (c.colorMode == 0) out[i] = below[i] + blend * (layer[i] - below[i]);
        else if (c.colorMode == 1) out[i] = below[i] * (1.0f + blend * (layer[i] - 1.0f));
    }
    return out;
}


ModelLayerTokens parseModelLayerTokens(const std::string& name) {
    ModelLayerTokens t;
    const size_t slash = name.find('/');
    if (slash == std::string::npos) return t;
    const std::string family = name.substr(0, slash);
    std::vector<std::string> f;
    for (size_t i = slash + 1, j; i <= name.size(); i = j + 1) {
        j = name.find('_', i);
        if (j == std::string::npos) j = name.size();
        f.push_back(name.substr(i, j - i));
    }
    if (f.size() < 9 || f[0] != "lit" || f[2] != "lit") return t;
    const int n = std::atoi(f[8].c_str());
    if (n < 1 || n > 8) return t;
    t.layers = n;
    t.mask126 = !f[6].empty() && f[6][0] == '3';
    // A `2` in the family (tm2o, tm2co, m2o, m2co): the model has a second UV set.
    const bool secondUvFamily = family.find('2') != std::string::npos;
    t.kind.assign(size_t(n), ModelLayerKind::Standard);
    t.uv1.assign(size_t(n), false);
    for (int L = 1; L < n; ++L) {
        const size_t at = 18 + 8 * size_t(L - 1);
        if (at + 2 >= f.size()) break;
        const unsigned long long hex = std::strtoull(f[at + 2].c_str(), nullptr, 16);
        if (f[at + 1] == "1") t.kind[size_t(L)] = ModelLayerKind::Code;
        else if ((hex >> 24) == 4) t.kind[size_t(L)] = ModelLayerKind::Overlay;
        t.uv1[size_t(L)] = secondUvFamily && (hex & 0x80000ull) != 0;
    }
    return t;
}

}  // namespace iw8r
