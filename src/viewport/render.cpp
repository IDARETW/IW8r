#include "render.h"

#include <algorithm>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace iw8r::app {

bool Prof::on = false;

bool perfOff(const char* name) {
    static const std::string list = [] {
        const char* v = std::getenv("IW8R_PERF_OFF");
        return "," + std::string(v ? v : "") + ",";
    }();
    return list.size() > 2 && list.find("," + std::string(name) + ",") != std::string::npos;
}

namespace {
struct ProfEntry {
    const char* name;
    long long ns;
    long long count;
};
std::vector<ProfEntry>& profEntries() {
    static std::vector<ProfEntry> v;
    return v;
}
long long profNow() {
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (long long)(double(c.QuadPart) * 1e9 / double(f.QuadPart));
}
}  // namespace

void Prof::add(const char* name, long long ns) {
    for (auto& e : profEntries())
        if (e.name == name || std::strcmp(e.name, name) == 0) {
            e.ns += ns;
            ++e.count;
            return;
        }
    profEntries().push_back({name, ns, 1});
}

std::string Prof::dump() {
    std::string out;
    char buf[160];
    for (const auto& e : profEntries()) {
        std::snprintf(buf, sizeof(buf), " %s=%.3fms/%lld", e.name, double(e.ns) / 1e6, e.count);
        out += buf;
    }
    profEntries().clear();
    return out;
}

ProfScope::ProfScope(const char* n) : name(n), t0(Prof::on ? profNow() : 0) {}
ProfScope::~ProfScope() {
    if (Prof::on) Prof::add(name, profNow() - t0);
}

namespace {

const char* kShader = R"(
cbuffer CB : register(b0) { row_major float4x4 viewProj; float4 eyeStart; float4 fogColHalf; float4 litParams; row_major float4x4 sunViewProj; float4 grade0; float4 gradeLight; float4 gradeDark; };
// Colour grade (3.15): grade0 = contrast, brightness, desaturation, flags (1 film, 2 invert, 4 LUT); gradeLight.w = LUT size.
Texture3D gradeLut : register(t4);
SamplerState lutSamp : register(s2);
float3 applyGrade(float3 c) {
    int f = int(grade0.w + 0.5);
    if (f == 0) return c;
    c = saturate(c);
    if (f & 1) {
        float l = dot(c, float3(0.299, 0.587, 0.114));
        c = lerp(c, l.xxx, grade0.z);
        c = (c - 0.5) * grade0.x + 0.5 + grade0.y;
        c *= lerp(gradeDark.rgb, gradeLight.rgb, saturate(l));
        if (f & 2) c = 1.0 - c;
        c = saturate(c);
    }
    if (f & 4) {
        float n = max(gradeLight.w, 2.0);
        c = gradeLut.SampleLevel(lutSamp, c * ((n - 1.0) / n) + 0.5 / n, 0).rgb;
    }
    return c;
}
// litParams: x = lighting decode scale (2 in the lit preview: vertex colours carry light / 2), y = 1 with the sun shadow
// map, z = shadow depth bias, w = exposure. In the lit preview a vertex alpha a >= 0.5 carries the sun's share of the
// light, (a - 0.5) * 2, which the shadow map removes where the sun is blocked.
Texture2D<float> shadowMap : register(t2);
SamplerComparisonState shadowSamp : register(s1);
float sunShadow(float3 w) {
    if (litParams.y < 0.5) return 1.0;
    float4 lp = mul(float4(w, 1.0), sunViewProj);
    float2 uv = float2(lp.x * 0.5 + 0.5, 0.5 - lp.y * 0.5);
    if (uv.x <= 0 || uv.y <= 0 || uv.x >= 1 || uv.y >= 1 || lp.z >= 1) return 1.0;
    float s = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            s += shadowMap.SampleCmpLevelZero(shadowSamp, uv + float2(x, y) / 2048.0, lp.z - litParams.z);
    return s / 9.0;
}
float3 litDecode(float3 c, float a, float3 w) {
    if (litParams.x < 1.5) return c;
    c *= litParams.x;
    float share = saturate((a - 0.5) * 2.0);
    if (litParams.y > 0.5) c *= 1.0 - share * (1.0 - sunShadow(w));
    return c;
}
float3 exposeSoft(float3 c) {
    if (litParams.x < 1.5) return c;
    c *= litParams.w;
    return c / (1.0 + max(c - 1.0, 0.0));  // soft shoulder above 1 instead of a hard clip
}
float3 applyFog(float3 c, float3 w) { if (fogColHalf.a <= 0) return c; float d = max(0.0, distance(w, eyeStart.xyz) - eyeStart.w); return lerp(c, fogColHalf.rgb, 1.0 - exp2(-d / fogColHalf.a)); }
struct VSIn { float3 pos : POSITION; float4 col : COLOR; };
struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR; float3 w : TEXCOORD1; };
PSIn vs_main(VSIn i) { PSIn o; o.pos = mul(float4(i.pos, 1.0), viewProj); o.col = i.col; o.w = i.pos; return o; }
struct VSInst { float3 pos : POSITION; float4 col : COLOR; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2; };
PSIn vs_inst(VSInst i) {
    float4 p = float4(i.pos, 1.0);
    float3 w = float3(dot(i.r0, p), dot(i.r1, p), dot(i.r2, p));
    PSIn o; o.pos = mul(float4(w, 1.0), viewProj); o.col = i.col; o.w = w; return o;
}
float4 ps_main(PSIn i) : SV_TARGET {
    if (litParams.x < 1.5) return float4(applyGrade(applyFog(i.col.rgb, i.w)), i.col.a);
    return float4(applyGrade(applyFog(exposeSoft(litDecode(i.col.rgb, i.col.a, i.w)), i.w)), 1.0);
}
float4 ps_depth(PSIn i) : SV_TARGET { return float4(0, 0, 0, 1); }
)";

const char* kTexShader = R"(
cbuffer CB : register(b0) { row_major float4x4 viewProj; float4 eyeStart; float4 fogColHalf; float4 litParams; row_major float4x4 sunViewProj; float4 grade0; float4 gradeLight; float4 gradeDark; };
// Colour grade (3.15): grade0 = contrast, brightness, desaturation, flags (1 film, 2 invert, 4 LUT); gradeLight.w = LUT size.
Texture3D gradeLut : register(t4);
SamplerState lutSamp : register(s2);
float3 applyGrade(float3 c) {
    int f = int(grade0.w + 0.5);
    if (f == 0) return c;
    c = saturate(c);
    if (f & 1) {
        float l = dot(c, float3(0.299, 0.587, 0.114));
        c = lerp(c, l.xxx, grade0.z);
        c = (c - 0.5) * grade0.x + 0.5 + grade0.y;
        c *= lerp(gradeDark.rgb, gradeLight.rgb, saturate(l));
        if (f & 2) c = 1.0 - c;
        c = saturate(c);
    }
    if (f & 4) {
        float n = max(gradeLight.w, 2.0);
        c = gradeLut.SampleLevel(lutSamp, c * ((n - 1.0) / n) + 0.5 / n, 0).rgb;
    }
    return c;
}
// litParams: x = lighting decode scale (2 in the lit preview: vertex colours carry light / 2), y = 1 with the sun shadow
// map, z = shadow depth bias, w = exposure. In the lit preview a vertex alpha a >= 0.5 carries the sun's share of the
// light, (a - 0.5) * 2, which the shadow map removes where the sun is blocked.
Texture2D<float> shadowMap : register(t2);
SamplerComparisonState shadowSamp : register(s1);
float sunShadow(float3 w) {
    if (litParams.y < 0.5) return 1.0;
    float4 lp = mul(float4(w, 1.0), sunViewProj);
    float2 uv = float2(lp.x * 0.5 + 0.5, 0.5 - lp.y * 0.5);
    if (uv.x <= 0 || uv.y <= 0 || uv.x >= 1 || uv.y >= 1 || lp.z >= 1) return 1.0;
    float s = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            s += shadowMap.SampleCmpLevelZero(shadowSamp, uv + float2(x, y) / 2048.0, lp.z - litParams.z);
    return s / 9.0;
}
float3 litDecode(float3 c, float a, float3 w) {
    if (litParams.x < 1.5) return c;
    c *= litParams.x;
    float share = saturate((a - 0.5) * 2.0);
    if (litParams.y > 0.5) c *= 1.0 - share * (1.0 - sunShadow(w));
    return c;
}
float3 exposeSoft(float3 c) {
    if (litParams.x < 1.5) return c;
    c *= litParams.w;
    return c / (1.0 + max(c - 1.0, 0.0));  // soft shoulder above 1 instead of a hard clip
}
float3 applyFog(float3 c, float3 w) { if (fogColHalf.a <= 0) return c; float d = max(0.0, distance(w, eyeStart.xyz) - eyeStart.w); return lerp(c, fogColHalf.rgb, 1.0 - exp2(-d / fogColHalf.a)); }
cbuffer Batch : register(b1) { float4 batchFlags; float4 batchFlags2; };  // x = 1 when t1 holds the material's normal map, y = blend mode, z = sway, w = sway base z; batchFlags2.x = 1 when t3 holds an emissive map
Texture2D emi0 : register(t3);
TextureCube probeCube : register(t5);  // 3.16: the nearest captured reflection probe (batchFlags2.y = 1 when bound)
cbuffer Wind : register(b3) { float4 wind; };         // xy = direction x amplitude, z = wave phase (core foliage_sway)
struct PixelLight { float4 posRadius; float4 colorOuter; float4 dirInner; };
cbuffer Lights : register(b2) { float4 lightCount; PixelLight lights[64]; };
// View > Game Lighting: game0 = (on, LUT input scale, lightmap bound, 0), sun toward, sun colour, unbaked light.
cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; };
Texture2D gameLmA : register(t6);
Texture2D gameLmB : register(t7);
Texture2D gameLmD : register(t8);
Texture3D gameLut : register(t9);
Texture2D tex0 : register(t0);
Texture2D nrm0 : register(t1);
SamplerState samp0 : register(s0);
struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float2 lm : TEXCOORD4; };
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 w : TEXCOORD1; float3 n : TEXCOORD2; float3 t : TEXCOORD3; float2 lm : TEXCOORD4; };
PSIn vs_tex(VSIn i) {
    PSIn o;
    float3 p = i.pos;
    if (batchFlags.z > 0.5 && dot(wind.xy, wind.xy) > 0) {
        // foliage_sway.cpp swayOffset: amplitude grows with the square of the height above the base (full at 128 units).
        float h = saturate((p.z - batchFlags.w) / 128.0);
        float ph = dot(p, float3(0.045, 0.031, 0.017));
        float wave = 0.7 * sin(wind.z + ph) + 0.3 * sin(1.7 * wind.z + 1.3 * ph);
        p.xy += wind.xy * (h * h * wave);
    }
    o.pos = mul(float4(p, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = p; o.n = i.n; o.t = i.t; o.lm = i.lm; return o;
}
// Textured instanced reference props: the model's vertices placed by three transform rows per instance.
struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float2 lm : TEXCOORD4; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2; };
PSIn vs_tex_inst(VSInInst i) {
    float4 p = float4(i.pos, 1.0);
    float3 w = float3(dot(i.r0, p), dot(i.r1, p), dot(i.r2, p));
    PSIn o; o.pos = mul(float4(w, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = w; o.n = float3(0, 0, 0); o.t = float3(0, 0, 0); o.lm = float2(-1, -1); return o;
}
// Sun shadow pass of textured batches: alpha-tested cut-outs (leaves, grates) cast their holes (research 66).
void ps_shadow_clip(PSIn i) { clip(tex0.Sample(samp0, i.uv).a - 0.3); }
float3 srgbToLinear3(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
// The game's post chain: exposure x the +1.5 EV base (game0.y), the 20-stop log shaper, the universal colour LUT.
float3 gamePost(float3 c) {
    float3 x = max(c, 0) * game0.y;
    float3 u = saturate((log2(x + 1.0 / 1024.0) + 10.0) / 20.0) * (63.0 / 64.0) + 0.5 / 64.0;
    return gameLut.SampleLevel(lutSamp, u, 0).rgb;
}
float4 ps_tex(PSIn i) : SV_TARGET {
    float4 t = tex0.Sample(samp0, i.uv);
    // Blend 0: alpha-tested foliage and grates. Blended materials keep their coverage; without a mask (alpha 1) a
    // blended surface (glass, a decal sheet) shows at 60 percent so what is behind it stays visible.
    float outA = 1.0;
    if (batchFlags.y < 0.5) clip(t.a - 0.3);
    else outA = t.a > 0.99 ? 0.6 : t.a;
    if (game0.x > 0.5) {
        // View > Game Lighting: map_surface_realtime.hlsl (the converter's world shader, iw3 style) and the post chain.
        float3 albedo = srgbToLinear3(t.rgb) * i.col.rgb;  // i.col: selection and brush entity tints (white otherwise)
        if (i.col.a < 0.5) return float4(gamePost(albedo), 1.0);  // sky box: unlit
        float3 Nf = dot(i.n, i.n) > 0.5 ? normalize(i.n) : normalize(cross(ddx(i.w), ddy(i.w)));
        float3 N = Nf;
        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
            float4 nm = nrm0.Sample(samp0, i.uv);
            float2 sd = nm.ga * 2.0 - 1.0;
            float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
            float2 slope = oct / max(0.02, 1.0 - abs(oct.x) - abs(oct.y));
            float3 T = normalize(i.t - Nf * dot(i.t, Nf)), B = cross(Nf, T);
            N = normalize(T * slope.x + B * slope.y + Nf);
        }
        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));
        float visibility = 1.0;
        if (game0.z > 0.5 && i.lm.x >= 0) {
            float4 a = gameLmA.SampleLevel(lutSamp, i.lm, 0);
            float3 b = gameLmB.SampleLevel(lutSamp, i.lm, 0).rgb;
            float3 d = gameLmD.SampleLevel(lutSamp, i.lm, 0).xyz;
            indirect = a.rgb * saturate(dot(N, Nf)) + b * saturate(dot(normalize(d + 1e-6), N));
            visibility = a.a;
        }
        // The sun: the baked sun depth where it covers the point (here the sun shadow map), else the lightmap's mask.
        if (litParams.y > 0.5) visibility = sunShadow(i.w);
        float3 light = indirect + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
        float3 c = albedo * light;
        // Local lights (LocalLighting): colour x game intensity / d^2 x w^2 (3 - 2w), w = (1 - d / r) x spot cone;
        // specular as the iw3 style's (F0 .04, gloss .3).
        float3 V = normalize(eyeStart.xyz - i.w);
        int count = int(lightCount.x);
        for (int k = 0; k < count; ++k) {
            float3 dl = lights[k].posRadius.xyz - i.w;
            float dist = length(dl);
            if (dist >= lights[k].posRadius.w || dist < 1e-4) continue;
            float3 l = dl / dist;
            float spot = 1.0;
            if (lights[k].colorOuter.w > -1.5)
                spot = saturate((dot(-l, lights[k].dirInner.xyz) - lights[k].colorOuter.w) /
                                max(1e-4, lights[k].dirInner.w - lights[k].colorOuter.w));
            float w = saturate(1.0 - dist / lights[k].posRadius.w) * spot;
            float att = w * w * (3.0 - 2.0 * w) / max(dist * dist, 1.0);
            float3 irr = lights[k].colorOuter.rgb * att * saturate(dot(N, l));
            float3 h = normalize(V + l);
            float power = exp2(13.0 * 0.3);
            float dist2 = (power + 2.0) * 0.5 * pow(saturate(dot(N, h)), power);
            float fres = 0.04 + 0.96 * pow(1.0 - saturate(dot(V, h)), 5.0);
            c += albedo * irr + fres * dist2 * 0.25 * irr;
        }
        if (batchFlags2.x > 0.5) c += srgbToLinear3(emi0.Sample(samp0, i.uv).rgb);
        c = applyFog(c, i.w);
        c = gamePost(c);
        if (batchFlags.y > 1.5) return float4(c * outA, 1.0);
        return float4(c, outA);
    }
    if (i.col.a < 0.5) return float4(applyGrade(t.rgb * i.col.rgb), 1.0);  // sky box: never fogged, graded
    float3 c = t.rgb * litDecode(i.col.rgb, i.col.a, i.w);
    float3 Ns = dot(i.n, i.n) > 0.5 ? normalize(i.n) : normalize(cross(ddx(i.w), ddy(i.w)));
    float glossS = 0.0;
    if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
        // Normal map (IW8 "_n&_g", core/normal_pack.h: gloss in R, occlusion in B, the normal as an octahedral pair
        // in G and A): the per-face shade already in the vertex colour is modulated by how much the bumped normal
        // turns toward the light, plus a gloss highlight.
        float4 nm = nrm0.Sample(samp0, i.uv);
        float2 sd = nm.ga * 2.0 - 1.0;
        float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
        float2 slope = oct / max(0.02, 1.0 - abs(oct.x) - abs(oct.y));
        float3 N = normalize(i.n), T = normalize(i.t - N * dot(i.t, N)), B = cross(N, T);
        float3 bumped = normalize(T * slope.x + B * slope.y + N);
        const float3 L = normalize(float3(0.35, 0.55, 0.75));
        float flat = 0.55 + 0.45 * abs(dot(N, L)), bump = 0.55 + 0.45 * abs(dot(bumped, L));
        c *= bump / max(flat, 0.05) * (0.6 + 0.4 * nm.b);
        float3 V = normalize(eyeStart.xyz - i.w), H = normalize(L + V);
        float gloss = nm.r;
        c += pow(saturate(dot(bumped, H)), 8.0 + 120.0 * gloss) * 0.25 * gloss;
        Ns = bumped;
        glossS = gloss;
        if (batchFlags2.y > 0.5) {
            // Captured probe along the reflection vector, Schlick Fresnel (F0 0.04), blurred by roughness (mip bias).
            float3 R = reflect(-V, bumped);
            float fres = 0.04 + 0.96 * pow(1.0 - saturate(dot(bumped, V)), 5.0);
            c += probeCube.SampleBias(samp0, R, (1.0 - gloss) * 4.0).rgb * fres * gloss * 0.8;
        }
    }
    // Light entities, per pixel (lit preview): linear falloff to the radius, spot cone between inner and outer.
    int count = int(lightCount.x);
    float3 Vv = normalize(eyeStart.xyz - i.w);
    float3 Lsum = 0, Ssum = 0;
    for (int k = 0; k < count; ++k) {
        float3 d = lights[k].posRadius.xyz - i.w;
        float dist = length(d);
        if (dist >= lights[k].posRadius.w || dist < 1e-4) continue;
        float3 l = d / dist;
        float ndl = dot(Ns, l);
        if (dot(i.n, i.n) <= 0.5) ndl = abs(ndl);  // derived normals have no reliable side
        float fall = 1.0 - dist / lights[k].posRadius.w;
        float a = saturate(ndl) * fall * fall * lightCount.y;  // y: light gain (View > Lit Preview Brightness)
        if (lights[k].colorOuter.w > -1.5) {
            float cs = dot(-l, lights[k].dirInner.xyz);
            a *= saturate((cs - lights[k].colorOuter.w) / max(1e-4, lights[k].dirInner.w - lights[k].colorOuter.w));
        }
        Lsum += lights[k].colorOuter.rgb * a;
        Ssum += lights[k].colorOuter.rgb * pow(saturate(dot(Ns, normalize(l + Vv))), 8.0 + 120.0 * glossS) * 0.3 * glossS * a;
    }
    // Bright lights roll off instead of clipping to white (the game tone-maps; this keeps the texture readable).
    Lsum = Lsum / (1.0 + 0.5 * Lsum);  // soft roll-off toward 2x
    c += t.rgb * Lsum + Ssum / (1.0 + Ssum);
    // Emissive (texture slot 18): glows regardless of the lighting (screens, lamps, signs).
    if (batchFlags2.x > 0.5) c += emi0.Sample(samp0, i.uv).rgb * (litParams.x > 1.5 ? 1.0 / max(litParams.w, 0.05) : 1.0);
    c = exposeSoft(c);
    if (batchFlags.y > 1.5) return float4(applyGrade(applyFog(c, i.w)) * outA, 1.0);  // additive: colour scaled by coverage
    return float4(applyGrade(applyFog(c, i.w)), outA);
}
)";

template <class T>
void safeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

}  // namespace

SceneRenderer::~SceneRenderer() {
    releaseTargets();
    safeRelease(cb_);
    safeRelease(vbRefTex_);
    for (auto& g : refInst_) safeRelease(g.vb), safeRelease(g.inst);
    safeRelease(vsInst_);
    safeRelease(layoutInst_);
    safeRelease(shadowSrv_);
    safeRelease(shadowDsv_);
    safeRelease(shadowTex_);
    safeRelease(cmpSampler_);
    safeRelease(rsShadow_);
    safeRelease(cbBatch_);
    safeRelease(cbWind_);
    safeRelease(cbLights_);
    safeRelease(cbGame_);
    for (int k = 0; k < 3; ++k) safeRelease(gameLmSrv_[k]), safeRelease(gameLmTex_[k]);
    safeRelease(gameLutSrv_);
    safeRelease(gameLutTex_);
    releaseStreams();
    safeRelease(vbRef_);
    safeRelease(vs_);
    safeRelease(ps_);
    safeRelease(layout_);
    safeRelease(rsTris_);
    safeRelease(rsLines_);
    safeRelease(dsWrite_);
    safeRelease(dsTest_);
    safeRelease(dsOff_);
    safeRelease(blend_);
    safeRelease(blendAdd_);
    safeRelease(vsTex_);
    safeRelease(psTex_);
    safeRelease(psShadowClip_);
    safeRelease(layoutTex_);
    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.inst);
    refTexInst_.clear();
    safeRelease(vsTexInst_);
    safeRelease(layoutTexInst_);
    safeRelease(sampler_);
    safeRelease(lutSrv_);
    safeRelease(lutTex_);
    safeRelease(lutSampler_);
    safeRelease(probeSrv_);
    safeRelease(probeTex_);
    releaseTextures();
}

void SceneRenderer::releaseTextures() {
    for (auto*& t : textures_) safeRelease(t);
    textures_.clear();
    textureBytes_ = 0;
}

int SceneRenderer::createTexture(const uint8_t* rgba, int width, int height) {
    if (!dev_ || !rgba || width < 1 || height < 1) return -1;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width);
    td.Height = UINT(height);
    td.MipLevels = 0;  // full chain
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &tex))) return -1;
    ctx_->UpdateSubresource(tex, 0, nullptr, rgba, UINT(width * 4), 0);
    ID3D11ShaderResourceView* srv = nullptr;
    const HRESULT hr = dev_->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    if (FAILED(hr)) return -1;
    ctx_->GenerateMips(srv);
    textures_.push_back(srv);
    textureBytes_ += size_t(width) * size_t(height) * 4 * 4 / 3;  // + the mip chain
    return int(textures_.size()) - 1;
}

bool SceneRenderer::init(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::string* err) {
    dev_ = dev;
    ctx_ = ctx;
    ID3DBlob* vsb = nullptr;
    ID3DBlob* psb = nullptr;
    ID3DBlob* errb = nullptr;
    auto compile = [&](const char* entry, const char* target, ID3DBlob*& out) {
        HRESULT hr = D3DCompile(kShader, std::strlen(kShader), "iw8r_scene", nullptr, nullptr, entry, target, 0, 0, &out, &errb);
        if (FAILED(hr)) {
            if (err) *err = errb ? std::string((const char*)errb->GetBufferPointer(), errb->GetBufferSize()) : "D3DCompile failed";
            safeRelease(errb);
            return false;
        }
        return true;
    };
    if (!compile("vs_main", "vs_5_0", vsb) || !compile("ps_main", "ps_5_0", psb)) return false;
    {
        // Textured pipeline (camera textured mode).
        ID3DBlob* tvs = nullptr;
        ID3DBlob* tps = nullptr;
        auto compileTex = [&](const char* entry, const char* target, ID3DBlob*& out) {
            HRESULT hr = D3DCompile(kTexShader, std::strlen(kTexShader), "iw8r_tex", nullptr, nullptr, entry, target, 0, 0, &out, &errb);
            if (FAILED(hr)) {
                if (err) *err = errb ? std::string((const char*)errb->GetBufferPointer(), errb->GetBufferSize()) : "D3DCompile failed";
                safeRelease(errb);
                return false;
            }
            return true;
        };
        if (compileTex("vs_tex", "vs_5_0", tvs) && compileTex("ps_tex", "ps_5_0", tps)) {
            dev_->CreateVertexShader(tvs->GetBufferPointer(), tvs->GetBufferSize(), nullptr, &vsTex_);
            dev_->CreatePixelShader(tps->GetBufferPointer(), tps->GetBufferSize(), nullptr, &psTex_);
            ID3DBlob* sps = nullptr;
            if (compileTex("ps_shadow_clip", "ps_5_0", sps)) {
                dev_->CreatePixelShader(sps->GetBufferPointer(), sps->GetBufferSize(), nullptr, &psShadowClip_);
                safeRelease(sps);
            }
            const D3D11_INPUT_ELEMENT_DESC til[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            dev_->CreateInputLayout(til, 6, tvs->GetBufferPointer(), tvs->GetBufferSize(), &layoutTex_);
            ID3DBlob* tivs = nullptr;
            if (compileTex("vs_tex_inst", "vs_5_0", tivs)) {
                dev_->CreateVertexShader(tivs->GetBufferPointer(), tivs->GetBufferSize(), nullptr, &vsTexInst_);
                const D3D11_INPUT_ELEMENT_DESC tiil[] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                };
                dev_->CreateInputLayout(tiil, 9, tivs->GetBufferPointer(), tivs->GetBufferSize(), &layoutTexInst_);
                safeRelease(tivs);
            }
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = 32;  // batchFlags + batchFlags2
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            dev_->CreateBuffer(&bd, nullptr, &cbBatch_);
            dev_->CreateBuffer(&bd, nullptr, &cbWind_);
            bd.ByteWidth = 16 + UINT(sizeof(PixelLight)) * kMaxPixelLights;
            dev_->CreateBuffer(&bd, nullptr, &cbLights_);
            bd.ByteWidth = 64;  // game0, sun toward, sun colour, unbaked light
            dev_->CreateBuffer(&bd, nullptr, &cbGame_);
        }
        safeRelease(tvs);
        safeRelease(tps);
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_ANISOTROPIC;
        sd.MaxAnisotropy = 8;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        dev_->CreateSamplerState(&sd, &sampler_);
    }
    dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_);
    dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_);
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    dev_->CreateInputLayout(il, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_);
    safeRelease(vsb);
    {
        // 3.20 instanced reference props: per-vertex position + colour, per-instance three transform rows.
        ID3DBlob* ivs = nullptr;
        if (compile("vs_inst", "vs_5_0", ivs)) {
            dev_->CreateVertexShader(ivs->GetBufferPointer(), ivs->GetBufferSize(), nullptr, &vsInst_);
            const D3D11_INPUT_ELEMENT_DESC iil[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTROW", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTROW", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            };
            dev_->CreateInputLayout(iil, 5, ivs->GetBufferPointer(), ivs->GetBufferSize(), &layoutInst_);
            safeRelease(ivs);
        }
    }
    safeRelease(psb);

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 224;  // viewProj, eye + fog start, fog colour + halfway, lit params, sun shadow viewProj, grade (3 x float4)
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev_->CreateBuffer(&cbd, nullptr, &cb_);

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.DepthBias = 40;
    rd.SlopeScaledDepthBias = 1.5f;
    dev_->CreateRasterizerState(&rd, &rsTris_);
    rd.DepthBias = 0;
    rd.SlopeScaledDepthBias = 0;
    rd.AntialiasedLineEnable = TRUE;
    dev_->CreateRasterizerState(&rd, &rsLines_);

    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dev_->CreateDepthStencilState(&dd, &dsWrite_);
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dev_->CreateDepthStencilState(&dd, &dsTest_);
    dd.DepthEnable = FALSE;
    dev_->CreateDepthStencilState(&dd, &dsOff_);

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev_->CreateBlendState(&bd, &blend_);
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    dev_->CreateBlendState(&bd, &blendAdd_);
    return vs_ && ps_ && layout_ && cb_;
}

void SceneRenderer::releaseTargets() {
    safeRelease(srv_);
    safeRelease(rtv_);
    safeRelease(dsv_);
    safeRelease(color_);
    safeRelease(depth_);
}

void SceneRenderer::setReflectionProbe(const uint8_t* faces, int size) {
    safeRelease(probeSrv_);
    safeRelease(probeTex_);
    if (!faces || size < 1 || !dev_) return;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = UINT(size);
    td.MipLevels = 0;  // full chain (generated) for the roughness blur
    td.ArraySize = 6;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &probeTex_))) return;
    D3D11_TEXTURE2D_DESC got{};
    probeTex_->GetDesc(&got);
    for (UINT f = 0; f < 6; ++f)
        ctx_->UpdateSubresource(probeTex_, D3D11CalcSubresource(0, f, got.MipLevels), nullptr, faces + size_t(f) * size_t(size) * size_t(size) * 4,
                                UINT(size) * 4, 0);
    if (FAILED(dev_->CreateShaderResourceView(probeTex_, nullptr, &probeSrv_))) {
        safeRelease(probeTex_);
        return;
    }
    ctx_->GenerateMips(probeSrv_);
}

void SceneRenderer::setGradeLut(int size, const uint8_t* rgba) {
    safeRelease(lutSrv_);
    safeRelease(lutTex_);
    gradeFlags_ &= ~4;
    grade_[7] = 0;
    if (size < 2 || !rgba || !dev_) return;
    D3D11_TEXTURE3D_DESC td{};
    td.Width = td.Height = td.Depth = UINT(size);
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba;
    init.SysMemPitch = UINT(size) * 4;
    init.SysMemSlicePitch = UINT(size) * UINT(size) * 4;
    if (FAILED(dev_->CreateTexture3D(&td, &init, &lutTex_)) || FAILED(dev_->CreateShaderResourceView(lutTex_, nullptr, &lutSrv_))) {
        safeRelease(lutTex_);
        return;
    }
    if (!lutSampler_) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        dev_->CreateSamplerState(&sd, &lutSampler_);
    }
    gradeFlags_ |= 4;
    grade_[7] = float(size);
}

void SceneRenderer::setGameLightmap(int width, int height, const float* a, const float* b, const float* d) {
    for (int k = 0; k < 3; ++k) safeRelease(gameLmSrv_[k]), safeRelease(gameLmTex_[k]);
    if (!dev_ || width < 1 || height < 1 || !a || !b || !d) return;
    const float* src[3] = {a, b, d};
    for (int k = 0; k < 3; ++k) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(width), td.Height = UINT(height);
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{src[k], UINT(width) * 16, 0};
        if (FAILED(dev_->CreateTexture2D(&td, &init, &gameLmTex_[k])) ||
            FAILED(dev_->CreateShaderResourceView(gameLmTex_[k], nullptr, &gameLmSrv_[k]))) {
            for (int j = 0; j < 3; ++j) safeRelease(gameLmSrv_[j]), safeRelease(gameLmTex_[j]);
            return;
        }
    }
}

void SceneRenderer::setGameTonemapLut(int size, const uint8_t* rgba) {
    safeRelease(gameLutSrv_);
    safeRelease(gameLutTex_);
    if (size < 2 || !rgba || !dev_) return;
    D3D11_TEXTURE3D_DESC td{};
    td.Width = td.Height = td.Depth = UINT(size);
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{rgba, UINT(size) * 4, UINT(size) * UINT(size) * 4};
    if (FAILED(dev_->CreateTexture3D(&td, &init, &gameLutTex_)) || FAILED(dev_->CreateShaderResourceView(gameLutTex_, nullptr, &gameLutSrv_))) {
        safeRelease(gameLutTex_);
        return;
    }
    if (!lutSampler_) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        dev_->CreateSamplerState(&sd, &lutSampler_);
    }
}

void SceneRenderer::resize(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == w_ && h == h_ && color_) return;
    releaseTargets();
    w_ = w;
    h_ = h;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(w);
    td.Height = UINT(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    dev_->CreateTexture2D(&td, nullptr, &color_);
    dev_->CreateRenderTargetView(color_, nullptr, &rtv_);
    dev_->CreateShaderResourceView(color_, nullptr, &srv_);
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    dev_->CreateTexture2D(&td, nullptr, &depth_);
    dev_->CreateDepthStencilView(depth_, nullptr, &dsv_);
}

bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches) {
    safeRelease(vbRefTex_);
    refTexFirst_.clear(), refTexCount_.clear(), refTexIds_.clear(), refTexChunks_.clear();
    refChunkTotal_ = refCulledLast_ = 0;
    size_t total = 0;
    for (const auto& b : batches) total += b.size();
    if (!total || !dev_) return true;
    std::vector<GpuTexVertex> all;
    all.reserve(total);
    for (const auto& b : batches) {
        refTexFirst_.push_back(UINT(all.size()));
        refTexCount_.push_back(UINT(b.size()));
        // 3.20: spatial order and chunks for frustum culling.
        std::vector<iw8r::Vec3> corners;
        corners.reserve(b.size() - b.size() % 3);
        for (size_t i = 0; i + 2 < b.size(); i += 3)
            for (size_t k = 0; k < 3; ++k) corners.push_back({b[i + k].x, b[i + k].y, b[i + k].z});
        std::vector<uint32_t> order;
        std::vector<iw8r::DrawChunk> chunks;
        iw8r::buildDrawChunks(corners, 2048, order, chunks);
        for (uint32_t t : order)
            for (size_t k = 0; k < 3; ++k) all.push_back(b[size_t(t) * 3 + k]);
        for (size_t i = order.size() * 3; i < b.size(); ++i) all.push_back(b[i]);  // a stray partial triangle (none expected)
        refChunkTotal_ += chunks.size();
        refTexChunks_.push_back(std::move(chunks));
    }
    refTexIds_.assign(batches.size(), -1);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = UINT(all.size() * sizeof(GpuTexVertex));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{all.data(), 0, 0};
    if (FAILED(dev_->CreateBuffer(&bd, &init, &vbRefTex_))) {
        refTexFirst_.clear(), refTexCount_.clear(), refTexIds_.clear(), refTexChunks_.clear();
        return false;
    }
    if (whiteTex_ < 0) {
        const uint8_t white[16] = {200, 200, 200, 255, 200, 200, 200, 255, 200, 200, 200, 255, 200, 200, 200, 255};
        whiteTex_ = createTexture(white, 2, 2);
    }
    return true;
}

bool SceneRenderer::setReferenceInstances(const std::vector<InstancedMesh>& meshes) {
    for (auto& g : refInst_) safeRelease(g.vb), safeRelease(g.inst);
    refInst_.clear();
    refInstTotal_ = 0;
    shadowDirty_ = true;
    if (!dev_ || !vsInst_ || !layoutInst_) return meshes.empty();
    for (const InstancedMesh& m : meshes) {
        const size_t count = m.rows.size() / 12;
        if (m.tris.empty() || count == 0) continue;
        GpuInstanced g;
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.ByteWidth = UINT(m.tris.size() * sizeof(GpuVertex));
        D3D11_SUBRESOURCE_DATA init{m.tris.data(), 0, 0};
        if (FAILED(dev_->CreateBuffer(&bd, &init, &g.vb))) continue;
        bd.ByteWidth = UINT(count * 12 * sizeof(float));
        D3D11_SUBRESOURCE_DATA iinit{m.rows.data(), 0, 0};
        if (FAILED(dev_->CreateBuffer(&bd, &iinit, &g.inst))) {
            safeRelease(g.vb);
            continue;
        }
        g.vertices = unsigned(m.tris.size());
        g.instances = unsigned(count);
        g.editorMarker = m.editorMarker;
        refInstTotal_ += count;
        refInst_.push_back(g);
    }
    return true;
}

bool SceneRenderer::setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes) {
    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.inst);
    refTexInst_.clear();
    if (!dev_ || !vsTexInst_ || !layoutTexInst_) return meshes.empty();
    for (const InstancedTexMesh& m : meshes) {
        const size_t count = m.rows.size() / 12;
        GpuTexInstanced g;  // kept even when empty: the index is the caller's mesh index (textures are set by it)
        if (!m.tris.empty() && count > 0) {
            D3D11_BUFFER_DESC bd{};
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.ByteWidth = UINT(m.tris.size() * sizeof(GpuTexVertex));
            D3D11_SUBRESOURCE_DATA init{m.tris.data(), 0, 0};
            if (SUCCEEDED(dev_->CreateBuffer(&bd, &init, &g.vb))) {
                bd.ByteWidth = UINT(count * 12 * sizeof(float));
                D3D11_SUBRESOURCE_DATA iinit{m.rows.data(), 0, 0};
                if (SUCCEEDED(dev_->CreateBuffer(&bd, &iinit, &g.inst))) {
                    g.vertices = unsigned(m.tris.size());
                    g.instances = unsigned(count);
                } else {
                    safeRelease(g.vb);
                }
            }
        }
        refTexInst_.push_back(g);
    }
    return true;
}

void SceneRenderer::drawReferenceTexInstances() {
    if (refTexInst_.empty() || !vsTexInst_ || !layoutTexInst_ || !psTex_) return;
    ctx_->IASetInputLayout(layoutTexInst_);
    ctx_->VSSetShader(vsTexInst_, nullptr, 0);
    ctx_->PSSetShader(psTex_, nullptr, 0);
    ctx_->PSSetSamplers(0, 1, &sampler_);
    if (cbBatch_) {
        const float flags[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
        ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
        ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
    }
    ID3D11ShaderResourceView* noNrm = nullptr;
    ctx_->PSSetShaderResources(1, 1, &noNrm);
    const UINT strides[2] = {UINT(sizeof(GpuTexVertex)), UINT(12 * sizeof(float))}, offsets[2] = {0, 0};
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        const int id = g.texture >= 0 ? g.texture : whiteTex_;
        if (id < 0 || size_t(id) >= textures_.size()) continue;
        ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        ID3D11Buffer* bufs[2] = {g.vb, g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, g.instances, 0, 0);
    }
    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
}

void SceneRenderer::drawReferenceInstances(ReferencePass pass) {
    if (refInst_.empty() || !vsInst_ || !layoutInst_) return;
    ctx_->IASetInputLayout(layoutInst_);
    ctx_->VSSetShader(vsInst_, nullptr, 0);
    const UINT strides[2] = {UINT(sizeof(GpuVertex)), UINT(12 * sizeof(float))}, offsets[2] = {0, 0};
    for (const auto& g : refInst_) {
        if (!drawsReferencePiece(g.editorMarker, pass)) continue;  // editor markers: on screen only
        if (pass != ReferencePass::Shadow) stats_.referenceInstanceVertices += size_t(g.vertices) * g.instances;
        ID3D11Buffer* bufs[2] = {g.vb, g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, g.instances, 0, 0);
    }
    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
}

bool SceneRenderer::setReference(const std::vector<GpuVertex>& input) {
    safeRelease(vbRef_);
    shadowDirty_ = true;
    refCount_ = 0;
    refChunks_.clear();
    if (input.empty() || !dev_) return true;
    // 3.20: spatial order and chunks for frustum culling.
    std::vector<iw8r::Vec3> corners;
    for (size_t i = 0; i + 2 < input.size(); i += 3)
        for (size_t k = 0; k < 3; ++k) corners.push_back({input[i + k].x, input[i + k].y, input[i + k].z});
    std::vector<uint32_t> order;
    iw8r::buildDrawChunks(corners, 2048, order, refChunks_);
    std::vector<GpuVertex> tris;
    tris.reserve(input.size());
    for (uint32_t t : order)
        for (size_t k = 0; k < 3; ++k) tris.push_back(input[size_t(t) * 3 + k]);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = UINT(tris.size() * sizeof(GpuVertex));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{tris.data(), 0, 0};
    if (FAILED(dev_->CreateBuffer(&bd, &init, &vbRef_))) return false;
    refCount_ = tris.size();
    return true;
}

void SceneRenderer::drawN(unsigned count, unsigned first, bool lines) {
    ctx_->Draw(count, first);
    if (inShadowPass_) {
        ++stats_.shadowDrawCalls;
        return;
    }
    ++stats_.drawCalls;
    if (lines) stats_.lines += count / 2;
    else stats_.triangles += count / 3;
}

void SceneRenderer::drawIdx(unsigned count, unsigned first) {
    ctx_->DrawIndexed(count, first, 0);
    ++stats_.drawCalls;
    stats_.triangles += count / 3;
}

void SceneRenderer::drawInst(unsigned vertices, unsigned instances, unsigned firstVertex, unsigned firstInstance) {
    ctx_->DrawInstanced(vertices, instances, firstVertex, firstInstance);
    if (inShadowPass_) {
        ++stats_.shadowDrawCalls;
        return;
    }
    ++stats_.drawCalls;
    stats_.triangles += size_t(vertices / 3) * instances;
}

void SceneRenderer::finishGpu() {
    // Blocks until the GPU has finished everything submitted so far (benchmark: CPU + GPU time of a frame).
    if (!dev_ || !ctx_) return;
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    ID3D11Query* q = nullptr;
    if (FAILED(dev_->CreateQuery(&qd, &q)) || !q) return;
    ctx_->End(q);
    ctx_->Flush();
    BOOL done = FALSE;
    while (ctx_->GetData(q, &done, sizeof(done), 0) != S_OK || !done) YieldProcessor();
    q->Release();
}

// ---------------------------------------------------------------------------------------------------------------
// Kept vertex streams (docs/research/85): every stream has its own vertex buffer, and only the vertex ranges that
// changed since the last frame are sent to it.

void SceneRenderer::releaseStreams() {
    for (auto& [id, g] : gpu_) {
        safeRelease(g.vb);
        safeRelease(g.ib);
    }
    gpu_.clear();
}

SceneRenderer::StreamGpu& SceneRenderer::syncVertices(GeoStreamBase& s, const void* data, size_t count, size_t stride) {
    StreamGpu& g = gpu_[s.id];
    if (g.lastSeen != frame_) ++streamsSeen_;
    g.lastSeen = frame_;
    const size_t bytes = count * stride;
    if (!bytes) {
        // Nothing to hold: the buffer is given back (a view mode without flat triangles, say), and whatever comes
        // next is sent whole.
        safeRelease(g.vb);
        g.capBytes = 0;
        s.dirty.clear();
        s.all = true;
        return g;
    }
    bool all = s.all;
    // A buffer far too big for what it holds now (everything was made again much smaller) is replaced.
    const bool tooBig = g.vb && g.capBytes > (size_t(16) << 20) && bytes < g.capBytes / 4;
    if (!g.vb || bytes > g.capBytes || tooBig) {
        safeRelease(g.vb);
        g.capBytes = 0;
        const size_t cap = bytes + bytes / 4 + 16384;
        if (cap > 0xfff00000ull) return g;  // past what one buffer can hold: not drawn
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(cap);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &g.vb))) {
            g.vb = nullptr;
            return g;
        }
        g.capBytes = cap;
        all = true;
    }
    const auto put = [&](size_t lo, size_t hi) {
        hi = std::min(hi, bytes);
        if (hi <= lo) return;
        const D3D11_BOX box{UINT(lo), 0, 0, UINT(hi), 1, 1};
        ctx_->UpdateSubresource(g.vb, 0, &box, static_cast<const uint8_t*>(data) + lo, 0, 0);
        stats_.uploadBytes += hi - lo;
    };
    if (all) {
        put(0, bytes);
    } else {
        for (size_t k = 0; k < s.dirty.size(); ++k) put(s.dirty[k].lo * stride, s.dirty[k].hi * stride);
    }
    s.taken();
    return g;
}

void SceneRenderer::buildChunks(const SceneBatch& b, StreamGpu& g) {
    // 3.20: triangles in spatial order, cut into chunks with boxes. The vertices stay where they are (items are
    // written in place); an index buffer holds the spatial order.
    const size_t tris = b.v.size() / 3;
    corners_.resize(tris * 3);
    for (size_t i = 0; i < tris * 3; ++i) corners_[i] = {b.v[i].x, b.v[i].y, b.v[i].z};
    iw8r::buildDrawChunks(corners_, kChunkTriangles, chunkOrder_, g.chunks);
    g.posOf.resize(tris);
    indices_.resize(tris * 3);
    for (size_t p = 0; p < chunkOrder_.size(); ++p) {
        const uint32_t t = chunkOrder_[p];
        g.posOf[t] = uint32_t(p);
        indices_[3 * p] = t * 3, indices_[3 * p + 1] = t * 3 + 1, indices_[3 * p + 2] = t * 3 + 2;
    }
    const size_t bytes = indices_.size() * sizeof(uint32_t);
    g.chunked = false;
    if (!g.ib || bytes > g.ibCapBytes) {
        safeRelease(g.ib);
        g.ibCapBytes = 0;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(bytes + bytes / 4 + 4096);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        if (bytes == 0 || FAILED(dev_->CreateBuffer(&bd, nullptr, &g.ib))) {
            g.ib = nullptr;
            g.chunks.clear();
            return;
        }
        g.ibCapBytes = bd.ByteWidth;
    }
    const D3D11_BOX box{0, 0, 0, UINT(bytes), 1, 1};
    ctx_->UpdateSubresource(g.ib, 0, &box, indices_.data(), 0, 0);
    stats_.uploadBytes += bytes;
    g.chunked = true;
    g.loose = 0;
    // The work vectors of a huge batch are given back.
    if (corners_.capacity() > (size_t(1) << 20)) std::vector<iw8r::Vec3>().swap(corners_);
    if (indices_.capacity() > (size_t(1) << 22)) std::vector<uint32_t>().swap(indices_);
    if (chunkOrder_.capacity() > (size_t(1) << 22)) std::vector<uint32_t>().swap(chunkOrder_);
}

SceneRenderer::StreamGpu* SceneRenderer::syncBatch(SceneBatch& b) {
    const bool everything = b.all || b.structural;
    const bool moved = b.moved;
    const DirtyRanges ranges = b.dirty;
    StreamGpu& g = syncVertices(b, b.v.data(), b.v.size(), sizeof(GpuTexVertex));
    b.structural = false;
    b.moved = false;
    if (!g.vb) return nullptr;
    const size_t tris = b.v.size() / 3;
    const bool fresh = everything || !g.synced;
    const auto corner = [&](size_t i) { return iw8r::Vec3{b.v[i].x, b.v[i].y, b.v[i].z}; };
    if (tris > kChunkMinTriangles) {
        // The chunks depend on the positions only. When everything was made again but no vertex moved (another
        // view mode, new colours), the chunks and their index buffer are still right: a hash of the positions tells.
        bool rebuild = !g.chunked || g.loose > tris / 4;
        if (fresh) {
            uint64_t h = 1469598103934665603ull ^ b.v.size();
            for (const GpuTexVertex& v : b.v) {
                uint32_t p[3];
                std::memcpy(p, &v.x, sizeof(p));
                h = (h ^ (uint64_t(p[0]) | (uint64_t(p[1]) << 32))) * 1099511628211ull;
                h = (h ^ p[2]) * 1099511628211ull;
            }
            rebuild = rebuild || !g.posHashValid || h != g.posHash || g.posOf.size() != tris;
            g.posHash = h;
            g.posHashValid = true;
        } else if (moved) {
            g.posHashValid = false;  // vertices moved in place: the chunks are no longer those of a fresh build
        }
        if (rebuild) {
            IW8R_PROF("d.chunks");
            buildChunks(b, g);
        } else if (fresh) {
            // same positions: nothing to do
        } else {
            // Vertices written in place: the boxes of their chunks grow to hold them. Many grown boxes cull badly,
            // so past a quarter of the batch the chunks are made again.
            for (size_t r = 0; r < ranges.size(); ++r)
                for (size_t t = ranges[r].lo / 3; t < std::min((ranges[r].hi + 2) / 3, tris); ++t) {
                    iw8r::DrawChunk& c = g.chunks[g.posOf[t] / kChunkTriangles];
                    const iw8r::AABB before = c.bounds;
                    for (size_t k = 0; k < 3; ++k) c.bounds.add(corner(t * 3 + k));
                    if (!(before.mins == c.bounds.mins) || !(before.maxs == c.bounds.maxs)) ++g.loose;
                }
        }
    } else {
        g.chunked = false;  // a small batch draws whole
    }
    g.synced = true;
    if (b.blend && (fresh || !ranges.empty())) {
        // Blended batches draw farthest first, by the middle of their vertices.
        double cx = 0, cy = 0, cz = 0;
        for (const auto& v : b.v) cx += v.x, cy += v.y, cz += v.z;
        const double n = double(std::max<size_t>(1, b.v.size()));
        g.centroid[0] = cx / n, g.centroid[1] = cy / n, g.centroid[2] = cz / n;
    }
    return &g;
}

void SceneRenderer::drawFrame(const FrameInput& in) {
    if (!rtv_ || !in.viewProj || !in.clear) return;
    IW8R_PROF("d.total");
    ++frame_;
    streamsSeen_ = 0;
    stats_ = {};
    inShadowPass_ = false;
    const float* viewProj = in.viewProj;
    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    ctx_->ClearRenderTargetView(rtv_, in.clear);
    ctx_->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    D3D11_VIEWPORT vp{0, 0, float(w_), float(h_), 0, 1};
    ctx_->RSSetViewports(1, &vp);
    D3D11_MAPPED_SUBRESOURCE m;
    const bool shadowOn = shadowEnabled_ && createShadowResources();
    auto writeCbWith = [&](const float* vpm, bool fog, bool lit) {
        float cb[56];
        std::memcpy(cb, vpm, 64);
        std::memcpy(cb + 16, fog_, sizeof(float) * 8);
        if (!fog) cb[23] = 0;
        const bool litOn = lit && litDecode_ > 1.5f;
        cb[24] = litOn ? litDecode_ : 1.0f;
        cb[25] = litOn && shadowOn ? 1.0f : 0.0f;
        cb[26] = shadowBias_;
        cb[27] = litOn ? exposure_ : 1.0f;
        std::memcpy(cb + 28, sunViewProj_, 64);
        std::memcpy(cb + 44, grade_, sizeof(grade_));
        cb[47] = lit ? float(gradeFlags_) : 0.0f;  // lines (lit = false) keep their colours
        if (SUCCEEDED(ctx_->Map(cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            std::memcpy(m.pData, cb, sizeof(cb));
            ctx_->Unmap(cb_, 0);
        }
    };
    auto writeCb = [&](bool fog) { writeCbWith(viewProj, fog, true); };
    {
        // Colour grade LUT (3.15): t4 / s2 for every pass (a null view samples black, but the LUT flag is off then).
        ID3D11ShaderResourceView* lut = (gradeFlags_ & 4) ? lutSrv_ : nullptr;
        ctx_->PSSetShaderResources(4, 1, &lut);
        ctx_->PSSetShaderResources(5, 1, &probeSrv_);
        if (lutSampler_) ctx_->PSSetSamplers(2, 1, &lutSampler_);
    }
    // Bring the GPU copies up to date first: the sun shadow pass draws the same buffers.
    struct Flat {
        GeoStream<GpuVertex>* s = nullptr;
        StreamGpu* g = nullptr;
        bool ready() const { return s && g && g->vb && !s->v.empty(); }
    };
    const auto syncFlat = [&](GeoStream<GpuVertex>* s) {
        Flat f;
        f.s = s;
        if (s) f.g = &syncVertices(*s, s->v.data(), s->v.size(), sizeof(GpuVertex));
        return f;
    };
    Flat fShadowOnly, fCasters, fFaceSel, fNoCast, fLines, fLinesMisc, fOverlay[4];
    {
        IW8R_PROF("d.sync");
        fShadowOnly = syncFlat(in.shadowOnly);
        fCasters = syncFlat(in.casters);
        fFaceSel = syncFlat(in.faceSel);
        if (!in.clean) {
            fNoCast = syncFlat(in.noCast);
            fLines = syncFlat(in.lines);
            fLinesMisc = syncFlat(in.linesMisc);
            for (int k = 0; k < 4; ++k) fOverlay[k] = syncFlat(in.overlay[k]);
        }
        drawList_.clear();
        if (vsTex_ && psTex_ && layoutTex_)
            for (const std::vector<SceneBatch*>* list : {in.batches, in.extraBatches}) {
                if (!list) continue;
                for (SceneBatch* b : *list) {
                    if (!b || b->v.empty()) continue;
                    if (StreamGpu* g = syncBatch(*b)) drawList_.push_back({b, g});
                }
            }
    }
    const UINT stride = sizeof(GpuVertex), offset = 0, tstride = sizeof(GpuTexVertex);
    static const bool shadowAlways = perfOff("shadow");  // benchmark: as before, on every change of any geometry
    if (shadowAlways && (in.geometryChanged || in.casterVersion != shadowCasterVersion_)) shadowDirty_ = true;
    if (shadowOn && (shadowDirty_ || in.casterVersion != shadowCasterVersion_)) {
        // Sun shadow map (lit preview): depth of everything from the sun, drawn again only when a caster or the sun
        // changed (the camera moving and the selection do not need it).
        IW8R_PROF("d.shadow");
        ID3D11RenderTargetView* noRtv = nullptr;
        ctx_->OMSetRenderTargets(1, &noRtv, shadowDsv_);
        inShadowPass_ = true;
        ctx_->ClearDepthStencilView(shadowDsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);
        D3D11_VIEWPORT svp{0, 0, float(kShadowSize), float(kShadowSize), 0, 1};
        ctx_->RSSetViewports(1, &svp);
        writeCbWith(sunViewProj_, false, false);
        ctx_->VSSetConstantBuffers(0, 1, &cb_);
        ctx_->PSSetShader(nullptr, nullptr, 0);
        ctx_->RSSetState(rsShadow_ ? rsShadow_ : rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetInputLayout(layout_);
        ctx_->VSSetShader(vs_, nullptr, 0);
        for (const Flat* f : {&fShadowOnly, &fCasters, &fFaceSel}) {  // markers and tool faces cast nothing
            if (!f->ready()) continue;
            ctx_->IASetVertexBuffers(0, 1, &f->g->vb, &stride, &offset);
            drawN(UINT(f->s->v.size()), 0);
        }
        if (!drawList_.empty()) {
            ctx_->IASetInputLayout(layoutTex_);
            ctx_->VSSetShader(vsTex_, nullptr, 0);
            if (cbBatch_) {
                const float flags[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
                ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
            }
            // Opaque and alpha-tested batches only (glass and effects cast no shadow); the clip shader keeps the
            // holes of cut-out textures (no texture bound = no pixel shader, solid).
            if (psShadowClip_ && sampler_) {
                ctx_->PSSetShader(psShadowClip_, nullptr, 0);
                ctx_->PSSetSamplers(0, 1, &sampler_);
            }
            for (const BatchDraw& d : drawList_) {
                const SceneBatch& b = *d.b;
                if (b.blend != 0 || b.v.front().rgba >> 24 < 0x80) continue;
                if (psShadowClip_ && b.texture >= 0 && size_t(b.texture) < textures_.size())
                    ctx_->PSSetShaderResources(0, 1, &textures_[size_t(b.texture)]);
                ctx_->IASetVertexBuffers(0, 1, &d.g->vb, &tstride, &offset);
                drawN(UINT(b.v.size()), 0);
            }
            ctx_->PSSetShader(nullptr, nullptr, 0);
        }
        if (vbRef_ && refCount_ && refVisible_) {
            ctx_->IASetInputLayout(layout_);
            ctx_->VSSetShader(vs_, nullptr, 0);
            ctx_->IASetVertexBuffers(0, 1, &vbRef_, &stride, &offset);
            drawN(UINT(refCount_), 0);
        }
        if (refVisible_) drawReferenceInstances(ReferencePass::Shadow);
        shadowDirty_ = false;
        shadowCasterVersion_ = in.casterVersion;
        inShadowPass_ = false;
        ++stats_.shadowPasses;
        ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
        ctx_->RSSetViewports(1, &vp);
    }
    writeCb(true);
    if (shadowOn) {
        ctx_->PSSetShaderResources(2, 1, &shadowSrv_);
        ctx_->PSSetSamplers(1, 1, &cmpSampler_);
    }
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &cb_);
    ctx_->PSSetConstantBuffers(0, 1, &cb_);  // fog
    ctx_->PSSetShader(ps_, nullptr, 0);
    ctx_->GSSetShader(nullptr, nullptr, 0);
    const float bf[4] = {0, 0, 0, 0};
    ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
    const iw8r::Frustum frustum = iw8r::frustumFromViewProj(viewProj);
    texCulledLast_ = 0;
    if (!drawList_.empty()) {
        // One vertex buffer per batch; one draw per texture, or one per run of visible chunks.
        IW8R_PROF("d.tex");
        ctx_->IASetInputLayout(layoutTex_);
        ctx_->VSSetShader(vsTex_, nullptr, 0);
        ctx_->PSSetShader(psTex_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (cbBatch_) ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
        if (cbBatch_) ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
        if (cbWind_) {
            ctx_->UpdateSubresource(cbWind_, 0, nullptr, wind_, 0, 0);
            ctx_->VSSetConstantBuffers(3, 1, &cbWind_);
        }
        if (cbGame_) {
            const bool on = game_.on && gameLutSrv_ != nullptr;
            const float g[16] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, 0,
                                 game_.sunToward[0], game_.sunToward[1], game_.sunToward[2], 0,
                                 game_.sunColor[0], game_.sunColor[1], game_.sunColor[2], 0,
                                 game_.sky[0], game_.sky[1], game_.sky[2], 0};
            ctx_->UpdateSubresource(cbGame_, 0, nullptr, g, 0, 0);
            ctx_->PSSetConstantBuffers(4, 1, &cbGame_);
            ID3D11ShaderResourceView* gs[4] = {gameLmSrv_[0], gameLmSrv_[1], gameLmSrv_[2], gameLutSrv_};
            ctx_->PSSetShaderResources(6, 4, gs);
            if (lutSampler_) ctx_->PSSetSamplers(2, 1, &lutSampler_);
        }
        if (cbLights_) {
            struct {
                float count[4];
                PixelLight l[kMaxPixelLights];
            } data{};
            const size_t n = std::min<size_t>(pixelLights_.size(), size_t(kMaxPixelLights));
            data.count[0] = float(n);
            data.count[1] = lightGain_;
            for (size_t k = 0; k < n; ++k) data.l[k] = pixelLights_[k];
            ctx_->UpdateSubresource(cbLights_, 0, nullptr, &data, 0, 0);
            ctx_->PSSetConstantBuffers(2, 1, &cbLights_);
        }
        // Opaque batches first, then blended ones farthest first (a stable sort, as before, without a heap buffer).
        drawOrder_.clear();
        const size_t count = drawList_.size();
        for (size_t k = 0; k < count; ++k)
            if (!drawList_[k].b->blend) drawOrder_.push_back(uint32_t(k));
        const size_t firstBlended = drawOrder_.size();
        for (size_t k = 0; k < count; ++k) {
            BatchDraw& d = drawList_[k];
            if (!d.b->blend) continue;
            // The batch centre's clip-space w is its distance along the view direction.
            d.depth = float(d.g->centroid[0] * viewProj[3] + d.g->centroid[1] * viewProj[7] + d.g->centroid[2] * viewProj[11] + viewProj[15]);
            size_t at = drawOrder_.size();
            drawOrder_.push_back(uint32_t(k));
            while (at > firstBlended && drawList_[drawOrder_[at - 1]].depth < d.depth) {
                drawOrder_[at] = drawOrder_[at - 1];
                --at;
            }
            drawOrder_[at] = uint32_t(k);
        }
        for (uint32_t k : drawOrder_) {
            const SceneBatch& b = *drawList_[k].b;
            StreamGpu& g = *drawList_[k].g;
            if (b.texture < 0 || size_t(b.texture) >= textures_.size()) continue;
            const bool chunked = refCulling_ && g.chunked && !g.chunks.empty();
            ++stats_.batches;
            if (b.blend) {
                ctx_->OMSetDepthStencilState(dsTest_, 0);
                ctx_->OMSetBlendState(b.blend == 2 && blendAdd_ ? blendAdd_ : blend_, bf, 0xffffffff);
            }
            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(b.texture)]);
            const bool hasNormal = b.normalTexture >= 0 && size_t(b.normalTexture) < textures_.size();
            ID3D11ShaderResourceView* nsrv = hasNormal ? textures_[size_t(b.normalTexture)] : nullptr;
            ctx_->PSSetShaderResources(1, 1, &nsrv);
            const bool hasEmissive = b.emissiveTexture >= 0 && size_t(b.emissiveTexture) < textures_.size();
            ID3D11ShaderResourceView* esrv = hasEmissive ? textures_[size_t(b.emissiveTexture)] : nullptr;
            ctx_->PSSetShaderResources(3, 1, &esrv);
            if (cbBatch_) {
                const float flags[8] = {hasNormal ? 1.0f : 0.0f, float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ,
                                        hasEmissive ? 1.0f : 0.0f, probeSrv_ ? 1.0f : 0.0f, 0, 0};
                ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            }
            ctx_->IASetVertexBuffers(0, 1, &g.vb, &tstride, &offset);
            if (chunked) {
                ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT_R32_UINT, 0);
                texCulledLast_ += forVisibleRuns(frustum, g.chunks, [&](uint32_t first, uint32_t n) { drawIdx(n * 3, first * 3); });
            } else {
                drawN(UINT(b.v.size()), 0);
            }
        }
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
        ID3D11ShaderResourceView* noSrv[2] = {nullptr, nullptr};
        ctx_->PSSetShaderResources(0, 2, noSrv);
        ctx_->IASetInputLayout(layout_);
        ctx_->VSSetShader(vs_, nullptr, 0);
        ctx_->PSSetShader(ps_, nullptr, 0);
    }
    if (vbRefTex_ && refVisible_ && vsTex_ && psTex_ && layoutTex_) {  // textured stock reference
        IW8R_PROF("d.ref");
        ctx_->IASetInputLayout(layoutTex_);
        ctx_->VSSetShader(vsTex_, nullptr, 0);
        ctx_->PSSetShader(psTex_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
        if (cbBatch_) {
            const float flags[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
            ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
        }
        ID3D11ShaderResourceView* noNrm = nullptr;
        ctx_->PSSetShaderResources(1, 1, &noNrm);
        refCulledLast_ = 0;
        ID3D11ShaderResourceView* bound = nullptr;
        for (size_t k = 0; k < refTexIds_.size(); ++k) {
            const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            const auto bind = [&] {
                // Batches that share a texture (the plain one while images load) do not bind it again.
                if (bound != textures_[size_t(id)]) ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]), bound = textures_[size_t(id)];
            };
            if (refCulling_ && k < refTexChunks_.size() && !refTexChunks_[k].empty()) {
                bool any = false;
                refCulledLast_ += forVisibleRuns(frustum, refTexChunks_[k], [&](uint32_t first, uint32_t n) {
                    if (!any) bind(), any = true;
                    drawN(n * 3, refTexFirst_[k] + first * 3);
                });
                continue;
            }
            bind();
            drawN(refTexCount_[k], refTexFirst_[k]);
        }
        ID3D11ShaderResourceView* none2[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, none2);
        ctx_->IASetInputLayout(layout_);
        ctx_->VSSetShader(vs_, nullptr, 0);
        ctx_->PSSetShader(ps_, nullptr, 0);
    } else if (vbRef_ && refCount_ && refVisible_) {  // stock world reference
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetVertexBuffers(0, 1, &vbRef_, &stride, &offset);
        if (refCulling_ && !refChunks_.empty()) {
            refCulledLast_ = forVisibleRuns(frustum, refChunks_, [&](uint32_t first, uint32_t n) { drawN(n * 3, first * 3); });
        } else {
            drawN(UINT(refCount_), 0);
        }
    }
    if (refVisible_ && !refInst_.empty()) {  // instanced reference props (3.20)
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        drawReferenceInstances(in.capture || in.clean ? ReferencePass::Capture : ReferencePass::Screen);
    }
    if (refVisible_ && !refTexInst_.empty()) {  // the same props with their textures
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        drawReferenceTexInstances();
    }
    const auto drawFlat = [&](const Flat& f, bool lines) {
        if (!f.ready()) return;
        ctx_->IASetVertexBuffers(0, 1, &f.g->vb, &stride, &offset);
        drawN(UINT(f.s->v.size()), 0, lines);
    };
    if (fCasters.ready() || fFaceSel.ready() || fNoCast.ready()) {
        IW8R_PROF("d.flat");
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        drawFlat(fCasters, false);
        drawFlat(fFaceSel, false);
        drawFlat(fNoCast, false);
    }
    if (litDecode_ > 1.5f) writeCbWith(viewProj, true, false);  // lines keep their plain colours
    ID3D11ShaderResourceView* noShadowSrv = nullptr;
    if (shadowOn) ctx_->PSSetShaderResources(2, 1, &noShadowSrv);
    if (fLines.ready() || fLinesMisc.ready()) {
        IW8R_PROF("d.lines");
        ctx_->RSSetState(rsLines_);
        ctx_->OMSetDepthStencilState(dsTest_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        drawFlat(fLines, true);
        drawFlat(fLinesMisc, true);
    }
    if (fOverlay[0].ready() || fOverlay[1].ready() || fOverlay[2].ready() || fOverlay[3].ready()) {
        if (fog_[7] > 0) writeCbWith(viewProj, false, false);  // selection / gizmo lines stay unfogged
        ctx_->RSSetState(rsLines_);
        ctx_->OMSetDepthStencilState(dsOff_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        for (const Flat& f : fOverlay) drawFlat(f, true);
    }
    ID3D11RenderTargetView* none = nullptr;
    ctx_->OMSetRenderTargets(1, &none, nullptr);
    // Buffers of streams this frame did not use are released right away: a batch that is gone (its last face was
    // deleted, or another view mode made other batches) must not keep its memory. A stream that comes back is sent
    // whole again.
    if (gpu_.size() > streamsSeen_)
        for (auto it = gpu_.begin(); it != gpu_.end();) {
            if (it->second.lastSeen != frame_) {
                safeRelease(it->second.vb);
                safeRelease(it->second.ib);
                it = gpu_.erase(it);
            } else {
                ++it;
            }
        }
}

bool SceneRenderer::createShadowResources() {
    if (shadowSrv_ && shadowDsv_ && cmpSampler_) return true;
    if (!dev_ || shadowFailed_) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = kShadowSize;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    D3D11_SAMPLER_DESC cs{};
    cs.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    cs.AddressU = cs.AddressV = cs.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    cs.BorderColor[0] = cs.BorderColor[1] = cs.BorderColor[2] = cs.BorderColor[3] = 1.0f;
    cs.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    cs.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.DepthBias = 1000;
    rd.SlopeScaledDepthBias = 2.0f;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &shadowTex_)) || FAILED(dev_->CreateDepthStencilView(shadowTex_, &dd, &shadowDsv_)) ||
        FAILED(dev_->CreateShaderResourceView(shadowTex_, &sd, &shadowSrv_)) || FAILED(dev_->CreateSamplerState(&cs, &cmpSampler_))) {
        shadowFailed_ = true;
        return false;
    }
    dev_->CreateRasterizerState(&rd, &rsShadow_);
    shadowDirty_ = true;
    return true;
}

void SceneRenderer::setSunShadow(bool enabled, const float sunViewProj[16], float bias) {
    if (enabled && std::memcmp(sunViewProj, sunViewProj_, sizeof(sunViewProj_)) != 0) shadowDirty_ = true;
    if (enabled && !shadowEnabled_) shadowDirty_ = true;
    shadowEnabled_ = enabled;
    std::memcpy(sunViewProj_, sunViewProj, sizeof(sunViewProj_));
    shadowBias_ = bias;
}

void sunShadowMatrix(const double toward[3], const double mins[3], const double maxs[3], float out[16]) {
    // Orthographic view from the sun over the box: x / y across, depth along the light, D3D z in [0, 1] (row vectors).
    const double c[3] = {(mins[0] + maxs[0]) / 2, (mins[1] + maxs[1]) / 2, (mins[2] + maxs[2]) / 2};
    const double ex[3] = {(maxs[0] - mins[0]) / 2, (maxs[1] - mins[1]) / 2, (maxs[2] - mins[2]) / 2};
    const double R = std::max(1.0, std::sqrt(ex[0] * ex[0] + ex[1] * ex[1] + ex[2] * ex[2]));
    double tl = std::sqrt(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
    if (tl < 1e-9) tl = 1;
    const double f[3] = {-toward[0] / tl, -toward[1] / tl, -toward[2] / tl};  // from the sun into the scene
    const double up0[3] = {std::fabs(f[2]) > 0.95 ? 1.0 : 0.0, 0.0, std::fabs(f[2]) > 0.95 ? 0.0 : 1.0};
    double r[3] = {f[1] * up0[2] - f[2] * up0[1], f[2] * up0[0] - f[0] * up0[2], f[0] * up0[1] - f[1] * up0[0]};
    const double rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (double& v : r) v /= rl;
    const double u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    const double eye[3] = {c[0] - f[0] * 2 * R, c[1] - f[1] * 2 * R, c[2] - f[2] * 2 * R};
    auto dot3 = [](const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const double depth = 4 * R;
    for (int i = 0; i < 16; ++i) out[i] = 0;
    for (int k = 0; k < 3; ++k) {
        out[k * 4 + 0] = float(r[k] / R);
        out[k * 4 + 1] = float(u[k] / R);
        out[k * 4 + 2] = float(f[k] / depth);
    }
    out[12] = float(-dot3(eye, r) / R);
    out[13] = float(-dot3(eye, u) / R);
    out[14] = float(-dot3(eye, f) / depth);
    out[15] = 1;
}

size_t SceneRenderer::countNonClearPixels(const float clear[4]) {
    if (!color_) return 0;
    D3D11_TEXTURE2D_DESC td{};
    color_->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &staging))) return 0;
    ctx_->CopyResource(staging, color_);
    D3D11_MAPPED_SUBRESOURCE m;
    size_t count = 0;
    if (SUCCEEDED(ctx_->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
        const uint8_t c[3] = {uint8_t(clear[0] * 255 + 0.5f), uint8_t(clear[1] * 255 + 0.5f), uint8_t(clear[2] * 255 + 0.5f)};
        for (UINT y = 0; y < td.Height; ++y) {
            const uint8_t* row = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch;
            for (UINT x = 0; x < td.Width; ++x) {
                const uint8_t* p = row + x * 4;
                if (std::abs(p[0] - c[0]) > 2 || std::abs(p[1] - c[1]) > 2 || std::abs(p[2] - c[2]) > 2) ++count;
            }
        }
        ctx_->Unmap(staging, 0);
    }
    staging->Release();
    return count;
}

bool SceneRenderer::readback(std::vector<uint8_t>& rgba, int& w, int& h) {
    if (!color_) return false;
    D3D11_TEXTURE2D_DESC td{};
    color_->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &staging))) return false;
    ctx_->CopyResource(staging, color_);
    D3D11_MAPPED_SUBRESOURCE m;
    bool ok = false;
    if (SUCCEEDED(ctx_->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
        w = int(td.Width);
        h = int(td.Height);
        rgba.resize(size_t(w) * size_t(h) * 4);
        for (int y = 0; y < h; ++y)
            std::memcpy(rgba.data() + size_t(y) * size_t(w) * 4, static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch, size_t(w) * 4);
        ctx_->Unmap(staging, 0);
        ok = true;
    }
    staging->Release();
    return ok;
}

}  // namespace iw8r::app