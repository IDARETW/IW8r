// The game's analytic fog (research 111). Float math throughout, like the shader; every step names its source.
#include "game_fog.h"

#include <cmath>
#include <cstdio>
#include <map>

namespace iw8r {

namespace {

constexpr float kLn2 = 0.69314718f;     // the exe's constant at .rdata 0x23e65d4
constexpr float kLog2e = 1.442695f;     // .rdata 0x251aa7c
constexpr float kFltEps = 1.19209e-7f;  // .rdata 0x23c3de0
constexpr float kDeg = 0.017453292f;    // .rdata 0x23bda84
constexpr float kHalfPi = 1.5707964f;

// HLSL saturate (a NaN gives 0, as on the GPU).
float sat(float x) { return x > 0.0f ? (x < 1.0f ? x : 1.0f) : 0.0f; }

// The exe's sRGB -> linear (0x1724740; threshold 0.03928, not 0.04045).
float srgbToLinearExe(float c) { return c <= 0.03928f ? c * 0.0773994f : std::pow((c + 0.055f) * 0.947867f, 2.4f); }

}  // namespace

GameFogVision gameFogVisionFromText(const std::string& text) {
    GameFogVision v;
    std::map<std::string, std::string> kv;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t q1 = line.find('"'), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::string key = line.substr(0, q1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t' || key.front() == '\0')) key.erase(key.begin());
        kv[key] = line.substr(q1 + 1, q2 - q1 - 1);
    }
    const auto num = [&](const char* tok, float& out) {
        const auto it = kv.find(tok);
        if (it == kv.end() || it->second.empty()) return;
        float a = 0;
        if (std::sscanf(it->second.c_str(), "%f", &a) == 1) out = a;
    };
    const auto vec = [&](const char* tok, float out[3]) {
        const auto it = kv.find(tok);
        if (it == kv.end()) return;
        float a[3];
        if (std::sscanf(it->second.c_str(), "%f %f %f", &a[0], &a[1], &a[2]) != 3) return;
        for (int k = 0; k < 3; ++k) out[k] = a[k];
    };
    num("MKKLQTSKSO", v.atmospheric);
    num("RPNOKKL", v.startDist);
    num("SNNNRLTML", v.halfPlane);
    num("NRTMTRQTRQ", v.maxDistance);
    num("NLQKSSPMKP", v.skyDistance);
    num("MKQSQRPPQR", v.extinctionDesaturation);
    num("LNOQTMMLSP", v.inscatteringEv);
    vec("LNMRMNLNTT", v.rayleighAlbedo);
    vec("LRKPNSNORL", v.mieAlbedo);
    num("MPRLMQNQO", v.mieAnisotropy);
    num("NPMPRRLNPO", v.mieBlend);
    vec("RLNQONPKS", v.sunDir);
    num("LOROLOTNMP", v.skyMinAngle);
    num("SPQNRNKQR", v.skyMaxAngle);
    num("NOSTLRKPQ", v.heightBase);
    num("POKSTQPPQ", v.heightHalfPlane);
    num("LOLKSRNSMR", v.heightMaxOffset);
    num("NKOQRKQRMT", v.heightAdditional);
    num("MSTOLQOTKN", v.density);
    vec("NMLMMNQLSQ", v.color);
    num("NTPTLNNQQL", v.colorIntensity);
    num("TRPROKTLS", v.maxOpacity);
    num("MPNPSMMTTT", v.sunDensity);
    vec("MRTOMMTTQM", v.sunColor);
    num("MLORTTKOLN", v.sunColorIntensity);
    num("NKLTMMOOLP", v.sunBeginFadeAngle);
    num("NONQQTPNLT", v.sunEndFadeAngle);
    num("NORQKLTPTR", v.skyIntensity);
    if (const auto it = kv.find("MLOSSOSRMM"); it != kv.end() && !it->second.empty()) v.hasSpline = true;
    return v;
}

GameFogRefdef gameFogRefdef(const GameFogVision& v) {
    GameFogRefdef f{};
    // 0x1723448-0x17234ab: the inscattered radiance scale.
    const float v4 = std::pow(2.0f, v.inscatteringEv - 2.84f) * 0.001f;
    f.radianceScale = v4;
    // 0x17234f0-0x1723618: the fog spline blend set. A vision whose fogSpline is empty holds one entry of weight 255
    // that is not a spline asset, so the entry takes the vision's own start / half plane / max distance (the sums
    // below with weight 1). The sky distance is the vision's, not blended.
    f.startDist = v.startDist;
    f.maxDistance = v.maxDistance;
    f.skyDistance = v.skyDistance;
    float density = 0;
    if (v.halfPlane != 0) density = std::fmax(kLn2 / v.halfPlane, 0.0f);
    // 0x1723626-0x17237d3: Rayleigh and Mie scattering, extinction, max optical depth.
    float ray[3], mie[3], ext[3];
    for (int c = 0; c < 3; ++c) {
        ray[c] = srgbToLinearExe(v.rayleighAlbedo[c]) * ((1.0f - v.mieBlend) * density);
        mie[c] = srgbToLinearExe(v.mieAlbedo[c]) * (density * v.mieBlend);
        ext[c] = std::fmax((mie[c] + ray[c]) * (1.0f - v.extinctionDesaturation) + v.extinctionDesaturation * density, kFltEps);
        f.extinction[c] = ext[c];
        f.rayleigh[c] = v4 / ext[c] * ray[c];
        f.mie[c] = v4 / ext[c] * mie[c];
        f.maxOpticalDepth[c] = f.maxDistance * kLog2e * ext[c];
    }
    f.mieAnisotropy = v.mieAnisotropy;
    // 0x17237e7-0x1723869: the fog sun direction, normalised (a zero vector stays zero).
    float len = std::sqrt(v.sunDir[0] * v.sunDir[0] + v.sunDir[1] * v.sunDir[1] + v.sunDir[2] * v.sunDir[2]);
    if (!(len > 0.0f)) len = 1.0f;
    for (int c = 0; c < 3; ++c) f.sunDir[c] = v.sunDir[c] * (1.0f / len);
    // 0x1723871-0x17238ae: the sky band, in zenith angle (the shader approximates it from dir.z).
    const float minRad = v.skyMinAngle * kDeg;
    const float range = std::fmax(v.skyMaxAngle * kDeg - minRad, kFltEps);
    f.skyFalloffBias = (kHalfPi - minRad) / range;
    f.skyFalloffScale = -kHalfPi / range;
    // 0x17238b6-0x17238f8: the height fog.
    f.heightBase = v.heightBase;
    f.heightDensity = v.heightHalfPlane > 0 ? kLn2 / v.heightHalfPlane : 0.0f;
    f.heightMaxOffset = v.heightMaxOffset;
    f.heightAdditional = v.heightAdditional;
    f.atmospheric = 0.0f < v.atmospheric;  // 0x17238fe
    if (!f.atmospheric) {
        // 0x1723917-0x1723ac3: the plain fog reuses the same slots.
        for (int c = 0; c < 3; ++c) {
            f.rayleigh[c] = srgbToLinearExe(v.color[c]) * v.colorIntensity;
            f.mie[c] = srgbToLinearExe(v.sunColor[c]) * v.sunColorIntensity;
        }
        f.maxDistance = v.maxOpacity;
        f.skyDistance = v.skyIntensity * v.maxOpacity;
        f.maxOpticalDepth[0] = v.density != 0 ? std::fmax(kLn2 / v.density - 1e-5f, 0.0f) * kLog2e : 0.0f;
        f.maxOpticalDepth[1] = v.sunDensity != 0 ? std::fmax(kLn2 / v.sunDensity - 1e-5f, 0.0f) * kLog2e : 0.0f;
        f.maxOpticalDepth[2] = 0;
        f.heightDensity = 0;
        f.mieAnisotropy = 1.0f - v.maxOpacity;
        const float a = std::cos(v.sunBeginFadeAngle * kDeg), b = std::cos(v.sunEndFadeAngle * kDeg);
        const float s = a > b ? 1.0f / (a - b) : 100.0f;
        f.sunFalloffScale = s;
        f.sunFalloffBias = -(s * b);
    }
    return f;
}

GameFogConstants gameFogConstants(const GameFogRefdef& f, float eyeZ, float skyDomeDistance) {
    // R_SetFrameFog (0x19a9940); cb7 starts at viewInfo + 0x1890, so cb7[131] is + 0x20c0.
    GameFogConstants k{};
    float (*c)[4] = k.c;
    c[0][0] = f.skyDistance - f.startDist, c[0][1] = f.skyFalloffScale, c[0][2] = f.skyFalloffBias, c[0][3] = f.skyDistance;
    c[1][0] = f.sunDir[0], c[1][1] = f.sunDir[1], c[1][2] = f.sunDir[2], c[1][3] = -f.startDist;
    for (int i = 0; i < 3; ++i) c[2][i] = f.maxOpticalDepth[i], c[3][i] = f.mie[i], c[4][i] = f.rayleigh[i];
    c[2][3] = std::fmax(f.heightDensity, kFltEps);
    c[3][3] = f.mieAnisotropy;
    // 0x19a9b1f-0x19a9b40: the camera's height-fog factor.
    c[4][3] = std::exp(-(std::fmax(eyeZ - f.heightBase, -f.heightMaxOffset) * f.heightDensity));
    c[5][0] = 0, c[5][1] = 0, c[5][2] = 1, c[5][3] = f.heightAdditional;
    c[6][0] = f.sunFalloffScale, c[6][1] = f.sunFalloffBias, c[6][2] = f.atmospheric ? 0.0f : -1.0f;
    c[6][3] = 1.0f / std::fmax(f.maxDistance, 0.001f);
    c[7][0] = skyDomeDistance;
    return k;
}

float gameFogLutSample(const float* lut, float x) {
    if (!lut) return 0.0f;
    // u = x(2 - x) x 0.992188 + 0.003906: texel coordinate x(2 - x) x 127 (PO:192-194).
    const float s = sat(x);
    float t = (s * (2.0f - s) * 0.992188f + 0.003906f) * float(kGameFogLutSize) - 0.5f;
    if (t < 0) t = 0;
    if (t > float(kGameFogLutSize - 1)) t = float(kGameFogLutSize - 1);
    int i = int(t);
    if (i > kGameFogLutSize - 2) i = kGameFogLutSize - 2;
    const float w = t - float(i);
    return lut[i] + (lut[i + 1] - lut[i]) * w;
}

void gameFogSplineBlend(const GameFogSplineEntry* e, int count, float startDist, float maxDistance, float out[kGameFogLutSize]) {
    // R_FogSpline_GetBlendParameters (0xef1360): per entry (weight, maxDist x ln2 / half x sum(w half) / ln2,
    // 1 / maxDist, -start / maxDist); CB0[0] = (count, 1 / maxDistance, maxDistance, startDist).
    if (count > 8) count = 8;
    float p[8][4] = {};
    float sumWH = 0;
    for (int k = 0; k < count; ++k) {
        p[k][0] = e[k].weight;
        p[k][1] = e[k].maxDistance * (e[k].halfPlane != 0 ? kLn2 / e[k].halfPlane : 0.0f);
        p[k][2] = e[k].maxDistance > 0 ? 1.0f / e[k].maxDistance : 0.0f;
        p[k][3] = -(p[k][2] * e[k].startDist);
        sumWH += e[k].weight * e[k].halfPlane;
    }
    const float scale = sumWH != 0 ? 1.0f / (kLn2 / sumWH) : 0.0f;
    for (int k = 0; k < count; ++k) p[k][1] *= scale;
    const float rcpMax = maxDistance != 0 ? 1.0f / maxDistance : 0.0f;
    // cs_blend_fog_splines (191): one texel per thread.
    for (int i = 0; i < kGameFogLutSize; ++i) {
        const float u = 1.0f - std::sqrt(1.0f - float(i) * 0.007874f);
        const float rel = u * maxDistance;           // r0.y
        const float dist = u * maxDistance + startDist;  // r0.x
        float acc = 0;
        for (int k = 0; k < count; ++k) {
            const float z = dist * p[k][2] + p[k][3];
            const float val = z - gameFogLutSample(e[k].curve, z);
            acc += p[k][0] * (rel - p[k][1] * val);
        }
        out[i] = i == 0 ? 0.0f : acc * rcpMax;
    }
}

GameFogSample gameFogEvaluate(const GameFogConstants& k, const float* lut, const float dir[3], float distance, bool sky) {
    const float (*c)[4] = k.c;  // c[n] = cb7[131 + n]
    GameFogSample out{};
    // PO:39-57: vertical extent, sky band factor, path length past the start; the sky uses fogSkyDistance - start.
    float dz = dir[2] * distance;
    float skyF = 0.0f;
    float path = distance + c[1][3];
    if (sky) {
        dz = dir[2] * c[0][0];
        skyF = sat(dir[2] * c[0][1] + c[0][2]);
        path = c[0][0];
    }
    const float cosSun = dir[0] * c[1][0] + dir[1] * c[1][1] + dir[2] * c[1][2];  // PO:58
    // PO:59-161 (the Warzone circle fog, cb7[139..151]) is off on MP maps: no change to path, T or L.
    float T[3], L[3];
    if (c[6][2] < -0.5f) {
        // Plain fog (PO:166-180).
        const float sunF = sat(cosSun * c[6][0] + c[6][1]);
        const float tf = std::fmax(std::exp2(-(path * c[2][0])), c[3][3]);
        const float ts = std::fmax(std::exp2(-(path * c[2][1])), c[3][3]);
        float t = sat(sunF * (ts - tf) + tf);
        if (sky) t = 1.0f - c[0][3] * skyF;
        for (int i = 0; i < 3; ++i) {
            T[i] = t;
            L[i] = (sunF * (c[3][i] - c[4][i]) + c[4][i]) * (1.0f - t);
        }
    } else {
        // Atmospheric fog (PO:182-214).
        float x = sat(path * c[6][3]);
        x = std::fmax(x - gameFogLutSample(lut, x), 0.0f);
        const float kz = dz * c[2][3];
        float h = c[4][3];
        if (std::fabs(kz) > 1e-6f) h = (1.0f - std::exp2(kz * -1.442695f)) / kz * c[4][3];
        h += c[5][3];
        x *= h;
        for (int i = 0; i < 3; ++i) {
            T[i] = std::exp2(-(x * c[2][i]));
            if (sky) T[i] = T[i] * skyF + (1.0f - skyF);
        }
        const float phaseR = (cosSun * cosSun + 1.0f) * 0.059683f;  // 3 / (16 pi) (1 + cos^2)
        const float g = c[3][3];
        const float m = 1.0f - g * cosSun;
        const float phaseM = (1.0f - g * g) * 0.079577f / (m * m);  // (1 - g^2) / (4 pi (1 - g cos)^2)
        for (int i = 0; i < 3; ++i) L[i] = (c[3][i] * phaseM + c[4][i] * phaseR) * (1.0f - T[i]);
    }
    for (int i = 0; i < 3; ++i) out.transmittance[i] = T[i], out.inscatter[i] = L[i];
    return out;
}

}  // namespace iw8r
