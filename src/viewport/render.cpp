#include "render.h"
#include "stock_probes.h"

#include <algorithm>
#include <d3d11_1.h>
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
cbuffer CB : register(b0) { row_major float4x4 viewProj; float4 eyeStart; float4 fogColHalf; float4 litParams; row_major float4x4 sunViewProj; float4 grade0; float4 gradeLight; float4 gradeDark; row_major float4x4 sunViewProjNear; float4 sunNear; };
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
// Two cascades (slice 1: the box around the camera, sunViewProjNear / sunNear = bias, on; slice 0: the whole scene),
// as the game's cascaded sun shadows: small props and thin poles keep their shadows near the camera.
Texture2DArray<float> shadowMap : register(t2);
SamplerComparisonState shadowSamp : register(s1);
float shadowPcf(float2 uv, float z, float slice) {
    float s = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            s += shadowMap.SampleCmpLevelZero(shadowSamp, float3(uv + float2(x, y) / 2048.0, slice), z);
    return s / 9.0;
}
float sunShadow(float3 w) {
    if (litParams.y < 0.5) return 1.0;
    if (sunNear.y > 0.5) {
        float4 ln = mul(float4(w, 1.0), sunViewProjNear);
        float2 un = float2(ln.x * 0.5 + 0.5, 0.5 - ln.y * 0.5);
        if (un.x > 0.01 && un.y > 0.01 && un.x < 0.99 && un.y < 0.99 && ln.z < 1) return shadowPcf(un, ln.z - sunNear.x, 1.0);
    }
    float4 lp = mul(float4(w, 1.0), sunViewProj);
    float2 uv = float2(lp.x * 0.5 + 0.5, 0.5 - lp.y * 0.5);
    if (uv.x <= 0 || uv.y <= 0 || uv.x >= 1 || uv.y >= 1 || lp.z >= 1) return 1.0;
    return shadowPcf(uv, lp.z - litParams.z, 0.0);
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
cbuffer CB : register(b0) { row_major float4x4 viewProj; float4 eyeStart; float4 fogColHalf; float4 litParams; row_major float4x4 sunViewProj; float4 grade0; float4 gradeLight; float4 gradeDark; row_major float4x4 sunViewProjNear; float4 sunNear; };
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
// Two cascades (slice 1: the box around the camera, sunViewProjNear / sunNear = bias, on; slice 0: the whole scene),
// as the game's cascaded sun shadows: small props and thin poles keep their shadows near the camera.
Texture2DArray<float> shadowMap : register(t2);
SamplerComparisonState shadowSamp : register(s1);
float shadowPcf(float2 uv, float z, float slice) {
    float s = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            s += shadowMap.SampleCmpLevelZero(shadowSamp, float3(uv + float2(x, y) / 2048.0, slice), z);
    return s / 9.0;
}
float sunShadow(float3 w) {
    if (litParams.y < 0.5) return 1.0;
    if (sunNear.y > 0.5) {
        float4 ln = mul(float4(w, 1.0), sunViewProjNear);
        float2 un = float2(ln.x * 0.5 + 0.5, 0.5 - ln.y * 0.5);
        if (un.x > 0.01 && un.y > 0.01 && un.x < 0.99 && un.y < 0.99 && ln.z < 1) return shadowPcf(un, ln.z - sunNear.x, 1.0);
    }
    float4 lp = mul(float4(w, 1.0), sunViewProj);
    float2 uv = float2(lp.x * 0.5 + 0.5, 0.5 - lp.y * 0.5);
    if (uv.x <= 0 || uv.y <= 0 || uv.x >= 1 || uv.y >= 1 || lp.z >= 1) return 1.0;
    return shadowPcf(uv, lp.z - litParams.z, 0.0);
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
cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; float4 gameFog[6]; float4 gameSkyScale; };
// The stock map's atmosphere (GameLook::fog): distance fog with a height falloff, Rayleigh / Mie inscattering, extinction
// desaturation. `sky`: the sky takes the fog of the sky distance times the sky intensity.
float3 gameFogApply(float3 c, float3 w, bool sky) {
    if (gameFog[0].w < 0.5) return c;
    float3 v = w - eyeStart.xyz;
    float d = length(v);
    float3 dir = v / max(d, 1e-3);
    if (sky) d = gameFog[4].z;
    d = min(d, gameFog[5].x);
    float dd = max(d - gameFog[0].x, 0.0);
    float a = (eyeStart.z - gameFog[4].x) / gameFog[4].y;
    float b = a + dir.z * d / gameFog[4].y;
    float hf = abs(b - a) > 1e-3 ? (exp2(-a) - exp2(-b)) / ((b - a) * 0.693147) : exp2(-a);
    float amount = min(1.0 - exp2(-dd / gameFog[0].y * max(hf, 0.0)), gameFog[0].z);
    float g = gameFog[2].w, mu = dot(dir, gameSun.xyz);
    float hg = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * mu, 1e-4), 1.5);
    float3 inscatter = gameFog[1].rgb * lerp(gameFog[2].rgb, gameFog[3].rgb * hg, gameFog[1].w);
    if (sky) {
        // The sky's haze: brighter or dimmer by fogSkyIntensity, over the band of zenith angles fogSkyMin/MaxAngle.
        float zenith = degrees(acos(clamp(dir.z, -1.0, 1.0)));
        amount *= saturate((zenith - gameFog[5].y) / max(gameFog[5].z - gameFog[5].y, 1e-3));
        inscatter *= gameFog[4].w;
    }
    amount = saturate(amount);
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(c, l.xxx, gameFog[3].w * amount);
    return lerp(c, inscatter, amount);
}
Texture2D gameLmA : register(t6);
// The stock reference's lightmap rectangles in its atlas (setStockLightmap): x, y, width, height per lightmap index,
// and the atlas size in .x of lmInfo. A reference vertex carries index + u in its lightmap u.
cbuffer LightmapRects : register(b6) { float4 lmInfo; float4 lmRects[1024]; };
Texture2D gameLmB : register(t7);
Texture2D gameLmD : register(t8);
Texture3D gameLut : register(t9);
// The stock world shader's ggx_env_brdf_lut (A, B by N.V across and gloss down; research 95), when the editor has it.
Texture2D envBrdf : register(t10);
// The stock reference's reflection probes (setStockProbes): octahedral, world space, 4-texel gutter at every level.
Texture2DArray stockProbes : register(t11);
struct ProbeBox { float4 centerSlice; float4 axisX; float4 axisY; float4 axisZ; float4 originFeather; };
cbuffer Probes : register(b5) { float4 probeCount; float4 probeGridMin; float4 probeGridCell; ProbeBox probeBoxes[128]; };
// A grid over the boxes (setStockProbes): up to four candidate boxes per cell in priority order (255 = none), or the
// nearest box where none covers the cell. probeCount.yzw = grid size, probeGridCell.w = 1 when the grid is bound.
Texture3D<uint4> probeGrid : register(t13);
float3 probeAt(float slice, float3 dir, float mip) {
    float3 a = abs(dir);
    float2 o = dir.xy / max(a.x + a.y + a.z, 1e-6);
    if (dir.z < 0) o = (1.0 - abs(o.yx)) * float2(o.x >= 0 ? 1 : -1, o.y >= 0 ? 1 : -1);
    float g = float(4u << uint(mip));
    float2 uv = (o * 0.5 + 0.5) * (1.0 - g / 128.0) + g / 256.0;
    return stockProbes.SampleLevel(lutSamp, float3(uv, slice), mip).rgb;
}
// The stock shader's probe term (research 95): the boxes front to back by priority, each with its box-parallax
// direction and a feathered fade, until the weights reach 1; two levels around 5 - 5 g'.
float3 stockProbeLight(float3 p, float3 R, float gp) {
    float mip = clamp(5.0 - 5.0 * gp, 0.0, 5.0);
    float m0 = floor(mip), mf = mip - m0, m1 = min(m0 + 1.0, 5.0);
    float3 sum = 0;
    float wsum = 0;
    int n = int(probeCount.x);
    // The box nearest to p (largest inside margin, least outside): far terrain outside every box takes its probe for
    // the weight the boxes around p leave (owner 2026-11-12: Hideout's far ridge was black without one).
    float nearRem = -1e30;
    float nearSlice = 0;
    // With the grid: only the cell's candidates (Hideout's 48 boxes per pixel per layer pass cost 40 ms a frame).
    const bool grid = probeGridCell.w > 0.5;
    uint4 cand = uint4(0, 0, 0, 0);
    if (grid) {
        int3 dims = int3(probeCount.yzw);
        int3 cell = clamp(int3(floor((p - probeGridMin.xyz) / probeGridCell.xyz)), int3(0, 0, 0), dims - 1);
        cand = probeGrid.Load(int4(cell, 0));
        n = 4;
    }
    for (int j = 0; j < n && wsum < 0.999; ++j) {
        int k = j;
        if (grid) {
            k = int(j == 0 ? cand.x : j == 1 ? cand.y : j == 2 ? cand.z : cand.w);
            if (k >= 255) break;
        }
        ProbeBox b = probeBoxes[k];
        float3 d = p - b.centerSlice.xyz;
        float3 lp = float3(dot(d, b.axisX.xyz), dot(d, b.axisY.xyz), dot(d, b.axisZ.xyz));
        float3 h = float3(b.axisX.w, b.axisY.w, b.axisZ.w);
        float3 rem = h - abs(lp);
        float mrem = min(rem.x, min(rem.y, rem.z));
        if (mrem > nearRem) nearRem = mrem, nearSlice = b.centerSlice.w;
        if (rem.x < 0 || rem.y < 0 || rem.z < 0) continue;
        float fade = saturate(min(rem.x, min(rem.y, rem.z)) / max(b.originFeather.w, 1.0));
        // Box parallax: where the reflection ray leaves the box, seen from the probe's capture origin.
        float3 lr = float3(dot(R, b.axisX.xyz), dot(R, b.axisY.xyz), dot(R, b.axisZ.xyz));
        float3 t1 = (h - lp) / (abs(lr) > 1e-6 ? lr : 1e-6), t2 = (-h - lp) / (abs(lr) > 1e-6 ? lr : 1e-6);
        float3 tf = max(t1, t2);
        float t = min(tf.x, min(tf.y, tf.z));
        float3 dir = (p + R * max(t, 0.0)) - b.originFeather.xyz;
        if (dot(dir, dir) < 1e-6) dir = R;
        float w = fade * (1.0 - wsum);
        float3 c = lerp(probeAt(b.centerSlice.w, dir, m0), probeAt(b.centerSlice.w, dir, m1), mf);
        sum += c * w;
        wsum += w;
    }
    if (n > 0 && wsum < 0.999) {
        float w = 1.0 - wsum;
        sum += lerp(probeAt(nearSlice, R, m0), probeAt(nearSlice, R, m1), mf) * w;
        wsum += w;
    }
    return wsum > 1e-4 ? sum / wsum : float3(0, 0, 0);
}
float2 gameEnvBrdf(float nv, float g) {
    if (game0.w > 0.5) return envBrdf.SampleLevel(lutSamp, float2(nv, g) * (63.0 / 64.0) + 0.5 / 64.0, 0).rg;
    // Without the table: Karis's fit of the split-sum GGX table (roughness 1 - g).
    float4 r = float4(-1, -0.0275, -0.572, 0.022) * (1.0 - g) + float4(1, 0.0425, 1.04, -0.04);
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}
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
    // The model's vertex normal turned by the instance (uniform scale; zero = none: the pixel shader derives one).
    float3 nw = float3(dot(i.r0.xyz, i.n), dot(i.r1.xyz, i.n), dot(i.r2.xyz, i.n));
    PSIn o; o.pos = mul(float4(w, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = w; o.n = dot(nw, nw) > 1e-12 ? normalize(nw) : float3(0, 0, 0); o.t = float3(0, 0, 0); o.lm = float2(-1, -1); return o;
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
// A stock layered material's layer L >= 1 (research 97), drawn over the layers below with blending: layerP = (on, flags,
// reveal sharpness k, multiply), layerTint = tint RGB, layerUvT = (cos, sin, scale u, scale v), layerUvO = (offset u, v). The
// vertex blend weight is in the tangent x (the stock reference has no tangents). t12: the alpha (red) + reveal (green) map.
cbuffer Layer : register(b7) { float4 layerP; float4 layerTint; float4 layerUvT; float4 layerUvO; };
Texture2D revealTex : register(t12);
float4 ps_tex(PSIn i) : SV_TARGET {
    float layerB = -1.0;
    float2 uvS = i.uv;
    if (layerP.x > 0.5)
        uvS = float2(layerUvT.x * i.uv.x * layerUvT.z + layerUvT.y * i.uv.y * layerUvT.w,
                     -layerUvT.y * i.uv.x * layerUvT.z + layerUvT.x * i.uv.y * layerUvT.w) + layerUvO.xy;
    float4 t = tex0.Sample(samp0, uvS);
    if (layerP.x > 1.5) {
        // A stock decal volume (research 99): the mask at the box UV (clamped), the edge fade, the blend-map remap
        // op = m x saturate(m A + B), times the angle fade; the colour image tinted (layerTint).
        float2 bu = saturate(i.t.xy);
        float m = revealTex.SampleLevel(lutSamp, bu, 0).r;
        m *= 1.0 - saturate(256.0 * (max(abs(2.0 * bu.x - 1.0), abs(2.0 * bu.y - 1.0)) - (1.0 - 0.0078125)));
        layerB = m * saturate(m * layerP.y + layerP.z) * saturate(i.t.z);
        if (layerB < 1.0 / 255.0) discard;
        t.rgb *= pow(max(layerTint.rgb, 0.0), 1.0 / 2.2);
    } else if (layerP.x > 0.5) {
        float2 ar = revealTex.Sample(samp0, uvS).rg;
        float w = saturate(length(i.t) - 1.0), k = layerP.z;  // the tangent's length is 1 + the weight
        uint fl = (uint)(layerP.y + 0.5);
        float b = w;
        if (fl & 16u) {
            float lo = saturate(1.0 - w - k * sqrt(w)), hi = saturate(1.0 - w + k * sqrt(1.0 - w));
            b = saturate((ar.g - lo) / max(hi - lo, 1e-4));
        }
        if (fl & 8u) b *= ar.r;
        layerB = b;
        t.rgb *= pow(max(layerTint.rgb, 0.0), 1.0 / 2.2);  // the tint is linear; t is still sRGB here
    }
    // Blend 0: alpha-tested foliage and grates. Blended materials keep their coverage; without a mask (alpha 1) a
    // blended surface (glass, a decal sheet) shows at 60 percent so what is behind it stays visible.
    float outA = 1.0;
    // Cut-outs: the stock alpha-tested shaders keep opacity above 128 / 255 (research 100); the editor's own materials
    // keep the editor's 0.3.
    if (batchFlags.y < 0.5) clip(t.a - (game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3));
    else outA = t.a > 0.99 ? 0.6 : t.a;
    if (layerB >= 0.0) outA = layerB;
    // Colour mode multiply (layerP.w = 1; puddles, edge wear): below x lerp(1, layer, b), drawn with a multiplying blend
    // over the shaded result (in display space: the factor's 1/2.2 power, as the post chain is close to a gamma there).
    if (layerP.x > 0.5 && layerP.w > 0.5) return float4(pow(lerp(float3(1, 1, 1), srgbToLinear3(t.rgb), layerB), 1.0 / 2.2), 1.0);
    if (game0.x > 0.5) {
        // View > Game Lighting: map_surface_realtime.hlsl (the converter's world shader, iw3 style) and the post chain.
        // batchFlags2.z: the stock reference world (its vertex colours are an editor shade, not a tint), .w: its own
        // lightmap is bound (the stock world shader's formula, tangent-space direction).
        const bool stockRef = batchFlags2.z > 0.5;
        float3 albedo = srgbToLinear3(t.rgb) * (stockRef ? float3(1, 1, 1) : i.col.rgb);  // i.col: selection and brush entity tints
        if (!stockRef && i.col.a < 0.5) return float4(gamePost(gameFogApply(albedo * gameSkyScale.x, i.w, true)), 1.0);  // sky box
        float3 Nf = dot(i.n, i.n) > 0.5 ? normalize(i.n) : normalize(cross(ddx(i.w), ddy(i.w)));
        // A normal from the screen derivatives has no reliable side (instanced props have none of their own): it faces
        // the viewer.
        // A face seen from behind is lit with its normal turned toward the viewer: the stock two-sided foliage shaders
        // negate the tangent frame on back faces (research 100), and back-lit grass cards were dark speckles.
        if (dot(Nf, eyeStart.xyz - i.w) < 0) Nf = -Nf;
        float3 N = Nf;
        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
            float4 nm = nrm0.Sample(samp0, uvS);
            float2 sd = nm.ga * 2.0 - 1.0;
            float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
            float2 slope = oct / max(0.02, 1.0 - abs(oct.x) - abs(oct.y));
            float3 T = normalize(i.t - Nf * dot(i.t, Nf)), B = cross(Nf, T);
            N = normalize(T * slope.x + B * slope.y + Nf);
        }
        // Without a lightmap texel (props, models, new brushes): on a stock map the game lights them from its light grid;
        // here from the reflection probes' blurriest level (luminance-normalised) at the map's mean baked light
        // (gameSky.rgb, gameSky.w = 1); otherwise the unbaked light.
        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));  // the probes' light, below, where no lightmap
        float3 stockDw = Nf, stockB = 0;  // the stock lightmap's directional part, for E(R) below
        float visibility = 1.0;
        float2 lmUv = i.lm;
        bool lmOk = (stockRef ? batchFlags2.w > 0.5 : game0.z > 0.5) && i.lm.x >= 0;
        if (lmOk && stockRef) {
            float idx = floor(i.lm.x);
            float4 rc = idx < 1024 ? lmRects[int(idx)] : float4(0, 0, 0, 0);
            if (rc.z <= 0) lmOk = false;
            float2 half = 0.5 / max(lmInfo.xy, 1.0) / max(rc.zw, 1e-6);
            lmUv = rc.xy + clamp(float2(i.lm.x - idx, i.lm.y), half, 1.0 - half) * rc.zw;
        }
        if (lmOk) {
            float4 a = gameLmA.SampleLevel(lutSamp, lmUv, 0);
            float3 b = gameLmB.SampleLevel(lutSamp, lmUv, 0).rgb;
            float3 d = gameLmD.SampleLevel(lutSamp, lmUv, 0).xyz;
            if (stockRef) {
                a.rgb *= gameSkyScale.y, b *= gameSkyScale.y;  // the map's bakedLightScale
                // Stock: light = f C + (1 - f) C / D.z x sat(N . Dw), D in the surface's tangent frame (here the texture
                // frame from the derivatives; on a flat normal N . Dw = D.z whatever the frame).
                float3 dp1 = ddx(i.w), dp2 = ddy(i.w);
                float2 du1 = ddx(i.uv), du2 = ddy(i.uv);
                float3 p2 = cross(dp2, Nf), p1 = cross(Nf, dp1);
                float3 T = p2 * du1.x + p1 * du2.x, B = p2 * du1.y + p1 * du2.y;
                float s = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
                float3 Dw = normalize(T * s * d.x + B * s * d.y + Nf * d.z + 1e-6);
                indirect = a.rgb + b * saturate(dot(N, Dw));
                stockDw = Dw, stockB = b;
            } else {
                indirect = a.rgb * saturate(dot(N, Nf)) + b * saturate(dot(normalize(d + 1e-6), N));
                visibility = a.a;
            }
        }
        // Only where no lightmap lit the point: the probes' fallback light (it cost every lightmapped pixel two probe
        // lookups that were thrown away).
        if (!lmOk && gameSky.w > 0.5 && probeCount.x > 0.5)
            indirect = stockProbeLight(i.w, N, 0.0) * dot(gameSky.rgb, float3(0.2126, 0.7152, 0.0722));
        // The sun: the baked sun depth where it covers the point (here the sun shadow map), else the lightmap's mask.
        if (litParams.y > 0.5) visibility = sunShadow(i.w);
        float3 c;
        if (stockRef) {
            // The stock world PS (research 95 sections 2 "Ambient combine" and "Sun"): a dielectric (F0 .04) of the
            // material's default gloss (the reference has no gloss maps here), retro-reflective diffuse kd, the
            // environment term from the lightmap along the reflection (probes later) and the GGX sun.
            float3 Vs = normalize(eyeStart.xyz - i.w);
            const float3 f0 = 0.04;
            const float gp = 1.0 - sqrt(1.0 - 0.3);
            float a2 = 2.0 / (2.0 + exp2(20.0 * gp));
            float nv = saturate(dot(N, Vs));
            float2 ab = gameEnvBrdf(nv, gp);
            float3 Fenv = f0 * ab.x + ab.y;
            float3 R = reflect(-Vs, N);
            float3 Lr = (indirect - stockB * saturate(dot(N, stockDw))) + stockB * saturate(dot(R, stockDw));
            float tq = ((-0.442435 * gp + 1.7905) * gp - 2.24983) * gp + 0.948867;
            float cq = sqrt(0.5 * nv + 0.5);
            float fq = 1.05 * (1.0 - pow(1.0 - nv, 5.0));
            float kd = lerp(fq, cq * (cq + min(4.0 * tq * tq, 1.0)) * max(1.25 - 1.6 * tq, 0.55 - 0.2 * tq), tq) + 0.38 * tq * tq;
            float fmax = max(Fenv.r, max(Fenv.g, Fenv.b));
            float3 S = Lr;
            if (probeCount.x > 0.5 && layerP.x < 0.5) {  // layer and decal passes: the lightmap along R (they re-light per pass)
                // Probes are luminance-normalised: relit by the lightmap along the reflection (research 95).
                float3 P = stockProbeLight(i.w, R, gp) * dot(Lr, float3(0.2126, 0.7152, 0.0722));
                float3 mixed = lerp(P, Lr, 0.85);
                S = lerp(0.18 * mixed, P, 1.0);  // specular occlusion 1: no GTAO here
            }
            c = albedo * indirect * kd * (1.0 - fmax) + Fenv * S;
            float3 Ls = gameSun.xyz;
            float nl = dot(N, Ls);
            if (nl > 0) {
                float3 H = normalize(Ls + Vs);
                float nh = saturate(dot(N, H));
                float dd = nh * nh * (a2 - 1.0) + 1.0;
                float Dg = a2 / max(dd * dd, 1e-8);
                float Vg = 1.0 / ((abs(nl) + sqrt(a2 + (1.0 - a2) * nl * nl)) * (nv + sqrt(a2 + (1.0 - a2) * nv * nv)));
                float3 F = f0 + (1.0 - f0) * pow(1.0 - abs(dot(Vs, H)), 5.0);
                float2 abl = gameEnvBrdf(saturate(nl), gp);
                float3 fl = f0 * abl.x + abl.y;
                c += gameSunCol.rgb * visibility * saturate(nl) *
                     (albedo * (1.0 - max(fl.r, max(fl.g, fl.b))) + F * Dg * Vg * 1.0390625);
            }
        } else {
            float3 light = indirect + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
            c = albedo * light;
        }
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
        c = gameFog[0].w > 0.5 ? gameFogApply(c, i.w, false) : applyFog(c, i.w);
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
        float4 nm = nrm0.Sample(samp0, uvS);
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

// Auto exposure metering (setGameMetering): the shown frame back to scene luminance, log-averaged with a centre weight.
const char* kMeterShader = R"(
Texture2D frame : register(t0);
Texture1D invGrey : register(t1);
SamplerState lin : register(s0);
cbuffer Meter : register(b0) { float4 meter; };  // x = 1 / LUT input scale
struct V { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
V vs_full(uint id : SV_VertexID) {
    V o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 ps_meter(V i) : SV_TARGET {
    float3 c = frame.SampleLevel(lin, i.uv, 0).rgb;
    float d = saturate(dot(c, float3(0.2126, 0.7152, 0.0722)));
    float x = invGrey.SampleLevel(lin, d * (255.0 / 256.0) + 0.5 / 256.0, 0).r * meter.x;
    float r = saturate(length(i.uv - 0.5) * 1.41421356);
    float w = 1.0 - r;
    return float4(log(max(x, 1e-5)) * w, w, 0, 0);
}
)";

// IEEE binary16 from float (round to nearest; the lightmap values are finite and positive or small).
uint16_t floatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int exp = int((x >> 23) & 0xff) - 127 + 15;
    uint32_t mant = x & 0x7fffffu;
    if (exp <= 0) {
        if (exp < -10) return uint16_t(sign);
        mant |= 0x800000u;
        const uint32_t shift = uint32_t(14 - exp);
        return uint16_t(sign | ((mant + (1u << (shift - 1))) >> shift));
    }
    if (exp >= 31) return uint16_t(sign | 0x7bffu);  // clamp to the largest finite half
    const uint32_t h = sign | (uint32_t(exp) << 10) | (mant >> 13);
    return uint16_t(h + ((mant >> 12) & 1u));
}

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
    safeRelease(shadowDsvNear_);
    safeRelease(shadowTex_);
    safeRelease(cmpSampler_);
    safeRelease(rsShadow_);
    safeRelease(cbBatch_);
    safeRelease(cbLayer_);
    for (auto*& b : cbLayerAll_) safeRelease(b);
    cbLayerAll_.clear();
    if (ctx1_) ctx1_->Release(), ctx1_ = nullptr;
    safeRelease(rsDecal_);
    safeRelease(vbRefTexExtra_);
    safeRelease(cbWind_);
    safeRelease(cbLights_);
    safeRelease(cbGame_);
    for (int k = 0; k < 3; ++k) safeRelease(gameLmSrv_[k]), safeRelease(gameLmTex_[k]);
    safeRelease(gameLutSrv_);
    safeRelease(gameLutTex_);
    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
    safeRelease(probeArrSrv_), safeRelease(probeArrTex_), safeRelease(cbProbes_), safeRelease(cbLmRects_);
    safeRelease(probeGridSrv_), safeRelease(probeGridTex_);
    safeRelease(meterSrv_), safeRelease(meterRtv_), safeRelease(meterTex_);
    for (auto*& s : meterStage_) safeRelease(s);
    safeRelease(invGreySrv_), safeRelease(invGreyTex_);
    safeRelease(meterVs_), safeRelease(meterPs_), safeRelease(cbMeter_);
    releaseStreams();
    safeRelease(vbRef_);
    safeRelease(vs_);
    safeRelease(ps_);
    safeRelease(layout_);
    safeRelease(rsTris_);
    safeRelease(rsLines_);
    safeRelease(dsWrite_);
    safeRelease(blendNoColor_);
    safeRelease(dsTest_);
    safeRelease(dsOff_);
    safeRelease(blend_);
    safeRelease(blendAdd_);
    safeRelease(blendMul_);
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
            {
                D3D11_BUFFER_DESC lb = bd;
                lb.ByteWidth = 64;
                const float zero[16] = {};
                D3D11_SUBRESOURCE_DATA lz{zero, 0, 0};
                dev_->CreateBuffer(&lb, &lz, &cbLayer_);
            }
            dev_->CreateBuffer(&bd, nullptr, &cbWind_);
            bd.ByteWidth = 16 + UINT(sizeof(PixelLight)) * kMaxPixelLights;
            dev_->CreateBuffer(&bd, nullptr, &cbLights_);
            bd.ByteWidth = 176;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale
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
    cbd.ByteWidth = 304;  // viewProj, eye + fog start, fog colour + halfway, lit params, sun shadow viewProj, grade (3 x float4),
                          // near cascade viewProj, near params
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
    {
        D3D11_BLEND_DESC nb{};
        nb.RenderTarget[0].BlendEnable = FALSE;
        nb.RenderTarget[0].RenderTargetWriteMask = 0;
        dev_->CreateBlendState(&nb, &blendNoColor_);
    }
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    dev_->CreateBlendState(&bd, &blendAdd_);
    {
        D3D11_BLEND_DESC mb = bd;  // dst x src colour (stock multiply layers), alpha kept
        mb.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
        mb.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
        mb.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
        mb.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        dev_->CreateBlendState(&mb, &blendMul_);
    }
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

void SceneRenderer::setStockLightmap(int width, int height, const float* a, const float* b, const float* d,
                                     const std::vector<std::array<float, 4>>& rects) {
    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    if (dev_ && !cbLmRects_) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16 + 16 * kMaxLightmapRects;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        dev_->CreateBuffer(&bd, nullptr, &cbLmRects_);
    }
    if (cbLmRects_) {
        std::vector<float> cb(size_t(4 + 4 * kMaxLightmapRects), 0.0f);
        cb[0] = float(width), cb[1] = float(height);
        // A single lightmap without rectangles is index 0 over the whole texture.
        if (rects.empty()) cb[4] = 0, cb[5] = 0, cb[6] = 1, cb[7] = 1;
        for (size_t k = 0; k < rects.size() && k < size_t(kMaxLightmapRects); ++k)
            for (int c = 0; c < 4; ++c) cb[4 + 4 * k + size_t(c)] = rects[k][size_t(c)];
        ctx_->UpdateSubresource(cbLmRects_, 0, nullptr, cb.data(), 0, 0);
    }
    if (!dev_ || width < 1 || height < 1 || !a || !b || !d) return;
    const float* src[3] = {a, b, d};
    for (int k = 0; k < 3; ++k) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(width), td.Height = UINT(height);
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        // Half floats: a 2048 x 1024 stock lightmap is 16 MB per plane in float32.
        std::vector<uint16_t> half(size_t(width) * size_t(height) * 4);
        for (size_t i = 0; i < half.size(); ++i) half[i] = floatToHalf(src[k][i]);
        D3D11_SUBRESOURCE_DATA init{half.data(), UINT(width) * 8, 0};
        if (FAILED(dev_->CreateTexture2D(&td, &init, &stockLmTex_[k])) ||
            FAILED(dev_->CreateShaderResourceView(stockLmTex_[k], nullptr, &stockLmSrv_[k]))) {
            for (int j = 0; j < 3; ++j) safeRelease(stockLmSrv_[j]), safeRelease(stockLmTex_[j]);
            return;
        }
    }
}

void SceneRenderer::setEnvBrdfLut(int width, int height, const uint8_t* rg) {
    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
    if (!dev_ || width < 2 || height < 2 || !rg) return;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width), td.Height = UINT(height);
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{rg, UINT(width) * 2, 0};
    if (FAILED(dev_->CreateTexture2D(&td, &init, &envBrdfTex_)) || FAILED(dev_->CreateShaderResourceView(envBrdfTex_, nullptr, &envBrdfSrv_)))
        safeRelease(envBrdfTex_);
}

void SceneRenderer::setStockProbes(const StockProbeSet* set) {
    safeRelease(probeArrSrv_), safeRelease(probeArrTex_);
    stockProbeSlices_ = 0;
    if (!dev_) return;
    if (!cbProbes_) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 48 + 80 * kMaxProbeBoxes;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &cbProbes_))) return;
    }
    safeRelease(probeGridSrv_), safeRelease(probeGridTex_);
    std::vector<float> cb(size_t(12 + 20 * kMaxProbeBoxes), 0.0f);
    const auto upload = [&] { ctx_->UpdateSubresource(cbProbes_, 0, nullptr, cb.data(), 0, 0); };
    if (!set || set->probes.empty() || set->size < 16 || set->mipCount < 1) {
        upload();
        return;
    }
    const size_t perProbe = bc6hBytesPerProbe(set->size, set->mipCount);
    for (const auto& p : set->probes)
        if (p.bc6h.size() < perProbe) {
            upload();
            return;
        }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = UINT(set->size);
    td.MipLevels = UINT(set->mipCount);
    td.ArraySize = UINT(set->probes.size());
    td.Format = DXGI_FORMAT_BC6H_UF16;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    for (const auto& p : set->probes) {
        size_t off = 0;
        for (int m = 0; m < set->mipCount; ++m) {
            const int s = std::max(set->size >> m, 4);
            const size_t blocksRow = size_t(s + 3) / 4;
            init.push_back({p.bc6h.data() + off, UINT(blocksRow * 16), 0});
            off += blocksRow * blocksRow * 16;
        }
    }
    if (FAILED(dev_->CreateTexture2D(&td, init.data(), &probeArrTex_)) || FAILED(dev_->CreateShaderResourceView(probeArrTex_, nullptr, &probeArrSrv_))) {
        safeRelease(probeArrTex_);
        upload();
        return;
    }
    stockProbeSlices_ = set->probes.size();
    // Boxes, highest priority first (the fallback box, priority -FLT_MAX, last).
    std::vector<size_t> order(set->boxes.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return set->boxes[a].priority > set->boxes[b].priority; });
    size_t n = 0;
    for (size_t k : order) {
        const StockProbeBox& b = set->boxes[k];
        if (b.imageIndex < 0 || size_t(b.imageIndex) >= set->probes.size() || n >= size_t(kMaxProbeBoxes)) continue;
        float* e = cb.data() + 12 + 20 * n;
        e[0] = b.center[0], e[1] = b.center[1], e[2] = b.center[2], e[3] = float(b.imageIndex);
        for (int a = 0; a < 3; ++a) {
            e[4 + 4 * a] = b.axes[a][0], e[5 + 4 * a] = b.axes[a][1], e[6 + 4 * a] = b.axes[a][2];
            e[7 + 4 * a] = b.halfSize[a];
        }
        e[16] = b.position[0], e[17] = b.position[1], e[18] = b.position[2];
        e[19] = std::max({b.feather[0], b.feather[1], b.feather[2], 1.0f});
        ++n;
    }
    cb[0] = perfOff("probes") ? 0.0f : float(n);  // benchmark switch: no probes
    // The candidate grid over the boxes' world bounds (the fallback box, as large as the map, left out of the bounds).
    if (n > 4 && !perfOff("probegrid")) {
        struct WB {
            float lo[3], hi[3];
        };
        std::vector<WB> wb(n);
        float glo[3] = {1e30f, 1e30f, 1e30f}, ghi[3] = {-1e30f, -1e30f, -1e30f};
        for (size_t i = 0; i < n; ++i) {
            const float* e = cb.data() + 12 + 20 * i;
            for (int c = 0; c < 3; ++c) {
                float ext = 0;
                for (int a = 0; a < 3; ++a) ext += std::fabs(e[4 + 4 * a + c]) * e[7 + 4 * a];
                wb[i].lo[c] = e[c] - ext, wb[i].hi[c] = e[c] + ext;
            }
            if (i + 1 < n || n == 1)
                for (int c = 0; c < 3; ++c) glo[c] = std::min(glo[c], wb[i].lo[c]), ghi[c] = std::max(ghi[c], wb[i].hi[c]);
        }
        int dims[3];
        float cell[3];
        const float vol = std::max((ghi[0] - glo[0]) * (ghi[1] - glo[1]) * (ghi[2] - glo[2]), 1.0f);
        const float side = std::cbrt(vol / 65536.0f);  // about 64 x 64 x 16 cells
        for (int c = 0; c < 3; ++c) {
            dims[c] = std::clamp(int(std::ceil((ghi[c] - glo[c]) / std::max(side, 1.0f))), 1, 128);
            cell[c] = std::max((ghi[c] - glo[c]) / float(dims[c]), 1.0f);
        }
        std::vector<uint8_t> g(size_t(dims[0]) * dims[1] * dims[2] * 4, 255);
        for (int z = 0; z < dims[2]; ++z)
            for (int y = 0; y < dims[1]; ++y)
                for (int x = 0; x < dims[0]; ++x) {
                    const float clo[3] = {glo[0] + x * cell[0], glo[1] + y * cell[1], glo[2] + z * cell[2]};
                    const float chi[3] = {clo[0] + cell[0], clo[1] + cell[1], clo[2] + cell[2]};
                    uint8_t* q = &g[((size_t(z) * dims[1] + y) * dims[0] + x) * 4];
                    int used = 0;
                    for (size_t i = 0; i < n && used < 4; ++i) {
                        bool touch = true;
                        for (int c = 0; c < 3; ++c) touch = touch && wb[i].lo[c] <= chi[c] && wb[i].hi[c] >= clo[c];
                        if (touch) q[used++] = uint8_t(i);
                    }
                    if (used == 0) {
                        // Nearest box by its oriented margin at the cell centre (the shader's fallback).
                        const float p[3] = {0.5f * (clo[0] + chi[0]), 0.5f * (clo[1] + chi[1]), 0.5f * (clo[2] + chi[2])};
                        float best = -1e30f;
                        size_t bi = 0;
                        for (size_t i = 0; i < n; ++i) {
                            const float* e = cb.data() + 12 + 20 * i;
                            const float d[3] = {p[0] - e[0], p[1] - e[1], p[2] - e[2]};
                            float m = 1e30f;
                            for (int a = 0; a < 3; ++a) {
                                const float lp = d[0] * e[4 + 4 * a] + d[1] * e[5 + 4 * a] + d[2] * e[6 + 4 * a];
                                m = std::min(m, e[7 + 4 * a] - std::fabs(lp));
                            }
                            if (m > best) best = m, bi = i;
                        }
                        q[0] = uint8_t(bi);
                    }
                }
        D3D11_TEXTURE3D_DESC t3{};
        t3.Width = UINT(dims[0]), t3.Height = UINT(dims[1]), t3.Depth = UINT(dims[2]);
        t3.MipLevels = 1;
        t3.Format = DXGI_FORMAT_R8G8B8A8_UINT;
        t3.Usage = D3D11_USAGE_IMMUTABLE;
        t3.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA gi{g.data(), UINT(dims[0]) * 4, UINT(dims[0]) * UINT(dims[1]) * 4};
        if (SUCCEEDED(dev_->CreateTexture3D(&t3, &gi, &probeGridTex_)) &&
            SUCCEEDED(dev_->CreateShaderResourceView(probeGridTex_, nullptr, &probeGridSrv_))) {
            cb[1] = float(dims[0]), cb[2] = float(dims[1]), cb[3] = float(dims[2]);
            cb[4] = glo[0], cb[5] = glo[1], cb[6] = glo[2];
            cb[8] = cell[0], cb[9] = cell[1], cb[10] = cell[2], cb[11] = 1.0f;
        } else {
            safeRelease(probeGridTex_);
        }
    }
    upload();
}

void SceneRenderer::writeGameCb() {
    if (!cbGame_) return;
    const bool on = game_.on && gameLutSrv_ != nullptr;
    float g[44] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, envBrdfSrv_ ? 1.0f : 0.0f,
                   game_.sunToward[0], game_.sunToward[1], game_.sunToward[2], 0,
                   game_.sunColor[0], game_.sunColor[1], game_.sunColor[2], 0,
                   game_.sky[0], game_.sky[1], game_.sky[2], game_.skyIsStock ? 1.0f : 0.0f};
    for (int r = 0; r < 6; ++r)
        for (int k = 0; k < 4; ++k) g[16 + 4 * r + k] = game_.fog[r][k];
    g[40] = game_.skyScale;
    g[41] = game_.bakedScale;
    ctx_->UpdateSubresource(cbGame_, 0, nullptr, g, 0, 0);
    ctx_->PSSetConstantBuffers(4, 1, &cbGame_);
    ID3D11ShaderResourceView* gl[3] = {gameLutSrv_, envBrdfSrv_, probeArrSrv_};
    ctx_->PSSetShaderResources(9, 3, gl);
    ctx_->PSSetShaderResources(13, 1, &probeGridSrv_);
    if (cbProbes_) ctx_->PSSetConstantBuffers(5, 1, &cbProbes_);
    if (cbLmRects_) ctx_->PSSetConstantBuffers(6, 1, &cbLmRects_);
    if (lutSampler_) ctx_->PSSetSamplers(2, 1, &lutSampler_);
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

void SceneRenderer::setGameMetering(const float* inverseGrey256) {
    safeRelease(invGreySrv_);
    safeRelease(invGreyTex_);
    metering_ = false;
    meterHas_ = false;
    if (!inverseGrey256 || !dev_) return;
    D3D11_TEXTURE1D_DESC td{};
    td.Width = 256;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{inverseGrey256, 0, 0};
    if (FAILED(dev_->CreateTexture1D(&td, &init, &invGreyTex_)) || FAILED(dev_->CreateShaderResourceView(invGreyTex_, nullptr, &invGreySrv_))) {
        safeRelease(invGreyTex_);
        return;
    }
    metering_ = createMeterResources();
}

bool SceneRenderer::createMeterResources() {
    if (meterReady_) return true;
    ID3DBlob* vsb = nullptr;
    ID3DBlob* psb = nullptr;
    ID3DBlob* errb = nullptr;
    const bool ok = SUCCEEDED(D3DCompile(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", nullptr, nullptr, "vs_full", "vs_5_0", 0, 0, &vsb, &errb)) &&
                    SUCCEEDED(D3DCompile(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", nullptr, nullptr, "ps_meter", "ps_5_0", 0, 0, &psb, &errb));
    if (ok) {
        dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &meterVs_);
        dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &meterPs_);
    }
    safeRelease(vsb), safeRelease(psb), safeRelease(errb);
    if (!meterVs_ || !meterPs_) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 256;
    td.MipLevels = 9;  // 256 .. 1
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32_FLOAT;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &meterTex_)) || FAILED(dev_->CreateRenderTargetView(meterTex_, nullptr, &meterRtv_)) ||
        FAILED(dev_->CreateShaderResourceView(meterTex_, nullptr, &meterSrv_)))
        return false;
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = sd.Height = 1;
    sd.MipLevels = sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_R32G32_FLOAT;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (auto*& s : meterStage_)
        if (FAILED(dev_->CreateTexture2D(&sd, nullptr, &s))) return false;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 16;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(dev_->CreateBuffer(&bd, nullptr, &cbMeter_))) return false;
    meterReady_ = true;
    return true;
}

void SceneRenderer::meterFrame() {
    // Collect finished readbacks first (never waits: DO_NOT_WAIT).
    for (unsigned k = 0; k < 3; ++k) {
        if (!meterStageBusy_[k]) continue;
        D3D11_MAPPED_SUBRESOURCE m;
        if (ctx_->Map(meterStage_[k], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) == S_OK) {
            const float* v = static_cast<const float*>(m.pData);
            if (v[1] > 1e-6f) meterValue_ = std::exp(double(v[0]) / double(v[1])), meterHas_ = true;
            ctx_->Unmap(meterStage_[k], 0);
            meterStageBusy_[k] = false;
        }
    }
    const unsigned slot = meterNext_ % 3;
    if (meterStageBusy_[slot]) return;  // the GPU is behind: skip this frame's measurement
    ++meterNext_;
    D3D11_VIEWPORT vp{0, 0, 256, 256, 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &meterRtv_, nullptr);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(meterVs_, nullptr, 0);
    ctx_->PSSetShader(meterPs_, nullptr, 0);
    const float cb[4] = {1.0f / std::max(game_.lutScale, 1e-6f), 0, 0, 0};
    ctx_->UpdateSubresource(cbMeter_, 0, nullptr, cb, 0, 0);
    ctx_->PSSetConstantBuffers(0, 1, &cbMeter_);
    ID3D11ShaderResourceView* srvs[2] = {srv_, invGreySrv_};
    ctx_->PSSetShaderResources(0, 2, srvs);
    if (lutSampler_) ctx_->PSSetSamplers(0, 1, &lutSampler_);
    ctx_->Draw(3, 0);
    ID3D11RenderTargetView* none = nullptr;
    ctx_->OMSetRenderTargets(1, &none, nullptr);
    ID3D11ShaderResourceView* nulls[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 2, nulls);
    ctx_->GenerateMips(meterSrv_);
    ctx_->CopySubresourceRegion(meterStage_[slot], 0, 0, 0, 0, meterTex_, 8, nullptr);
    meterStageBusy_[slot] = true;
    D3D11_VIEWPORT full{0, 0, float(w_), float(h_), 0, 1};
    ctx_->RSSetViewports(1, &full);
}

bool SceneRenderer::takeMeteredLuminance(double& avg) {
    if (!meterHas_) return false;
    avg = meterValue_;
    meterHas_ = false;
    return true;
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

void SceneRenderer::setReferenceDecal(size_t batch, int sortKey, int revealTexture, const float params[16]) {
    if (batch >= refLayers_.size()) return;
    RefLayer& L = refLayers_[batch];
    L.decal = true;
    L.sortKey = sortKey;
    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
    refDecalDirty_ = true;
    layerParamsDirty_ = true;
}

void SceneRenderer::setReferenceLayer(size_t batch, int revealTexture, const float params[16]) {
    if (batch >= refLayers_.size() || refLayers_[batch].layer <= 0) return;
    RefLayer& L = refLayers_[batch];
    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
    layerParamsDirty_ = true;
}

bool SceneRenderer::appendReferenceDecals(const std::vector<std::vector<GpuTexVertex>>& batches) {
    // Drop the previous extras.
    if (refTexExtraFrom_ != ~size_t(0) && refTexExtraFrom_ <= refTexIds_.size()) {
        const size_t n = refTexExtraFrom_;
        refTexFirst_.resize(n), refTexCount_.resize(n), refTexIds_.resize(n), refTexChunks_.resize(n);
        refLayers_.resize(n), refNrmIds_.resize(n);
        refDecalDirty_ = true;
    }
    safeRelease(vbRefTexExtra_);
    refTexExtraFrom_ = refTexIds_.size();
    refTexExtraBounds_.clear();
    if (!dev_ || batches.empty()) return true;
    std::vector<GpuTexVertex> all;
    for (const auto& b : batches) {
        refTexFirst_.push_back(UINT(all.size()));
        refTexCount_.push_back(UINT(b.size()));
        all.insert(all.end(), b.begin(), b.end());
        refTexChunks_.emplace_back();  // small: drawn whole
        iw8r::AABB bb;
        for (const GpuTexVertex& v : b) bb.add(iw8r::Vec3{v.x, v.y, v.z});
        refTexExtraBounds_.push_back(bb);
        refTexIds_.push_back(-1);
        RefLayer L;
        L.decal = true;  // never drawn opaque; ready once setReferenceDecal gives its textures
        refLayers_.push_back(L);
        refNrmIds_.push_back(-1);
    }
    if (all.empty()) return true;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = UINT(all.size() * sizeof(GpuTexVertex));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{all.data(), 0, 0};
    if (FAILED(dev_->CreateBuffer(&bd, &init, &vbRefTexExtra_))) return false;
    refDecalDirty_ = true;
    return true;
}

bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers) {
    safeRelease(vbRefTexExtra_);
    refTexExtraFrom_ = ~size_t(0);
    refTexExtraBounds_.clear();
    refLayers_.assign(batches.size(), RefLayer{});
    refNrmIds_.assign(batches.size(), -1);
    refLayerOrder_.clear();
    refDecalOrder_.clear();
    refDecalDirty_ = false;
    if (!rsDecal_ && dev_) {
        // Decals lie on the surface below: pulled toward the camera a little (research 98: polygon offset).
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        rd.DepthBias = -16;
        rd.SlopeScaledDepthBias = -1.0f;
        dev_->CreateRasterizerState(&rd, &rsDecal_);
    }
    if (flatNrmTex_ < 0 && dev_) {
        const uint8_t flat[16] = {128, 128, 255, 128, 128, 128, 255, 128, 128, 128, 255, 128, 128, 128, 255, 128};
        flatNrmTex_ = createTexture(flat, 2, 2);
    }
    if (layers && layers->size() == batches.size()) {
        for (size_t k = 0; k < batches.size(); ++k)
            if ((*layers)[k] > 0) refLayers_[k].layer = (*layers)[k], refLayerOrder_.push_back(k);
        std::stable_sort(refLayerOrder_.begin(), refLayerOrder_.end(), [&](size_t a, size_t b) { return refLayers_[a].layer < refLayers_[b].layer; });
    }
    if (whiteFullTex_ < 0 && dev_) {
        const uint8_t white[16] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
        whiteFullTex_ = createTexture(white, 2, 2);
    }
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

void SceneRenderer::cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle) {
    const iw8r::Frustum fr = iw8r::frustumFromViewProj(viewProj);
    instCulledLast_ = 0;
    std::vector<float> keep;
    for (GpuTexInstanced& g : refTexInst_) {
        if (!g.vb || !g.inst || !g.visInst || g.rows.empty()) continue;
        keep.clear();
        const size_t n = g.rows.size() / 12;
        for (size_t i = 0; i < n; ++i) {
            const float* r = &g.rows[i * 12];
            const float c[3] = {r[0] * g.centre[0] + r[1] * g.centre[1] + r[2] * g.centre[2] + r[3],
                                r[4] * g.centre[0] + r[5] * g.centre[1] + r[6] * g.centre[2] + r[7],
                                r[8] * g.centre[0] + r[9] * g.centre[1] + r[10] * g.centre[2] + r[11]};
            const float sc = std::sqrt(r[0] * r[0] + r[4] * r[4] + r[8] * r[8]);
            const float rad = g.radius * std::max(sc, 1e-6f);
            bool out = false;
            for (const auto& pl : fr.planes)
                if (pl.n.x * c[0] + pl.n.y * c[1] + pl.n.z * c[2] - pl.d > rad) {
                    out = true;
                    break;
                }
            if (!out) {
                const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                out = dist > rad && rad < dist * pixelAngle;  // under about a pixel across
            }
            if (out) {
                ++instCulledLast_;
                continue;
            }
            keep.insert(keep.end(), r, r + 12);
        }
        g.visible = unsigned(keep.size() / 12);
        if (g.visible) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (SUCCEEDED(ctx_->Map(g.visInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, keep.data(), keep.size() * sizeof(float));
                ctx_->Unmap(g.visInst, 0);
            } else {
                g.visible = g.instances;  // fall back to every placement
            }
        }
    }
}

bool SceneRenderer::setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes) {
    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.inst), safeRelease(g.visInst);
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
                    g.visible = g.instances;
                    g.rows = m.rows;
                    // The model's bounding sphere (box centre, half diagonal).
                    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
                    for (const GpuTexVertex& v : m.tris) {
                        const float p[3] = {v.x, v.y, v.z};
                        for (int c = 0; c < 3; ++c) lo[c] = std::min(lo[c], p[c]), hi[c] = std::max(hi[c], p[c]);
                    }
                    float r2 = 0;
                    for (int c = 0; c < 3; ++c) g.centre[c] = 0.5f * (lo[c] + hi[c]), r2 += 0.25f * (hi[c] - lo[c]) * (hi[c] - lo[c]);
                    g.radius = std::sqrt(r2);
                    D3D11_BUFFER_DESC dd = bd;
                    dd.Usage = D3D11_USAGE_DYNAMIC;
                    dd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    dev_->CreateBuffer(&dd, nullptr, &g.visInst);
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
    ctx_->PSSetShader(texPsOverride_ ? texPsOverride_ : psTex_, nullptr, 0);
    ctx_->PSSetSamplers(0, 1, &sampler_);
    if (cbBatch_) {
        const float flags[8] = {0, 0, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};
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
        const bool culled = instCull_ && g.visInst && g.visible <= g.instances;
        if (culled && g.visible == 0) continue;
        ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        ID3D11Buffer* bufs[2] = {g.vb, culled ? g.visInst : g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, culled ? g.visible : g.instances, 0, 0);
    }
    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
}

void SceneRenderer::drawReferenceTexInstancesShadow() {
    if (refTexInst_.empty() || !vsTexInst_ || !layoutTexInst_) return;
    ctx_->IASetInputLayout(layoutTexInst_);
    ctx_->VSSetShader(vsTexInst_, nullptr, 0);
    ctx_->PSSetShader(psShadowClip_, nullptr, 0);  // none: solid
    if (psShadowClip_ && sampler_) ctx_->PSSetSamplers(0, 1, &sampler_);
    const UINT strides[2] = {UINT(sizeof(GpuTexVertex)), UINT(12 * sizeof(float))}, offsets[2] = {0, 0};
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if (psShadowClip_) {
            const int id = g.texture >= 0 ? g.texture : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        }
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
    ctx_->PSSetShader(nullptr, nullptr, 0);
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
        float cb[76];
        std::memcpy(cb, vpm, 64);
        std::memcpy(cb + 16, fog_, sizeof(float) * 8);
        if (!fog) cb[23] = 0;
        const bool litOn = lit && litDecode_ > 1.5f;
        cb[24] = litOn ? litDecode_ : 1.0f;
        // The sun shadow map: the lit preview, and Game Lighting (the stock lightmaps hold no sun visibility: the game
        // shadows the sun live, so without the map every surface took the full sun and the view looked washed out).
        cb[25] = (litOn || (lit && game_.on)) && shadowOn ? 1.0f : 0.0f;
        cb[26] = shadowBias_;
        cb[27] = litOn ? exposure_ : 1.0f;
        std::memcpy(cb + 28, sunViewProj_, 64);
        std::memcpy(cb + 44, grade_, sizeof(grade_));
        cb[47] = lit ? float(gradeFlags_) : 0.0f;  // lines (lit = false) keep their colours
        std::memcpy(cb + 56, sunViewProjNear_, 64);
        cb[72] = shadowBiasNear_;
        cb[73] = nearEnabled_ ? 1.0f : 0.0f;
        cb[74] = cb[75] = 0;
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
    const bool farShadowDue = shadowOn && (shadowDirty_ || in.casterVersion != shadowCasterVersion_);
    const bool nearShadowDue = shadowOn && nearEnabled_ && shadowDsvNear_ && (farShadowDue || nearDirty_);
    if (farShadowDue || nearShadowDue) {
        // Sun shadow maps (lit preview, Game Lighting): depth of everything from the sun. The whole-scene cascade is drawn
        // again only when a caster or the sun changed; the near cascade also when the camera has moved its box.
        IW8R_PROF("d.shadow");
        ID3D11RenderTargetView* noRtv = nullptr;
        inShadowPass_ = true;
        D3D11_VIEWPORT svp{0, 0, float(kShadowSize), float(kShadowSize), 0, 1};
        ctx_->RSSetViewports(1, &svp);
        const auto drawCasters = [&](ID3D11DepthStencilView* sdsv, const float* sm, bool cull) {
        ctx_->OMSetRenderTargets(1, &noRtv, sdsv);
        ctx_->ClearDepthStencilView(sdsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        writeCbWith(sm, false, false);
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
            if (cull && !refChunks_.empty())
                forVisibleRuns(iw8r::frustumFromViewProj(sm), refChunks_, [&](uint32_t first, uint32_t n) { drawN(n * 3, first * 3); });
            else
                drawN(UINT(refCount_), 0);
        }
        if (refVisible_) drawReferenceInstances(ReferencePass::Shadow);
        // The textured props (trees, crates, poles) cast too: their leaves through the cut-out test.
        if (refVisible_) drawReferenceTexInstancesShadow();
        };
        if (farShadowDue) drawCasters(shadowDsv_, sunViewProj_, false);
        if (nearShadowDue) drawCasters(shadowDsvNear_, sunViewProjNear_, true);
        if (farShadowDue) {
            shadowDirty_ = false;
            shadowCasterVersion_ = in.casterVersion;
            ++stats_.shadowPasses;
        }
        if (nearShadowDue) nearDirty_ = false;
        inShadowPass_ = false;
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
    {
        static const bool noCull = perfOff("instcull");  // benchmark switch
        instCull_ = !noCull && !in.capture && !in.clean;
        if (instCull_ && !refTexInst_.empty()) {
            // One pixel of the frame: the projection's x scale (viewProj is row-vector; column 0 of the projection part
            // is not separable here, so the half-width tangent comes from the frame size and a 90-degree fallback).
            const float eyeP[3] = {fog_[0], fog_[1], fog_[2]};  // setFog carries the eye
            cullTexInstances(viewProj, eyeP, pixelAngle_);
        }
    }
    texCulledLast_ = 0;
    // Game Lighting: depth pre-pass of the opaque textured geometry (scene batches, the stock world, the stock props).
    // Off by default: measured on mp_rust (18 M triangles) it cost more than it saved (14.3 against 10.7 ms), the
    // geometry being drawn twice; IW8R_DEPTH_PREPASS=1 turns it on for scenes bound by shading instead.
    static const bool usePrepass = std::getenv("IW8R_DEPTH_PREPASS") && std::string(std::getenv("IW8R_DEPTH_PREPASS")) == "1";
    const bool prepass = usePrepass && game_.on && psShadowClip_ && blendNoColor_ && vsTex_ && layoutTex_ && sampler_;
    if (prepass) {
        IW8R_PROF("d.prepass");
        ctx_->OMSetBlendState(blendNoColor_, bf, 0xffffffff);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->RSSetState(rsTris_);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetInputLayout(layoutTex_);
        ctx_->VSSetShader(vsTex_, nullptr, 0);
        ctx_->PSSetShader(psShadowClip_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        if (cbBatch_) ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
        if (cbWind_) {
            ctx_->UpdateSubresource(cbWind_, 0, nullptr, wind_, 0, 0);
            ctx_->VSSetConstantBuffers(3, 1, &cbWind_);
        }
        for (const BatchDraw& d : drawList_) {
            const SceneBatch& b = *d.b;
            StreamGpu& g = *d.g;
            if (b.blend || b.texture < 0 || size_t(b.texture) >= textures_.size()) continue;
            if (cbBatch_) {  // the same vertex inputs as the shading pass (sway), so the depths match exactly
                const float flags[8] = {0, float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ, 0, 0, 0, 0};
                ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            }
            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(b.texture)]);
            ctx_->IASetVertexBuffers(0, 1, &g.vb, &tstride, &offset);
            if (refCulling_ && g.chunked && !g.chunks.empty()) {
                ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT_R32_UINT, 0);
                forVisibleRuns(frustum, g.chunks, [&](uint32_t first, uint32_t n) { drawIdx(n * 3, first * 3); });
            } else {
                drawN(UINT(b.v.size()), 0);
            }
        }
        if (vbRefTex_ && refVisible_) {
            if (cbBatch_) {
                const float flags[8] = {0, 0, 0, 0, 0, 0, 1.0f, 0};
                ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            }
            ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: later
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
                if (refCulling_ && k < refTexChunks_.size() && !refTexChunks_[k].empty())
                    forVisibleRuns(frustum, refTexChunks_[k], [&](uint32_t first, uint32_t n) { drawN(n * 3, refTexFirst_[k] + first * 3); });
                else
                    drawN(refTexCount_[k], refTexFirst_[k]);
            }
        }
        if (refVisible_ && !refTexInst_.empty()) {
            texPsOverride_ = psShadowClip_;
            drawReferenceTexInstances();
            texPsOverride_ = nullptr;
        }
        ID3D11ShaderResourceView* noTex[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, noTex);
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
        ctx_->IASetInputLayout(layout_);
        ctx_->VSSetShader(vs_, nullptr, 0);
        ctx_->PSSetShader(ps_, nullptr, 0);
    }
    // After the pre-pass the opaque textured geometry only tests depth (equal depths pass LESS_EQUAL).
    ID3D11DepthStencilState* texOpaqueDs = prepass ? dsTest_ : dsWrite_;
    if (!drawList_.empty()) {
        // One vertex buffer per batch; one draw per texture, or one per run of visible chunks.
        IW8R_PROF("d.tex");
        ctx_->IASetInputLayout(layoutTex_);
        ctx_->VSSetShader(vsTex_, nullptr, 0);
        ctx_->PSSetShader(psTex_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (cbBatch_) ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
        if (cbBatch_) ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
        if (cbWind_) {
            ctx_->UpdateSubresource(cbWind_, 0, nullptr, wind_, 0, 0);
            ctx_->VSSetConstantBuffers(3, 1, &cbWind_);
        }
        if (cbGame_) {
            writeGameCb();
            ID3D11ShaderResourceView* gs[3] = {gameLmSrv_[0], gameLmSrv_[1], gameLmSrv_[2]};
            ctx_->PSSetShaderResources(6, 3, gs);
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
        ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
        writeGameCb();
        {
            ID3D11ShaderResourceView* sl[3] = {stockLmSrv_[0], stockLmSrv_[1], stockLmSrv_[2]};
            ctx_->PSSetShaderResources(6, 3, sl);
        }
        // x = 1: every stock batch samples t1, its normal map or the flat one (research 95 encoding: g, a = 0.5).
        const bool refNormals = flatNrmTex_ >= 0 && size_t(flatNrmTex_) < textures_.size();
        if (cbBatch_) {
            const float flags[8] = {refNormals ? 1.0f : 0.0f, 0, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};
            ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
            ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
        }
        ID3D11ShaderResourceView* noNrm = nullptr;
        ctx_->PSSetShaderResources(1, 1, &noNrm);
        ID3D11ShaderResourceView* boundNrm = nullptr;
        const auto bindNormal = [&](size_t k) {
            if (!refNormals) return;
            const int n = k < refNrmIds_.size() && refNrmIds_[k] >= 0 && size_t(refNrmIds_[k]) < textures_.size() ? refNrmIds_[k] : flatNrmTex_;
            if (boundNrm != textures_[size_t(n)]) ctx_->PSSetShaderResources(1, 1, &textures_[size_t(n)]), boundNrm = textures_[size_t(n)];
        };
        refCulledLast_ = 0;
        ID3D11ShaderResourceView* bound = nullptr;
        for (size_t k = 0; k < refTexIds_.size(); ++k) {
            if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: below
            const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            const auto bind = [&] {
                // Batches that share a texture (the plain one while images load) do not bind it again.
                if (bound != textures_[size_t(id)]) ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]), bound = textures_[size_t(id)];
                bindNormal(k);
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
        // The layers of layered stock materials (terrain): over their base, in layer order, blended by the layer's
        // weight and reveal map, depth-tested against the base (same vertices, same depth) without writing it.
        static const bool noLayers = perfOff("layers");  // benchmark switch: no layer or decal passes
        if (!noLayers && (!refLayerOrder_.empty() || refDecalDirty_ || !refDecalOrder_.empty()) && cbLayer_) {
            if (cbBatch_) {
                const float flags[8] = {refNormals ? 1.0f : 0.0f, 1.0f, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};  // y = 1: blended, no clip
                ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            }
            ctx_->OMSetDepthStencilState(dsTest_, 0);
            ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
            if (refDecalDirty_) {
                refDecalDirty_ = false;
                refDecalOrder_.clear();
                for (size_t k = 0; k < refLayers_.size(); ++k)
                    if (refLayers_[k].decal) refDecalOrder_.push_back(k);
                std::stable_sort(refDecalOrder_.begin(), refDecalOrder_.end(), [&](size_t a, size_t b) {
                    const RefLayer &x = refLayers_[a], &y = refLayers_[b];
                    return x.sortKey != y.sortKey ? x.sortKey < y.sortKey : x.layer < y.layer;
                });
            }
            ID3D11Buffer* boundRefVb = vbRefTex_;
            if (!ctx1_ && ctx_ && dev_) {
                D3D11_FEATURE_DATA_D3D11_OPTIONS o{};
                if (SUCCEEDED(dev_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof(o))) && o.ConstantBufferOffsetting)
                    ctx_->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1_));
            }
            constexpr size_t kPerCb = 256;  // batches per 64 KB buffer (256 bytes each)
            if (ctx1_ && (layerParamsDirty_ || cbLayerAllBatches_ != refLayers_.size())) {
                layerParamsDirty_ = false;
                const size_t chunks = (refLayers_.size() + kPerCb - 1) / kPerCb;
                if (cbLayerAllBatches_ != refLayers_.size()) {
                    for (auto*& b : cbLayerAll_) safeRelease(b);
                    cbLayerAll_.assign(chunks, nullptr);
                    cbLayerAllBatches_ = refLayers_.size();
                    D3D11_BUFFER_DESC bd{};
                    bd.ByteWidth = UINT(kPerCb * 256);
                    bd.Usage = D3D11_USAGE_DEFAULT;
                    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                    for (auto*& b : cbLayerAll_)
                        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &b))) cbLayerAllBatches_ = 0;
                }
                std::vector<float> all(kPerCb * 64, 0.0f);  // 256 bytes (16 constants) per batch
                for (size_t c = 0; c < cbLayerAll_.size() && cbLayerAllBatches_; ++c) {
                    std::fill(all.begin(), all.end(), 0.0f);
                    for (size_t j = 0; j < kPerCb && c * kPerCb + j < refLayers_.size(); ++j)
                        std::memcpy(&all[j * 64], refLayers_[c * kPerCb + j].params, sizeof(refLayers_[0].params));
                    if (cbLayerAll_[c]) ctx_->UpdateSubresource(cbLayerAll_[c], 0, nullptr, all.data(), 0, 0);
                }
            }
            const bool offsetCb = ctx1_ && !cbLayerAll_.empty() && cbLayerAllBatches_ == refLayers_.size();
            int lastFlags = -1;
            ID3D11BlendState* lastBlend = nullptr;
            ID3D11ShaderResourceView *lastCol = nullptr, *lastRev = nullptr;
            static const bool noPropDecals = perfOff("propdecals");  // benchmark switch
            // Opaque layers first, then the decals (their layers with them) with a small depth offset.
            std::vector<size_t> passOrder;
            passOrder.reserve(refLayerOrder_.size() + refDecalOrder_.size());
            for (size_t k : refLayerOrder_)
                if (!refLayers_[k].decal) passOrder.push_back(k);
            const size_t firstDecal = passOrder.size();
            passOrder.insert(passOrder.end(), refDecalOrder_.begin(), refDecalOrder_.end());
            for (size_t pi = 0; pi < passOrder.size(); ++pi) {
                const size_t k = passOrder[pi];
                if (pi == firstDecal && rsDecal_) ctx_->RSSetState(rsDecal_);
                {
                    ID3D11Buffer* want = k >= refTexExtraFrom_ && vbRefTexExtra_ ? vbRefTexExtra_ : vbRefTex_;
                    if (want != boundRefVb) ctx_->IASetVertexBuffers(0, 1, &want, &tstride, &offset), boundRefVb = want;
                }
                const RefLayer& L = refLayers_[k];
                if (!L.ready || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;
                if (k >= refTexExtraFrom_) {
                    // Prop decals: small detail; drawn in view and within 4,000 units (Hideout: 790k triangles, 38 ms).
                    if (noPropDecals) continue;
                    const size_t e = k - refTexExtraFrom_;
                    if (e < refTexExtraBounds_.size() && refTexExtraBounds_[e].valid()) {
                        const iw8r::AABB& bb = refTexExtraBounds_[e];
                        const iw8r::Vec3 c = bb.center();
                        const double dx = c.x - fog_[0], dy = c.y - fog_[1], dz = c.z - fog_[2];
                        const double r = iw8r::length(bb.maxs - bb.mins) * 0.5;
                        if (std::sqrt(dx * dx + dy * dy + dz * dz) - r > 4000.0) continue;
                        bool out = false;
                        for (const auto& pl : frustum.planes)
                            if (pl.n.x * c.x + pl.n.y * c.y + pl.n.z * c.z - pl.d > r) {
                                out = true;
                                break;
                            }
                        if (out) continue;
                    }
                }
                const bool volume = L.params[0] > 1.5f;  // decal volumes carry their box UV in the tangent: no normal map
                if (cbBatch_ && int(volume) != lastFlags) {
                    lastFlags = int(volume);
                    const float flags[8] = {refNormals && !volume ? 1.0f : 0.0f, 1.0f, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};
                    ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
                }
                const int rv = L.reveal >= 0 && size_t(L.reveal) < textures_.size() ? L.reveal : whiteFullTex_;
                if (rv < 0 || size_t(rv) >= textures_.size()) continue;
                if (offsetCb && cbLayerAll_[k / kPerCb]) {
                    const UINT first = UINT((k % kPerCb) * 16), num = 16;
                    ctx1_->PSSetConstantBuffers1(7, 1, &cbLayerAll_[k / kPerCb], &first, &num);
                } else {
                    ctx_->UpdateSubresource(cbLayer_, 0, nullptr, L.params, 0, 0);
                }
                {
                    ID3D11BlendState* want = L.params[3] > 0.5f && blendMul_ ? blendMul_ : blend_;
                    if (want != lastBlend) {
                        const float bfz[4] = {0, 0, 0, 0};
                        ctx_->OMSetBlendState(want, bfz, 0xffffffff);
                        lastBlend = want;
                    }
                }
                if (textures_[size_t(refTexIds_[k])] != lastCol) lastCol = textures_[size_t(refTexIds_[k])], ctx_->PSSetShaderResources(0, 1, &lastCol);
                if (textures_[size_t(rv)] != lastRev) lastRev = textures_[size_t(rv)], ctx_->PSSetShaderResources(12, 1, &lastRev);
                bindNormal(k);
                if (refCulling_ && k < refTexChunks_.size() && !refTexChunks_[k].empty())
                    refCulledLast_ += forVisibleRuns(frustum, refTexChunks_[k], [&](uint32_t first, uint32_t n) { drawN(n * 3, refTexFirst_[k] + first * 3); });
                else
                    drawN(refTexCount_[k], refTexFirst_[k]);
            }
            const float zero[16] = {};
            ctx_->UpdateSubresource(cbLayer_, 0, nullptr, zero, 0, 0);
            ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // back to the plain (zero) layer buffer
            {
                const float bfz[4] = {0, 0, 0, 0};
                ctx_->OMSetBlendState(blend_, bfz, 0xffffffff);
            }
            ID3D11ShaderResourceView* noRev = nullptr;
            ctx_->PSSetShaderResources(12, 1, &noRev);
            ctx_->PSSetShaderResources(1, 1, &noRev);
            ctx_->RSSetState(rsTris_);
            ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
            if (boundRefVb != vbRefTex_) ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
        }
        ID3D11ShaderResourceView* none2[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, none2);
        ctx_->PSSetShaderResources(1, 1, none2);
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
        ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
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
    if (metering_ && game_.on && srv_ && !in.capture) meterFrame();
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
    if (shadowSrv_ && shadowDsv_ && shadowDsvNear_ && cmpSampler_) return true;
    if (!dev_ || shadowFailed_) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = kShadowSize;
    td.MipLevels = 1;
    td.ArraySize = 2;  // 0: the whole scene, 1: the near cascade
    td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    dd.Texture2DArray.FirstArraySlice = 0;
    dd.Texture2DArray.ArraySize = 1;
    D3D11_DEPTH_STENCIL_VIEW_DESC ddNear = dd;
    ddNear.Texture2DArray.FirstArraySlice = 1;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sd.Texture2DArray.MipLevels = 1;
    sd.Texture2DArray.ArraySize = 2;
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
        FAILED(dev_->CreateDepthStencilView(shadowTex_, &ddNear, &shadowDsvNear_)) ||
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

void SceneRenderer::setSunShadowNear(bool enabled, const float sunViewProj[16], float bias) {
    if (enabled && (!nearEnabled_ || std::memcmp(sunViewProj, sunViewProjNear_, sizeof(sunViewProjNear_)) != 0)) nearDirty_ = true;
    nearEnabled_ = enabled;
    std::memcpy(sunViewProjNear_, sunViewProj, sizeof(sunViewProjNear_));
    shadowBiasNear_ = bias;
}

void sunShadowMatrix(const double toward[3], const double mins[3], const double maxs[3], float out[16]) {
    const double c[3] = {(mins[0] + maxs[0]) / 2, (mins[1] + maxs[1]) / 2, (mins[2] + maxs[2]) / 2};
    const double ex[3] = {(maxs[0] - mins[0]) / 2, (maxs[1] - mins[1]) / 2, (maxs[2] - mins[2]) / 2};
    const double R = std::max(1.0, std::sqrt(ex[0] * ex[0] + ex[1] * ex[1] + ex[2] * ex[2]));
    sunShadowMatrixAround(toward, c, R, R, out);
}

void sunShadowMatrixAround(const double toward[3], const double c[3], double R, double depthRadius, float out[16]) {
    // Orthographic view from the sun over a sphere: x / y across (radius R), depth along the light (2 x depthRadius
    // before and after the centre), D3D z in [0, 1] (row vectors).
    R = std::max(1.0, R);
    const double D = std::max(R, depthRadius);
    double tl = std::sqrt(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
    if (tl < 1e-9) tl = 1;
    const double f[3] = {-toward[0] / tl, -toward[1] / tl, -toward[2] / tl};  // from the sun into the scene
    const double up0[3] = {std::fabs(f[2]) > 0.95 ? 1.0 : 0.0, 0.0, std::fabs(f[2]) > 0.95 ? 0.0 : 1.0};
    double r[3] = {f[1] * up0[2] - f[2] * up0[1], f[2] * up0[0] - f[0] * up0[2], f[0] * up0[1] - f[1] * up0[0]};
    const double rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (double& v : r) v /= rl;
    const double u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    const double eye[3] = {c[0] - f[0] * 2 * D, c[1] - f[1] * 2 * D, c[2] - f[2] * 2 * D};
    auto dot3 = [](const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const double depth = 4 * D;
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