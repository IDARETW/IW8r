#include "render.h"
#include "smodel_lod.h"
#include "stock_probes.h"

#include <algorithm>
#include <memory>
#include <d3d11_1.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace iw8r::app {

bool Prof::on = false;
void (*Prof::gpuMark)(const char*, bool) = nullptr;

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

ProfScope::ProfScope(const char* n) : name(n), t0(Prof::on ? profNow() : 0) {
    if (Prof::on && Prof::gpuMark) Prof::gpuMark(n, true);
}
ProfScope::~ProfScope() {
    if (Prof::on) {
        Prof::add(name, profNow() - t0);
        if (Prof::gpuMark) Prof::gpuMark(name, false);
    }
}

// GPU scope timing of one frame (Prof::gpuMark): timestamp queries in pairs, inside one disjoint query.
namespace {
struct GpuProfState {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Query* disjoint = nullptr;
    std::vector<ID3D11Query*> pool;
    size_t used = 0;
    struct Rec {
        const char* name;
        size_t q0, q1;
    };
    std::vector<Rec> recs;
    std::vector<size_t> stack;
};
GpuProfState& gpuProf() {
    static GpuProfState s;
    return s;
}
size_t gpuQuery() {
    GpuProfState& g = gpuProf();
    if (g.used == g.pool.size()) {
        D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP, 0};
        ID3D11Query* q = nullptr;
        if (FAILED(g.dev->CreateQuery(&qd, &q))) return size_t(-1);
        g.pool.push_back(q);
    }
    g.ctx->End(g.pool[g.used]);
    return g.used++;
}
void gpuMarkImpl(const char* name, bool begin) {
    GpuProfState& g = gpuProf();
    if (begin) {
        g.recs.push_back({name, gpuQuery(), size_t(-1)});
        g.stack.push_back(g.recs.size() - 1);
    } else if (!g.stack.empty()) {
        g.recs[g.stack.back()].q1 = gpuQuery();
        g.stack.pop_back();
    }
}
const char* gpuName(const char* name) {
    static std::vector<std::unique_ptr<std::string>> names;
    for (const auto& n : names)
        if (n->compare(4, std::string::npos, name) == 0) return n->c_str();
    names.push_back(std::make_unique<std::string>(std::string("gpu.") + name));
    return names.back()->c_str();
}
}  // namespace

struct GpuProfFrame {
    bool active = false;
    GpuProfFrame(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
        static const bool want = std::getenv("IW8R_BENCH_GPU") != nullptr;
        if (!want || !Prof::on || !dev || !ctx || Prof::gpuMark) return;
        GpuProfState& g = gpuProf();
        g.dev = dev, g.ctx = ctx;
        if (!g.disjoint) {
            D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            if (FAILED(dev->CreateQuery(&qd, &g.disjoint))) return;
        }
        g.used = 0, g.recs.clear(), g.stack.clear();
        ctx->Begin(g.disjoint);
        Prof::gpuMark = gpuMarkImpl;
        active = true;
    }
    ~GpuProfFrame() {
        if (!active) return;
        Prof::gpuMark = nullptr;
        GpuProfState& g = gpuProf();
        g.ctx->End(g.disjoint);
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        while (g.ctx->GetData(g.disjoint, &dj, sizeof(dj), 0) == S_FALSE) {}
        if (dj.Disjoint || dj.Frequency == 0) return;
        std::vector<UINT64> t(g.used, 0);
        for (size_t i = 0; i < g.used; ++i)
            while (g.ctx->GetData(g.pool[i], &t[i], sizeof(UINT64), 0) == S_FALSE) {}
        for (const auto& r : g.recs)
            if (r.q0 < g.used && r.q1 < g.used && t[r.q1] >= t[r.q0])
                Prof::add(gpuName(r.name), (long long)(double(t[r.q1] - t[r.q0]) * 1e9 / double(dj.Frequency)));
    }
};

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
    float sw, sh, sn;
    shadowMap.GetDimensions(sw, sh, sn);  // the map's side (View > Game Lighting Options > Sun Shadow Detail)
    float s = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            s += shadowMap.SampleCmpLevelZero(shadowSamp, float3(uv + float2(x, y) / sw, slice), z);
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
    float sw, sh, sn;
    shadowMap.GetDimensions(sw, sh, sn);  // the map's side (View > Game Lighting Options > Sun Shadow Detail)
    float s = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            s += shadowMap.SampleCmpLevelZero(shadowSamp, float3(uv + float2(x, y) / sw, slice), z);
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
// The alpha-test cut of cut-out surfaces: 0.5 in Game Lighting for every batch (the build's map_surface_realtime.hlsl
// `clip(texel.a - .5)` in the shading pass and the depth pre-pass; the stock alpha-tested shaders keep 128 / 255,
// research 100), the editor's 0.3 in the other views. sunNear.z = 1 while Game Lighting is on (also in the shadow passes).
float cutoutAt() { return sunNear.z > 0.5 ? 0.5 : 0.3; }
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
cbuffer Batch : register(b1) { float4 batchFlags; float4 batchFlags2; float4 batchMat; };  // x = 1 when t1 holds the material's normal map, y = blend mode, z = sway, w = sway base z; batchFlags2.x = 1 when t3 holds an emissive map
// batchMat (Game Lighting, an own material's shader state, SceneRenderer::TextureMaterial): x = 1 unlit (unlit, additive
// and multiply materials: the colour as is), y = the unlit glow gain (> 1), z = 1 multiply, w = 1 blended glass.
Texture2D emi0 : register(t3);
Texture2D spec0 : register(t29);  // an own material's specular map (batchFlags2.w = 1 on a batch that is not the stock reference)
TextureCube probeCube : register(t5);  // 3.16: the nearest captured reflection probe (batchFlags2.y = 1 when bound)
cbuffer Wind : register(b3) { float4 wind; };         // xy = direction x amplitude, z = wave phase (core foliage_sway)
struct PixelLight { float4 posRadius; float4 colorOuter; float4 dirInner; };
cbuffer Lights : register(b2) { float4 lightCount; PixelLight lights[64]; };
// View > Game Lighting: game0 = (on, LUT input scale, lightmap bound, 0), sun toward, sun colour, unbaked light.
cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; float4 gameFog[6]; float4 gameSkyScale; float4 gameTerrainCut; float4 gameAo;
                              float4 fogK[8]; float4 gameTime; };  // gameTime.x: seconds (fmod 43200, research 133)  // fogK = the game's cb7[131..138] (core game_fog, research 111)
// The stock map's analytic fog, the game's own (research 111: cs_post_opaque_processes PO:39-214 with the exe's
// constants cb7[131..138] in fogK, filled by core gameFogConstants; a 1:1 port of core gameFogEvaluate). The fog
// spline image is zero on the stock maps (their visions name no spline) and is left out. gameFog[0].w = on.
// `sky`: the sky's band of the fog. Owner 2026-11-13: the earlier fitted fog was too bright on mp_rust (sRGB albedos
// used as linear, and a desaturation the game does not do).
float3 gameFogApply(float3 c, float3 w, bool sky) {
    if (gameFog[0].w < 0.5) return c;
    float3 v = w - eyeStart.xyz;
    float dist = length(v);
    float3 dir = v / max(dist, 1e-3);
    float dz = dir.z * dist, skyF = 0.0, path = dist + fogK[1].w;
    if (sky) {
        dz = dir.z * fogK[0].x;
        skyF = saturate(dir.z * fogK[0].y + fogK[0].z);
        path = fogK[0].x;
    }
    float cosSun = dot(dir, fogK[1].xyz);
    float3 T, L;
    if (fogK[6].z < -0.5) {
        float sunF = saturate(cosSun * fogK[6].x + fogK[6].y);
        float tf = max(exp2(-(path * fogK[2].x)), fogK[3].w);
        float ts = max(exp2(-(path * fogK[2].y)), fogK[3].w);
        float t = saturate(sunF * (ts - tf) + tf);
        if (sky) t = 1.0 - fogK[0].w * skyF;
        T = t.xxx;
        L = (sunF * (fogK[3].rgb - fogK[4].rgb) + fogK[4].rgb) * (1.0 - t);
    } else {
        float x = saturate(path * fogK[6].w);
        float kz = dz * fogK[2].w;
        float h = abs(kz) > 1e-6 ? (1.0 - exp2(kz * -1.442695)) / kz * fogK[4].w : fogK[4].w;
        x *= h + fogK[5].w;
        T = exp2(-(x * fogK[2].xyz));
        if (sky) T = T * skyF + (1.0 - skyF);
        float phaseR = (cosSun * cosSun + 1.0) * 0.059683;
        float g = fogK[3].w;
        float m = 1.0 - g * cosSun;
        float phaseM = (1.0 - g * g) * 0.079577 / (m * m);
        L = (fogK[3].rgb * phaseM + fogK[4].rgb * phaseR) * (1.0 - T);
    }
    return c * T + L;
}
// The surface's fog (the stock map's analytic fog, else the own map's exponential one). Both are affine in c:
// fogAny(c) - fogAny(0) = c x T (the transmittance alone, for additive surfaces that only dim).
float3 fogAny(float3 c, float3 w) { return gameFog[0].w > 0.5 ? gameFogApply(c, w, false) : applyFog(c, w); }
Texture2D gameLmA : register(t6);
// The stock reference's lightmap rectangles in its atlas (setStockLightmap): x, y, width, height per lightmap index,
// and the atlas size in .x of lmInfo. A reference vertex carries index + u in its lightmap u.
cbuffer LightmapRects : register(b6) { float4 lmInfo; float4 lmRects[1024]; };
Texture2D gameLmB : register(t7);
Texture2D gameLmD : register(t8);
Texture3D gameLut : register(t9);
// The stock world shader's ggx_env_brdf_lut (A, B by N.V across and gloss down; research 95), when the editor has it.
Texture2D envBrdf : register(t10);
// Its specular occlusion table gtso_lut (research 95, W:796), when the editor has it (gameSun.w = 1).
Texture3D gtsoLut : register(t18);
// The stock reference's reflection probes (setStockProbes): octahedral, world space, 4-texel gutter at every level.
Texture2DArray stockProbes : register(t11);
struct ProbeBox { float4 centerSlice; float4 axisX; float4 axisY; float4 axisZ; float4 originFeather; };
cbuffer Probes : register(b5) { float4 probeCount; float4 probeGridMin; float4 probeGridCell; ProbeBox probeBoxes[128]; };
// A grid over the boxes (setStockProbes): up to four candidate boxes per cell in priority order (255 = none), or the
// nearest box where none covers the cell. probeCount.yzw = grid size, probeGridCell.w = 1 when the grid is bound.
Texture3D<uint4> probeGrid : register(t13);
Texture2D<float> aoMap : register(t14);  // screen-space ambient occlusion (gameAo.x = 1 when drawn this frame)
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
    float nearSlice = probeGridMin.w;  // the fallback probe's image (0 without one): where every box is hidden
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
struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float2 lm : TEXCOORD4;
              float4 edge : TEXCOORD6; };
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 w : TEXCOORD1; float3 n : TEXCOORD2; float3 t : TEXCOORD3; float2 lm : TEXCOORD4;
               float3 lgvBack : TEXCOORD5; float3 edge : TEXCOORD6; float4 vc : TEXCOORD7; };  // vc: a prop's vertex colour (linear; research 133); w = 1 on props
Buffer<uint> tomeVis : register(t30);  // cull by tome: one bit per GfxWorld surface (wind.w = 1 while bound)
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
    o.pos = mul(float4(p, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = p; o.n = i.n; o.t = i.t; o.lm = i.lm;
    o.lgvBack = float3(0, 0, 0);
    o.vc = float4(1, 1, 1, 0);
    o.edge = i.edge.xyz;
    if (wind.w > 0.5 && i.edge.a < 0.5) {
        // A stock reference vertex: its surface + 1 (referenceSurfaceTag). A surface the tome hides goes off screen.
        const uint tag = uint(i.edge.r * 255.0 + 0.5) | (uint(i.edge.g * 255.0 + 0.5) << 8) | (uint(i.edge.b * 255.0 + 0.5) << 16);
        if (tag != 0u) {
            const uint s = tag - 1u;
            if (((tomeVis[s >> 5] >> (s & 31u)) & 1u) == 0u) o.pos = float4(0, 0, 0, -1);
        }
    }
    return o;
}
// Textured instanced reference props: the model's vertices placed by three transform rows per instance.
// A prop mesh's flag / foliage waves (research 133 section 6): constants 79-82 at rows 8..11 of its per-mesh record
// (the same range as cbuffer Layer, bound to b8 for the vertex shader; unbound = zero = no waves).
cbuffer PropWave : register(b8) { float4 pwRows[8]; float4 wave79; float4 wave80; float4 wave81; float4 wave82; };
// The stock props' light grid atlas (research 105 section 1.4; SceneRenderer::setStockLgvAtlas): texture 0 the DC
// (R11G11B10F), texture 1 six tiles of normalised SH bytes (2 x 3), five float4 per box (rows, uvScale, uvBias).
Texture3D<float3> lgvDc : register(t40);
Texture3D<float4> lgvTiles : register(t41);
Buffer<float4> lgvBoxes : register(t42);
SamplerState lgvSamp : register(s4);
// E(n) and E(-n) of a box at world point w (the static-model VS: saturate(M (w, 1)), trilinear, slot j channel c =
// q[3 j + c] x DC_c x sqrt3 | sqrt5; band weights 1, 4/9, 1/16). False when the box is not in the bound buffer.
bool lgvLight(uint box, float3 w, float3 n, out float3 e, out float3 back) {
    uint count = 0;
    lgvBoxes.GetDimensions(count);
    e = float3(0, 0, 0), back = float3(0, 0, 0);
    if (box * 5u + 4u >= count) return false;
    const float4 p = float4(w, 1.0);
    const float3 c = saturate(float3(dot(lgvBoxes[box * 5u], p), dot(lgvBoxes[box * 5u + 1u], p), dot(lgvBoxes[box * 5u + 2u], p)));
    const float3 uvw = c * lgvBoxes[box * 5u + 3u].xyz + lgvBoxes[box * 5u + 4u].xyz;
    const float3 dc = lgvDc.SampleLevel(lgvSamp, uvw, 0);
    float q[24];
    [unroll] for (int t = 0; t < 6; ++t) {
        const float4 v = lgvTiles.SampleLevel(lgvSamp, float3(uvw.x * 0.5 + 0.5 * float(t & 1), (uvw.y + float(t >> 1)) / 3.0, uvw.z), 0) * 2.0 - 1.0;
        q[4 * t] = v.x, q[4 * t + 1] = v.y, q[4 * t + 2] = v.z, q[4 * t + 3] = v.w;
    }
    const float r3 = 1.7320508, r5 = 2.2360680;
    const float3 s0 = float3(q[0], q[1], q[2]) * dc * r3, s1 = float3(q[3], q[4], q[5]) * dc * r5;
    const float3 s2 = float3(q[6], q[7], q[8]) * dc * r3, s3 = float3(q[9], q[10], q[11]) * dc * r3;
    const float3 s4 = float3(q[12], q[13], q[14]) * dc * r5, s5 = float3(q[15], q[16], q[17]) * dc * r5;
    const float3 s6 = float3(q[18], q[19], q[20]) * dc * r5, s7 = float3(q[21], q[22], q[23]) * dc * r5;
    const float3 band1 = 0.217157 * (s0 * n.z - s2 * n.y - s3 * n.x);  // 4/9 x 0.488603
    const float3 band2 = 0.0625 * (0.315392 * (3.0 * n.z * n.z - 1.0) * s1 - 1.092548 * n.x * n.z * s4 - 1.092548 * n.y * n.z * s5 +
                                   0.546274 * (n.x * n.x - n.y * n.y) * s6 + 1.092548 * n.x * n.y * s7);
    e = max(0.282095 * dc + band1 + band2, 0.0);
    back = max(0.282095 * dc - band1 + band2, 0.0);  // E(-n): the second band is even
    return true;
}
struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float4 vcol : COLOR1; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;
                  float4 l0 : INSTROW3; float4 l1 : INSTROW4; float4 l2 : INSTROW5; };
PSIn vs_tex_inst(VSInInst i) {
    float4 p = float4(i.pos, 1.0);
    if (wave80.w != 0 || wave81.w != 0 || wave82.w != 0) {
        // The static-model VS (research 133 section 5.2): speed from the placement's origin, three sines over the clock
        // and the UV, weighted by the vertex colour alpha, along the vertex normal.
        const float s = (i.r0.w + i.r1.w + i.r2.w) / 4096.0;
        const float tri = abs(1.0 - 2.0 * frac(wave79.z * s)) * 2.0 - 1.0;
        const float phase = (wave79.x + wave79.w * tri) * (gameTime.x + wave79.y);
        const float2 q = 1.0 - i.uv;
        const float h = sin(phase * wave80.z + dot(q, wave80.xy)) * wave80.w + sin(phase * wave81.z + dot(q, wave81.xy)) * wave81.w +
                        sin(phase * wave82.z + dot(q, wave82.xy)) * wave82.w;
        p.xyz += h * i.vcol.a * i.n;
    }
    float3 w = float3(dot(i.r0, p), dot(i.r1, p), dot(i.r2, p));
    // The model's vertex normal turned by the instance (uniform scale; zero = none: the pixel shader derives one).
    float3 nw = float3(dot(i.r0.xyz, i.n), dot(i.r1.xyz, i.n), dot(i.r2.xyz, i.n));
    PSIn o; o.pos = mul(float4(w, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = w; o.n = dot(nw, nw) > 1e-12 ? normalize(nw) : float3(0, 0, 0); o.t = float3(0, 0, 0); o.lm = float2(-1, -1); o.edge = float3(1, 0, 0);
    // The vertex colour of a VERTCOL model, stored sRGB: linear as the game's static-model VS (research 133 section 4).
    o.vc = float4(i.vcol.rgb <= 0.04045 ? i.vcol.rgb / 12.92 : pow((i.vcol.rgb + 0.055) / 1.055, 2.4), 1.0);  // w: a prop
    // The prop's light grid box (research 105 section 1.4, DC + first order), per vertex with the vertex normal as the
    // game's static-model shader does: E = 0.282095 DC + 4/9 0.488603 (s0 n.z - s2 n.y - s3 n.x), in lightmap units
    // (the pixel shader applies bakedLightScale). Props have no tangent and no lightmap UV: E rides in t, marked by
    // lm = (-2, -2) (propLight()). DC r < 0: none (the probes' fallback).
    if (i.l0.x < -2.5) {
        // Per vertex from the box in the atlas (research 105 section 1.4); without the atlas, the box centre's DC and
        // slots 0 / 2 kept in the record.
        float3 e, back;
        if (!lgvLight(uint(i.l0.y + 0.5), w, o.n, e, back)) {
            const float3 dc = float3(i.l0.zw, i.l1.x), s0 = i.l1.yzw, s2 = i.l2.xyz;
            e = max(0.282095 * dc + 0.217157 * (s0 * o.n.z - s2 * o.n.y), 0.0);
            back = max(0.282095 * dc - 0.217157 * (s0 * o.n.z - s2 * o.n.y), 0.0);
        }
        o.t = e;
        o.lm = float2(-2, -2);
        o.lgvBack = back;
    } else if (i.l0.x >= 0.0) {
        const float3 dc = i.l0.xyz, s0 = float3(i.l0.w, i.l1.x, i.l1.y), s2 = float3(i.l1.z, i.l1.w, i.l2.x), s3 = i.l2.yzw;
        o.t = max(0.282095 * dc + 0.217157 * (s0 * o.n.z - s2 * o.n.y - s3 * o.n.x), 0.0);
        o.lm = float2(-2, -2);
        o.lgvBack = max(0.282095 * dc - 0.217157 * (s0 * o.n.z - s2 * o.n.y - s3 * o.n.x), 0.0);  // E(-n): transmission
    } else {
        o.lgvBack = float3(0, 0, 0);
    }
    return o;
}
// Sun shadow pass of textured batches: alpha-tested cut-outs (leaves, grates) cast their holes (research 66).

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
cbuffer Layer : register(b7) { float4 layerP; float4 layerTint; float4 layerUvT; float4 layerUvO;
                               float4 matE; float4 matTr; float4 matW; float4 matX;  // matE/Tr/W: research 107 (RefLayer::extra)
                               float4 matRows8[4];  // rows 8-11: a prop's waves (PropWave, VS)
                               float4 matDual0; float4 matDual1; };  // the water's constants 110 / 111 (research 133 section 5.3)
Texture2D tr0 : register(t19);  // the transmission image (texture slot 68, sRGB) when matTr.w = 1
// Wrap diffuse (research 107 section 2.2): w = constant 103 per channel.
float wrapF(float x, float w) {
    w = max(w, 1e-6);
    const float m = max(x, -w) + w;
    return saturate(x < w ? m * m * 3.0 / (4.0 * w * (w * w + 3.0)) : 3.0 * x / (w * w + 3.0));
}
float3 wrap3(float x, float3 w) { return float3(wrapF(x, w.r), wrapF(x, w.g), wrapF(x, w.b)); }
cbuffer Tiles : register(b9) { float4 tileCount; float4 tileBox[256]; };  // the loaded streamed tiles (x0, y0, x1, y1)
// The coarse terrain's hole filler (vertex alpha 253) where a streamed tile is loaded: its geometry fills the hole.
bool tileFillHidden(float4 col, float3 w) {
    if (tileCount.x < 0.5 || abs(col.a - 253.0 / 255.0) > 0.0015 || batchFlags2.z < 0.5) return false;
    const int n = int(tileCount.x);
    for (int k = 0; k < n; ++k)
        if (all(w.xy >= tileBox[k].xy) && all(w.xy <= tileBox[k].zw)) return true;
    return false;
}
// Opaque with the stock specular code in alpha (block-compressed stock images): no alpha test. Editor batches say so
// with blend mode -2, stock batches and props with layerP.y = 2.
bool rawSpecAlpha() { return batchFlags.y < -1.5 || (layerP.x < 0.5 && layerP.y > 1.5); }
// The HDR scene to the frame: the post chain where something was drawn (alpha > 0); the clear colour elsewhere.
Texture2D<float4> hdrScene : register(t15);
// Screen-space reflections (research 104 section 8): the previous frame's HDR scene (half size, mips) and its linear
// depth, with the view they were drawn from.
Texture2D<float4> prevColor : register(t16);
Texture2D<float> prevZ : register(t17);
// The scene depth custom surface shaders may read (core custom_shader iw8rSceneDepth): the frame before, at full
// size, as linear view depth (1e9: sky; 0: off the screen). Unbound (no frame kept yet): Load gives 0, "no depth".
Texture2D<float> iw8rSceneZ : register(t31);
static const float iw8rSceneDepthStep = 1.0 / 1024.0;
float iw8rSceneDepth(float2 pixel) {
    uint zw, zh;
    iw8rSceneZ.GetDimensions(zw, zh);
    if (pixel.x < 0.0 || pixel.y < 0.0 || pixel.x >= float(zw) || pixel.y >= float(zh)) return 0.0;
    return iw8rSceneZ.Load(int3(int2(pixel), 0));
}
cbuffer Prev : register(b10) { row_major float4x4 prevVP; float4 prevFwd; float4 prevSize; };  // prevSize: w, h, valid
struct VFull { float4 pos : SV_POSITION; };
VFull vs_post(uint id : SV_VertexID) {
    VFull o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
// The bloom veil (research 109 section 1.2): its chain's source / add levels, the result (V2) and the post constants:
// veilDst = (dst w, dst h, 1 / w, 1 / h), veilW = (wIn, wOut); postFx0 = (veil, screen w, screen h, film grain); postVig0..2 = the
// vignette (core gameVignetteConstants); postWb0..2 = the white balance matrix's rows (research 120), postWb0.w > 0.5 = on.
Texture2D<float4> veilSrc : register(t25);
Texture2D<float4> veilAdd : register(t26);
Texture2D<float4> veilTex : register(t27);
cbuffer Veil : register(b12) {
    float4 veilDst; float4 veilW; float4 postFx0; float4 postVig0; float4 postVig1; float4 postVig2;
    float4 postWb0; float4 postWb1; float4 postWb2;
};
static const float VR_OUT = 1.886476, VR_IN = 0.939981, VW_C = 0.479925, VW_OUT = 0.075948, VW_IN = 0.302964;
// Tap k at the fixed rotation n = 0.5 (the game rotates per pixel and frame under TAA; the editor has none).
float2 veilTap(int k, float stepA) { const float a = (k + 0.75) * stepA; return float2(sin(a), cos(a)) * ((k & 1) ? VR_IN : VR_OUT); }
float4 ps_veil_down(VFull i) : SV_TARGET {
    const float2 uv = i.pos.xy * veilDst.zw, d = veilDst.zw;
    float3 c = VW_C * veilSrc.SampleLevel(lutSamp, uv, 0).rgb;
    [unroll] for (int k = 0; k < 8; ++k) c += ((k & 1) ? VW_IN : VW_OUT) * veilSrc.SampleLevel(lutSamp, uv + veilTap(k, 0.78539816) * d, 0).rgb;
    return float4(clamp(c * 0.501110, -32255.0, 32255.0), 1.0);
}
float4 ps_veil_karis(VFull i) : SV_TARGET {  // mip 1: the firefly control (each tap weighted 1 / (1 + its max))
    const float2 uv = i.pos.xy * veilDst.zw, d = veilDst.zw;
    float4 s[5];
    s[4] = float4(VW_C * max(veilSrc.SampleLevel(lutSamp, uv, 0).rgb, 0.0), VW_C);
    [unroll] for (int k = 0; k < 4; ++k) {
        const float w = (k & 1) ? VW_IN : VW_OUT;
        s[k] = float4(w * max(veilSrc.SampleLevel(lutSamp, uv + veilTap(k, 1.5707963) * d, 0).rgb, 0.0), w);
    }
    [unroll] for (int j = 0; j < 5; ++j) s[j] *= 1.0 / (1.0 + max(s[j].r, max(s[j].g, s[j].b)));
    float3 r = 0;
    [unroll] for (int g = 0; g < 4; ++g) { const float4 a = s[4] + s[g] + s[(g + 1) & 3]; r += a.rgb / max(a.w, 1e-5); }
    return float4(clamp(0.25 * r, -32255.0, 32255.0), 1.0);
}
float4 ps_veil_up(VFull i) : SV_TARGET {
    const float2 uv = i.pos.xy * veilDst.zw, d = veilDst.zw * 2.0;  // offsets in source texels
    float3 b = VW_C * veilSrc.SampleLevel(lutSamp, uv, 0).rgb;
    [unroll] for (int k = 0; k < 4; ++k) b += ((k & 1) ? VW_IN : VW_OUT) * veilSrc.SampleLevel(lutSamp, uv + veilTap(k, 1.5707963) * d, 0).rgb;
    const float3 add = veilW.y > 0.0 ? veilAdd.SampleLevel(lutSamp, uv, 0).rgb : float3(0, 0, 0);
    return float4(b * 0.807919 * veilW.x + add * veilW.y, 1.0);
}
// The game's volumetric fog (research 109 section 2, a march of its froxel rule per pixel): 32 samples over the 128
// slices of its four cascades (128 / 384 / 640 / 1024 units, 4 to 2180 units), each sigma = air x 0.01 x the height
// falloff, the sun's light by the shadow map and the Henyey-Greenstein phase clipped at 1, the ambient per slice
// without the slice thickness (as the game's shader), then out = scene x T + S. The sky takes the whole volume.
Texture2D<float> volDepth : register(t28);
cbuffer Volume : register(b13) { row_major float4x4 volInvVP; float4 volEye; float4 volSunDir; float4 volSunRad; float4 volAlbedo;
                                 float4 volP; float4 volQ; float4 volAmb;
                                 float4 volBoxInfo;      // x: boxes in use (0..8)
                                 float4 volBox[40];      // per box 5 rows: centre + density, three axes + half sizes,
                                                         // (1 / falloff, -0.1 x height fade, 0, 0)
                                 float4 volLampInfo;     // x: lamps in use (0..8), y: spot brightness, z: omni brightness
                                 float4 volLamp[24];     // per lamp 3 rows, as PixelLight: position + radius, colour + cos
                                                         // outer (below -1.5: omni), axis + cos inner
                                 float4 volBoxMask[40]; };  // per box 5 rows: a code per mask (picture x 4 + axis, below 0:
                                                            // none), then per mask 1 / sizeU, 1 / sizeV, the two offsets
// The mask pictures of the volumetric boxes (research 127 section A.2): one grey picture per slice, repeated.
Texture2DArray<float> volMask : register(t24);
SamplerState volMaskSamp : register(s3);
// The light of the flagged lamps in the air at a point, seen along V (research 126 section 1.4): each lamp's light
// there (the falloff and cone it has on surfaces), the phase toward the eye, pi, and the vision's lamp brightness.
float3 volLampLight(float3 P, float3 V, float g) {
    float3 sum = 0.0;
    const int n = int(volLampInfo.x);
    [loop] for (int k = 0; k < n; ++k) {
        const float3 dl = volLamp[k * 3].xyz - P;
        const float dist = length(dl);
        const float radius = volLamp[k * 3].w;
        if (dist >= radius) continue;
        const float3 l = dl / max(dist, 1e-3);
        const bool omni = volLamp[k * 3 + 1].w < -1.5;
        float cone = 1.0;
        if (!omni)
            cone = saturate((dot(-l, volLamp[k * 3 + 2].xyz) - volLamp[k * 3 + 1].w) / max(1e-4, volLamp[k * 3 + 2].w - volLamp[k * 3 + 1].w));
        const float w = saturate(1.0 - dist / radius) * cone;
        const float cth = dot(V, -l);  // the light travels from the lamp to P, then along -V to the eye
        const float ph = min((1.0 - g * g) / (12.566371 * pow(max(1.0 + g * g + 2.0 * g * cth, 1e-4), 1.5)), 1.0);
        sum += volLamp[k * 3 + 1].rgb * (w * w * (3.0 - 2.0 * w) / max(dist * dist, 1.0)) * ph * 3.141593 * (omni ? volLampInfo.z : volLampInfo.y);
    }
    return sum;
}
// The fog a volumetric box adds at a point (research 127 section A.2): extinction per unit.
float volBoxSigma(float3 P) {
    float sum = 0.0;
    const int n = int(volBoxInfo.x);
    [loop] for (int b = 0; b < n; ++b) {
        const float3 rel = P - volBox[b * 5].xyz;
        const float3 local = float3(dot(rel, volBox[b * 5 + 1].xyz), dot(rel, volBox[b * 5 + 2].xyz), dot(rel, volBox[b * 5 + 3].xyz));
        const float3 halfSize = float3(volBox[b * 5 + 1].w, volBox[b * 5 + 2].w, volBox[b * 5 + 3].w);
        if (any(abs(local) > halfSize)) continue;
        const float invFalloff = volBox[b * 5 + 4].x;
        // 0 in the core, 1 at a face; the corners are rounded.
        const float3 q = max(abs(local) * invFalloff - (halfSize * invFalloff - 1.0), 0.0);
        // Each mask multiplies: the picture lies on the two box axes other than the mask's own, v turned over.
        float m = 1.0;
        const float4 codes = volBoxMask[b * 5];
        [loop] for (int k = 0; k < 4; ++k) {
            const float code = codes[k];
            if (code < 0.0) continue;
            const int axis = int(code) & 3;
            const float2 uv = axis == 0 ? local.yz : (axis == 1 ? local.xz : local.xy);
            const float4 t = volBoxMask[b * 5 + 1 + k];
            m *= volMask.SampleLevel(volMaskSamp, float3(uv.x * t.x + t.z, -uv.y * t.y + t.w, floor(code * 0.25)), 0);
        }
        sum += volBox[b * 5].w * saturate(1.0 - length(q)) * exp(volBox[b * 5 + 4].y * (local.z + halfSize.z)) * m;
    }
    return sum;
}
float3 volumetricFog(float2 px, float3 c) {
    if (volQ.z < 0.5) return c;
    const float z = volDepth.Load(int3(px, 0));
    const float2 ndc = float2(px.x / max(postFx0.y, 1.0) * 2.0 - 1.0, 1.0 - px.y / max(postFx0.z, 1.0) * 2.0);
    const float4 wp = mul(float4(ndc, z, 1.0), volInvVP);
    float3 V = wp.xyz / wp.w - volEye.xyz;
    float D = length(V);
    V /= max(D, 1e-3);
    if (z >= 0.999999) D = 1e9;
    const float g = volQ.y, cth = dot(V, volSunDir.xyz);
    const float ph = min((1.0 - g * g) / (12.566371 * pow(max(1.0 + g * g - 2.0 * g * cth, 1e-4), 1.5)), 1.0);
    float T = 1.0;
    float3 S = 0;
    [loop] for (int k = 0; k < 32; ++k) {
        const float s = (k * 4 + 2) / 128.0;
        const float d = 4.0 + dot(saturate((s - float4(0.0, 0.25, 0.5, 0.75)) * 4.0), float4(128, 384, 640, 1024));
        if (d > D) break;
        const float dz = (k < 8 ? 128.0 : k < 16 ? 384.0 : k < 24 ? 640.0 : 1024.0) / 32.0 * 4.0;
        const float3 P = volEye.xyz + V * d;
        const float sig = volAlbedo.w * exp(max(P.z - volP.z, 0.0) * volP.w) + volBoxSigma(P);
        const float3 sun = (sunShadow(P) * volQ.x * ph * 3.141593 * volSunRad.rgb + volLampLight(P, V, g)) * dz;
        const float3 amb = volP.y * volAmb.rgb * 4.0;
        S += T * volAlbedo.rgb * sig * (sun + amb);
        T *= exp(-(sig + volP.x) * dz);
    }
    return c * T + S;
}
float4 ps_post(VFull i) : SV_TARGET {
    float4 h = hdrScene.Load(int3(i.pos.xy, 0));
    if (h.a <= 0.0) discard;
    if (gameAo.y > 0.5 || gameAo.z > 0.5 || (gameSkyScale.z > 0.5 && gameSkyScale.z < 1.5)) return float4(h.rgb, 1.0);  // debug views
    float3 c = volumetricFog(i.pos.xy, max(h.rgb, 0.0));
    const float2 suv = i.pos.xy / max(postFx0.yz, 1.0);
    // The veil (research 109 section 1.3): a lerp toward the very wide blur, in linear HDR before the exposure.
    if (postFx0.x > 0.0) c = (1.0 - postFx0.x) * c + postFx0.x * clamp(veilTex.SampleLevel(lutSamp, suv, 0).rgb, 0.0, 32255.0);
    // The vignette (section 4.1), after the veil, in linear HDR.
    if (postVig0.z > 0.0) {
        const float2 p = (suv - 0.5) * postVig0.xy + postVig1.xy;
        const float r = length(p - clamp(p, -postVig1.zw, postVig1.zw));
        const float t = saturate(r * postVig2.x + postVig2.y);
        c *= 1.0 - (t + 0.6 * (t * t * (3.0 - 2.0 * t) - t)) * postVig0.z;
    }
    // The white balance (research 120): the map's 3x3 matrix on the linear colour, after the vignette and before the
    // exposure (cs_tonemap CB0[0..2]). Kept at 0 or above for the log shaper.
    if (postWb0.w > 0.5) c = max(float3(dot(postWb0.xyz, c), dot(postWb1.xyz, c), dot(postWb2.xyz, c)), 0.0);
    float3 shown = gamePost(c);
    if (postFx0.w > 0.0) {
        // Film grain (research 109 section 4.2: the game's filmgrain shader): white noise times the display colour,
        // none on black, fading toward white. The game moves the noise with time; here it stands still.
        const float T = 0.7310586;
        const float ga = frac(suv.y * T) * 1024.0 - 512.0, gb = frac(suv.x * T) * 1024.0 - 512.0;
        const float pp = gb * frac(gb * 0.554550) + ga, qq = ga * frac(ga * 0.308517) + gb;
        const float noise = frac(pp * qq) * 2.0 - 1.0;
        const float med = max(min(max(shown.r, shown.g), shown.b), min(shown.r, shown.g));
        const float k = med + 0.1 * saturate(1.0 - med);
        shown += shown * noise * max(1.0 - k * k, 0.0) * postFx0.w;
    }
    return float4(shown, 1.0);
}
void ps_shadow_clip(PSIn i) {
    if (!rawSpecAlpha()) clip(tex0.Sample(samp0, i.uv).a - cutoutAt());
}
// The depth pre-pass: the shading pass's own cut-out threshold and the coarse terrain cut, so the depths match.
void ps_prepass(PSIn i) {
    if (tileFillHidden(i.col, i.w)) discard;
    if (batchFlags2.z < -0.5 && dot(i.n, i.n) > 1e-6 && dot(i.n, eyeStart.xyz - i.w) < 0.0) discard;  // G11, as ps_tex
    if (!rawSpecAlpha()) clip(tex0.Sample(samp0, i.uv).a - cutoutAt());
    if (gameTerrainCut.w > 0.5 && batchFlags2.z > 0.5 && i.col.a > 0.99 && i.col.a < 0.998 && distance(i.w.xy, gameTerrainCut.xy) < gameTerrainCut.z) discard;
}
// Stock local lights (research 106): Replay's packed records (96 bytes), the nearest 256 in view; per 32-pixel screen
// tile 8 words of bits (bit k = record k). localInfo = (on, tiles across, tile size, IES strip bound).
struct GameLight { float3 diffuse; float collimationOffset; float3 dir; uint packed; float3 bulbDir; float bulbRadius;
                   float3 pos; float invRadius; float4 spot; float3 uvIrHeat; uint iesLookup; };
StructuredBuffer<GameLight> gameLights : register(t20);
Texture1D<float> iesStrip : register(t21);
StructuredBuffer<uint> lightTiles : register(t22);
cbuffer LocalLights : register(b11) { float4 localInfo; };
// Their shadow maps (research 106 section 4): per slot the light's view-projection and (near, far, k, bias); depth t24.
struct LightShadowSlot { row_major float4x4 vp; float4 p; };
StructuredBuffer<LightShadowSlot> lightShadowSlots : register(t23);
Texture2DArray<float> lightShadowMaps : register(t24);
float gameLocalShadow(uint slot, float3 P) {
    const LightShadowSlot s = lightShadowSlots[slot];
    const float4 c = mul(float4(P, 1.0), s.vp);
    if (c.w <= 1e-3) return 1.0;
    const float2 uv = c.xy / c.w * float2(0.5, -0.5) + 0.5;
    if (any(uv <= 0.0) || any(uv >= 1.0)) return 1.0;
    const float n = s.p.x, f = s.p.y, k = s.p.z, b = s.p.w;
    const float zr = (f - c.w) / (f - n);  // the receiver's reversed linear depth, as the stored one
    const float2 texel = 1.0 / 512.0;
    float sum = 0.0;
    [unroll] for (int q = 0; q < 4; ++q) {
        const float2 o = float2(q & 1 ? 0.5 : -0.5, q & 2 ? 0.5 : -0.5) * texel;
        const float d = lightShadowMaps.SampleLevel(lutSamp, float3(uv + o, slot), 0);
        const float z = n * f / max(f - d * (f - n), 1e-4);
        const float zs = (f - z) / (f - n);
        sum += saturate((exp(k * (zr - zs)) - b) / (1.0 - b));
    }
    return sum * 0.25;
}
float3 glSegPoint(GameLight l, float3 p) {
    return l.pos + clamp(dot(p - l.pos, l.bulbDir) / max(dot(l.bulbDir, l.bulbDir), 1e-20), -1.0, 1.0) * l.bulbDir;
}
// One light (research 106 section 3.2 / 5.3, world space): tube closest point, spot apex, IES, smoothstep window,
// bulb-capped diffuse at a representative point, GGX specular toward the tube with the sphere-light normalisation.
// wrap: the material's wrap diffuse (rgb, .w = 1 when it has one); trans: its transmission colour (.w = 1 when it has
// one). Those two kinds of material take the lamp's diffuse light by the wrap rule (research 107 section 2.2), and a
// transmission material also lets the lamp shine through (section 1).
void gameLocalLight(GameLight l, float3 P, float3 N, float3 V, float3 diffuseColor, float3 f0, float gp, float cavity,
                    float4 wrap, float4 trans, inout float3 diffSum, inout float3 specSum) {
    const float3 Q = glSegPoint(l, P), v = Q - P;
    const float d = length(v);
    if (d * l.invRadius >= 1.0 || d < 1e-4) return;
    const float3 L = v / d;
    const bool isSpot = ((l.packed >> 8) & 3u) == 2u;
    const float3 Lc = isSpot ? normalize(Q + l.dir * l.spot.z - P) : L;
    const float front = (!isSpot || dot(v, l.dir) > 0.0) ? 1.0 : 0.0;
    const float cosA = dot(l.dir, Lc), s2a = 1.0 - min(cosA * cosA, 1.0);
    const float u = cosA > 0.0 ? 0.5 * s2a : 1.0 - 0.5 * s2a;
    float prof = 1.0;
    if (localInfo.w > 0.5 && l.iesLookup != 0u) {
        const float2 ies = f16tof32(uint2(l.iesLookup & 0xffffu, l.iesLookup >> 16));
        prof = iesStrip.SampleLevel(lutSamp, ies.x + u * ies.y, 0);
    }
    const float spotF = saturate(cosA * l.spot.x + l.spot.y);
    const float w = saturate(l.spot.w * (1.0 - d * l.invRadius)) * (isSpot ? spotF : 1.0);
    float att = prof * front * w * w * (3.0 - 2.0 * w);
    if (att <= 0.0) return;
    // Its shadow map (packed bits 0-7 = slot): spots over the whole cone, shadowed omnis inside their cone only.
    const uint slot = l.packed & 0xffu;
    if (slot != 0xffu) att *= lerp(1.0, gameLocalShadow(slot, P), isSpot ? 1.0 : spotF);
    if (att <= 0.0) return;
    const float3 X = P + (N + L) * (d / max(1.0 + dot(N, L), 1e-3));
    const float3 vd = glSegPoint(l, X) - P;
    const float dd = max(length(vd), 1e-4);
    const float3 Ld = vd / dd;
    const float NdotL = dot(N, Ld), s2 = l.bulbRadius * l.bulbRadius / (dd * dd), y = NdotL * rsqrt(s2);
    // Plain materials: the bulb-capped irradiance with its horizon term, for the diffuse and the specular alike.
    float Es = att * ((1.0 - y * y > 0.0) ? pow(saturate(s2 * (0.5 + 0.5 * y)), 1.5) : saturate(NdotL * s2));
    float3 Ed = Es;
    const bool wrapKind = wrap.w > 0.5 || trans.w > 0.5;
    float cap = 1.0;
    if (wrapKind) {
        // Wrap and transmission materials: the cap by the distance to the bulb, the wrap merged with the bulb's horizon
        // softening, and on wrap materials the falloff x shadow product raised per channel as the sun's shadow is.
        const float dc = d + l.collimationOffset;
        cap = saturate(l.bulbRadius * l.bulbRadius / max(dc * dc, 1e-8));
        const float sb = l.bulbRadius / dd, h = 0.77 * sb + 0.33 * sb * sb;
        const float3 w0 = wrap.w > 0.5 ? wrap.rgb : float3(0, 0, 0);
        const float3 wq = clamp(sqrt(w0 * w0 + h * h), 1e-6, 1.25);
        float3 attC = att;
        if (wrap.w > 0.5) {
            const float mw = min(w0.r, min(w0.g, w0.b));
            const float3 kw = 1.0 + 1.35 * (w0 - mw);
            attC = pow(saturate(att), float3(kw.b * kw.g, kw.b * kw.r, kw.g * kw.r));
        }
        Ed = attC * cap * wrap3(NdotL, wq);
        Es = att * cap * saturate(NdotL);
    }
    const float E = Es;
    if (E <= 0.0 && max(Ed.r, max(Ed.g, Ed.b)) <= 0.0 && trans.w < 0.5) return;
    const float3 R = reflect(-V, N), QP = Q - P;
    const float aR = dot(l.bulbDir, R);
    float t = clamp((aR * dot(R, QP) - dot(l.bulbDir, QP)) / max(dot(l.bulbDir, l.bulbDir) - aR * aR, 1e-12), -1.5, 1.5);
    t -= t * t * t * (4.0 / 27.0);
    const float3 S = QP + t * l.bulbDir, perp = R * dot(S, R) - S;
    float x = l.bulbRadius * rsqrt(max(dot(perp, perp), 1e-8));
    x *= rsqrt(x * x + 0.125);
    const float3 Hs = normalize(normalize(S + perp * x) + V);
    const float a2 = 2.0 / (2.0 + exp2(20.0 * gp)), NdotH = dot(N, Hs), NdotV = saturate(dot(N, V));
    const float dq = NdotH * NdotH * (a2 - 1.0) + 1.0;
    const float D = a2 / max(dq * dq, 1e-8);
    const float Vis = 1.0 / ((abs(NdotL) + sqrt(a2 + (1.0 - a2) * NdotL * NdotL)) * (NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV)));
    const float ap = pow(a2, 0.748925), norm = ap / (ap + 0.375892 * l.bulbRadius * rsqrt(max(dot(S, S), 1e-8)));
    const float3 F = f0 + (1.0 - f0) * pow(1.0 - abs(dot(V, normalize(Ld + V))), 5.0);
    const float2 AB = gameEnvBrdf(saturate(NdotL), gp);
    const float3 Fl = f0 * AB.x + AB.y;
    diffSum += diffuseColor * l.diffuse * Ed * (1.0 - max(Fl.r, max(Fl.g, Fl.b)));
    specSum += l.diffuse * F * (D * Vis * norm * E * cavity);
    if (trans.w > 0.5) {
        // The lamp through the surface: lit from behind (N.L < 0), the lamp's point picked along the eye ray continued
        // through the surface, a GGX lobe of the surface's roughness about -N, the sphere-light energy factor.
        const float nlb = -NdotL;
        if (nlb > 0.0 && att > 0.0) {
            const float3 Rb = -V;
            const float aB = dot(l.bulbDir, Rb);
            float tb = clamp((aB * dot(Rb, QP) - dot(l.bulbDir, QP)) / max(dot(l.bulbDir, l.bulbDir) - aB * aB, 1e-12), -1.5, 1.5);
            tb -= tb * tb * tb * (4.0 / 27.0);
            const float3 Sb = QP + tb * l.bulbDir, perpB = Rb * dot(Sb, Rb) - Sb;
            float xb = l.bulbRadius * rsqrt(max(dot(perpB, perpB), 1e-8));
            xb *= rsqrt(xb * xb + 0.125);
            const float3 Hb = normalize(normalize(Sb + perpB * xb) - R);
            const float hb = dot(-N, Hb), qb = hb * hb * (a2 - 1.0) + 1.0;
            const float Db = a2 / max(qb * qb, 1e-8);
            const float Vb = 1.0 / ((nlb + sqrt(a2 + (1.0 - a2) * nlb * nlb)) * (NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV)));
            const float normB = ap / (ap + 0.375892 * l.bulbRadius * rsqrt(max(dot(Sb, Sb), 1e-8)));
            const float2 ABb = gameEnvBrdf(saturate(nlb), gp);
            const float3 Flb = f0 * ABb.x + ABb.y;
            diffSum += l.diffuse * trans.rgb * (att * cap * nlb * Db * Vb * normB * (1.0 - max(Flb.r, max(Flb.g, Flb.b))));
        }
    }
}
Texture2D revealTex : register(t12);
// vs_tex_inst: a prop lit from its light grid box carries that light in t (no tangent) and lm = (-2, -2).
bool propLit(PSIn i) { return i.lm.x < -1.5; }
float4 ps_tex(PSIn i) : SV_TARGET {
    // Research 132 G11: an own one-sided material is not drawn from behind (the build culls its back faces).
    if (batchFlags2.z < -0.5 && dot(i.n, i.n) > 1e-6 && dot(i.n, eyeStart.xyz - i.w) < 0.0) discard;
    // UV animation (research 133 section 1, constant 63 in matX.yzw): turn about the texture centre, then scroll; every
    // texture of the material reads the animated UV. Stock reference batches only (the plain layer buffer is zero).
    if (batchFlags2.z > 0.5 && any(matX.yzw != 0)) {
        float sa, ca;
        sincos(gameTime.x * matX.w, sa, ca);
        const float2 d = i.uv - 0.5;
        i.uv = float2(ca * d.x - sa * d.y, sa * d.x + ca * d.y) + 0.5 + gameTime.x * matX.yz;
    }
    const float3 propLgv = propLit(i) ? i.t : float3(0, 0, 0);
    if (propLit(i)) i.t = float3(0, 0, 0);  // no vertex tangent: the normal map frame from the screen derivatives
    // The coarse super terrain (vertex alpha 254) inside the near-camera layer patch: the patch draws the ground there.
    if (gameTerrainCut.w > 0.5 && batchFlags2.z > 0.5 && i.col.a > 0.99 && i.col.a < 0.998 && distance(i.w.xy, gameTerrainCut.xy) < gameTerrainCut.z) discard;
    if (tileFillHidden(i.col, i.w)) discard;
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
        // The tangent's length is 1 + the weight on the world; a model layer's weight is 1 (research 107 section 3.2).
        float w = i.vc.w > 0.5 ? 1.0 : saturate(length(i.t) - 1.0), k = layerP.z;
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
    // Cut-outs: the stock alpha-tested shaders keep opacity above 128 / 255 (research 100) and the build cuts own
    // materials at .5 too: Game Lighting cuts every batch at 0.5, the other views at the editor's 0.3 (cutoutAt).
    if (batchFlags.y < 0.5 && !rawSpecAlpha()) {
        const float cutAt = cutoutAt();
        if (batchFlags.y < -0.5) {
            // Placed props (blend mode -1): the cut-out edge is anti-aliased from the alpha's screen slope and blended,
            // depth still written. The game's TAA smooths its sub-pixel grass blades the same way; a hard cut leaves
            // speckled noise (Speedball grass: 2.7x the in-game pixel-to-pixel contrast).
            const float cov = saturate((t.a - cutAt) / max(fwidth(t.a), 1e-4) + 0.5);
            clip(cov - 1.0 / 255.0);
            outA = cov;
        } else {
            clip(t.a - cutAt);
        }
    } else if (batchFlags.y > 0.5) {
        // Game Lighting: the build's coverage, alpha = texel.a (map_surface_realtime.hlsl 1770; a blended material
        // with alpha 1 is opaque there). The other views keep a mask-less blended surface at 60 percent.
        outA = game0.x > 0.5 ? t.a : (t.a > 0.99 ? 0.6 : t.a);
    }
    if (layerB >= 0.0) outA = layerB;
    // Colour mode multiply (layerP.w = 1; puddles, edge wear): below x lerp(1, layer, b), drawn with a multiplying blend
    // over the shaded result (in display space: the factor's 1/2.2 power, as the post chain is close to a gamma there).
    if (layerP.x > 0.5 && layerP.w > 0.5) {
        const float3 mulLin = lerp(float3(1, 1, 1), srgbToLinear3(t.rgb), layerB);
        return float4(game0.x > 0.5 && gameAo.w > 0.5 ? mulLin : pow(mulLin, 1.0 / 2.2), 1.0);  // HDR: a linear multiply
    }
    if (game0.x > 0.5) {
        // View > Game Lighting: map_surface_realtime.hlsl (the converter's world shader, iw3 style) and the post chain.
        // batchFlags2.z: the stock reference world (its vertex colours are an editor shade, not a tint), .w: its own
        // lightmap is bound (the stock world shader's formula, tangent-space direction).
        const bool stockRef = batchFlags2.z > 0.5;
        // Screen-space ambient occlusion: the indirect diffuse (multi-bounce, as the game) and the specular occlusion.
        const float aoV = gameAo.x > 0.5 ? lerp(1.0, aoMap.Load(int3(i.pos.xy, 0)), gameSunCol.w) : 1.0;  // x the AO strength
        if (gameAo.y > 0.5) return float4(aoV, aoV, aoV, 1.0);
        if (gameAo.z > 0.5) return float4(t.rgb, outA);  // debug: the albedo as sampled
        float3 albedo = srgbToLinear3(t.rgb) * (stockRef ? i.vc.rgb : i.col.rgb);  // i.col: selection and brush entity tints; vc: a prop's vertex colour
        if (!stockRef && i.col.a < 0.5) {  // sky box
            // t.a: an RGBM face's multiplier (research 132 G10; 1 on an 8-bit face), the range folded into the scale.
            const float3 sky = gameFogApply(albedo * t.a * gameSkyScale.x, i.w, true);
            return float4(gameAo.w > 0.5 ? sky : gamePost(sky), 1.0);
        }
        if (!stockRef && (batchMat.x > 0.5 || batchMat.z > 0.5)) {
            // An own unlit, additive or multiply material (map_surface_realtime.hlsl 1626-1640): light = 1, or the
            // unlit glow gain above 1 (MAP_EMISSION); no sun, lamps or specular.
            if (batchMat.z > 0.5) {
                // Multiply (MSR 1871-1881): what is behind x lerp(1, colour, the fog's transmittance), with the
                // multiplying blend state; on the HDR scene a linear multiply, else in display terms.
                const float3 T = fogAny(float3(1, 1, 1), i.w) - fogAny(float3(0, 0, 0), i.w);
                const float3 m = lerp(float3(1, 1, 1), albedo, dot(T, float3(1, 1, 1) / 3.0));
                return float4(gameAo.w > 0.5 ? m : pow(m, 1.0 / 2.2), 1.0);
            }
            float3 cu = albedo * (batchMat.y > 1.0 ? batchMat.y : 1.0);
            // Additive (MSR 1887-1896): colour x alpha, dimmed by the fog (no in-scatter of its own).
            cu = batchFlags.y > 1.5 ? fogAny(cu, i.w) - fogAny(float3(0, 0, 0), i.w) : fogAny(cu, i.w);
            if (gameAo.w < 0.5) cu = gamePost(cu);
            if (batchFlags.y > 1.5) return float4(cu * outA, 1.0);
            return float4(cu, outA);
        }
        if (stockRef && matX.x > 0.5) {
            // A stock `unlit_` material (research 132 G2): its colour as is, x the emissive image and constant when it has
            // them (research 107 section 5), fogged and exposed like the rest; no lightmap, probes, sun or lamps.
            float3 cu = albedo;
            if (matE.w > 0.5) cu = srgbToLinear3(emi0.Sample(samp0, uvS).rgb) * matE.rgb;
            cu = fogAny(cu, i.w);
            if (gameAo.w < 0.5) cu = gamePost(cu);
            if (batchFlags.y > 1.5) return float4(cu * outA, 1.0);
            return float4(cu, outA);
        }
        float3 Nf = dot(i.n, i.n) > 0.5 ? normalize(i.n) : normalize(cross(ddx(i.w), ddy(i.w)));
        // A normal from the screen derivatives has no reliable side (instanced props have none of their own): it faces
        // the viewer.
        // A face seen from behind is lit with its normal turned toward the viewer: the stock two-sided foliage shaders
        // negate the tangent frame on back faces (research 100), and back-lit grass cards were dark speckles.
        if (dot(Nf, eyeStart.xyz - i.w) < 0) Nf = -Nf;
        float3 N = Nf;
        // A stock base batch with its material constants (layerP.x = 0, layerUvO.y = 1; research 95): gloss from the
        // normal map's red channel, cavity from its blue, the normal scaled by the material's strength.
        const bool baseMat = layerP.x < 0.5 && layerUvO.y > 0.5;
        float nmGloss = -1.0, nmCavity = 1.0, dualGloss = -1.0;
        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
            float4 nm = nrm0.Sample(samp0, uvS);
            if (baseMat) {
                nmGloss = nm.r, nmCavity = nm.b;
                nm.ga = (nm.ga - 0.5) * layerUvO.x + 0.5;
            }
            float2 sd = nm.ga * 2.0 - 1.0;
            float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
            // The stock decode (research 95, W:425-428): the octahedral direction normalised, its z replaced by
            // 1 - 0.6 |xy|^2 (the normal map's relief is a little flatter than the plain decode).
            float2 nxy = normalize(float3(oct, max(1.0 - abs(oct.x) - abs(oct.y), 0.0))).xy;
            if (stockRef && baseMat && (matDual0.x > 0.0 || matDual1.x > 0.0)) {
                // The water's two scrolling layers (research 133 section 5.3, PS_9786960c:138-187): uv x scale + time x
                // scroll each, decoded alike, faded as the mip nears the last, summed by weight; gloss and cavity summed.
                uint nw, nh, levels;
                nrm0.GetDimensions(0, nw, nh, levels);
                float2 sum = 0;
                nmGloss = 0, nmCavity = 0;
                [unroll] for (int k = 0; k < 2; ++k) {
                    const float4 c = k == 0 ? matDual0 : matDual1;
                    const float2 uvK = uvS * c.y + gameTime.x * c.zw;
                    float4 nk = nrm0.Sample(samp0, uvK);
                    const float fade = saturate((float(levels) - 1.0 - nrm0.CalculateLevelOfDetail(samp0, uvK)) * 0.5);
                    nmGloss += nk.r * c.x, nmCavity += nk.b * c.x;
                    nk.ga = (nk.ga - 0.5) * layerUvO.x + 0.5;
                    const float2 sk = nk.ga * 2.0 - 1.0;
                    const float2 ok = float2(sk.x + sk.y, sk.x - sk.y) * 0.5;
                    sum += normalize(float3(ok, max(1.0 - abs(ok.x) - abs(ok.y), 0.0))).xy * fade * c.x;
                }
                nxy = sum;
                // Each layer's gloss remapped, then summed by weight and saturated (the water is near-mirror).
                dualGloss = saturate(nmGloss * layerTint.z + layerTint.w * (matDual0.x + matDual1.x));
            }
            const float3 nT = normalize(float3(nxy, 1.0 - 0.6 * dot(nxy, nxy)));
            float2 slope = nT.xy / max(nT.z, 0.02);
            float3 Tt = i.t - Nf * dot(i.t, Nf);
            float3 T, B;
            if (dot(Tt, Tt) > 1e-8) {
                T = normalize(Tt), B = cross(Nf, T);
            } else {
                // No vertex tangent (placed props): the texture frame from the screen derivatives.
                float3 dp1 = ddx(i.w), dp2 = ddy(i.w);
                float2 du1 = ddx(uvS), du2 = ddy(uvS);
                float3 p2 = cross(dp2, Nf), p1 = cross(Nf, dp1);
                T = p2 * du1.x + p1 * du2.x, B = p2 * du1.y + p1 * du2.y;
                float sc = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
                T *= sc, B *= sc;
            }
            N = normalize(T * slope.x + B * slope.y + Nf);
        }
        // Without a lightmap texel (props, models, new brushes): on a stock map the game lights them from its light grid;
        // here from the reflection probes' blurriest level (luminance-normalised) at the map's mean baked light
        // (gameSky.rgb, gameSky.w = 1); otherwise the unbaked light.
        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));  // the probes' light, below, where no lightmap
        // An own map built without lightmaps (gameSky.w = 2): the converter's world shader gives every surface this
        // constant sky fill by the vertex normal (map_surface_realtime.hlsl, "Soft hemispherical sky fill"; research 117).
        if (gameSky.w > 1.5) indirect = lerp(float3(0.22, 0.23, 0.24), float3(0.38, 0.41, 0.44), saturate(Nf.z * 0.5 + 0.5));
        float3 stockDw = Nf, stockB = 0;  // the stock lightmap's directional part, for E(R) below
        float3 stockDirN = 0;  // that part as it entered `indirect` (wrapped on wrap-diffuse materials)
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
                stockDirN = b * (matW.w > 0.5 ? wrap3(dot(N, Dw), matW.rgb) : saturate(dot(N, Dw)).xxx);
                indirect = a.rgb + stockDirN;
                stockDw = Dw, stockB = b;
            } else {
                indirect = a.rgb * saturate(dot(N, Nf)) + b * saturate(dot(normalize(d + 1e-6), N));
                visibility = a.a;
            }
        }
        // Only where no lightmap lit the point: the probes' fallback light (it cost every lightmapped pixel two probe
        // lookups that were thrown away).
        if (!lmOk && stockRef && propLit(i)) {
            indirect = propLgv * gameSkyScale.y;  // the prop's light grid box (vs_tex_inst), x bakedLightScale
        } else if (!lmOk && !stockRef && propLit(i)) {
            // A model of an own map: the light the build's light grid gives it at its place, by the triangle's normal
            // (SceneCache::modelLights; the converter's InstanceAmbient).
            indirect = propLgv;
        } else if (!lmOk && gameSky.w > 0.5 && gameSky.w < 1.5 && probeCount.x > 0.5)
            indirect = stockProbeLight(i.w, N, 0.0) * dot(gameSky.rgb, float3(0.2126, 0.7152, 0.0722));
        // The sun: the baked sun depth where it covers the point (here the sun shadow map), else the lightmap's mask.
        if (litParams.y > 0.5) visibility = sunShadow(i.w);
        float3 c;
        if (stockRef) {
            // The stock world PS (research 95 sections 2 "Ambient combine" and "Sun"): a dielectric (F0 .04) of the
            // material's default gloss (the reference has no gloss maps here), retro-reflective diffuse kd, the
            // environment term from the lightmap along the reflection (probes later) and the GGX sun.
            float3 Vs = normalize(eyeStart.xyz - i.w);
            // The material (research 95 "Surface decode"): s = alpha x spec scale + bias (the colour alpha keeps the
            // code as 192..254 on opaque stock images), metal = sat(s - .1) / .9, f0 = metal x albedo + min(s, .1);
            // gloss = red x gloss scale + bias. Without constants: a dielectric of gloss .3.
            float3 f0 = 0.04;
            float glossM = 0.3;
            float3 diffAlbedo = albedo;
            if (baseMat) {
                // The specular code: raw in a block-compressed image (layerP.y = 2), 192..254 in an RGBA one (1).
                const float aCode = layerP.y > 1.5 ? t.a : layerP.y > 0.5 && t.a < 0.999 ? saturate((t.a * 255.0 - 192.0) / 62.0) : -1.0;
                if (aCode >= 0.0) {
                    const float sCode = aCode * layerTint.x + layerTint.y;
                    const float metal = saturate(sCode - 0.1) * 1.111111;
                    f0 = metal * albedo + min(sCode, 0.1);
                    diffAlbedo = albedo * max(1.0 - metal, 0.0);
                }
                if (nmGloss >= 0.0) glossM = saturate(nmGloss * layerTint.z + layerTint.w);
                if (dualGloss >= 0.0) glossM = dualGloss;
            }
            const float gp = 1.0 - sqrt(1.0 - glossM);
            float a2 = 2.0 / (2.0 + exp2(20.0 * gp));
            float nv = saturate(dot(N, Vs));
            float2 ab = gameEnvBrdf(nv, gp);
            float3 Fenv = f0 * ab.x + ab.y;
            float3 R = reflect(-Vs, N);
            float3 Lr = (indirect - stockDirN) + stockB * saturate(dot(R, stockDw));
            float tq = ((-0.442435 * gp + 1.7905) * gp - 2.24983) * gp + 0.948867;
            float cq = sqrt(0.5 * nv + 0.5);
            float fq = 1.05 * (1.0 - pow(1.0 - nv, 5.0));
            float kd = lerp(fq, cq * (cq + min(4.0 * tq * tq, 1.0)) * max(1.25 - 1.6 * tq, 0.55 - 0.2 * tq), tq) + 0.38 * tq * tq;
            float fmax = max(Fenv.r, max(Fenv.g, Fenv.b));
            // Specular occlusion: the game's gtso_lut(max(sqrt(1 - ao), sqrt(1 - cavity)), a2, saturate(Ngeom . R))
            // (research 95 W:796, raw coordinates, linear clamp); without the table, the smaller of the two.
            const float cavS = baseMat ? nmCavity : 1.0;
            const float so = gameSun.w > 0.5
                ? gtsoLut.SampleLevel(lutSamp, float3(max(sqrt(saturate(1.0 - aoV)), sqrt(saturate(1.0 - cavS))), a2, saturate(dot(Nf, R))), 0).r
                : min(aoV, cavS);
            float3 S = lerp(0.18 * Lr, Lr, so);
            const bool probesOnLayers = gameSkyScale.w > 0.5;  // IW8R_PERF_OFF=layerprobes turns it off
            // The rough-surface fallback (research 107 section 4.2: cb7[186] = (-10, 2) on every map): below g' 0.1 the
            // probe is not read at all (the result is then the plain S above), up to g' 0.2 it fades in.
            const float probeFb = saturate(2.0 - 10.0 * gp);
            if (probeFb < 1.0 && probeCount.x > 0.5 && (layerP.x < 0.5 || (probesOnLayers && layerUvO.w < 0.5))) {  // every pass but the terrain patch (research 95)
                // Probes are luminance-normalised: relit by the lightmap along the reflection (research 95).
                float3 P = stockProbeLight(i.w, R, gp) * dot(Lr, float3(0.2126, 0.7152, 0.0722));
                P = lerp(P, Lr, probeFb);
                float3 mixed = lerp(P, Lr, 0.85);
                S = lerp(0.18 * mixed, P, so);  // so: the specular occlusion above
            }
            const float3 aoMB = aoV / (1.0 - min(diffAlbedo, 0.85) * (1.0 - aoV));  // research 95: multi-bounce AO
            // Screen-space reflections (research 104): materials that trace them (base: layerP.z, layers: layerTint.w),
            // glossy pixels, the previous frame's scene marched along the reflection of the vertex normal, the hit
            // read on the normal-mapped ray with a roughness cone; S -> lerp(S, ssr, w).
            const bool ssrMat = (layerP.x < 0.5 ? layerP.z > 0.5 : (layerP.x < 1.5 && layerTint.w > 0.5)) || gameSkyScale.z > 1.5;
            float ssrDbg = 0.0, ssrWDbg = 0.0, ssrADbg = 0.0;  // debug view: 0.25 flagged, 0.5 traced, 0.75 hit, 1 used
            if (ssrMat) ssrDbg = 0.25;
            const bool ssrTest = gameSkyScale.z > 1.5;  // IW8R_SSR_ALL: every surface a mirror of what the trace hits
            float3 ssrTestColor = 0.0;
            if (ssrMat && prevSize.z > 0.5 && probeCount.x > 0.5 && (gp >= 0.3 || ssrTest)) {
                ssrDbg = 0.5;
                const float3 X = i.w, Ng = Nf, Vv = Vs;
                const float3 Rg = reflect(-Vv, Ng);
                const float wF = saturate(4.0 * dot(prevFwd.xyz, R));
                if (wF > 0.0 && dot(Rg, -Vv) > 0.0 && dot(prevFwd.xyz, Rg) > 0.0) {
                    const float4 ca = mul(float4(X, 1.0), prevVP), ce = mul(float4(X + 65500.0 * Rg, 1.0), prevVP);
                    float3 A = float3(ca.xy / ca.w * float2(0.5, -0.5) + 0.5, 1.0 / ca.w);
                    const float3 E = float3(ce.xy / ce.w * float2(0.5, -0.5) + 0.5, 1.0 / ce.w);
                    const float3 D = E - A;
                    const float len = length(D.xy * prevSize.xy);
                    A.xy += D.xy * (1.0 / max(len, 1e-3));  // one pixel out
                    float zPrev = 1.0 / A.z, tHit = 1.0;
                    bool hit = false;
                    [loop] for (int k = 0; k < 24; ++k) {
                        float tk = (k + 0.5) / 24.0;
                        tk *= tk;
                        const float3 Q = A + tk * D;
                        if (any(abs(Q.xy - 0.5) >= 0.5)) break;
                        const float zr = 1.0 / Q.z;
                        const float zs = prevZ.Load(int3(int2(Q.xy * prevSize.xy), 0));
                        if (zs * 1.002 < zr && abs(zr - zs) < max(abs(zr - zPrev), 48.0) && tk > 1.0 / len) {
                            hit = true, tHit = tk;
                            break;
                        }
                        zPrev = zr;
                    }
                    if (hit && tHit < 0.9999) {
                        const float rr = 1.0 - gp;
                        const float cone = max(0.0, min(1.01737 * rr * rr * rr + 0.070128, 1.0) * tHit + 1.48707 * rr - 0.284052);
                        const float4 cr = mul(float4(X + 65500.0 * R, 1.0), prevVP);
                        const float2 uvR = cr.w > 0.0 ? cr.xy / cr.w * float2(0.5, -0.5) + 0.5 : A.xy;
                        const float2 uvH = lerp(A.xy, uvR, tHit);
                        const float2 e2 = uvH * 2.0 - 1.0;
                        const float edge = saturate((1.0 - e2.x * e2.x) * (1.0 - e2.y * e2.y) * 2.0 * prevSize.x / max(prevSize.y, 1.0));
                        const float4 sc = prevColor.SampleLevel(lutSamp, uvH, 3.0 * min(cone, 1.0));
                        ssrDbg = 0.75, ssrWDbg = wF * edge, ssrADbg = sc.a;
                        if (sc.a > 0.0) ssrTestColor = sc.rgb * wF * edge;
                        if (sc.a > 0.0) S = lerp(S, sc.rgb, wF * edge), ssrDbg = 1.0;
                    }
                }
            }
            if (gameSkyScale.z > 0.5 && gameSkyScale.z < 1.5) return ssrMat ? float4(ssrDbg, ssrWDbg, ssrADbg, 1.0) : float4(0, 0, 0, 1);
            if (ssrTest) return float4(ssrTestColor, 1.0);
            c = diffAlbedo * indirect * kd * (1.0 - fmax) * aoMB + Fenv * S;
            float3 Ls = gameSun.xyz;
            float nl = dot(N, Ls);
            if (matW.w > 0.5) {
                // Wrap diffuse (research 107 section 2.2): the diffuse part wraps past the terminator per channel and the
                // shadow takes a per-channel power (a coloured penumbra); the env BRDF and the specular keep the plain N.L.
                const float mw = min(matW.r, min(matW.g, matW.b));
                const float3 kw = 1.0 + 1.35 * (matW.rgb - mw);
                const float3 ew = float3(kw.b * kw.g, kw.b * kw.r, kw.g * kw.r);
                const float2 abw = gameEnvBrdf(saturate(nl), gp);
                const float3 flw = f0 * abw.x + abw.y;
                c += gameSunCol.rgb * pow(max(saturate(visibility), 1e-6), ew) * wrap3(nl, matW.rgb) * diffAlbedo *
                     (1.0 - max(flw.r, max(flw.g, flw.b)));
            }
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
                     ((matW.w > 0.5 ? 0.0 : diffAlbedo * (1.0 - max(fl.r, max(fl.g, fl.b)))) + F * Dg * Vg * 1.0390625 * (baseMat ? nmCavity : 1.0));
            }
            // Transmission (research 107 section 1): grass, foliage, tarps, cloth, flags. The baked light seen from the
            // back side, and the sun through the blade as a GGX lobe of the surface's own roughness.
            if (matTr.w > 0.5) {
                const float3 T = srgbToLinear3(tr0.Sample(samp0, uvS).rgb) * matTr.rgb;
                const float kTr = 1.0 - exp2(-3.32193 * a2);
                const float3 Eb = lmOk ? (indirect - stockDirN) + stockB * saturate(dot(-N, stockDw))
                                       : (propLit(i) ? i.lgvBack * gameSkyScale.y : indirect);
                c += T * (1.0 - fmax) * kTr * Eb * aoMB;
                const float nlb = dot(gameSun.xyz, -N);
                if (nlb > 0.0) {
                    const float3 Hb = normalize(gameSun.xyz - R);
                    const float hb = dot(-N, Hb), qb = hb * hb * (a2 - 1.0) + 1.0;
                    const float Db = a2 / max(qb * qb, 1e-8);
                    const float Vb = 1.0 / ((nlb + sqrt(a2 + (1.0 - a2) * nlb * nlb)) * (nv + sqrt(a2 + (1.0 - a2) * nv * nv)));
                    const float2 abb = gameEnvBrdf(nlb, gp);
                    const float3 flb = f0 * abb.x + abb.y;
                    c += gameSunCol.rgb * T * visibility * nlb * Db * Vb * 1.0390625 * (1.0 - max(flb.r, max(flb.g, flb.b)));
                }
            }
            // Emissive (research 107 section 5): the image x constant 36, unlit, Fresnel-weighted, exposed as the rest.
            if (matE.w > 0.5) c += srgbToLinear3(emi0.Sample(samp0, uvS).rgb) * matE.rgb * (1.0 - fmax);
            // The map's local lights (research 106): the drawn ones of this pixel's 32-pixel tile; no AO, as the game.
            if (localInfo.x > 0.5) {
                // The transmission colour for the lamps (the image x the material's constant), as the sun's above.
                const float4 lampT = matTr.w > 0.5 ? float4(srgbToLinear3(tr0.Sample(samp0, uvS).rgb) * matTr.rgb, 1.0) : float4(0, 0, 0, 0);
                const uint2 tile = uint2(i.pos.xy) / uint(localInfo.z);
                const uint base = (tile.y * uint(localInfo.y) + tile.x) * 8u;
                float3 dSum = 0, sSum = 0;
                [loop] for (uint wd = 0; wd < 8u; ++wd) {
                    uint bits = lightTiles[base + wd];
                    [loop] while (bits != 0u) {
                        const uint b = firstbitlow(bits);
                        bits &= bits - 1u;
                        gameLocalLight(gameLights[wd * 32u + b], i.w, N, Vs, diffAlbedo, f0, gp, baseMat ? nmCavity : 1.0, matW, lampT, dSum, sSum);
                    }
                }
                c += dSum + sSum;
            }
        } else {
            // The converter's world shader occludes only the indirect light, in the multi-bounce form
            // ao / (1 - min(albedo, .85) (1 - ao)) (map_surface_realtime.hlsl; research 117).
            const float3 aoOwn = aoV / max(1.0 - min(albedo, 0.85) * (1.0 - aoV), 1e-3);
#ifdef IW8R_CUSTOM
            // A custom surface shader (core custom_shader): the map's own lighting function.
            IW8RSurface s;
            s.albedo = albedo, s.alpha = outA, s.normal = N, s.view = normalize(eyeStart.xyz - i.w), s.position = i.w;
            s.uv = i.uv, s.sunDir = gameSun.xyz, s.sunColor = gameSunCol.rgb, s.sunShadow = visibility;
            s.ambient = indirect * aoOwn, s.gloss = 0.3;
            s.faceNormal = dot(i.n, i.n) > 0.5 ? normalize(i.n) : N;
            s.pixel = i.pos.xy;
            s.viewDepth = mul(float4(i.w, 1.0), viewProj).w;
            s.eye = eyeStart.xyz;
            iw8rEdgeFields(s, i.edge, ddx(i.w), ddy(i.w));
            c = iw8rSurface(s);
#else
            float3 light = indirect * aoOwn + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
            c = albedo * light;
            if (batchFlags2.w > 0.5) {
                // A material with a specular map, as the build's world shader draws it (the converter's
                // SourceSunSpecular, map_surface_realtime.hlsl): specular map x lerp(.8, 4, (1 - |N.V|)^2.5) x
                // (probe + .625 x lobe x sun^(1/2.2) x shadow), added in gamma space. The lobe is exp((R.L - .99925) x
                // power), power 2^(9.3775 gloss) + 7, widened by the normal's screen variance with its energy kept.
                // The probe: the map's captured one when there is one, else the build's default probe (grey 128).
                const float4 sp = spec0.Sample(samp0, uvS);
                const float3 Vo = normalize(eyeStart.xyz - i.w);
                const float3 Ro = reflect(-Vo, N);
                const float3 ndx = ddx(N), ndy = ddy(N);
                const float variance = min(0.5 * (dot(ndx, ndx) + dot(ndy, ndy)), 0.18);
                const float srcPower = exp2(sp.a * 9.3775177) + 7.0;
                const float power = max(2.0 / (2.0 / (srcPower + 2.0) + variance) - 2.0, 1.0);
                const float lobe = saturate(exp((dot(Ro, gameSun.xyz) - 0.99925) * power)) * (power + 2.0) / (srcPower + 2.0);
                const float response = lerp(0.8, 4.0, pow(saturate(1.0 - abs(dot(Vo, N))), 2.5));
                const float3 probe = batchFlags2.y > 0.5 ? probeCube.SampleLevel(samp0, Ro, clamp(6.0 - 8.0 * sp.a, 0.0, 6.0)).rgb
                                                         : float3(0.50196, 0.50196, 0.50196);
                const float3 sunTerm = 0.625 * lobe * pow(max(gameSunCol.rgb, 0.0), 1.0 / 2.2) * visibility;
                c = pow(pow(max(c, 0.0), 1.0 / 2.2) + sp.rgb * response * (probe + sunTerm), 2.2);
            }
#endif
        }
        // Local lights (LocalLighting): colour x game intensity / d^2 x w^2 (3 - 2w), w = (1 - d / r) x spot cone;
        // specular as the iw3 style's (F0 .04, gloss .3).
        float3 V = normalize(eyeStart.xyz - i.w);
        int count = int(lightCount.x);
        if (localInfo.x > 0.5) {
            // The game's light path (research 132 G8): the map's lamps as the build writes them (bulb, window, cone,
            // shadow maps of spots with iw8r_shadows), tile-binned; a stock map's lamps on brushes added to it.
            const uint2 tile = uint2(i.pos.xy) / uint(localInfo.z);
            const uint base = (tile.y * uint(localInfo.y) + tile.x) * 8u;
            float3 dSum = 0, sSum = 0;
            const float gpOwn = 1.0 - sqrt(1.0 - 0.3);  // the build's gloss 0.3
            [loop] for (uint wd = 0; wd < 8u; ++wd) {
                uint bits = lightTiles[base + wd];
                [loop] while (bits != 0u) {
                    const uint b = firstbitlow(bits);
                    bits &= bits - 1u;
                    gameLocalLight(gameLights[wd * 32u + b], i.w, N, V, albedo, float3(0.04, 0.04, 0.04), gpOwn, 1.0,
                                   float4(0, 0, 0, 0), float4(0, 0, 0, 0), dSum, sSum);
                }
            }
            c += dSum + sSum;
            count = 0;  // the simplified loop below would count them twice
        }
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
#ifdef IW8R_FILTER
        {
            // A map shader's filter (core custom_shader): on the lit, fogged colour with the exposure applied, as the
            // game's lit pixel shaders end (their last step multiplies by the exposure, cb7[42].x).
            const float pre = max(game0.y, 1e-9);
#ifdef IW8R_FILTER_SCREEN
            // With the screen (iw8rFilterScreen: the pixel, its view depth, a pixel's size per unit of depth).
            const float filterDepth = mul(float4(i.w, 1.0), viewProj).w;
            const float3 filterRay = (i.w - eyeStart.xyz) / max(filterDepth, 1e-3);
            c = iw8rFilterScreen(c * pre, i.pos.xy, filterDepth, max(length(ddx(filterRay)), length(ddy(filterRay)))) / pre;
#else
            c = iw8rFilter(c * pre) / pre;
#endif
        }
#endif
        if (gameAo.w < 0.5) c = gamePost(c);  // HDR frames: the post pass applies it (drawPost)
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
// Game Lighting ambient occlusion: normal-oriented hemisphere samples in world space from the pre-pass depth, then a
// 4 x 4 depth-aware blur (the sample rotation repeats every 4 pixels, so the blur averages it out).
const char* kAoShader = R"(
Texture2D<float> depthTex : register(t0);
Texture2D<float> aoIn : register(t1);
cbuffer Ao : register(b8) { row_major float4x4 aoVp; row_major float4x4 aoIvp; float4 aoEye; float4 aoPrm; };  // radius, power, width, height
struct V { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
V vs_full(uint id : SV_VertexID) {
    V o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float3 worldAt(float2 uv, float d) {
    float4 w = mul(float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0), aoIvp);
    return w.xyz / w.w;
}
float depthAt(int2 p) { return depthTex.Load(int3(clamp(p, int2(0, 0), int2(aoPrm.zw) - 1), 0)); }
float ps_ao(V i) : SV_TARGET {
    int2 px = int2(i.pos.xy);
    float2 inv = 1.0 / aoPrm.zw;
    float2 uv = (float2(px) + 0.5) * inv;
    float d = depthAt(px);
    if (d >= 0.999999) return 1.0;
    float3 P = worldAt(uv, d);
    float dl = depthAt(px - int2(1, 0)), dr = depthAt(px + int2(1, 0)), du = depthAt(px - int2(0, 1)), dd = depthAt(px + int2(0, 1));
    float3 ex = abs(dr - d) < abs(d - dl) ? worldAt(uv + float2(inv.x, 0), dr) - P : P - worldAt(uv - float2(inv.x, 0), dl);
    float3 ey = abs(dd - d) < abs(d - du) ? worldAt(uv + float2(0, inv.y), dd) - P : P - worldAt(uv - float2(0, inv.y), du);
    float3 n = cross(ex, ey);
    if (dot(n, n) < 1e-20) return 1.0;
    n = normalize(n);
    float3 toEye = aoEye.xyz - P;
    if (dot(n, toEye) < 0) n = -n;
    float dist = length(toEye);
    uint2 q = uint2(px) & 3;
    float rot = (float((q.x * 4 + q.y) * 7 % 16) + 0.5) / 16.0 * 6.2831853;
    float3 t0 = normalize(abs(n.z) < 0.9 ? cross(n, float3(0, 0, 1)) : cross(n, float3(1, 0, 0)));
    float3 b0 = cross(n, t0);
    float3 T = t0 * cos(rot) + b0 * sin(rot), B = cross(n, T);
    float R = aoPrm.x;
    float occ = 0.0;
    [unroll] for (int k = 0; k < 12; ++k) {
        float f = (k + 0.5) / 12.0;
        float phi = k * 2.39996323;
        float rr = sqrt(f);
        float3 h = float3(cos(phi) * rr, sin(phi) * rr, sqrt(max(1.0 - f, 0.0)));
        float3 S = P + (T * h.x + B * h.y + n * h.z) * (R * lerp(0.2, 1.0, f * f)) + n * (0.02 * R);
        float4 c = mul(float4(S, 1.0), aoVp);
        if (c.w <= 1e-4) continue;
        float2 suv = float2(c.x / c.w * 0.5 + 0.5, 0.5 - c.y / c.w * 0.5);
        if (any(suv < 0.0) || any(suv > 1.0)) continue;
        float sd = depthAt(int2(suv * aoPrm.zw));
        if (sd >= 0.999999) continue;
        float3 Q = worldAt(suv, sd);
        if (length(Q - aoEye.xyz) < length(S - aoEye.xyz) - 0.03 * R) {
            float r = saturate(R / max(length(Q - P), 1e-3));
            occ += r * r * (3.0 - 2.0 * r);
        }
    }
    float ao = 1.0 - occ / 12.0;
    ao = lerp(ao, 1.0, saturate((dist - 6000.0) / 4000.0));
    return pow(saturate(ao), aoPrm.y);
}
// Kept for the next frame's screen-space reflections: the linear view depth (clip w; 1e9 for the sky) at half size,
// the farther of each 2 x 2 (the game's floatZ max chain), and the HDR scene averaged over 2 x 2.
Texture2D<float4> hdrIn : register(t2);
// The same at full size, one pixel each: the scene depth custom surface shaders read in the next frame.
// The faces were drawn with a depth offset of 1.5 x their steepest depth step per pixel (rsTris_, so that editor
// lines lie in front of them); the game's depth has none, so it is taken off here. The buffer's depth is linear across
// a face on screen, so the face's step along an axis is the difference to a neighbour on the same face: on the side
// where this pixel and the next two lie on one line (the other side crosses an edge of the face; at a corner where two
// faces meet the difference across the edge can be the smaller one, so the smaller difference alone is not enough).
// A face too narrow for that gets the smaller difference.
float faceStep(float d, float a1, float a2, float b1, float b2) {
    const float bendA = abs(a2 - 2.0 * a1 + d), bendB = abs(b2 - 2.0 * b1 + d);
    const float stepA = abs(a1 - d), stepB = abs(b1 - d);
    if (min(bendA, bendB) > 4e-7 + 0.1 * min(stepA, stepB)) return min(stepA, stepB);
    return bendA < bendB ? stepA : bendB < bendA ? stepB : min(stepA, stepB);
}
float ps_scenez(V i) : SV_TARGET {
    const int2 q = int2(i.pos.xy);
    float d = depthTex.Load(int3(q, 0));
    const float l = depthAt(q + int2(-1, 0)), r = depthAt(q + int2(1, 0));
    const float u = depthAt(q + int2(0, -1)), b = depthAt(q + int2(0, 1));
    if (d >= 0.999999) {
        // A face a pixel wide and nearly edge-on is pushed to the far plane: with a surface on both sides it is
        // taken as the farther of the two, not as a pixel of sky.
        const float acrossX = max(l, r), acrossY = max(u, b);
        d = min(acrossX, acrossY);
        if (d >= 0.999999) return 1e9;
    } else {
        const float stepX = faceStep(d, l, depthAt(q + int2(-2, 0)), r, depthAt(q + int2(2, 0)));
        const float stepY = faceStep(d, u, depthAt(q + int2(0, -2)), b, depthAt(q + int2(0, 2)));
        d -= 1.5 * max(stepX, stepY);
    }
    // Kept with the game's precision (its scene depth is depth / 6 in a 16-bit float, research 104), so that a shader
    // sees in the viewport what it will see in the game.
    const float z = mul(float4(worldAt((float2(q) + 0.5) / aoPrm.zw, d), 1.0), aoVp).w;
    return f16tof32(f32tof16(z / 6.0)) * 6.0;
}
float ps_prevz(V i) : SV_TARGET {
    int2 p = int2(i.pos.xy) * 2;
    float w = 0.0;
    [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x) {
            const int2 q = clamp(p + int2(x, y), int2(0, 0), int2(aoPrm.zw) - 1);
            const float d = depthTex.Load(int3(q, 0));
            float wl = 1e9;
            if (d < 0.999999) wl = mul(float4(worldAt((float2(q) + 0.5) / aoPrm.zw, d), 1.0), aoVp).w;
            w = max(w, wl);
        }
    return w;
}
float4 ps_prevcolor(V i) : SV_TARGET {
    int2 p = int2(i.pos.xy) * 2;
    float4 sum = 0.0;
    float n = 0.0;
    [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x) {
            const float4 c = hdrIn.Load(int3(clamp(p + int2(x, y), int2(0, 0), int2(aoPrm.zw) - 1), 0));
            if (c.a > 0.0) sum += float4(c.rgb, 1.0), n += 1.0;
        }
    return n > 0.0 ? float4(sum.rgb / n, 1.0) : float4(0, 0, 0, 0);
}
float viewDist(int2 p) {
    int2 c = clamp(p, int2(0, 0), int2(aoPrm.zw) - 1);
    float d = depthTex.Load(int3(c, 0));
    return length(worldAt((float2(c) + 0.5) / aoPrm.zw, d) - aoEye.xyz);
}
float ps_ao_blur(V i) : SV_TARGET {
    int2 px = int2(i.pos.xy);
    float L0 = viewDist(px);
    float sum = 0.0, wsum = 0.0;
    [unroll] for (int y = -2; y < 2; ++y)
        [unroll] for (int x = -2; x < 2; ++x) {
            int2 p = clamp(px + int2(x, y), int2(0, 0), int2(aoPrm.zw) - 1);
            float w = saturate(1.0 - abs(viewDist(p) - L0) / (0.05 * L0 + 1.0)) + 1e-4;
            sum += aoIn.Load(int3(p, 0)) * w;
            wsum += w;
        }
    return sum / wsum;
}
)";

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
// The HDR scene (linear, before exposure): its luminance directly (research 95 section 3), same weights.
float4 ps_meter_hdr(V i) : SV_TARGET {
    float4 c = frame.SampleLevel(lin, i.uv, 0);
    float x = c.a > 0.0 ? dot(max(c.rgb, 0.0), float3(0.2126, 0.7152, 0.0722)) : 0.0;
    float r = saturate(length(i.uv - 0.5) * 1.41421356);
    float w = c.a > 0.0 ? 1.0 - r : 0.0;
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
    for (auto*& b : cbInstAll_) safeRelease(b);
    cbInstAll_.clear();
    cbInstAllMeshes_ = 0;
    if (ctx1_) ctx1_->Release(), ctx1_ = nullptr;
    safeRelease(rsDecal_);
    safeRelease(vbRefTexExtra_);
    releaseExtraGroups();
    safeRelease(cbWind_);
    safeRelease(tomeSrv_);
    safeRelease(tomeBuf_);
    tomeBufWords_ = 0;
    tomeDirty_ = true;
    safeRelease(cbLights_);
    safeRelease(cbGame_);
    safeRelease(cbVol_);
    safeRelease(volMaskSrv_);
    volMaskVersion_ = 0;
    for (auto*& ps : customPs_) safeRelease(ps);
    customPs_.clear();
    for (int k = 0; k < 3; ++k) safeRelease(gameLmSrv_[k]), safeRelease(gameLmTex_[k]);
    safeRelease(gameLutSrv_);
    safeRelease(gameLutTex_);
    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
    safeRelease(gtsoSrv_), safeRelease(gtsoTex_);
    for (auto*& s : lgvSrv_) safeRelease(s);
    for (auto*& t : lgvTex_) safeRelease(t);
    safeRelease(lgvBoxBuf_), safeRelease(lgvSampler_);
    safeRelease(stockLightSrv_), safeRelease(stockLightBuf_), safeRelease(stockTileSrv_), safeRelease(stockTileBuf_);
    safeRelease(cbLocal_), safeRelease(iesSrv_), safeRelease(iesTex_);
    stockTileCap_ = 0;
    for (auto*& d : lightShadowDsv_) safeRelease(d);
    safeRelease(lightShadowSrv_), safeRelease(lightShadowTex_), safeRelease(lightSlotSrv_), safeRelease(lightSlotBuf_);
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
    safeRelease(rsLinesFast_);
    safeRelease(dsWrite_);
    safeRelease(blendNoColor_);
    safeRelease(dsTest_);
    safeRelease(dsOff_);
    safeRelease(blend_);
    safeRelease(blendAdd_);
    safeRelease(blendMul_);
    safeRelease(vsTex_);
    safeRelease(psTex_);
    safeRelease(filterPs_);
    safeRelease(psShadowClip_);
    safeRelease(psPrepass_);
    safeRelease(cbTiles_);
    safeRelease(postVs_), safeRelease(postPs_), safeRelease(meterHdrPs_);
    safeRelease(veilDownPs_), safeRelease(veilKarisPs_), safeRelease(veilUpPs_), safeRelease(cbVeil_);
    releaseVeilResources();
    safeRelease(aoVs_), safeRelease(aoPs_), safeRelease(aoBlurPs_), safeRelease(cbAo_);
    safeRelease(prevZPs_), safeRelease(prevColorPs_), safeRelease(cbPrev_);
    safeRelease(sceneZPs_);
    safeRelease(layoutTex_);
    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.ib), safeRelease(g.inst), safeRelease(g.visInst);
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
    texMat_.clear(), texMatSet_.clear();
}

void SceneRenderer::writeBatchCb(const float flags[8], const float* mat) {
    if (!cbBatch_) return;
    float all[12] = {};
    std::memcpy(all, flags, 8 * sizeof(float));
    if (mat) std::memcpy(all + 8, mat, 4 * sizeof(float));
    ctx_->UpdateSubresource(cbBatch_, 0, nullptr, all, 0, 0);
}

int SceneRenderer::createCustomShader(const std::string& source, std::string* err) {
    if (!dev_) return -1;
    ID3DBlob* blob = nullptr;
    ID3DBlob* errb = nullptr;
    const HRESULT hr = D3DCompile(source.data(), source.size(), "iw8r_custom", nullptr, nullptr, "ps_tex", "ps_5_0", 0, 0, &blob, &errb);
    if (FAILED(hr)) {
        if (err) *err = errb ? std::string(static_cast<const char*>(errb->GetBufferPointer()), errb->GetBufferSize()) : "D3DCompile failed";
        safeRelease(errb);
        return -1;
    }
    safeRelease(errb);
    ID3D11PixelShader* ps = nullptr;
    const HRESULT ch = dev_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps);
    safeRelease(blob);
    if (FAILED(ch)) {
        if (err) *err = "CreatePixelShader failed";
        return -1;
    }
    customPs_.push_back(ps);
    return int(customPs_.size()) - 1;
}

const char* SceneRenderer::texShaderSource() { return kTexShader; }

bool SceneRenderer::compileProgram(const std::string& source, const char* entry, std::vector<uint8_t>& program, std::string* err) {
    program.clear();
    ID3DBlob* blob = nullptr;
    ID3DBlob* errb = nullptr;
    const HRESULT hr = D3DCompile(source.data(), source.size(), "iw8r_program", nullptr, nullptr, entry, "ps_5_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errb);
    if (err) *err = errb ? std::string(static_cast<const char*>(errb->GetBufferPointer()), errb->GetBufferSize()) : std::string();
    safeRelease(errb);
    if (FAILED(hr) || !blob) {
        safeRelease(blob);
        if (err && err->empty()) *err = "D3DCompile failed";
        return false;
    }
    // Reflection and debug data are not needed where the program goes (it is spliced into another shader).
    ID3DBlob* stripped = nullptr;
    if (SUCCEEDED(D3DStripShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                 D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO | D3DCOMPILER_STRIP_TEST_BLOBS, &stripped)) && stripped) {
        safeRelease(blob);
        blob = stripped;
    }
    const auto* p = static_cast<const uint8_t*>(blob->GetBufferPointer());
    program.assign(p, p + blob->GetBufferSize());
    safeRelease(blob);
    return true;
}

bool SceneRenderer::pixelShaderAccepted(const std::vector<uint8_t>& program) {
    if (!dev_ || program.empty()) return false;
    ID3D11PixelShader* ps = nullptr;
    const bool ok = SUCCEEDED(dev_->CreatePixelShader(program.data(), program.size(), nullptr, &ps)) && ps;
    safeRelease(ps);
    return ok;
}

bool SceneRenderer::setReferenceFilterShader(const std::string& source, std::string* err) {
    if (source.empty()) {
        safeRelease(filterPs_);
        filterScreen_ = false;
        return true;
    }
    if (!dev_) return false;
    ID3DBlob* blob = nullptr;
    ID3DBlob* errb = nullptr;
    const HRESULT hr = D3DCompile(source.data(), source.size(), "iw8r_filter", nullptr, nullptr, "ps_tex", "ps_5_0", 0, 0, &blob, &errb);
    if (FAILED(hr)) {
        if (err) *err = errb ? std::string(static_cast<const char*>(errb->GetBufferPointer()), errb->GetBufferSize()) : "D3DCompile failed";
        safeRelease(errb);
        return false;
    }
    safeRelease(errb);
    ID3D11PixelShader* ps = nullptr;
    const HRESULT ch = dev_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps);
    safeRelease(blob);
    if (FAILED(ch)) {
        if (err) *err = "CreatePixelShader failed";
        return false;
    }
    safeRelease(filterPs_);
    filterPs_ = ps;
    filterScreen_ = source.find("#define IW8R_FILTER_SCREEN") != std::string::npos;
    return true;
}

bool SceneRenderer::compileCheck(const std::string& source, std::string* err) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* errb = nullptr;
    const HRESULT hr = D3DCompile(source.data(), source.size(), "iw8r_custom", nullptr, nullptr, "ps_tex", "ps_5_0", 0, 0, &blob, &errb);
    if (err) *err = errb ? std::string(static_cast<const char*>(errb->GetBufferPointer()), errb->GetBufferSize()) : std::string();
    safeRelease(errb);
    safeRelease(blob);
    if (FAILED(hr) && err && err->empty()) *err = "D3DCompile failed";
    return SUCCEEDED(hr);
}

int SceneRenderer::createTextureBlocks(int dxgiFormat, int width, int height, const std::vector<std::string>& levels) {
    if (!dev_ || levels.empty() || width < 4 || height < 4) return -1;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width);
    td.Height = UINT(height);
    td.MipLevels = UINT(levels.size());
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT(dxgiFormat);
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const UINT blockBytes = dxgiFormat == DXGI_FORMAT_BC1_UNORM ? 8 : 16;
    std::vector<D3D11_SUBRESOURCE_DATA> init(levels.size());
    UINT w = UINT(width), h = UINT(height);
    size_t bytes = 0;
    for (size_t k = 0; k < levels.size(); ++k) {
        init[k].pSysMem = levels[k].data();
        init[k].SysMemPitch = std::max(1u, (w + 3) / 4) * blockBytes;
        init[k].SysMemSlicePitch = UINT(levels[k].size());
        bytes += levels[k].size();
        w = std::max(1u, w / 2), h = std::max(1u, h / 2);
    }
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev_->CreateTexture2D(&td, init.data(), &tex))) return -1;
    ID3D11ShaderResourceView* srv = nullptr;
    const HRESULT hr = dev_->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    if (FAILED(hr)) return -1;
    textures_.push_back(srv);
    texRawAlpha_.resize(textures_.size(), 0);
    texRawAlpha_.back() = 1;
    textureBytes_ += bytes;
    return int(textures_.size()) - 1;
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
    texRawAlpha_.resize(textures_.size(), 0);
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
            ID3DBlob* pvs = nullptr;
            ID3DBlob* ppo = nullptr;
            if (compileTex("vs_post", "vs_5_0", pvs) && compileTex("ps_post", "ps_5_0", ppo)) {
                dev_->CreateVertexShader(pvs->GetBufferPointer(), pvs->GetBufferSize(), nullptr, &postVs_);
                dev_->CreatePixelShader(ppo->GetBufferPointer(), ppo->GetBufferSize(), nullptr, &postPs_);
            }
            safeRelease(pvs), safeRelease(ppo);
            for (const auto& [entry, out] : {std::pair<const char*, ID3D11PixelShader**>{"ps_veil_down", &veilDownPs_},
                                             {"ps_veil_karis", &veilKarisPs_}, {"ps_veil_up", &veilUpPs_}}) {
                ID3DBlob* vb = nullptr;
                if (compileTex(entry, "ps_5_0", vb)) dev_->CreatePixelShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, out);
                safeRelease(vb);
            }
            ID3DBlob* pps = nullptr;
            if (compileTex("ps_prepass", "ps_5_0", pps)) {
                dev_->CreatePixelShader(pps->GetBufferPointer(), pps->GetBufferSize(), nullptr, &psPrepass_);
                safeRelease(pps);
            }
            const D3D11_INPUT_ELEMENT_DESC til[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 6, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            dev_->CreateInputLayout(til, 7, tvs->GetBufferPointer(), tvs->GetBufferSize(), &layoutTex_);
            ID3DBlob* tivs = nullptr;
            if (compileTex("vs_tex_inst", "vs_5_0", tivs)) {
                dev_->CreateVertexShader(tivs->GetBufferPointer(), tivs->GetBufferSize(), nullptr, &vsTexInst_);
                // PropVertex (32 bytes): position, uv, colour, normal as signed bytes, the model's vertex colour.
                const D3D11_INPUT_ELEMENT_DESC tiil[] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"NORMAL", 0, DXGI_FORMAT_R8G8B8A8_SNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                };
                dev_->CreateInputLayout(tiil, UINT(std::size(tiil)), tivs->GetBufferPointer(), tivs->GetBufferSize(), &layoutTexInst_);
                safeRelease(tivs);
            }
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = 48;  // batchFlags + batchFlags2 + batchMat (writeBatchCb)
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
            bd.ByteWidth = 352;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale, terrain cut, ao, fogK[8], time
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
    rd.AntialiasedLineEnable = FALSE;
    dev_->CreateRasterizerState(&rd, &rsLinesFast_);

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
    // Alpha keeps the largest value: it marks the pixels the HDR scene drew (drawPost); the frame's alpha is unused.
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
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
    safeRelease(depthSrv_);
    safeRelease(depth_);
    safeRelease(hdrSrv_), safeRelease(hdrRtv_), safeRelease(hdrTex_);
    safeRelease(prevColorSrv_), safeRelease(prevColorRtv_), safeRelease(prevColorTex_);
    safeRelease(prevZSrv_), safeRelease(prevZRtv_), safeRelease(prevZTex_);
    safeRelease(sceneZSrv_), safeRelease(sceneZRtv_), safeRelease(sceneZTex_);
    sceneZValid_ = false;
    prevValid_ = false;
    for (int k = 0; k < 2; ++k) safeRelease(aoSrv_[k]), safeRelease(aoRtv_[k]), safeRelease(aoTex_[k]);
    aoValid_ = false;
}

bool SceneRenderer::createAoResources() {
    if (aoVs_ && aoPs_ && aoBlurPs_ && cbAo_) return true;
    if (aoTried_ || !dev_) return false;
    aoTried_ = true;
    ID3DBlob *vsb = nullptr, *psb = nullptr, *pbb = nullptr, *errb = nullptr;
    const auto compile = [&](const char* entry, const char* target, ID3DBlob*& out) {
        const bool ok = SUCCEEDED(D3DCompile(kAoShader, std::strlen(kAoShader), "iw8r_ao", nullptr, nullptr, entry, target, 0, 0, &out, &errb));
        safeRelease(errb);
        return ok;
    };
    if (compile("vs_full", "vs_5_0", vsb) && compile("ps_ao", "ps_5_0", psb) && compile("ps_ao_blur", "ps_5_0", pbb)) {
        dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &aoVs_);
        dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &aoPs_);
        dev_->CreatePixelShader(pbb->GetBufferPointer(), pbb->GetBufferSize(), nullptr, &aoBlurPs_);
        ID3DBlob *zb = nullptr, *cb = nullptr;
        if (compile("ps_prevz", "ps_5_0", zb)) dev_->CreatePixelShader(zb->GetBufferPointer(), zb->GetBufferSize(), nullptr, &prevZPs_);
        safeRelease(zb);
        if (compile("ps_scenez", "ps_5_0", zb)) dev_->CreatePixelShader(zb->GetBufferPointer(), zb->GetBufferSize(), nullptr, &sceneZPs_);
        if (compile("ps_prevcolor", "ps_5_0", cb)) dev_->CreatePixelShader(cb->GetBufferPointer(), cb->GetBufferSize(), nullptr, &prevColorPs_);
        safeRelease(zb), safeRelease(cb);
    }
    safeRelease(vsb), safeRelease(psb), safeRelease(pbb);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 160;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    dev_->CreateBuffer(&bd, nullptr, &cbAo_);
    return aoVs_ && aoPs_ && aoBlurPs_ && cbAo_;
}

namespace {
// The inverse of a 4 x 4 matrix (row-major); false when singular.
bool invert4(const float* m, float* out) {
    double inv[16], a[16];
    for (int k = 0; k < 16; ++k) a[k] = m[k];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (std::fabs(det) < 1e-30) return false;
    for (int k = 0; k < 16; ++k) out[k] = float(inv[k] / det);
    return true;
}
}  // namespace

// The finished frame's depth as linear view depth at full size (ps_scenez), for custom surface shaders in the next
// frame. Only while a custom shader exists.
void SceneRenderer::keepSceneDepth(const float* viewProj) {
    if (!sceneDepthWanted() || !sceneZRtv_ || !sceneZPs_ || !depthSrv_ || !aoVs_ || !createAoResources()) {
        sceneZValid_ = false;
        return;
    }
    float cb[40];
    std::memcpy(cb, viewProj, 64);
    if (!invert4(viewProj, cb + 16)) {
        sceneZValid_ = false;
        return;
    }
    cb[32] = fog_[0], cb[33] = fog_[1], cb[34] = fog_[2], cb[35] = 0;
    cb[36] = 40.0f, cb[37] = 1.0f, cb[38] = float(w_), cb[39] = float(h_);
    ctx_->UpdateSubresource(cbAo_, 0, nullptr, cb, 0, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(31, 1, &none);  // this frame's input becomes the output
    D3D11_VIEWPORT fv{0, 0, float(w_), float(h_), 0, 1};
    ctx_->RSSetViewports(1, &fv);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(aoVs_, nullptr, 0);
    ctx_->PSSetConstantBuffers(8, 1, &cbAo_);
    ctx_->OMSetRenderTargets(1, &sceneZRtv_, nullptr);
    ctx_->PSSetShader(sceneZPs_, nullptr, 0);
    ctx_->PSSetShaderResources(0, 1, &depthSrv_);
    ctx_->Draw(3, 0);
    ctx_->PSSetShaderResources(0, 1, &none);
    ctx_->OMSetRenderTargets(1, &sceneRtv_, nullptr);
    // The frame just drawn read the depth of the view before: stale when that was another view.
    sceneZStale_ = !sceneZValid_ || std::memcmp(sceneZViewProj_, viewProj, 64) != 0;
    std::memcpy(sceneZViewProj_, viewProj, 64);
    sceneZValid_ = true;
}

void SceneRenderer::keepFrameForSsr(const float* viewProj) {
    if (!prevColorRtv_ || !prevZRtv_ || !prevZPs_ || !prevColorPs_ || !depthSrv_ || !hdrSrv_ || !createAoResources()) {
        prevValid_ = false;
        return;
    }
    IW8R_PROF("d.ssr_keep");
    float cb[40];
    std::memcpy(cb, viewProj, 64);
    if (!invert4(viewProj, cb + 16)) {
        prevValid_ = false;
        return;
    }
    cb[32] = fog_[0], cb[33] = fog_[1], cb[34] = fog_[2], cb[35] = 0;
    cb[36] = 40.0f, cb[37] = 1.0f, cb[38] = float(w_), cb[39] = float(h_);
    ctx_->UpdateSubresource(cbAo_, 0, nullptr, cb, 0, 0);
    ID3D11ShaderResourceView* none4[4] = {nullptr, nullptr, nullptr, nullptr};
    ctx_->PSSetShaderResources(16, 2, none4);  // this frame's inputs become the outputs
    D3D11_VIEWPORT hv{0, 0, float(std::max(1, w_ / 2)), float(std::max(1, h_ / 2)), 0, 1};
    ctx_->RSSetViewports(1, &hv);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(aoVs_, nullptr, 0);
    ctx_->PSSetConstantBuffers(8, 1, &cbAo_);
    // linear depth (reads the depth buffer: no depth bound)
    ctx_->OMSetRenderTargets(1, &prevZRtv_, nullptr);
    ctx_->PSSetShader(prevZPs_, nullptr, 0);
    ID3D11ShaderResourceView* zin[3] = {depthSrv_, nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 3, zin);
    ctx_->Draw(3, 0);
    // the scene, then its mips
    ctx_->OMSetRenderTargets(1, &prevColorRtv_, nullptr);
    ctx_->PSSetShader(prevColorPs_, nullptr, 0);
    ID3D11ShaderResourceView* cin[3] = {depthSrv_, nullptr, hdrSrv_};
    ctx_->PSSetShaderResources(0, 3, cin);
    ctx_->Draw(3, 0);
    ctx_->PSSetShaderResources(0, 3, none4);
    ctx_->OMSetRenderTargets(1, &sceneRtv_, nullptr);
    ctx_->GenerateMips(prevColorSrv_);
    D3D11_VIEWPORT fv{0, 0, float(w_), float(h_), 0, 1};
    ctx_->RSSetViewports(1, &fv);
    std::memcpy(prevViewProj_, viewProj, 64);
    prevValid_ = true;
}

void SceneRenderer::releaseVeilResources() {
    for (auto* list : {&veilS_, &veilV_})
        for (VeilLevel& l : *list) safeRelease(l.srv), safeRelease(l.rtv), safeRelease(l.tex);
    veilS_.clear(), veilV_.clear();
    veilCount_ = veilW_ = veilH_ = 0;
}

bool SceneRenderer::createVeilResources() {
    if (veilW_ == w_ && veilH_ == h_ && veilCount_ > 0) return true;
    releaseVeilResources();
    if (!dev_ || w_ < 8 || h_ < 8) return false;
    // research 109 section 1.2: veilMip 2, veilCount = min(min(floor(log2(min(W, H))), 9) - 2, 7), scene mips 1..count+1.
    const int lg = int(std::floor(std::log2(double(std::min(w_, h_)))));
    const int count = std::clamp(std::min(lg, 9) - 2, 1, 7);
    const auto make = [&](int mip, VeilLevel& l) {
        l.w = std::max(1, w_ >> mip), l.h = std::max(1, h_ >> mip);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(l.w), td.Height = UINT(l.h);
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &l.tex)) && SUCCEEDED(dev_->CreateRenderTargetView(l.tex, nullptr, &l.rtv)) &&
               SUCCEEDED(dev_->CreateShaderResourceView(l.tex, nullptr, &l.srv));
    };
    veilS_.resize(size_t(count + 2));
    veilV_.resize(size_t(count + 1));
    bool ok = true;
    for (int m = 1; m <= count + 1 && ok; ++m) ok = make(m, veilS_[size_t(m)]);
    for (int m = 2; m <= count && ok; ++m) ok = make(m, veilV_[size_t(m)]);
    if (!cbVeil_ && ok) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 144;  // 36 floats: veil, post, vignette, white balance
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ok = SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &cbVeil_));
    }
    if (!ok) {
        releaseVeilResources();
        return false;
    }
    veilCount_ = count, veilW_ = w_, veilH_ = h_;
    return true;
}

bool SceneRenderer::drawVeil() {
    if (!postFxEnabled_ || game_.veil <= 0 || !hdrSrv_ || !postVs_ || !veilDownPs_ || !veilKarisPs_ || !veilUpPs_ || !createVeilResources())
        return false;
    IW8R_PROF("d.veil");
    static const float kWeights[7] = {0, 0.025f, 0.075f, 0.15f, 0.2f, 0.25f, 0.3f};  // s_veilWeights (Replay .rdata)
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(postVs_, nullptr, 0);
    if (lutSampler_) ctx_->PSSetSamplers(2, 1, &lutSampler_);
    ID3D11ShaderResourceView* none2[2] = {nullptr, nullptr};
    const auto pass = [&](ID3D11PixelShader* ps, const VeilLevel& dst, ID3D11ShaderResourceView* src, ID3D11ShaderResourceView* add,
                          float wIn, float wOut) {
        float cb[36] = {float(dst.w), float(dst.h), 1.0f / float(dst.w), 1.0f / float(dst.h), wIn, wOut, 0, 0};
        ctx_->UpdateSubresource(cbVeil_, 0, nullptr, cb, 0, 0);
        ctx_->PSSetShaderResources(25, 2, none2);
        ctx_->OMSetRenderTargets(1, &dst.rtv, nullptr);
        D3D11_VIEWPORT vp{0, 0, float(dst.w), float(dst.h), 0, 1};
        ctx_->RSSetViewports(1, &vp);
        ID3D11ShaderResourceView* in[2] = {src, add};
        ctx_->PSSetShaderResources(25, 2, in);
        ctx_->PSSetConstantBuffers(12, 1, &cbVeil_);
        ctx_->PSSetShader(ps, nullptr, 0);
        ctx_->Draw(3, 0);
    };
    const int c = veilCount_;
    pass(veilKarisPs_, veilS_[1], hdrSrv_, nullptr, 0, 0);
    for (int m = 2; m <= c + 1; ++m) pass(veilDownPs_, veilS_[size_t(m)], veilS_[size_t(m - 1)].srv, nullptr, 0, 0);
    // V_m = Up(V_{m+1} or the smallest mip) x wIn + S_m x weights[m - 2]; the first wIn is weights[count - 1].
    for (int m = c; m >= 2; --m) {
        ID3D11ShaderResourceView* src = m == c ? veilS_[size_t(c + 1)].srv : veilV_[size_t(m + 1)].srv;
        const float wIn = m == c ? kWeights[std::min(c - 1, 6)] : 1.0f, wOut = kWeights[std::min(m - 2, 6)];
        pass(veilUpPs_, veilV_[size_t(m)], src, wOut > 0 ? veilS_[size_t(m)].srv : nullptr, wIn, wOut);
    }
    ctx_->PSSetShaderResources(25, 2, none2);
    D3D11_VIEWPORT full{0, 0, float(w_), float(h_), 0, 1};
    ctx_->RSSetViewports(1, &full);
    return c >= 2;
}

void SceneRenderer::drawPost() {
    IW8R_PROF("d.post");
    const bool veil = drawVeil();
    veilDrawn_ = veil;
    {
        // The post constants (b12): veil strength and screen size, the vignette.
        float cb[36] = {};
        cb[8] = veil ? game_.veil : 0.0f, cb[9] = float(w_), cb[10] = float(h_);
        cb[11] = postFxEnabled_ && filmGrainEnabled_ ? game_.grain : 0.0f;
        if (postFxEnabled_) std::memcpy(cb + 12, game_.vignette, sizeof(game_.vignette));
        static const bool noWb = std::getenv("IW8R_NO_WHITE_BALANCE") != nullptr;  // a bench switch
        whiteBalanceDrawn_ = whiteBalanceEnabled_ && game_.whiteBalanceOn && !noWb;
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 3; ++k) cb[24 + r * 4 + k] = whiteBalanceDrawn_ ? game_.whiteBalance[r * 3 + k] : (r == k ? 1.0f : 0.0f);
        cb[27] = whiteBalanceDrawn_ ? 1.0f : 0.0f;
        if (cbVeil_ || createVeilResources()) {
            ctx_->UpdateSubresource(cbVeil_, 0, nullptr, cb, 0, 0);
            ctx_->PSSetConstantBuffers(12, 1, &cbVeil_);
        }
        ID3D11ShaderResourceView* vt = veil ? veilV_[2].srv : nullptr;
        ctx_->PSSetShaderResources(27, 1, &vt);
    }
    writeGameCb();  // b4 (exposure), the LUT on t9 and its sampler
    {
        // The volumetric fog's constants (b13) and inputs: the depth (t28), the sun shadow maps (t2, s1) and the
        // frame's constants (b0: the shadow matrices).
        float cb[468] = {};
        const bool volOn = game_.on && (volumetricOn_ || game_.volOwn) && game_.vol[2][2] > 0.5f && depthSrv_ && invert4(lastViewProj_, cb);
        if (volOn) {
            cb[16] = fog_[0], cb[17] = fog_[1], cb[18] = fog_[2];
            cb[20] = game_.sunToward[0], cb[21] = game_.sunToward[1], cb[22] = game_.sunToward[2];
            cb[24] = game_.sunColor[0], cb[25] = game_.sunColor[1], cb[26] = game_.sunColor[2];
            std::memcpy(cb + 28, game_.vol, sizeof(game_.vol));
            cb[40] = game_.sky[0], cb[41] = game_.sky[1], cb[42] = game_.sky[2];
            const int boxes = std::clamp(game_.volBoxCount, 0, 8);
            cb[44] = float(boxes);
            std::memcpy(cb + 48, game_.volBoxes, size_t(boxes) * 20 * sizeof(float));
            const int lamps = std::clamp(game_.volLampCount, 0, 8);
            cb[208] = float(lamps), cb[209] = game_.volLampBrightness[0], cb[210] = game_.volLampBrightness[1];
            std::memcpy(cb + 212, game_.volLamps, size_t(lamps) * 12 * sizeof(float));
            // Masks: without a picture array every code is "none".
            for (int b = 0; b < boxes; ++b)
                for (int k = 0; k < 20; ++k) cb[308 + b * 20 + k] = volMaskSrv_ || k >= 4 ? game_.volBoxMasks[b][k] : -1.0f;
        }
        if (!cbVol_ && dev_) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = sizeof(cb);
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            dev_->CreateBuffer(&bd, nullptr, &cbVol_);
        }
        if (cbVol_) {
            ctx_->UpdateSubresource(cbVol_, 0, nullptr, cb, 0, 0);
            ctx_->PSSetConstantBuffers(13, 1, &cbVol_);
        }
        ctx_->PSSetConstantBuffers(0, 1, &cb_);
        ID3D11ShaderResourceView* d = volOn ? depthSrv_ : nullptr;
        ctx_->PSSetShaderResources(28, 1, &d);
        ID3D11ShaderResourceView* masks = volOn ? volMaskSrv_ : nullptr;
        ctx_->PSSetShaderResources(24, 1, &masks);
        if (sampler_) ctx_->PSSetSamplers(3, 1, &sampler_);
        if (shadowSrv_ && cmpSampler_) {
            ctx_->PSSetShaderResources(2, 1, &shadowSrv_);
            ctx_->PSSetSamplers(1, 1, &cmpSampler_);
        }
    }
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(postVs_, nullptr, 0);
    ctx_->PSSetShader(postPs_, nullptr, 0);
    ctx_->PSSetShaderResources(15, 1, &hdrSrv_);
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(15, 1, &none);
    ctx_->PSSetShaderResources(27, 1, &none);
    ctx_->PSSetShaderResources(28, 1, &none);
    sceneRtv_ = rtv_;
    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    const float bf[4] = {0, 0, 0, 0};
    ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
}

void SceneRenderer::drawAo(const float* viewProj) {
    aoValid_ = false;
    if (!depthSrv_ || !aoRtv_[0] || !aoRtv_[1] || !createAoResources()) return;
    IW8R_PROF("d.ao");
    float cb[40];
    std::memcpy(cb, viewProj, 64);
    if (!invert4(viewProj, cb + 16)) return;
    static const float radius = std::getenv("IW8R_AO_RADIUS") ? float(std::atof(std::getenv("IW8R_AO_RADIUS"))) : 40.0f;
    static const float power = std::getenv("IW8R_AO_POWER") ? float(std::atof(std::getenv("IW8R_AO_POWER"))) : 1.0f;
    cb[32] = fog_[0], cb[33] = fog_[1], cb[34] = fog_[2], cb[35] = 0;
    cb[36] = std::max(radius, 1.0f), cb[37] = std::max(power, 0.1f), cb[38] = float(w_), cb[39] = float(h_);
    ctx_->UpdateSubresource(cbAo_, 0, nullptr, cb, 0, 0);
    ID3D11ShaderResourceView* none2[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(14, 1, none2);  // last frame's AO is about to be written
    ctx_->OMSetRenderTargets(1, &aoRtv_[0], nullptr);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(aoVs_, nullptr, 0);
    ctx_->PSSetShader(aoPs_, nullptr, 0);
    ctx_->PSSetConstantBuffers(8, 1, &cbAo_);
    ID3D11ShaderResourceView* in0[2] = {depthSrv_, nullptr};
    ctx_->PSSetShaderResources(0, 2, in0);
    ctx_->Draw(3, 0);
    ctx_->OMSetRenderTargets(1, &aoRtv_[1], nullptr);
    ctx_->PSSetShader(aoBlurPs_, nullptr, 0);
    ID3D11ShaderResourceView* in1[2] = {depthSrv_, aoSrv_[0]};
    ctx_->PSSetShaderResources(0, 2, in1);
    ctx_->Draw(3, 0);
    ctx_->PSSetShaderResources(0, 2, none2);
    ctx_->OMSetRenderTargets(1, &sceneRtv_, dsv_);
    aoValid_ = true;
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

void SceneRenderer::setIesStrip(int width, const uint8_t* r8) {
    safeRelease(iesSrv_), safeRelease(iesTex_);
    if (!dev_ || width < 2 || !r8) return;
    D3D11_TEXTURE1D_DESC td{};
    td.Width = UINT(width);
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{r8, UINT(width), 0};
    if (FAILED(dev_->CreateTexture1D(&td, &init, &iesTex_)) || FAILED(dev_->CreateShaderResourceView(iesTex_, nullptr, &iesSrv_)))
        safeRelease(iesTex_);
}

namespace {
// A clear bit: the tome hides it. Past the bits, or a negative index: not hidden.
bool tomeBitClear(const std::vector<uint32_t>& bits, int i) {
    return i >= 0 && size_t(i >> 5) < bits.size() && ((bits[size_t(i >> 5)] >> (unsigned(i) & 31u)) & 1u) == 0u;
}
}  // namespace

void SceneRenderer::prepareStockLocalLights(const float* M) {
    constexpr uint32_t kMaxLights = 256, kTile = 32;
    stockLightsOn_ = false;
    stockLightsDrawn_ = 0;
    tomeCulledLights_ = 0;
    if (!dev_) return;
    if (!cbLocal_) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &cbLocal_))) return;
    }
    float info[4] = {0, 0, float(kTile), iesSrv_ ? 1.0f : 0.0f};
    static const bool off = std::getenv("IW8R_NO_STOCK_LIGHTS") != nullptr;
    const auto done = [&] { ctx_->UpdateSubresource(cbLocal_, 0, nullptr, info, 0, 0); };
    if (off || !stockLightsEnabled_ || !game_.on || stockLights_.empty() || !M || w_ <= 0 || h_ <= 0) return done();
    // The game's rule (research 106 section 2.1): the lights in view, nearest to the camera first, at most 256.
    const iw8r::Frustum fr = iw8r::frustumFromViewProj(M);
    const float* eye = fog_;
    stockLightOrder_.clear();
    for (uint32_t i = 0; i < uint32_t(stockLights_.size()); ++i) {
        const iw8r::StockGameLight& l = stockLights_[i];
        const float r = l.invRadius > 0 ? 1.0f / l.invRadius : 0.0f;
        bool out = false;
        for (const auto& pl : fr.planes)
            if (pl.n.x * l.pos[0] + pl.n.y * l.pos[1] + pl.n.z * l.pos[2] - pl.d > r) {
                out = true;
                break;
            }
        if (out) continue;
        // Cull by tome: the game bins a light only when the camera's query returned it (research 110).
        if (!tomeLightBits_.empty() && i < stockLightShadows_.size() && tomeBitClear(tomeLightBits_, stockLightShadows_[i].primaryIndex)) {
            ++tomeCulledLights_;
            continue;
        }
        const float dx = l.pos[0] - eye[0], dy = l.pos[1] - eye[1], dz = l.pos[2] - eye[2];
        stockLightOrder_.push_back({dx * dx + dy * dy + dz * dz, i});
    }
    if (stockLightOrder_.empty()) return done();
    const size_t n = std::min<size_t>(stockLightOrder_.size(), kMaxLights);
    std::partial_sort(stockLightOrder_.begin(), stockLightOrder_.begin() + ptrdiff_t(n), stockLightOrder_.end());
    // Shadow slots (research 106 section 4.1): the nearest drawn lights with a shadow map keep or get one; a slot not used
    // this frame goes to a nearer light that needs one. Lights without a slot draw unshadowed.
    static const bool noLightShadows = std::getenv("IW8R_NO_LIGHT_SHADOWS") != nullptr;
    if (lightSlotOf_.size() != stockLights_.size()) lightSlotOf_.assign(stockLights_.size(), -1);
    if (!noLightShadows && lightShadowsEnabled_ && stockLightShadows_.size() == stockLights_.size() && createLightShadowResources()) {
        for (size_t k = 0; k < n; ++k) {
            const uint32_t li = stockLightOrder_[k].second;
            if (!stockLightShadows_[li].cast) continue;
            int s = lightSlotOf_[li];
            if (s < 0) {
                // a free slot, else the one used longest ago (not this frame)
                int best = -1;
                for (int j = 0; j < kLightSlots; ++j) {
                    if (lightSlots_[size_t(j)].light < 0) {
                        best = j;
                        break;
                    }
                    if (lightSlots_[size_t(j)].lastUsed < frame_ && (best < 0 || lightSlots_[size_t(j)].lastUsed < lightSlots_[size_t(best)].lastUsed))
                        best = j;
                }
                if (best < 0) continue;  // every slot is in use this frame
                LightSlot& sl = lightSlots_[size_t(best)];
                if (sl.light >= 0 && size_t(sl.light) < lightSlotOf_.size()) lightSlotOf_[size_t(sl.light)] = -1;
                sl = LightSlot{};
                sl.light = int(li);
                const iw8r::StockLightShadow& sh = stockLightShadows_[li];
                iw8r::stockLightShadowMatrix(sh, sl.vp);
                sl.params[0] = sh.zNear, sl.params[1] = sh.zFar, sl.params[2] = sh.k, sl.params[3] = sh.bias;
                lightSlotOf_[li] = best;
                lightSlotsChanged_ = true;
                s = best;
            }
            lightSlots_[size_t(s)].lastUsed = frame_;
        }
        // The slots' matrices and lookup constants (t23), when a slot changed.
        if (lightSlotsChanged_ && lightSlotBuf_) {
            std::array<float, kLightSlots * 20> sb{};
            for (int j = 0; j < kLightSlots; ++j) {
                std::memcpy(&sb[size_t(j) * 20], lightSlots_[size_t(j)].vp, 16 * sizeof(float));
                std::memcpy(&sb[size_t(j) * 20 + 16], lightSlots_[size_t(j)].params, 4 * sizeof(float));
            }
            ctx_->UpdateSubresource(lightSlotBuf_, 0, nullptr, sb.data(), 0, 0);
            lightSlotsChanged_ = false;
        }
    }
    lightShadowsDrawn_ = 0;
    for (const LightSlot& sl : lightSlots_) lightShadowsDrawn_ += sl.light >= 0 && sl.drawn ? 1 : 0;
    if (!stockLightBuf_) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(kMaxLights * sizeof(iw8r::StockGameLight));
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = UINT(sizeof(iw8r::StockGameLight));
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &stockLightBuf_)) ||
            FAILED(dev_->CreateShaderResourceView(stockLightBuf_, nullptr, &stockLightSrv_))) {
            safeRelease(stockLightBuf_);
            return done();
        }
    }
    const uint32_t tx = (uint32_t(w_) + kTile - 1) / kTile, ty = (uint32_t(h_) + kTile - 1) / kTile;
    const size_t words = size_t(tx) * ty * 8;
    if (words > stockTileCap_) {
        safeRelease(stockTileSrv_), safeRelease(stockTileBuf_);
        stockTileCap_ = 0;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(words * 4);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = 4;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &stockTileBuf_)) ||
            FAILED(dev_->CreateShaderResourceView(stockTileBuf_, nullptr, &stockTileSrv_))) {
            safeRelease(stockTileBuf_);
            return done();
        }
        stockTileCap_ = words;
    }
    // Each light's screen rectangle: its bounding box projected (the whole screen when a corner is behind the eye).
    stockTileMask_.assign(words, 0u);
    D3D11_MAPPED_SUBRESOURCE ml;
    if (FAILED(ctx_->Map(stockLightBuf_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ml))) return done();
    auto* dst = static_cast<iw8r::StockGameLight*>(ml.pData);
    for (size_t k = 0; k < n; ++k) {
        const iw8r::StockGameLight& l = stockLights_[stockLightOrder_[k].second];
        dst[k] = l;
        {
            const uint32_t li = stockLightOrder_[k].second;
            const int s = li < lightSlotOf_.size() ? lightSlotOf_[li] : -1;
            dst[k].packed = (l.packed & ~0xFFu) | (s >= 0 && lightSlots_[size_t(s)].drawn && lightShadowsEnabled_ ? uint32_t(s) : 0xFFu);
        }
        const float r = 1.0f / l.invRadius;
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        bool behind = false;
        for (int c = 0; c < 8 && !behind; ++c) {
            const float p[3] = {l.pos[0] + ((c & 1) ? r : -r), l.pos[1] + ((c & 2) ? r : -r), l.pos[2] + ((c & 4) ? r : -r)};
            const float cx = p[0] * M[0] + p[1] * M[4] + p[2] * M[8] + M[12];
            const float cy = p[0] * M[1] + p[1] * M[5] + p[2] * M[9] + M[13];
            const float cw = p[0] * M[3] + p[1] * M[7] + p[2] * M[11] + M[15];
            if (cw <= 1e-3f) {
                behind = true;
                break;
            }
            const float sx = (cx / cw * 0.5f + 0.5f) * float(w_), sy = (0.5f - 0.5f * cy / cw) * float(h_);
            x0 = std::min(x0, sx), x1 = std::max(x1, sx), y0 = std::min(y0, sy), y1 = std::max(y1, sy);
        }
        if (behind) x0 = 0, y0 = 0, x1 = float(w_ - 1), y1 = float(h_ - 1);
        x0 = std::max(x0, 0.0f), y0 = std::max(y0, 0.0f), x1 = std::min(x1, float(w_ - 1)), y1 = std::min(y1, float(h_ - 1));
        if (x0 > x1 || y0 > y1) continue;
        const uint32_t a0 = uint32_t(x0) / kTile, a1 = uint32_t(x1) / kTile, b0 = uint32_t(y0) / kTile, b1 = uint32_t(y1) / kTile;
        const uint32_t bit = 1u << (k & 31u), word = uint32_t(k >> 5);
        for (uint32_t ty2 = b0; ty2 <= b1; ++ty2)
            for (uint32_t tx2 = a0; tx2 <= a1; ++tx2) stockTileMask_[(size_t(ty2) * tx + tx2) * 8 + word] |= bit;
    }
    ctx_->Unmap(stockLightBuf_, 0);
    D3D11_MAPPED_SUBRESOURCE mt;
    if (FAILED(ctx_->Map(stockTileBuf_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mt))) return done();
    std::memcpy(mt.pData, stockTileMask_.data(), words * 4);
    ctx_->Unmap(stockTileBuf_, 0);
    info[0] = 1, info[1] = float(tx);
    stockLightsOn_ = true;
    stockLightsDrawn_ = n;
    done();
}

bool SceneRenderer::createLightShadowResources() {
    if (lightShadowSrv_ && lightSlotSrv_) return true;
    if (!dev_) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = kLightSlotSize;
    td.MipLevels = 1;
    td.ArraySize = kLightSlots;
    td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &lightShadowTex_))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R32_FLOAT;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sv.Texture2DArray.MipLevels = 1;
    sv.Texture2DArray.ArraySize = kLightSlots;
    bool ok = SUCCEEDED(dev_->CreateShaderResourceView(lightShadowTex_, &sv, &lightShadowSrv_));
    for (int j = 0; j < kLightSlots && ok; ++j) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = DXGI_FORMAT_D32_FLOAT;
        dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dv.Texture2DArray.FirstArraySlice = UINT(j);
        dv.Texture2DArray.ArraySize = 1;
        ok = SUCCEEDED(dev_->CreateDepthStencilView(lightShadowTex_, &dv, &lightShadowDsv_[size_t(j)]));
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = UINT(kLightSlots * 80);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = 80;
    ok = ok && SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &lightSlotBuf_)) &&
         SUCCEEDED(dev_->CreateShaderResourceView(lightSlotBuf_, nullptr, &lightSlotSrv_));
    if (!ok) {
        for (auto*& d : lightShadowDsv_) safeRelease(d);
        safeRelease(lightShadowSrv_), safeRelease(lightShadowTex_), safeRelease(lightSlotSrv_), safeRelease(lightSlotBuf_);
    }
    return ok;
}

void SceneRenderer::setGtsoLut(int width, int height, int depth, const uint8_t* r) {
    safeRelease(gtsoSrv_), safeRelease(gtsoTex_);
    if (!dev_ || width < 2 || height < 2 || depth < 2 || !r) return;
    D3D11_TEXTURE3D_DESC td{};
    td.Width = UINT(width), td.Height = UINT(height), td.Depth = UINT(depth);
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{r, UINT(width), UINT(width) * UINT(height)};
    if (FAILED(dev_->CreateTexture3D(&td, &init, &gtsoTex_)) || FAILED(dev_->CreateShaderResourceView(gtsoTex_, nullptr, &gtsoSrv_)))
        safeRelease(gtsoTex_);
}

bool SceneRenderer::setStockLgvAtlas(int width, int depth, const uint32_t* tex0, const uint32_t* tex1, const float* boxes, size_t boxCount) {
    for (auto*& s : lgvSrv_) safeRelease(s);
    for (auto*& t : lgvTex_) safeRelease(t);
    safeRelease(lgvBoxBuf_);
    if (!dev_ || width < 1 || depth < 1 || !tex0 || !tex1 || !boxes || boxCount == 0) return false;
    const UINT w = UINT(width), d = UINT(depth);
    D3D11_TEXTURE3D_DESC td{};
    td.MipLevels = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.Width = w, td.Height = w, td.Depth = d;
    td.Format = DXGI_FORMAT_R11G11B10_FLOAT;  // the game's texture 0 bits as they are (research 105 section 1.3)
    D3D11_SUBRESOURCE_DATA i0{tex0, 4 * w, 4 * w * w};
    bool ok = SUCCEEDED(dev_->CreateTexture3D(&td, &i0, &lgvTex_[0])) && SUCCEEDED(dev_->CreateShaderResourceView(lgvTex_[0], nullptr, &lgvSrv_[0]));
    td.Width = 2 * w, td.Height = 3 * w;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D11_SUBRESOURCE_DATA i1{tex1, 8 * w, 8 * w * 3 * w};
    ok = ok && SUCCEEDED(dev_->CreateTexture3D(&td, &i1, &lgvTex_[1])) && SUCCEEDED(dev_->CreateShaderResourceView(lgvTex_[1], nullptr, &lgvSrv_[1]));
    if (ok) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(boxCount * 5 * 16);
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA ib{boxes, 0, 0};
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.FirstElement = 0;
        sd.Buffer.NumElements = UINT(boxCount * 5);
        ok = SUCCEEDED(dev_->CreateBuffer(&bd, &ib, &lgvBoxBuf_)) && SUCCEEDED(dev_->CreateShaderResourceView(lgvBoxBuf_, &sd, &lgvSrv_[2]));
    }
    if (ok && !lgvSampler_) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;  // the VS's trilinear fetch (texel centres are the probes)
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        ok = SUCCEEDED(dev_->CreateSamplerState(&sd, &lgvSampler_));
    }
    if (!ok) {
        for (auto*& s : lgvSrv_) safeRelease(s);
        for (auto*& t : lgvTex_) safeRelease(t);
        safeRelease(lgvBoxBuf_);
    }
    return ok;
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
    probeCb_.clear();
    probeBoxInstance_.clear();
    probeFallbackBox_ = -1;
    tomeCulledProbes_ = 0;
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
        probeBoxInstance_.push_back(int(k));
        // The fallback box (priority -FLT_MAX, as large as the map): the last one. The Umbra view never hides it,
        // and the shader starts from its image where no box is left (probeGridMin.w).
        if (b.priority < -1.0e30f) probeFallbackBox_ = int(n), cb[7] = float(b.imageIndex);
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
    probeCb_ = cb;
    applyTomeProbes();  // uploads; hides what the Umbra view hides
}

void SceneRenderer::writeGameCb() {
    if (!cbGame_) return;
    const bool on = game_.on && gameLutSrv_ != nullptr;
    float g[88] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, envBrdfSrv_ ? 1.0f : 0.0f,
                   game_.sunToward[0], game_.sunToward[1], game_.sunToward[2], on && gtsoSrv_ ? 1.0f : 0.0f,
                   game_.sunColor[0], game_.sunColor[1], game_.sunColor[2], 0,
                   game_.sky[0], game_.sky[1], game_.sky[2], game_.skyIsStock ? 1.0f : (game_.skyIsUnbaked ? 2.0f : 0.0f)};
    for (int r = 0; r < 6; ++r)
        for (int k = 0; k < 4; ++k) g[16 + 4 * r + k] = game_.fog[r][k];
    g[40] = game_.skyScale;
    g[41] = game_.bakedScale;
    static const bool noLayerProbes = perfOff("layerprobes");  // benchmark switch: probes on base batches only
    g[43] = noLayerProbes ? 0.0f : 1.0f;
    static const bool ssrShow = std::getenv("IW8R_SSR_SHOW") != nullptr;  // debug: SSR materials and their hits
    static const bool ssrAll = std::getenv("IW8R_SSR_ALL") != nullptr;  // test: every glossy stock surface traces
    g[42] = ssrAll ? 2.0f : ssrShow || gameDebugView_ == 3 ? 1.0f : 0.0f;
    // The coarse super terrain is cut away inside the near-camera layer patch (x, y, radius, on).
    g[44] = terrainCut_[0], g[45] = terrainCut_[1], g[46] = terrainCut_[2], g[47] = terrainCut_[2] > 0 ? 1.0f : 0.0f;
    g[48] = on && aoValid_ ? 1.0f : 0.0f;
    static const bool aoShow = std::getenv("IW8R_AO_SHOW") != nullptr;  // debug: the AO buffer instead of the lighting
    g[49] = aoShow || gameDebugView_ == 1 ? 1.0f : 0.0f;
    static const bool albedoShow = std::getenv("IW8R_ALBEDO_SHOW") != nullptr;  // debug: Game Lighting shows the albedo
    g[50] = albedoShow || gameDebugView_ == 2 ? 1.0f : 0.0f;
    g[11] = std::clamp(gameAoStrength_, 0.0f, 2.0f);  // gameSunCol.w: the AO strength
    g[51] = hdrFrame_ ? 1.0f : 0.0f;  // the lit scene goes to the HDR target: no post chain in the shaders
    std::memcpy(&g[52], game_.fogK, sizeof(game_.fogK));
    g[84] = float(std::fmod(gameTime_, 43200.0));  // the game's clock wraps (research 133 section 0)
    ctx_->UpdateSubresource(cbGame_, 0, nullptr, g, 0, 0);
    ctx_->PSSetConstantBuffers(4, 1, &cbGame_);
    ctx_->VSSetConstantBuffers(4, 1, &cbGame_);  // the clock of the prop waves (vs_tex_inst)
    ctx_->VSSetShaderResources(40, 3, lgvSrv_);   // the props' light grid atlas (vs_tex_inst)
    if (lgvSampler_) ctx_->VSSetSamplers(4, 1, &lgvSampler_);
    ID3D11ShaderResourceView* gl[3] = {gameLutSrv_, envBrdfSrv_, probeArrSrv_};
    ctx_->PSSetShaderResources(9, 3, gl);
    ctx_->PSSetShaderResources(13, 1, &probeGridSrv_);
    ID3D11ShaderResourceView* ao = on && aoValid_ ? aoSrv_[1] : nullptr;
    ctx_->PSSetShaderResources(14, 1, &ao);
    ctx_->PSSetShaderResources(18, 1, &gtsoSrv_);
    {
        ID3D11ShaderResourceView* ll[5] = {stockLightsOn_ ? stockLightSrv_ : nullptr, iesSrv_, stockLightsOn_ ? stockTileSrv_ : nullptr,
                                           stockLightsOn_ ? lightSlotSrv_ : nullptr, stockLightsOn_ ? lightShadowSrv_ : nullptr};
        ctx_->PSSetShaderResources(20, 5, ll);
        if (cbLocal_) ctx_->PSSetConstantBuffers(11, 1, &cbLocal_);
    }
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

void SceneRenderer::setVolumeMasks(const std::vector<std::vector<uint8_t>>& pictures, uint64_t version) {
    if (version == volMaskVersion_) return;
    volMaskVersion_ = version;
    if (volMaskSrv_) volMaskSrv_->Release(), volMaskSrv_ = nullptr;
    if (!dev_ || pictures.empty()) return;
    const UINT n = UINT(std::min<size_t>(pictures.size(), size_t(kVolMaskSlices)));
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = UINT(kVolMaskSize);
    td.MipLevels = 1;
    td.ArraySize = n;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init(n);
    for (UINT i = 0; i < n; ++i) {
        if (pictures[i].size() != size_t(kVolMaskSize) * kVolMaskSize) return;
        init[i].pSysMem = pictures[i].data();
        init[i].SysMemPitch = UINT(kVolMaskSize);
    }
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev_->CreateTexture2D(&td, init.data(), &tex)) || !tex) return;
    dev_->CreateShaderResourceView(tex, nullptr, &volMaskSrv_);
    tex->Release();
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
        ID3DBlob* hb = nullptr;
        if (SUCCEEDED(D3DCompile(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", nullptr, nullptr, "ps_meter_hdr", "ps_5_0", 0, 0, &hb, nullptr)))
            dev_->CreatePixelShader(hb->GetBufferPointer(), hb->GetBufferSize(), nullptr, &meterHdrPs_);
        safeRelease(hb);
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
    const bool hdrMeter = hdrFrame_ && hdrSrv_ && meterHdrPs_;
    ctx_->PSSetShader(hdrMeter ? meterHdrPs_ : meterPs_, nullptr, 0);
    const float cb[4] = {1.0f / std::max(game_.lutScale, 1e-6f), 0, 0, 0};
    ctx_->UpdateSubresource(cbMeter_, 0, nullptr, cb, 0, 0);
    ctx_->PSSetConstantBuffers(0, 1, &cbMeter_);
    ID3D11ShaderResourceView* srvs[2] = {hdrMeter ? hdrSrv_ : srv_, invGreySrv_};
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
    // Depth, readable by the ambient occlusion pass (typeless: D24S8 to draw, R24 to read).
    td.Format = DXGI_FORMAT_R24G8_TYPELESS;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &depth_))) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dev_->CreateDepthStencilView(depth_, &dd, &dsv_);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        dev_->CreateShaderResourceView(depth_, &sd, &depthSrv_);
    } else {
        td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;  // no readable depth: no ambient occlusion
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        dev_->CreateTexture2D(&td, nullptr, &depth_);
        dev_->CreateDepthStencilView(depth_, nullptr, &dsv_);
    }
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &hdrTex_))) {
        dev_->CreateRenderTargetView(hdrTex_, nullptr, &hdrRtv_);
        dev_->CreateShaderResourceView(hdrTex_, nullptr, &hdrSrv_);
    }
    {
        D3D11_TEXTURE2D_DESC hd = td;
        hd.Width = std::max(1u, td.Width / 2), hd.Height = std::max(1u, td.Height / 2);
        hd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hd.MipLevels = 4;
        hd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hd.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (SUCCEEDED(dev_->CreateTexture2D(&hd, nullptr, &prevColorTex_))) {
            D3D11_RENDER_TARGET_VIEW_DESC rd{};
            rd.Format = hd.Format;
            rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            dev_->CreateRenderTargetView(prevColorTex_, &rd, &prevColorRtv_);
            dev_->CreateShaderResourceView(prevColorTex_, nullptr, &prevColorSrv_);
        }
        hd.Format = DXGI_FORMAT_R32_FLOAT;
        hd.MipLevels = 1;
        hd.MiscFlags = 0;
        if (SUCCEEDED(dev_->CreateTexture2D(&hd, nullptr, &prevZTex_))) {
            dev_->CreateRenderTargetView(prevZTex_, nullptr, &prevZRtv_);
            dev_->CreateShaderResourceView(prevZTex_, nullptr, &prevZSrv_);
        }
        hd.Width = td.Width, hd.Height = td.Height;
        if (SUCCEEDED(dev_->CreateTexture2D(&hd, nullptr, &sceneZTex_))) {
            dev_->CreateRenderTargetView(sceneZTex_, nullptr, &sceneZRtv_);
            dev_->CreateShaderResourceView(sceneZTex_, nullptr, &sceneZSrv_);
        }
        sceneZValid_ = false;
    }
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int k = 0; k < 2; ++k)
        if (SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &aoTex_[k]))) {
            dev_->CreateRenderTargetView(aoTex_[k], nullptr, &aoRtv_[k]);
            dev_->CreateShaderResourceView(aoTex_[k], nullptr, &aoSrv_[k]);
        }
}

void SceneRenderer::setReferenceDecal(size_t batch, int sortKey, int revealTexture, const float params[16]) {
    if (batch >= refLayers_.size()) return;
    RefLayer& L = refLayers_[batch];
    if (!L.decal) shadowDirty_ = nearDirty_ = true;  // it no longer casts (the shadow pass skips decals)
    L.decal = true;
    L.sortKey = sortKey;
    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
    refDecalDirty_ = true;
    layerParamsDirty_ = true;
}

void SceneRenderer::setReferenceBaseParams(size_t batch, const float params[16]) {
    if (batch >= refLayers_.size() || refLayers_[batch].layer > 0 || refLayers_[batch].decal) return;
    std::memcpy(refLayers_[batch].params, params, sizeof(refLayers_[batch].params));
    layerParamsDirty_ = true;
}

void SceneRenderer::setReferenceBaseExtra(size_t batch, const float extra[16], int emissiveTexture, int transmissionTexture) {
    if (batch >= refLayers_.size() || refLayers_[batch].layer > 0 || refLayers_[batch].decal) return;
    std::memcpy(refLayers_[batch].extra, extra, sizeof(refLayers_[batch].extra));
    refLayers_[batch].emissive = emissiveTexture, refLayers_[batch].transmission = transmissionTexture;
    layerParamsDirty_ = true;
}

void SceneRenderer::setReferenceBaseDualNormal(size_t batch, const float dual[8]) {
    if (batch >= refLayers_.size() || refLayers_[batch].layer > 0 || refLayers_[batch].decal) return;
    std::memcpy(refLayers_[batch].dual, dual, sizeof(refLayers_[batch].dual));
    layerParamsDirty_ = true;
}

void SceneRenderer::setReferenceInstWave(size_t mesh, const float wave[16]) {
    if (mesh >= refTexInst_.size()) return;
    std::memcpy(refTexInst_[mesh].wave, wave, sizeof(refTexInst_[mesh].wave));
    instParamsDirty_ = true;
}

void SceneRenderer::setReferenceInstExtra(size_t mesh, const float extra[16], int emissiveTexture, int transmissionTexture) {
    if (mesh >= refTexInst_.size()) return;
    std::memcpy(refTexInst_[mesh].extra, extra, sizeof(refTexInst_[mesh].extra));
    refTexInst_[mesh].emissive = emissiveTexture, refTexInst_[mesh].transmission = transmissionTexture;
    instParamsDirty_ = true;
}

// A/B switches for the research 107 terms: IW8R_NO_EMISSIVE, IW8R_NO_TRANSMISSION, IW8R_NO_WRAP clear their "on" floats
// in RefLayer::extra as it is uploaded.
static void maskExtraTerms(float* extra) {
    static const bool noE = std::getenv("IW8R_NO_EMISSIVE") != nullptr, noT = std::getenv("IW8R_NO_TRANSMISSION") != nullptr,
                      noW = std::getenv("IW8R_NO_WRAP") != nullptr;
    if (noE) extra[3] = 0;
    if (noT) extra[7] = 0;
    if (noW) extra[11] = 0;
}

bool SceneRenderer::uploadLayerParams() {
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
            for (size_t j = 0; j < kPerCb && c * kPerCb + j < refLayers_.size(); ++j) {
                const size_t k = c * kPerCb + j;
                std::memcpy(&all[j * 64], refLayers_[k].params, sizeof(refLayers_[0].params));
                std::memcpy(&all[j * 64 + 16], refLayers_[k].extra, sizeof(refLayers_[0].extra));
                maskExtraTerms(&all[j * 64 + 16]);
                std::memcpy(&all[j * 64 + 48], refLayers_[k].dual, sizeof(refLayers_[0].dual));  // rows 12-13
                // A base batch drawing a raw-alpha texture: opaque, its alpha the specular code.
                if (refLayers_[k].layer == 0 && !refLayers_[k].decal && k < refTexIds_.size() && textureRawAlpha(refTexIds_[k]))
                    all[j * 64 + 1] = 2.0f;
            }
            if (cbLayerAll_[c]) ctx_->UpdateSubresource(cbLayerAll_[c], 0, nullptr, all.data(), 0, 0);
        }
    }
    return ctx1_ && !cbLayerAll_.empty() && cbLayerAllBatches_ == refLayers_.size();
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
    static uint64_t version = 0;
    ExtraGroupInput g;
    g.key = 1, g.version = ++version, g.batches = &batches, g.nearOnly = true;
    return setReferenceExtras({g});
}

bool appendReferenceDecalsOld_unused(const std::vector<std::vector<GpuTexVertex>>& batches);
bool SceneRenderer_appendReferenceDecalsOld_unused() { return false; }
#if 0
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
    {
        size_t total = 0;
        for (const auto& b : batches) total += b.size();
        all.reserve(total);  // one allocation (growing by doubling briefly needed up to 3x)
    }
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
#endif

void SceneRenderer::releaseExtraGroups() {
    for (auto& g : extraGroups_) safeRelease(g.vb);
    extraGroups_.clear();
}

bool SceneRenderer::setReferenceExtras(const std::vector<ExtraGroupInput>& groups) {
    const size_t mainN = refTexExtraFrom_ != ~size_t(0) && refTexExtraFrom_ <= refTexIds_.size() ? refTexExtraFrom_ : refTexIds_.size();
    // The extras' per-batch state now, by group key (kept for the groups given again).
    struct Slice {
        size_t first = 0;
        size_t index = 0;
    };
    std::map<uint64_t, Slice> old;
    {
        size_t at = mainN;
        for (size_t i = 0; i < extraGroups_.size(); ++i) old[extraGroups_[i].key] = {at, i}, at += extraGroups_[i].first.size();
    }
    const std::vector<int> oTex(refTexIds_.begin() + std::ptrdiff_t(std::min(mainN, refTexIds_.size())), refTexIds_.end());
    const std::vector<int> oNrm(refNrmIds_.begin() + std::ptrdiff_t(std::min(mainN, refNrmIds_.size())), refNrmIds_.end());
    const std::vector<RefLayer> oLay(refLayers_.begin() + std::ptrdiff_t(std::min(mainN, refLayers_.size())), refLayers_.end());
    std::vector<ExtraGroupGpu> next;
    std::vector<int> reusedFrom;  // per next group: the old group's first extra index, -1 = new
    std::vector<uint8_t> taken(extraGroups_.size(), 0);
    bool worldChanged = false;
    for (const ExtraGroupInput& in : groups) {
        const auto it = old.find(in.key);
        if (it != old.end() && !taken[it->second.index] && extraGroups_[it->second.index].version == in.version &&
            (!in.batches || in.batches->size() == extraGroups_[it->second.index].first.size())) {
            taken[it->second.index] = 1;
            next.push_back(extraGroups_[it->second.index]);
            extraGroups_[it->second.index].vb = nullptr;  // moved
            reusedFrom.push_back(int(it->second.first - mainN));
            continue;
        }
        if (!in.batches) continue;  // asked to reuse a group that is not there
        ExtraGroupGpu g;
        g.key = in.key, g.version = in.version, g.world = in.world, g.nearOnly = in.nearOnly;
        // Each batch goes straight into one buffer (no CPU copy of the whole group: Verdansk's terrain layer group
        // briefly needed two to three times its size while a concatenated vector grew, std::bad_alloc 2026-11-13).
        size_t total = 0;
        for (size_t j = 0; j < in.batches->size(); ++j) {
            const auto& b = (*in.batches)[j];
            g.first.push_back(UINT(total));
            g.count.push_back(UINT(b.size()));
            total += b.size();
            iw8r::AABB bb;
            for (const GpuTexVertex& v : b) bb.add(iw8r::Vec3{v.x, v.y, v.z});
            g.bounds.push_back(bb);
            g.layer.push_back(in.world && j < in.layers.size() ? in.layers[j] : 0);
        }
        if (total > 0 && dev_ && total * sizeof(GpuTexVertex) < 0xFFFFFFFFull) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = UINT(total * sizeof(GpuTexVertex));
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            if (FAILED(dev_->CreateBuffer(&bd, nullptr, &g.vb))) {
                g.vb = nullptr;
            } else {
                for (size_t j = 0; j < in.batches->size(); ++j) {
                    const auto& b = (*in.batches)[j];
                    if (b.empty()) continue;
                    const D3D11_BOX box{UINT(g.first[j] * sizeof(GpuTexVertex)), 0, 0, UINT((g.first[j] + b.size()) * sizeof(GpuTexVertex)), 1, 1};
                    ctx_->UpdateSubresource(g.vb, 0, &box, b.data(), 0, 0);
                }
            }
        }
        worldChanged |= g.world;
        next.push_back(std::move(g));
        reusedFrom.push_back(-1);
    }
    for (size_t i = 0; i < extraGroups_.size(); ++i)
        if (!taken[i]) worldChanged |= extraGroups_[i].world, safeRelease(extraGroups_[i].vb);
    extraGroups_ = std::move(next);
    // The per-batch arrays: the main reference, then every group in order.
    refTexFirst_.resize(mainN), refTexCount_.resize(mainN), refTexIds_.resize(mainN), refTexChunks_.resize(mainN);
    refLayers_.resize(mainN), refNrmIds_.resize(mainN), refTexVb_.resize(mainN, nullptr), refTexNearOnly_.resize(mainN, 0);
    refTexExtraFrom_ = mainN;
    refTexExtraBounds_.clear();
    for (size_t gi = 0; gi < extraGroups_.size(); ++gi) {
        const ExtraGroupGpu& g = extraGroups_[gi];
        for (size_t j = 0; j < g.first.size(); ++j) {
            refTexFirst_.push_back(g.first[j]);
            refTexCount_.push_back(g.count[j]);
            refTexChunks_.emplace_back();  // drawn whole (frustum test on the bounds)
            refTexExtraBounds_.push_back(g.bounds[j]);
            refTexVb_.push_back(g.vb);
            refTexNearOnly_.push_back(g.nearOnly ? 1 : 0);
            if (reusedFrom[gi] >= 0) {
                const size_t o = size_t(reusedFrom[gi]) + j;
                refTexIds_.push_back(o < oTex.size() ? oTex[o] : -1);
                refNrmIds_.push_back(o < oNrm.size() ? oNrm[o] : -1);
                refLayers_.push_back(o < oLay.size() ? oLay[o] : RefLayer{});
            } else {
                refTexIds_.push_back(-1);
                refNrmIds_.push_back(-1);
                RefLayer L;
                L.layer = g.layer[j];
                L.decal = !g.world;  // decal-like extras are never drawn opaque; ready once setReferenceDecal gives textures
                refLayers_.push_back(L);
            }
        }
    }
    // Layer order: the main reference's layer batches, then the world groups' (by layer).
    refLayerOrder_.erase(std::remove_if(refLayerOrder_.begin(), refLayerOrder_.end(), [&](size_t k) { return k >= mainN; }), refLayerOrder_.end());
    for (size_t k = mainN; k < refLayers_.size(); ++k)
        if (refLayers_[k].layer > 0 && !refLayers_[k].decal) refLayerOrder_.push_back(k);
    std::stable_sort(refLayerOrder_.begin(), refLayerOrder_.end(), [&](size_t a, size_t b) { return refLayers_[a].layer < refLayers_[b].layer; });
    refDecalDirty_ = true;
    layerParamsDirty_ = true;
    if (worldChanged) shadowDirty_ = nearDirty_ = true;
    return true;
}

bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers) {
    safeRelease(vbRefTexExtra_);
    releaseExtraGroups();
    refTexExtraFrom_ = ~size_t(0);
    refTexExtraBounds_.clear();
    refLayers_.assign(batches.size(), RefLayer{});
    refNrmIds_.assign(batches.size(), -1);
    refTexVb_.assign(batches.size(), nullptr);
    refTexNearOnly_.assign(batches.size(), 0);
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

void SceneRenderer::setReferenceTomeVisibility(std::vector<uint32_t> surfaceBits, std::vector<uint32_t> modelBits) {
    tomeSurfBits_ = std::move(surfaceBits);
    tomeModelBits_ = std::move(modelBits);
    tomeDirty_ = true;
}

void SceneRenderer::setReferenceTomeClasses(std::vector<uint32_t> lightBits, std::vector<uint32_t> probeBits, std::vector<uint32_t> decalBits) {
    tomeLightBits_ = std::move(lightBits);
    tomeDecalBits_ = std::move(decalBits);
    const bool probesChanged = probeBits != tomeProbeBits_;
    tomeProbeBits_ = std::move(probeBits);
    if (probesChanged) applyTomeProbes();
    if (tomeLightBits_.empty()) tomeCulledLights_ = 0;
    if (tomeDecalBits_.empty()) tomeCulledDecals_ = 0;
}

void SceneRenderer::setReferenceDecalCollection(size_t batch, int collection) {
    if (batch < refLayers_.size()) refLayers_[batch].tomeCollection = collection;
}

// The probe constants with the boxes the tome hides made empty (a half size far below zero: no point is inside, and
// the box is never the nearest one). The fallback box is kept: a pixel no visible box covers takes it, as in the game.
void SceneRenderer::applyTomeProbes() {
    tomeCulledProbes_ = 0;
    if (!cbProbes_ || probeCb_.empty()) return;
    std::vector<float> cb = probeCb_;
    if (!tomeProbeBits_.empty())
        for (size_t i = 0; i < probeBoxInstance_.size(); ++i) {
            if (int(i) == probeFallbackBox_ || !tomeBitClear(tomeProbeBits_, probeBoxInstance_[i])) continue;
            float* e = cb.data() + 12 + 20 * i;
            e[7] = e[11] = e[15] = -1.0e35f;
            ++tomeCulledProbes_;
        }
    ctx_->UpdateSubresource(cbProbes_, 0, nullptr, cb.data(), 0, 0);
}

void SceneRenderer::bindTomeVisibility() {
    wind_[3] = 0.0f;
    if (!dev_ || tomeSurfBits_.empty()) return;
    if (!tomeBuf_ || tomeBufWords_ < tomeSurfBits_.size()) {
        safeRelease(tomeSrv_);
        safeRelease(tomeBuf_);
        tomeBufWords_ = std::max<size_t>(tomeSurfBits_.size(), 64);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(tomeBufWords_ * 4);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &tomeBuf_))) {
            tomeBufWords_ = 0;
            return;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_UINT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.FirstElement = 0;
        sd.Buffer.NumElements = UINT(tomeBufWords_);
        if (FAILED(dev_->CreateShaderResourceView(tomeBuf_, &sd, &tomeSrv_))) {
            safeRelease(tomeBuf_);
            tomeBufWords_ = 0;
            return;
        }
        tomeDirty_ = true;
    }
    if (tomeDirty_) {
        std::vector<uint32_t> words(tomeBufWords_, 0xFFFFFFFFu);  // past the tome's surfaces: drawn
        std::copy(tomeSurfBits_.begin(), tomeSurfBits_.end(), words.begin());
        ctx_->UpdateSubresource(tomeBuf_, 0, nullptr, words.data(), 0, 0);
        tomeDirty_ = false;
    }
    ctx_->VSSetShaderResources(30, 1, &tomeSrv_);
    wind_[3] = 1.0f;
}

void SceneRenderer::cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle) {
    // Cull by tome: a placement whose collection the tome hides (ids past the bits, or unknown, are drawn).
    tomeCulledProps_ = 0;
    const auto tomeHides = [&](const GpuTexInstanced& m, size_t i) {
        if (tomeModelBits_.empty() || i >= m.ids.size()) return false;
        const uint32_t id = m.ids[i];
        if (id == 0xFFFFFFFFu || (id >> 5) >= tomeModelBits_.size()) return false;
        return ((tomeModelBits_[id >> 5] >> (id & 31u)) & 1u) == 0u;
    };
    const iw8r::Frustum fr = iw8r::frustumFromViewProj(viewProj);
    instCulledLast_ = 0;
    std::vector<float> keep, keepBig;
    static const float bigPx = std::getenv("IW8R_PREPASS_PX") ? std::max(0.0f, float(std::atof(std::getenv("IW8R_PREPASS_PX")))) : 24.0f;
    static const float kPx = std::getenv("IW8R_INST_CULL_PX") ? std::max(0.5f, float(std::atof(std::getenv("IW8R_INST_CULL_PX")))) : 4.0f;
    static const bool noLodOut = std::getenv("IW8R_NO_FOLIAGE_LOD_OUT") != nullptr;
    // The game's field-of-view factor on LOD distances (GT R_SetLodParms): tanHalfFovY x 2.1186731, which is 1 at the
    // game's own view. The view-projection's second column holds the up axis x (1 / tanHalfFovY). IW8R_NO_LOD_FOV: off
    // (a bench switch).
    static const bool noLodFov = std::getenv("IW8R_NO_LOD_FOV") != nullptr;
    const float ysProj = std::sqrt(viewProj[1] * viewProj[1] + viewProj[5] * viewProj[5] + viewProj[9] * viewProj[9]);
    const float lodFov = noLodFov || !(ysProj > 1e-6f) ? 1.0f : iw8r::lodFovFactor(1.0f / ysProj);
    // IW8R_LOD_CENTRE_DIST: the distances of before 2026-11-14 (to the model's centre, every placement's scale 1; a
    // bench switch). The game measures to the nearest point of the placement's box.
    static const bool lodCentreDist = std::getenv("IW8R_LOD_CENTRE_DIST") != nullptr;
    // The visible placements, those large on screen first: the depth pre-pass draws only that prefix (`big`). One
    // buffer instead of two of the full size (Verdansk's instance buffers were 877 MB with every prop drawn).
    const auto upload = [&](GpuTexInstanced& g) {
        g.big = unsigned(keepBig.size() / 24);
        g.visible = unsigned((keepBig.size() + keep.size()) / 24);
        if (g.visible) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (SUCCEEDED(ctx_->Map(g.visInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, keepBig.data(), keepBig.size() * sizeof(float));
                std::memcpy(static_cast<char*>(ms.pData) + keepBig.size() * sizeof(float), keep.data(), keep.size() * sizeof(float));
                ctx_->Unmap(g.visInst, 0);
            } else {
                g.visible = g.instances;  // fall back to every placement
                g.big = 0;
            }
        }
    };
    // LOD groups (plan 1.13): the placement is tested once against the group's bounding sphere (all its LODs), then
    // goes to the mesh whose distance window holds it.
    struct PlaceEval {
        float dist, rad;
        float lod;  // the game's LOD distance: to the nearest point of the placement's box
        bool out;
    };
    std::vector<PlaceEval> ev;
    for (const std::vector<size_t>& grp : texLodGroups_) {
        if (grp.empty()) continue;
        const GpuTexInstanced& lead = refTexInst_[grp[0]];
        float radius = lead.radius;
        for (size_t mi : grp) {
            const GpuTexInstanced& m = refTexInst_[mi];
            const float d[3] = {m.centre[0] - lead.centre[0], m.centre[1] - lead.centre[1], m.centre[2] - lead.centre[2]};
            radius = std::max(radius, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + m.radius);
        }
        // The model's box: every level's together.
        float gLo[3] = {1e30f, 1e30f, 1e30f}, gHi[3] = {-1e30f, -1e30f, -1e30f};
        for (size_t mi : grp) {
            const GpuTexInstanced& m = refTexInst_[mi];
            for (int c = 0; c < 3; ++c)
                gLo[c] = std::min(gLo[c], m.boxMid[c] - m.boxHalf[c]), gHi[c] = std::max(gHi[c], m.boxMid[c] + m.boxHalf[c]);
        }
        const float gMid[3] = {0.5f * (gLo[0] + gHi[0]), 0.5f * (gLo[1] + gHi[1]), 0.5f * (gLo[2] + gHi[2])};
        const float gHalf[3] = {0.5f * (gHi[0] - gLo[0]), 0.5f * (gHi[1] - gLo[1]), 0.5f * (gHi[2] - gLo[2])};
        const bool scaled = !lodCentreDist && lead.cullScale.size() * 12 == lead.rows.size();
        const size_t n = lead.rows.size() / 12;
        ev.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float* r = &lead.rows[i * 12];
            const float c[3] = {r[0] * lead.centre[0] + r[1] * lead.centre[1] + r[2] * lead.centre[2] + r[3],
                                r[4] * lead.centre[0] + r[5] * lead.centre[1] + r[6] * lead.centre[2] + r[7],
                                r[8] * lead.centre[0] + r[9] * lead.centre[1] + r[10] * lead.centre[2] + r[11]};
            const float sc = std::sqrt(r[0] * r[0] + r[4] * r[4] + r[8] * r[8]);
            PlaceEval& e = ev[i];
            e.rad = radius * std::max(sc, 1e-6f);
            e.out = tomeHides(lead, i);
            if (e.out) ++tomeCulledProps_;
            for (const auto& pl : fr.planes)
                if (pl.n.x * c[0] + pl.n.y * c[1] + pl.n.z * c[2] - pl.d > e.rad) {
                    e.out = true;
                    break;
                }
            const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
            e.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!e.out) e.out = e.dist > e.rad && e.rad < e.dist * pixelAngle * kPx;
            e.lod = lodCentreDist ? e.dist : iw8r::placementBoxDistance(r, gMid, gHalf, eye);
            // Past the model's last LOD distance x the placement's scale the game does not draw it (research 119).
            if (!e.out && !noLodOut) e.out = iw8r::placementLodCulled(e.lod, lodFov, scaled ? lead.cullScale[i] : 1.0f, lead.lodOut);
            if (e.out) ++instCulledLast_;
        }
        for (size_t mi : grp) {
            GpuTexInstanced& g = refTexInst_[mi];
            if (!g.visInst) continue;
            keep.clear();
            keepBig.clear();
            // The last level has no end of its own: the placement is left out by the rule above (e.out).
            const bool last = g.lodOut > 0 && g.lodFar >= g.lodOut;
            const float farD = g.lodFar > 0 && !last ? g.lodFar : 1e30f;
            for (size_t i = 0; i < n; ++i) {
                const PlaceEval& e = ev[i];
                const float dSel = e.lod * lodFov;  // the level by distance x the view factor
                if (e.out || dSel < g.lodNear || dSel >= farD) continue;
                const float* r = &g.rows[i * 12];
                std::vector<float>& to = e.dist <= e.rad || e.rad >= e.dist * pixelAngle * bigPx ? keepBig : keep;
                to.insert(to.end(), r, r + 12);
                to.insert(to.end(), &g.light[i * 12], &g.light[i * 12] + 12);
            }
            upload(g);
        }
    }
    for (GpuTexInstanced& g : refTexInst_) {
        if (g.lodGroup >= 0) continue;  // above
        if (!g.vb || !g.inst || !g.visInst || g.rows.empty()) continue;
        keep.clear();
        keepBig.clear();
        const size_t n = g.rows.size() / 12;
        for (size_t i = 0; i < n; ++i) {
            const float* r = &g.rows[i * 12];
            const float c[3] = {r[0] * g.centre[0] + r[1] * g.centre[1] + r[2] * g.centre[2] + r[3],
                                r[4] * g.centre[0] + r[5] * g.centre[1] + r[6] * g.centre[2] + r[7],
                                r[8] * g.centre[0] + r[9] * g.centre[1] + r[10] * g.centre[2] + r[11]};
            const float sc = std::sqrt(r[0] * r[0] + r[4] * r[4] + r[8] * r[8]);
            const float rad = g.radius * std::max(sc, 1e-6f);
            bool out = tomeHides(g, i);
            if (out) ++tomeCulledProps_;
            for (const auto& pl : fr.planes)
                if (pl.n.x * c[0] + pl.n.y * c[1] + pl.n.z * c[2] - pl.d > rad) {
                    out = true;
                    break;
                }
            if (!out) {
                const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                out = dist > rad && rad < dist * pixelAngle * kPx;  // radius under k pixels (4: Verdansk 37 -> 15.5 ms)
                // A stock prop past its last LOD distance x the placement's scale: the game does not draw it (research
                // 100 section 3.3, 119).
                if (!out && !noLodOut && g.lodOut > 0) {
                    const float ld = lodCentreDist ? dist : iw8r::placementBoxDistance(r, g.boxMid, g.boxHalf, eye);
                    const float cs = !lodCentreDist && g.cullScale.size() == n ? g.cullScale[i] : 1.0f;
                    out = iw8r::placementLodCulled(ld, lodFov, cs, g.lodOut);
                }
            }
            if (out) {
                ++instCulledLast_;
                continue;
            }
            {
                const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                std::vector<float>& to = dist <= rad || rad >= dist * pixelAngle * bigPx ? keepBig : keep;
                to.insert(to.end(), r, r + 12);
                to.insert(to.end(), &g.light[i * 12], &g.light[i * 12] + 12);
            }
        }
        upload(g);
    }
}

namespace {
size_t bufferBytes(ID3D11Buffer* b) {
    if (!b) return 0;
    D3D11_BUFFER_DESC d{};
    b->GetDesc(&d);
    return d.ByteWidth;
}
double formatBytesPerTexel(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM:
        return f == DXGI_FORMAT_BC4_UNORM || f == DXGI_FORMAT_BC4_SNORM ? 0.5 : 0.5;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16: case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_TYPELESS:
        return 1;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT:
        return 8;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return 16;
    case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_TYPELESS:
        return 2;
    default:
        return 4;
    }
}
size_t texture2dBytes(ID3D11Texture2D* t) {
    if (!t) return 0;
    D3D11_TEXTURE2D_DESC d{};
    t->GetDesc(&d);
    double bytes = 0;
    for (UINT m = 0; m < std::max(1u, d.MipLevels); ++m)
        bytes += double(std::max(1u, d.Width >> m)) * double(std::max(1u, d.Height >> m)) * formatBytesPerTexel(d.Format);
    return size_t(bytes * std::max(1u, d.ArraySize));
}
}  // namespace

std::string SceneRenderer::gpuMemoryReport() const {
    size_t props = 0, propInst = 0, world = 0, extras = 0, streams = 0, lightmaps = 0, probes = 0, shadows = 0, targets = 0;
    for (const auto& g : refTexInst_)
        props += bufferBytes(g.vb) + bufferBytes(g.ib), propInst += bufferBytes(g.inst) + bufferBytes(g.visInst);
    for (const auto& g : refInst_) props += bufferBytes(g.vb), propInst += bufferBytes(g.inst);
    world += bufferBytes(vbRefTex_) + bufferBytes(vbRef_);
    {
        std::vector<ID3D11Buffer*> seen;
        for (const auto& g : extraGroups_) seen.push_back(g.vb);
        for (ID3D11Buffer* b : refTexVb_)
            if (b && std::find(seen.begin(), seen.end(), b) == seen.end()) seen.push_back(b), world += bufferBytes(b);
    }
    for (const auto& g : extraGroups_) extras += bufferBytes(g.vb);
    for (const auto& [id, s] : gpu_) streams += bufferBytes(s.vb) + bufferBytes(s.ib);
    for (ID3D11Texture2D* t : stockLmTex_) lightmaps += texture2dBytes(t);
    for (ID3D11Texture2D* t : gameLmTex_) lightmaps += texture2dBytes(t);
    probes += texture2dBytes(probeArrTex_) + texture2dBytes(probeTex_);
    shadows += texture2dBytes(shadowTex_) + texture2dBytes(lightShadowTex_);
    targets += texture2dBytes(color_) + texture2dBytes(depth_) + texture2dBytes(hdrTex_) + texture2dBytes(prevColorTex_) +
               texture2dBytes(prevZTex_) + texture2dBytes(aoTex_[0]) + texture2dBytes(aoTex_[1]);
    const auto mb = [](size_t b) { return std::to_string((b + (1u << 19)) >> 20); };
    return "props " + mb(props) + "+" + mb(propInst) + " world " + mb(world) + " extras " + mb(extras) + " streams " + mb(streams) +
           " textures " + mb(textureBytes_) + " lightmaps " + mb(lightmaps) + " probes " + mb(probes) + " shadows " + mb(shadows) +
           " targets " + mb(targets);
}

bool SceneRenderer::setReferenceTexInstanceLight(size_t mesh, const std::vector<float>& light) {
    if (!dev_ || mesh >= refTexInst_.size()) return false;
    GpuTexInstanced& g = refTexInst_[mesh];
    const size_t count = g.instances;
    if (!g.inst || count == 0 || light.size() != count * 12 || g.rows.size() != count * 12) return false;
    std::vector<float> gpuLight(count * 12);
    for (size_t i = 0; i < count; ++i) {
        const size_t s = i < g.order.size() ? g.order[i] : i;  // the caller's order -> the cells' order
        std::memcpy(&gpuLight[i * 12], &light[s * 12], 12 * sizeof(float));
    }
    std::vector<float> inst24(count * 24);
    for (size_t i = 0; i < count; ++i) {
        std::memcpy(&inst24[i * 24], &g.rows[i * 12], 12 * sizeof(float));
        std::memcpy(&inst24[i * 24 + 12], &gpuLight[i * 12], 12 * sizeof(float));
    }
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = UINT(count * 24 * sizeof(float));
    D3D11_SUBRESOURCE_DATA init{inst24.data(), 0, 0};
    ID3D11Buffer* fresh = nullptr;
    if (FAILED(dev_->CreateBuffer(&bd, &init, &fresh))) return false;
    safeRelease(g.inst);
    g.inst = fresh;
    g.light = std::move(gpuLight);  // the culling copies it into the visible-placement buffers every frame
    return true;
}

bool SceneRenderer::setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes) {
    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.ib), safeRelease(g.inst), safeRelease(g.visInst);
    refTexInst_.clear();
    if (!dev_ || !vsTexInst_ || !layoutTexInst_) return meshes.empty();
    for (const InstancedTexMesh& m : meshes) {
        const size_t count = m.rows.size() / 12;
        GpuTexInstanced g;  // kept even when empty: the index is the caller's mesh index (textures are set by it)
        const PropMeshData* pm = m.mesh.get();
        if (pm && !pm->vertices.empty() && pm->indices.size() >= 3 && count > 0) {
            D3D11_BUFFER_DESC bd{};
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            if (pm->vertices.size() <= 65536) {
                const std::vector<uint16_t> i16(pm->indices.begin(), pm->indices.end());
                bd.ByteWidth = UINT(i16.size() * sizeof(uint16_t));
                D3D11_SUBRESOURCE_DATA ii{i16.data(), 0, 0};
                if (FAILED(dev_->CreateBuffer(&bd, &ii, &g.ib))) g.ib = nullptr;
                g.ibFormat = DXGI_FORMAT_R16_UINT;
            } else {
                bd.ByteWidth = UINT(pm->indices.size() * sizeof(uint32_t));
                D3D11_SUBRESOURCE_DATA ii{pm->indices.data(), 0, 0};
                if (FAILED(dev_->CreateBuffer(&bd, &ii, &g.ib))) g.ib = nullptr;
                g.ibFormat = DXGI_FORMAT_R32_UINT;
            }
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.ByteWidth = UINT(pm->vertices.size() * sizeof(PropVertex));
            D3D11_SUBRESOURCE_DATA init{pm->vertices.data(), 0, 0};
            if (g.ib && SUCCEEDED(dev_->CreateBuffer(&bd, &init, &g.vb))) {
                // The placements in cell order (stable: the meshes of one LOD group, which hold the same placements in
                // the same order, get the same order).
                constexpr double kCell = 2048.0;
                std::vector<uint32_t> key(count);
                for (size_t i = 0; i < count; ++i) {
                    const auto cellOf = [&](float v) { return uint32_t(std::clamp(int(std::floor(v / kCell)) + 32768, 0, 65535)); };
                    key[i] = (cellOf(m.rows[i * 12 + 3]) << 16) | cellOf(m.rows[i * 12 + 7]);
                }
                g.order.resize(count);
                for (size_t i = 0; i < count; ++i) g.order[i] = uint32_t(i);
                std::stable_sort(g.order.begin(), g.order.end(), [&](uint32_t a, uint32_t b) { return key[a] < key[b]; });
                // 24 floats per instance on the GPU: the 3 transform rows, then the prop's light (research 105).
                const bool haveLight = m.light.size() == count * 12;
                if (m.ids.size() == count) {
                    g.ids.resize(count);
                    for (size_t i = 0; i < count; ++i) g.ids[i] = m.ids[g.order[i]];
                }
                if (m.cullScale.size() == count &&
                    std::any_of(m.cullScale.begin(), m.cullScale.end(), [](float v) { return v != 1.0f; })) {
                    g.cullScale.resize(count);
                    for (size_t i = 0; i < count; ++i) g.cullScale[i] = m.cullScale[g.order[i]];
                }
                g.rows.resize(count * 12);
                g.light.assign(count * 12, 0.0f);
                for (size_t i = 0; i < count; ++i) {
                    const size_t s = g.order[i];
                    std::memcpy(&g.rows[i * 12], &m.rows[s * 12], 12 * sizeof(float));
                    if (haveLight) std::memcpy(&g.light[i * 12], &m.light[s * 12], 12 * sizeof(float));
                    else g.light[i * 12] = -1.0f;  // none
                }
                std::vector<float> inst24(count * 24);
                for (size_t i = 0; i < count; ++i) {
                    std::memcpy(&inst24[i * 24], &g.rows[i * 12], 12 * sizeof(float));
                    std::memcpy(&inst24[i * 24 + 12], &g.light[i * 12], 12 * sizeof(float));
                }
                bd.ByteWidth = UINT(count * 24 * sizeof(float));
                D3D11_SUBRESOURCE_DATA iinit{inst24.data(), 0, 0};
                if (SUCCEEDED(dev_->CreateBuffer(&bd, &iinit, &g.inst))) {
                    g.vertices = unsigned(pm->indices.size());
                    g.instances = unsigned(count);
                    g.visible = g.instances;
                    g.lodOut = m.lodOut;
                    g.lodGroup = m.lodGroup;
                    g.lodNear = m.lodNear, g.lodFar = m.lodFar;
                    // The model's bounding sphere (box centre, half diagonal).
                    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
                    for (const PropVertex& v : pm->vertices) {
                        const float p[3] = {v.x, v.y, v.z};
                        for (int c = 0; c < 3; ++c) lo[c] = std::min(lo[c], p[c]), hi[c] = std::max(hi[c], p[c]);
                    }
                    float r2 = 0;
                    for (int c = 0; c < 3; ++c) g.centre[c] = 0.5f * (lo[c] + hi[c]), r2 += 0.25f * (hi[c] - lo[c]) * (hi[c] - lo[c]);
                    g.radius = std::sqrt(r2);
                    for (int c = 0; c < 3; ++c) g.boxMid[c] = g.centre[c], g.boxHalf[c] = 0.5f * (hi[c] - lo[c]);
                    if (m.hasModelBox)  // the LOD and cull distance are the whole model's, not this material part's
                        for (int c = 0; c < 3; ++c)
                            g.boxMid[c] = 0.5f * (m.modelBox[c] + m.modelBox[c + 3]), g.boxHalf[c] = 0.5f * (m.modelBox[c + 3] - m.modelBox[c]);
                    // The cells: each one's placements' bounding spheres in world space.
                    for (size_t i = 0; i < count; ++i) {
                        if (i == 0 || key[g.order[i]] != key[g.order[i - 1]]) g.cells.push_back({iw8r::AABB{}, uint32_t(i), 0});
                        GpuTexInstanced::InstCell& c = g.cells.back();
                        ++c.count;
                        const float* r = &g.rows[i * 12];
                        double wc[3], scale = 0;
                        for (int k = 0; k < 3; ++k) {
                            wc[k] = r[k * 4 + 0] * g.centre[0] + r[k * 4 + 1] * g.centre[1] + r[k * 4 + 2] * g.centre[2] + r[k * 4 + 3];
                            scale = std::max(scale, std::sqrt(double(r[k * 4 + 0]) * r[k * 4 + 0] + double(r[k * 4 + 1]) * r[k * 4 + 1] +
                                                              double(r[k * 4 + 2]) * r[k * 4 + 2]));
                        }
                        const double rad = g.radius * scale * 1.0001 + 1.0;
                        c.box.add(iw8r::Vec3{wc[0] - rad, wc[1] - rad, wc[2] - rad});
                        c.box.add(iw8r::Vec3{wc[0] + rad, wc[1] + rad, wc[2] + rad});
                    }
                    D3D11_BUFFER_DESC dd = bd;
                    dd.Usage = D3D11_USAGE_DYNAMIC;
                    dd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    dev_->CreateBuffer(&dd, nullptr, &g.visInst);
                } else {
                    safeRelease(g.vb);
                }
            }
            if (!g.vb) safeRelease(g.ib);
        }
        refTexInst_.push_back(g);
    }
    // The LOD groups: meshes of one model with the same placements (the culling decides once per placement).
    texLodGroups_.clear();
    std::unordered_map<int, size_t> groupAt;
    for (size_t i = 0; i < refTexInst_.size(); ++i) {
        const GpuTexInstanced& g = refTexInst_[i];
        if (g.lodGroup < 0 || !g.vb || !g.inst) continue;
        const auto [it, fresh] = groupAt.emplace(g.lodGroup, texLodGroups_.size());
        if (fresh) texLodGroups_.emplace_back();
        texLodGroups_[it->second].push_back(i);
    }
    // A group whose meshes disagree on the placements cannot share one decision: its meshes are culled on their own.
    for (auto& grp : texLodGroups_) {
        bool same = true;
        for (size_t k = 1; k < grp.size() && same; ++k) same = refTexInst_[grp[k]].rows.size() == refTexInst_[grp[0]].rows.size();
        if (!same) {
            for (size_t i : grp) refTexInst_[i].lodGroup = -1, refTexInst_[i].lodNear = 0, refTexInst_[i].lodFar = 0;
            grp.clear();
        }
    }
    return true;
}

void SceneRenderer::drawReferenceTexInstances() {
    if (refTexInst_.empty() || !vsTexInst_ || !layoutTexInst_ || !psTex_) return;
    IW8R_PROF(texPsOverride_ ? "d.props_pre" : "d.props");
    ctx_->IASetInputLayout(layoutTexInst_);
    ctx_->VSSetShader(vsTexInst_, nullptr, 0);
    ctx_->PSSetShader(texPsOverride_ ? texPsOverride_ : (filterPs_ ? filterPs_ : psTex_), nullptr, 0);
    ctx_->PSSetSamplers(0, 1, &sampler_);
    static const bool hardCut = std::getenv("IW8R_HARD_CUTOUT") != nullptr;
    const bool soft = !texPsOverride_ && !hardCut && blend_;
    ID3D11BlendState* prevBlend = nullptr;
    float prevFactor[4] = {0, 0, 0, 0};
    UINT prevMask = 0xffffffff;
    ctx_->OMGetBlendState(&prevBlend, prevFactor, &prevMask);
    if (soft) {
        const float bf[4] = {0, 0, 0, 0};
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
    }
    // Main pass: each mesh's normal map (the flat one without) and its material constants at its offset in b7.
    const bool propNormals = !texPsOverride_ && flatNrmTex_ >= 0 && size_t(flatNrmTex_) < textures_.size();
    constexpr size_t kInstPerCb = 256;
    // The props' constants: on the shading pass and the depth pre-pass (raw-alpha meshes are not cut out).
    const bool instCb = (propNormals || (texPsOverride_ && texPsOverride_ == psPrepass_)) && uploadInstParams();
    if (cbBatch_) {
        const float flags[8] = {propNormals ? 1.0f : 0.0f, soft ? -1.0f : 0.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};
        writeBatchCb(flags);
        ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
        ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
    }
    ID3D11ShaderResourceView* noNrm = nullptr;
    ctx_->PSSetShaderResources(1, 1, &noNrm);
    const UINT strides[2] = {UINT(sizeof(PropVertex)), UINT(24 * sizeof(float))}, offsets[2] = {0, 0};
    // The props' decal meshes after the others (setReferenceInstDecal); none in the depth pre-pass.
    ID3D11RasterizerState* savedRs = nullptr;
    ID3D11DepthStencilState* savedDs = nullptr;
    UINT savedRef = 0;
    bool decalStateSet = false;
    // Pass 0: the props; 1: their decal meshes; 2: see-through meshes (glass), blended by their alpha.
    bool glassStateSet = false;
    // The extra layers of model-layered materials (research 132 G9) over their meshes, between the props and their decal
    // meshes (the world's order: layers, then decals), as the world's layer pass: test-only depth, blended by the
    // layer's reveal step and alpha (or multiplied), its constants in b7. Its own states are put back afterwards.
    static const bool noPropLayers = perfOff("proplayers");  // benchmark switch: IW8R_PERF_OFF=proplayers
    const auto drawPropLayers = [&] {
        if (texPsOverride_ || !cbLayer_ || noPropLayers) return;
        bool layerStateSet = false;
        ID3D11DepthStencilState* prevDs = nullptr;
        UINT prevRef = 0;
        ID3D11BlendState* prevBs = nullptr;
        float prevBf[4] = {0, 0, 0, 0};
        UINT prevBm = 0xffffffff;
        for (const auto& g : refTexInst_) {
            if (g.layers.empty() || !g.vb || !g.inst || g.drawClass != 0) continue;
            if (!layerStateSet) {
                layerStateSet = true;
                ctx_->OMGetDepthStencilState(&prevDs, &prevRef);
                ctx_->OMGetBlendState(&prevBs, prevBf, &prevBm);
                ctx_->OMSetDepthStencilState(dsTest_, 0);
                if (cbBatch_) {
                    const float flags[8] = {0, 1.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};  // no normal map; blended
                    writeBatchCb(flags);
                }
                ID3D11ShaderResourceView* noN = nullptr;
                ctx_->PSSetShaderResources(1, 1, &noN);
                ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
            }
            const size_t mi = size_t(&g - refTexInst_.data());
            if (instCb && cbInstAll_[mi / kInstPerCb]) {  // the mesh's waves, so the layer stays on the moving surface
                const UINT first = UINT((mi % kInstPerCb) * 16), num = 16;
                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);
            }
            ID3D11Buffer* bufs[2] = {g.vb, g.inst};
            ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
            ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT(g.ibFormat), 0);
            for (const PropLayer& L : g.layers) {
                if (L.texture < 0 || size_t(L.texture) >= textures_.size()) continue;
                const int rv = L.reveal >= 0 && size_t(L.reveal) < textures_.size() ? L.reveal : whiteFullTex_;
                if (rv < 0 || size_t(rv) >= textures_.size()) continue;
                ctx_->UpdateSubresource(cbLayer_, 0, nullptr, L.params, 0, 0);
                const float bfz[4] = {0, 0, 0, 0};
                ctx_->OMSetBlendState(L.params[3] > 0.5f && blendMul_ ? blendMul_ : blend_, bfz, 0xffffffff);
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(L.texture)]);
                ctx_->PSSetShaderResources(12, 1, &textures_[size_t(rv)]);
                drawInstIdx(g.vertices, g.instances);
            }
        }
        if (!layerStateSet) return;
        const float zero16[16] = {};
        ctx_->UpdateSubresource(cbLayer_, 0, nullptr, zero16, 0, 0);  // the plain layer buffer is zero again
        ID3D11ShaderResourceView* noR = nullptr;
        ctx_->PSSetShaderResources(12, 1, &noR);
        ctx_->OMSetDepthStencilState(prevDs, prevRef);
        ctx_->OMSetBlendState(prevBs, prevBf, prevBm);
        if (prevDs) prevDs->Release();
        if (prevBs) prevBs->Release();
        if (cbBatch_) {  // the props' flags again for what follows
            const float flags[8] = {propNormals ? 1.0f : 0.0f, soft ? -1.0f : 0.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};
            writeBatchCb(flags);
        }
    };
    for (int decalPass = 0; decalPass < 3; ++decalPass) {
    if (decalPass == 1) drawPropLayers();
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        const int passOf = g.translucent && g.decal == 0 ? 2 : g.decal > 0 ? 1 : 0;
        if (passOf != decalPass || g.drawClass != 0) continue;
        if (decalPass >= 1 && texPsOverride_) continue;
        if (decalPass == 2 && !glassStateSet) {
            glassStateSet = true;
            if (!decalStateSet) {
                decalStateSet = true;
                ctx_->RSGetState(&savedRs);
                ctx_->OMGetDepthStencilState(&savedDs, &savedRef);
            }
            if (savedRs) ctx_->RSSetState(savedRs);
            ctx_->OMSetDepthStencilState(dsTest_, 0);
            if (blend_) {
                const float bf[4] = {0, 0, 0, 0};
                ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
            }
            if (cbBatch_) {
                const float flags[8] = {propNormals ? 1.0f : 0.0f, 1.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};  // y = 1: blended
                writeBatchCb(flags);
            }
        }
        if (decalPass == 1 && !decalStateSet && rsDecal_) {
            decalStateSet = true;
            ctx_->RSGetState(&savedRs);
            ctx_->OMGetDepthStencilState(&savedDs, &savedRef);
            ctx_->RSSetState(rsDecal_);
            ctx_->OMSetDepthStencilState(dsTest_, 0);
        }
        const int id = g.texture >= 0 ? g.texture : whiteTex_;
        if (id < 0 || size_t(id) >= textures_.size()) continue;
        const bool culled = instCull_ && g.visInst && g.visible <= g.instances;
        if (culled && g.visible == 0) continue;
        if (!culled && g.lodGroup >= 0 && g.lodNear > 0) continue;  // unculled: a LOD group draws its LOD0 only
        // The depth pre-pass: only the placements large on screen (the shading pass writes depth for the rest).
        const bool prepassBig = texPsOverride_ && texPsOverride_ == psPrepass_ && culled;
        if (prepassBig && g.big == 0) continue;
        ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        if (propNormals) {
            const int n = g.normal >= 0 && size_t(g.normal) < textures_.size() ? g.normal : flatNrmTex_;
            ctx_->PSSetShaderResources(1, 1, &textures_[size_t(n)]);
            // Research 107: emissive (t3) and transmission (t19) images of the mesh's material.
            ID3D11ShaderResourceView* e = g.emissive >= 0 && size_t(g.emissive) < textures_.size() ? textures_[size_t(g.emissive)] : nullptr;
            ID3D11ShaderResourceView* t = g.transmission >= 0 && size_t(g.transmission) < textures_.size() ? textures_[size_t(g.transmission)] : nullptr;
            ctx_->PSSetShaderResources(3, 1, &e);
            ctx_->PSSetShaderResources(19, 1, &t);
        }
        if (instCb) {
            const size_t mi = size_t(&g - refTexInst_.data());
            if (cbInstAll_[mi / kInstPerCb]) {
                const UINT first = UINT((mi % kInstPerCb) * 16), num = 16;
                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);
                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);  // PropWave
            }
        }
        ID3D11Buffer* bufs[2] = {g.vb, culled ? g.visInst : g.inst};  // the pre-pass: the visible buffer's large prefix
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT(g.ibFormat), 0);
        drawInstIdx(g.vertices, prepassBig ? g.big : culled ? g.visible : g.instances);
    }
    }  // decalPass
    if (decalStateSet) {
        ctx_->RSSetState(savedRs);
        ctx_->OMSetDepthStencilState(savedDs, savedRef);
        if (savedRs) savedRs->Release();
        if (savedDs) savedDs->Release();
    }
    if (instCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer
    if (instCb) {
        ID3D11Buffer* none = nullptr;  // no waves for what is drawn next
        ctx_->VSSetConstantBuffers(8, 1, &none);
    }
    if (propNormals) {
        ID3D11ShaderResourceView* noN = nullptr;
        ctx_->PSSetShaderResources(1, 1, &noN);
    }
    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
    ctx_->OMSetBlendState(prevBlend, prevFactor, prevMask);
    if (prevBlend) prevBlend->Release();
}

bool SceneRenderer::uploadInstParams() {
    constexpr size_t kInstPerCb = 256;
    if (!uploadLayerParams() || !ctx1_) return false;  // ctx1_: constant buffer offsets
    if (instParamsDirty_ || cbInstAllMeshes_ != refTexInst_.size()) {
        instParamsDirty_ = false;
        if (cbInstAllMeshes_ != refTexInst_.size()) {
            for (auto*& b : cbInstAll_) safeRelease(b);
            cbInstAll_.assign((refTexInst_.size() + kInstPerCb - 1) / kInstPerCb, nullptr);
            cbInstAllMeshes_ = refTexInst_.size();
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = UINT(kInstPerCb * 256);
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            for (auto*& b : cbInstAll_)
                if (FAILED(dev_->CreateBuffer(&bd, nullptr, &b))) cbInstAllMeshes_ = 0;
        }
        std::vector<float> all(kInstPerCb * 64, 0.0f);
        for (size_t c = 0; c < cbInstAll_.size() && cbInstAllMeshes_; ++c) {
            std::fill(all.begin(), all.end(), 0.0f);
            for (size_t j = 0; j < kInstPerCb && c * kInstPerCb + j < refTexInst_.size(); ++j) {
                const GpuTexInstanced& gi = refTexInst_[c * kInstPerCb + j];
                std::memcpy(&all[j * 64], gi.params, sizeof(gi.params));
                std::memcpy(&all[j * 64 + 16], gi.extra, sizeof(gi.extra));
                std::memcpy(&all[j * 64 + 32], gi.wave, sizeof(gi.wave));  // PropWave (b8) in vs_tex_inst
                maskExtraTerms(&all[j * 64 + 16]);
                if (textureRawAlpha(gi.texture)) all[j * 64 + 1] = 2.0f;  // opaque: alpha is the specular code
            }
            if (cbInstAll_[c]) ctx_->UpdateSubresource(cbInstAll_[c], 0, nullptr, all.data(), 0, 0);
        }
    }
    return cbInstAllMeshes_ == refTexInst_.size() && !cbInstAll_.empty();
}

void SceneRenderer::drawReferenceTexInstancesShadow(const iw8r::Frustum* view) {
    if (refTexInst_.empty() || !vsTexInst_ || !layoutTexInst_) return;
    ctx_->IASetInputLayout(layoutTexInst_);
    ctx_->VSSetShader(vsTexInst_, nullptr, 0);
    ctx_->PSSetShader(psShadowClip_, nullptr, 0);  // none: solid
    if (psShadowClip_ && sampler_) ctx_->PSSetSamplers(0, 1, &sampler_);
    if (cbBatch_) {
        const float zero[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        writeBatchCb(zero);
        ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
    }
    const bool instCb = psShadowClip_ && uploadInstParams();
    const UINT strides[2] = {UINT(sizeof(PropVertex)), UINT(24 * sizeof(float))}, offsets[2] = {0, 0};
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if (g.decal > 0 || g.drawClass == 2 || g.noCast) continue;  // decals and no-cast materials cast no shadow
        if (g.lodGroup >= 0 && g.lodNear > 0) continue;  // the sun shadow of a LOD group: its LOD0
        if (psShadowClip_) {
            const int id = g.texture >= 0 ? g.texture : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        }
        if (instCb) {
            const size_t mi = size_t(&g - refTexInst_.data());
            if (cbInstAll_[mi / 256]) {
                const UINT first = UINT((mi % 256) * 16), num = 16;
                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / 256], &first, &num);
                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / 256], &first, &num);  // PropWave
            }
        }
        ID3D11Buffer* bufs[2] = {g.vb, g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT(g.ibFormat), 0);
        if (!view || g.cells.empty()) {
            drawInstIdx(g.vertices, g.instances);
            continue;
        }
        // Runs of neighbouring cells in the view, one draw each. Every placement casts, also one the camera's view
        // leaves out by distance: a shadow map is kept over several frames (a lamp's until a caster changes), so
        // leaving casters out by where the camera stood when it was drawn would lose shadows after the camera moved.
        uint32_t runFirst = 0, runCount = 0;
        for (const auto& c : g.cells) {
            if (iw8r::frustumIntersectsAabb(*view, c.box)) {
                if (runCount && runFirst + runCount == c.first) runCount += c.count;
                else {
                    if (runCount) drawInstIdx(g.vertices, runCount, runFirst);
                    runFirst = c.first, runCount = c.count;
                }
            }
        }
        if (runCount) drawInstIdx(g.vertices, runCount, runFirst);
    }
    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ctx_->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    if (instCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
    if (instCb) {
        ID3D11Buffer* none = nullptr;
        ctx_->VSSetConstantBuffers(8, 1, &none);
    }
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

void SceneRenderer::drawInstIdx(unsigned indices, unsigned instances, unsigned firstInstance) {
    ctx_->DrawIndexedInstanced(indices, instances, 0, 0, firstInstance);
    if (inShadowPass_) {
        ++stats_.shadowDrawCalls;
        return;
    }
    ++stats_.drawCalls;
    stats_.triangles += size_t(indices / 3) * instances;
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
    GpuProfFrame gpuFrame(dev_, ctx_);  // IW8R_BENCH_GPU: every scope below timed on the GPU too
    IW8R_PROF("d.total");
    ++frame_;
    streamsSeen_ = 0;
    stats_ = {};
    inShadowPass_ = false;
    const float* viewProj = in.viewProj;
    // Game Lighting draws the lit scene into the HDR target (IW8R_NO_HDR: straight into the frame, as before).
    static const bool noHdr = std::getenv("IW8R_NO_HDR") != nullptr;
    // A clean screenshot (in.clean) takes the same path as the view, so it has the view's bloom, vignette and white
    // balance; a probe capture (in.capture without in.clean) never has the game look on.
    hdrFrame_ = game_.on && gameLutSrv_ && hdrRtv_ && postVs_ && postPs_ && !noHdr && (!in.capture || in.clean);
    prepareStockLocalLights(viewProj);
    if (!hdrFrame_) prevValid_ = false;
    sceneRtv_ = hdrFrame_ ? hdrRtv_ : rtv_;
    {
        // The previous frame for the reflections: only an HDR frame of the same size, with SSR on.
        static const bool noSsr = std::getenv("IW8R_NO_SSR") != nullptr;
        const bool ssrNow = hdrFrame_ && prevValid_ && ssrOn_ && !noSsr && prevColorSrv_ && prevZSrv_;
        if (!cbPrev_ && dev_) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = 96;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            dev_->CreateBuffer(&bd, nullptr, &cbPrev_);
        }
        if (cbPrev_) {
            float pc[24] = {};
            std::memcpy(pc, prevViewProj_, 64);
            // The previous camera's forward axis: the w column of its view-projection (row-vector convention).
            float fx = prevViewProj_[3], fy = prevViewProj_[7], fz = prevViewProj_[11];
            const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
            if (fl > 0) fx /= fl, fy /= fl, fz /= fl;
            pc[16] = fx, pc[17] = fy, pc[18] = fz;
            pc[20] = float(std::max(1, w_ / 2)), pc[21] = float(std::max(1, h_ / 2)), pc[22] = ssrNow ? 1.0f : 0.0f;
            ctx_->UpdateSubresource(cbPrev_, 0, nullptr, pc, 0, 0);
            ctx_->PSSetConstantBuffers(10, 1, &cbPrev_);
        }
        ID3D11ShaderResourceView* ps[2] = {ssrNow ? prevColorSrv_ : nullptr, ssrNow ? prevZSrv_ : nullptr};
        ctx_->PSSetShaderResources(16, 2, ps);
        ID3D11ShaderResourceView* sceneZ = hdrFrame_ && sceneZValid_ && sceneDepthWanted() ? sceneZSrv_ : nullptr;
        ctx_->PSSetShaderResources(31, 1, &sceneZ);
    }
    if (hdrFrame_) {
        const float zero[4] = {0, 0, 0, 0};
        ctx_->ClearRenderTargetView(hdrRtv_, zero);
    }
    ctx_->OMSetRenderTargets(1, &sceneRtv_, dsv_);
    if (!cbTiles_ && dev_) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16 + 256 * 16;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &cbTiles_))) tileBoxesDirty_ = true;
    }
    if (cbTiles_) {
        if (tileBoxesDirty_) {
            std::vector<float> tb(4 + 256 * 4, 0.0f);
            tb[0] = float(tileBoxes_.size());
            for (size_t k = 0; k < tileBoxes_.size(); ++k)
                for (int c = 0; c < 4; ++c) tb[4 + k * 4 + size_t(c)] = tileBoxes_[k][size_t(c)];
            ctx_->UpdateSubresource(cbTiles_, 0, nullptr, tb.data(), 0, 0);
            tileBoxesDirty_ = false;
        }
        ctx_->PSSetConstantBuffers(9, 1, &cbTiles_);
    }
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
        cb[74] = game_.on && gameLutSrv_ ? 1.0f : 0.0f;  // sunNear.z: Game Lighting's cut-out threshold (cutoutAt)
        cb[75] = 0;
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
    // Local light shadow maps: every slot again when the casters changed; each frame a few of those not current.
    if (farShadowDue)
        for (LightSlot& sl : lightSlots_) sl.drawn = false;
    bool lightShadowsDue = false;
    if (stockLightsOn_ && lightShadowSrv_)
        for (const LightSlot& sl : lightSlots_) lightShadowsDue |= sl.light >= 0 && !sl.drawn;
    if (farShadowDue || nearShadowDue || lightShadowsDue) {
        // Sun shadow maps (lit preview, Game Lighting): depth of everything from the sun. The whole-scene cascade is drawn
        // again only when a caster or the sun changed; the near cascade also when the camera has moved its box.
        IW8R_PROF("d.shadow");
        ID3D11RenderTargetView* noRtv = nullptr;
        inShadowPass_ = true;
        D3D11_VIEWPORT svp{0, 0, float(shadowSize_), float(shadowSize_), 0, 1};
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
                writeBatchCb(flags);
                ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
            }
            // Opaque and alpha-tested batches only (glass and effects cast no shadow); the clip shader keeps the
            // holes of cut-out textures (no texture bound = no pixel shader, solid).
            if (psShadowClip_ && sampler_) {
                ctx_->PSSetShader(psShadowClip_, nullptr, 0);
                ctx_->PSSetSamplers(0, 1, &sampler_);
            }
            if (cbBatch_) ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
            if (cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
            bool lastRaw = false;
            for (const BatchDraw& d : drawList_) {
                const SceneBatch& b = *d.b;
                if (b.blend != 0 || b.v.front().rgba >> 24 < 0x80) continue;
                if (psShadowClip_ && b.texture >= 0 && size_t(b.texture) < textures_.size())
                    ctx_->PSSetShaderResources(0, 1, &textures_[size_t(b.texture)]);
                if (const bool raw = textureRawAlpha(b.texture); raw != lastRaw && cbBatch_) {
                    const float rf[8] = {0, raw ? -2.0f : 0.0f, 0, 0, 0, 0, 0, 0};
                    writeBatchCb(rf);
                    lastRaw = raw;
                }
                ctx_->IASetVertexBuffers(0, 1, &d.g->vb, &tstride, &offset);
                drawN(UINT(b.v.size()), 0);
            }
            ctx_->PSSetShader(nullptr, nullptr, 0);
        }
        static const bool flatShadowRef = std::getenv("IW8R_FLAT_SHADOW_REF") != nullptr;  // A/B: the old caster list
        if (vbRefTex_ && refVisible_ && vsTex_ && layoutTex_ && psShadowClip_ && sampler_ && !flatShadowRef) {
            // The stock world casts from its textured batches: opaque and cut-out base batches only. Decals (puddles,
            // potholes, dirt sheets floating above the ground: Verdansk's streamed tiles hold thousands) and layer
            // passes cast nothing, as in game; cut-out surfaces (fences, grates) keep their holes.
            ctx_->IASetInputLayout(layoutTex_);
            ctx_->VSSetShader(vsTex_, nullptr, 0);
            ctx_->PSSetShader(psShadowClip_, nullptr, 0);
            ctx_->PSSetSamplers(0, 1, &sampler_);
            if (cbBatch_) {
                const float flags[8] = {0, 0, 0, 0, 0, 0, 1.0f, 0};
                writeBatchCb(flags);
                ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
                ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
            }
            const bool shCb = uploadLayerParams();
            const iw8r::Frustum sf = iw8r::frustumFromViewProj(sm);
            ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
            ID3D11Buffer* shVb = vbRefTex_;
            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal || refLayers_[k].drawClass == 2 || refLayers_[k].noCast)) continue;
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                if (cull && refExtraOutside(k, sf)) continue;
                if (ID3D11Buffer* v = refVbFor(k); v != shVb) ctx_->IASetVertexBuffers(0, 1, &v, &tstride, &offset), shVb = v;
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
                if (shCb && cbLayerAll_[k / 256]) {
                    const UINT first = UINT((k % 256) * 16), num = 16;
                    ctx1_->PSSetConstantBuffers1(7, 1, &cbLayerAll_[k / 256], &first, &num);
                }
                if (cull && k < refTexChunks_.size() && !refTexChunks_[k].empty())
                    forVisibleRuns(sf, refTexChunks_[k], [&](uint32_t first, uint32_t n) { drawN(n * 3, refTexFirst_[k] + first * 3); });
                else
                    drawN(refTexCount_[k], refTexFirst_[k]);
            }
            if (shCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
            ID3D11ShaderResourceView* noT = nullptr;
            ctx_->PSSetShaderResources(0, 1, &noT);
            ctx_->PSSetShader(nullptr, nullptr, 0);
            ctx_->IASetInputLayout(layout_);
            ctx_->VSSetShader(vs_, nullptr, 0);
        } else if (vbRef_ && refCount_ && refVisible_) {
            ctx_->IASetInputLayout(layout_);
            ctx_->VSSetShader(vs_, nullptr, 0);
            ctx_->IASetVertexBuffers(0, 1, &vbRef_, &stride, &offset);
            if (cull && !refChunks_.empty())
                forVisibleRuns(iw8r::frustumFromViewProj(sm), refChunks_, [&](uint32_t first, uint32_t n) { drawN(n * 3, first * 3); });
            else
                drawN(UINT(refCount_), 0);
        }
        if (refVisible_) drawReferenceInstances(ReferencePass::Shadow);
        // The textured props (trees, crates, poles) cast too: their leaves through the cut-out test. The near cascade
        // and the lamp maps draw the placements in their view only.
        if (refVisible_) {
            const iw8r::Frustum pf = iw8r::frustumFromViewProj(sm);
            drawReferenceTexInstancesShadow(cull ? &pf : nullptr);
        }
        };
        if (farShadowDue) drawCasters(shadowDsv_, sunViewProj_, false);
        if (nearShadowDue) drawCasters(shadowDsvNear_, sunViewProjNear_, true);
        if (lightShadowsDue) {
            // Up to six local light maps a frame (static world and props; the game also draws at most a few a frame).
            D3D11_VIEWPORT lvp{0, 0, float(kLightSlotSize), float(kLightSlotSize), 0, 1};
            ctx_->RSSetViewports(1, &lvp);
            int budget = 6;
            for (int j = 0; j < kLightSlots && budget > 0; ++j) {
                LightSlot& sl = lightSlots_[size_t(j)];
                if (sl.light < 0 || sl.drawn || !lightShadowDsv_[size_t(j)]) continue;
                drawCasters(lightShadowDsv_[size_t(j)], sl.vp, true);
                sl.drawn = true;
                ++stats_.lampShadowMaps;
                lightSlotsChanged_ = true;
                --budget;
            }
            ctx_->RSSetViewports(1, &svp);
        }
        if (farShadowDue) {
            shadowDirty_ = false;
            shadowCasterVersion_ = in.casterVersion;
            ++stats_.shadowPasses;
        }
        if (nearShadowDue) nearDirty_ = false, ++stats_.nearShadowPasses;
        inShadowPass_ = false;
        ctx_->OMSetRenderTargets(1, &sceneRtv_, dsv_);
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
        // A clean screenshot leaves out what the view leaves out (the game's distance rule); a probe capture draws all.
        instCull_ = !noCull && (!in.capture || in.clean);
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
    static const bool noAo = std::getenv("IW8R_NO_AO") != nullptr;
    aoValid_ = false;
    const bool aoWanted = game_.on && aoOn_ && !noAo && depthSrv_ && aoRtv_[1] && psPrepass_ && createAoResources();
    const bool prepass = (usePrepass || aoWanted) && game_.on && psPrepass_ && blendNoColor_ && vsTex_ && layoutTex_ && sampler_;
    if (prepass) {
        IW8R_PROF("d.prepass");
        writeGameCb();  // the cut-out threshold and the terrain cut (b4)
        ctx_->OMSetBlendState(blendNoColor_, bf, 0xffffffff);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->RSSetState(rsTris_);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetInputLayout(layoutTex_);
        ctx_->VSSetShader(vsTex_, nullptr, 0);
        ctx_->PSSetShader(psPrepass_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        if (cbBatch_) ctx_->VSSetConstantBuffers(1, 1, &cbBatch_), ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
        if (cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
        bindTomeVisibility();
        if (cbWind_) {
            ctx_->UpdateSubresource(cbWind_, 0, nullptr, wind_, 0, 0);
            ctx_->VSSetConstantBuffers(3, 1, &cbWind_);
        }
        for (const BatchDraw& d : drawList_) {
            const SceneBatch& b = *d.b;
            StreamGpu& g = *d.g;
            if (b.blend || b.texture < 0 || size_t(b.texture) >= textures_.size()) continue;
            if (cbBatch_) {  // the same vertex inputs as the shading pass (sway), so the depths match exactly
                // and the same hidden back faces of one-sided own materials (G11: flags[6] = -1).
                const TextureMaterial* ptm = textureMaterial(b.texture);
                const float flags[8] = {0, b.blend == 0 && textureRawAlpha(b.texture) ? -2.0f : float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ,
                                        0, 0, ptm && ptm->cullBack ? -1.0f : 0.0f, 0};
                writeBatchCb(flags);
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
                writeBatchCb(flags);
            }
            ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
            const bool preCb = uploadLayerParams();
            ID3D11Buffer* preVb = vbRefTex_;
            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal || refLayers_[k].drawClass != 0)) continue;  // layers, decals: later
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                if (refExtraOutside(k, frustum)) continue;
                if (ID3D11Buffer* v = refVbFor(k); v != preVb) ctx_->IASetVertexBuffers(0, 1, &v, &tstride, &offset), preVb = v;
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
                if (preCb && cbLayerAll_[k / 256]) {
                    const UINT first = UINT((k % 256) * 16), num = 16;
                    ctx1_->PSSetConstantBuffers1(7, 1, &cbLayerAll_[k / 256], &first, &num);
                }
                if (refCulling_ && k < refTexChunks_.size() && !refTexChunks_[k].empty())
                    forVisibleRuns(frustum, refTexChunks_[k], [&](uint32_t first, uint32_t n) { drawN(n * 3, refTexFirst_[k] + first * 3); });
                else
                    drawN(refTexCount_[k], refTexFirst_[k]);
            }
        }
        if (refVisible_ && !refTexInst_.empty()) {
            texPsOverride_ = psPrepass_;
            drawReferenceTexInstances();
            texPsOverride_ = nullptr;
        }
        ID3D11ShaderResourceView* noTex[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, noTex);
        if (aoWanted) {
            drawAo(viewProj);
            ctx_->RSSetViewports(1, &vp);
        }
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
        ctx_->IASetInputLayout(layout_);
        ctx_->VSSetShader(vs_, nullptr, 0);
        ctx_->PSSetShader(ps_, nullptr, 0);
    }
    // After the pre-pass the opaque textured geometry only tests depth (equal depths pass LESS_EQUAL).
    // After the pre-pass the opaque geometry still writes depth (LESS_EQUAL: the pre-pass depths pass): the pre-pass
    // leaves out the props small on screen.
    ID3D11DepthStencilState* texOpaqueDs = dsWrite_;
    // Opaque batches first, then blended ones farthest first (a stable sort, as before, without a heap buffer). In
    // Game Lighting the blended ones go by their material's sort key first (the build's sortKey, the converter's
    // default 4; SceneRenderer::TextureMaterial) and are drawn after the stock reference and its props, so a glass or
    // decal of the map blends over the whole opaque scene.
    drawOrder_.clear();
    size_t firstBlendedScene = 0;
    {
        const size_t count = drawList_.size();
        for (size_t k = 0; k < count; ++k)
            if (!drawList_[k].b->blend) drawOrder_.push_back(uint32_t(k));
        firstBlendedScene = drawOrder_.size();
        const auto sortKeyOf = [&](const SceneBatch& b) {
            const TextureMaterial* tm = game_.on ? textureMaterial(b.texture) : nullptr;
            return tm ? tm->sortKey : 4;
        };
        for (size_t k = 0; k < count; ++k) {
            BatchDraw& d = drawList_[k];
            if (!d.b->blend) continue;
            // The batch centre's clip-space w is its distance along the view direction.
            d.depth = float(d.g->centroid[0] * viewProj[3] + d.g->centroid[1] * viewProj[7] + d.g->centroid[2] * viewProj[11] + viewProj[15]);
            const int key = sortKeyOf(*d.b);
            size_t at = drawOrder_.size();
            drawOrder_.push_back(uint32_t(k));
            while (at > firstBlendedScene) {
                const BatchDraw& p = drawList_[drawOrder_[at - 1]];
                const int pk = sortKeyOf(*p.b);
                if (pk < key || (pk == key && p.depth >= d.depth)) break;
                drawOrder_[at] = drawOrder_[at - 1];
                --at;
            }
            drawOrder_[at] = uint32_t(k);
        }
    }
    const bool deferSceneBlended = game_.on;
    const auto drawSceneTex = [&](size_t from, size_t to) {
        if (from >= to) return;
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
        bindTomeVisibility();
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
        ID3D11PixelShader* boundTexPs = psTex_;
        for (size_t oi = from; oi < to; ++oi) {
            const uint32_t k = drawOrder_[oi];
            const SceneBatch& b = *drawList_[k].b;
            StreamGpu& g = *drawList_[k].g;
            if (b.texture < 0 || size_t(b.texture) >= textures_.size()) continue;
            const bool chunked = refCulling_ && g.chunked && !g.chunks.empty();
            ++stats_.batches;
            // Game Lighting: the own material's shader state (unlit, glow, multiply, glass; TextureMaterial).
            const TextureMaterial* tm = game_.on ? textureMaterial(b.texture) : nullptr;
            if (b.blend) {
                ctx_->OMSetDepthStencilState(dsTest_, 0);
                ID3D11BlendState* bs = tm && tm->multiply && blendMul_ ? blendMul_ : b.blend == 2 && blendAdd_ ? blendAdd_ : blend_;
                ctx_->OMSetBlendState(bs, bf, 0xffffffff);
            }
            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(b.texture)]);
            // The material's custom shader in Game Lighting (createCustomShader), else the editor's own.
            {
                ID3D11PixelShader* want = game_.on && b.shader >= 0 && size_t(b.shader) < customPs_.size() && customPs_[size_t(b.shader)]
                                              ? customPs_[size_t(b.shader)] : psTex_;
                if (want != boundTexPs) ctx_->PSSetShader(want, nullptr, 0), boundTexPs = want;
            }
            const bool hasNormal = b.normalTexture >= 0 && size_t(b.normalTexture) < textures_.size();
            ID3D11ShaderResourceView* nsrv = hasNormal ? textures_[size_t(b.normalTexture)] : nullptr;
            ctx_->PSSetShaderResources(1, 1, &nsrv);
            const bool hasEmissive = b.emissiveTexture >= 0 && size_t(b.emissiveTexture) < textures_.size();
            ID3D11ShaderResourceView* esrv = hasEmissive ? textures_[size_t(b.emissiveTexture)] : nullptr;
            ctx_->PSSetShaderResources(3, 1, &esrv);
            const bool hasSpec = game_.on && b.specTexture >= 0 && size_t(b.specTexture) < textures_.size();
            ID3D11ShaderResourceView* ssrv = hasSpec ? textures_[size_t(b.specTexture)] : nullptr;
            ctx_->PSSetShaderResources(29, 1, &ssrv);
            if (cbBatch_) {
                // flags[6] (batchFlags2.z): -1 = an own one-sided material in Game Lighting (back faces hidden, G11).
                const float flags[8] = {hasNormal ? 1.0f : 0.0f, b.blend == 0 && textureRawAlpha(b.texture) ? -2.0f : float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ,
                                        hasEmissive ? 1.0f : 0.0f, probeSrv_ ? 1.0f : 0.0f, tm && tm->cullBack ? -1.0f : 0.0f, hasSpec ? 1.0f : 0.0f};
                const float mat[4] = {tm && tm->unlit ? 1.0f : 0.0f, tm ? tm->emission : 0.0f, tm && tm->multiply && b.blend ? 1.0f : 0.0f,
                                      tm && tm->glass && b.blend == 1 ? 1.0f : 0.0f};
                writeBatchCb(flags, mat);
            }
            ctx_->IASetVertexBuffers(0, 1, &g.vb, &tstride, &offset);
            if (chunked) {
                ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT_R32_UINT, 0);
                texCulledLast_ += forVisibleRuns(frustum, g.chunks, [&](uint32_t first, uint32_t n) { drawIdx(n * 3, first * 3); });
            } else {
                drawN(UINT(b.v.size()), 0);
            }
        }
        if (boundTexPs != psTex_) ctx_->PSSetShader(psTex_, nullptr, 0);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
        ID3D11ShaderResourceView* noSrv[2] = {nullptr, nullptr};
        ctx_->PSSetShaderResources(0, 2, noSrv);
        ctx_->IASetInputLayout(layout_);
        ctx_->VSSetShader(vs_, nullptr, 0);
        ctx_->PSSetShader(ps_, nullptr, 0);
    };
    drawSceneTex(0, deferSceneBlended ? firstBlendedScene : drawOrder_.size());
    ID3D11PixelShader* const refPs = filterPs_ ? filterPs_ : psTex_;  // a map shader's filter on the stock reference
    if (vbRefTex_ && refVisible_ && vsTex_ && psTex_ && layoutTex_) {  // textured stock reference
        IW8R_PROF("d.ref");
        ctx_->IASetInputLayout(layoutTex_);
        ctx_->VSSetShader(vsTex_, nullptr, 0);
        ctx_->PSSetShader(refPs, nullptr, 0);
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
            writeBatchCb(flags);
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
        // Each base batch's material constants (spec / gloss remaps; setReferenceBaseParams) at its offset in b7.
        const bool baseCb = uploadLayerParams();
        constexpr size_t kBasePerCb = 256;
        ID3D11Buffer* baseVb = vbRefTex_;
        for (size_t k = 0; k < refTexIds_.size(); ++k) {
            if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal || refLayers_[k].drawClass != 0)) continue;  // layers, decals: below
            const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            if (refExtraOutside(k, frustum)) continue;  // a streamed world tile's batch out of view
            if (ID3D11Buffer* v = refVbFor(k); v != baseVb) ctx_->IASetVertexBuffers(0, 1, &v, &tstride, &offset), baseVb = v;
            const auto bind = [&] {
                // Batches that share a texture (the plain one while images load) do not bind it again.
                if (bound != textures_[size_t(id)]) ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]), bound = textures_[size_t(id)];
                bindNormal(k);
                // Research 107: the batch's emissive (t3) and transmission (t19) images, when its material has them.
                if (k < refLayers_.size() && (refLayers_[k].emissive >= 0 || refLayers_[k].transmission >= 0)) {
                    const RefLayer& L = refLayers_[k];
                    ID3D11ShaderResourceView* e = L.emissive >= 0 && size_t(L.emissive) < textures_.size() ? textures_[size_t(L.emissive)] : nullptr;
                    ID3D11ShaderResourceView* t = L.transmission >= 0 && size_t(L.transmission) < textures_.size() ? textures_[size_t(L.transmission)] : nullptr;
                    ctx_->PSSetShaderResources(3, 1, &e);
                    ctx_->PSSetShaderResources(19, 1, &t);
                }
                if (baseCb && cbLayerAll_[k / kBasePerCb]) {
                    const UINT first = UINT((k % kBasePerCb) * 16), num = 16;
                    ctx1_->PSSetConstantBuffers1(7, 1, &cbLayerAll_[k / kBasePerCb], &first, &num);
                }
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
        if (baseVb != vbRefTex_) ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
        if (baseCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer
        // The layers of layered stock materials (terrain): over their base, in layer order, blended by the layer's
        // weight and reveal map, depth-tested against the base (same vertices, same depth) without writing it.
        static const bool noLayers = perfOff("layers");  // benchmark switch: no layer or decal passes
        if (!noLayers && (!refLayerOrder_.empty() || refDecalDirty_ || !refDecalOrder_.empty()) && cbLayer_) {
            if (cbBatch_) {
                const float flags[8] = {refNormals ? 1.0f : 0.0f, 1.0f, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};  // y = 1: blended, no clip
                writeBatchCb(flags);
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
            constexpr size_t kPerCb = 256;  // batches per 64 KB buffer (256 bytes each)
            const bool offsetCb = uploadLayerParams();
            int lastFlags = -1;
            ID3D11BlendState* lastBlend = nullptr;
            ID3D11DepthStencilState* lastDs = dsTest_;
            ID3D11ShaderResourceView *lastCol = nullptr, *lastRev = nullptr;
            static const bool noPropDecals = perfOff("propdecals");  // benchmark switch
            // Opaque layers first, then the decals (their layers with them) with a small depth offset.
            std::vector<size_t> passOrder;
            passOrder.reserve(refLayerOrder_.size() + refDecalOrder_.size());
            for (size_t k : refLayerOrder_)
                if (!refLayers_[k].decal) passOrder.push_back(k);
            const size_t firstDecal = passOrder.size();
            passOrder.insert(passOrder.end(), refDecalOrder_.begin(), refDecalOrder_.end());
            layerPassDraws_ = layerPassTris_ = 0;
            tomeCulledDecals_ = 0;
            layerPassBatches_ = passOrder.size();
            for (size_t pi = 0; pi < passOrder.size(); ++pi) {
                const size_t k = passOrder[pi];
                if (pi == firstDecal && rsDecal_) ctx_->RSSetState(rsDecal_);
                {
                    ID3D11Buffer* want = refVbFor(k);
                    if (want != boundRefVb) ctx_->IASetVertexBuffers(0, 1, &want, &tstride, &offset), boundRefVb = want;
                }
                const RefLayer& L = refLayers_[k];
                if (!L.ready || L.drawClass != 0 || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;
                // Cull by tome: the game draws a decal volume collection only with its visibility bit (research 110).
                if (!tomeDecalBits_.empty() && tomeBitClear(tomeDecalBits_, L.tomeCollection)) {
                    ++tomeCulledDecals_;
                    continue;
                }
                if (k >= refTexExtraFrom_) {
                    // Prop decals: small detail; drawn in view and within 4,000 units (Hideout: 790k triangles, 38 ms).
                    if (noPropDecals && !(L.params[0] > 0.5f && L.params[0] < 1.5f)) continue;
                    const size_t e = k - refTexExtraFrom_;
                    if (e < refTexExtraBounds_.size() && refTexExtraBounds_[e].valid()) {
                        const iw8r::AABB& bb = refTexExtraBounds_[e];
                        const iw8r::Vec3 c = bb.center();
                        const double dx = c.x - fog_[0], dy = c.y - fog_[1], dz = c.z - fog_[2];
                        const double r = iw8r::length(bb.maxs - bb.mins) * 0.5;
                        // The terrain layer patch (params x = 1) has its own radius (the game's 17,368): view cull only.
                        const bool terrainLayer = L.params[0] > 0.5f && L.params[0] < 1.5f;
                        const bool nearOnly = k < refTexNearOnly_.size() && refTexNearOnly_[k];  // prop decals (not tiles)
                        if (nearOnly && !terrainLayer && std::sqrt(dx * dx + dy * dy + dz * dz) - r > 4000.0) continue;
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
                    writeBatchCb(flags);
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
                    // The terrain patch's base layer stands in for the cut-away coarse terrain: it writes depth.
                    ID3D11DepthStencilState* wantDs = L.params[14] > 0.5f ? dsWrite_ : dsTest_;
                    if (wantDs != lastDs) ctx_->OMSetDepthStencilState(wantDs, 0), lastDs = wantDs;
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
                    refCulledLast_ += forVisibleRuns(frustum, refTexChunks_[k], [&](uint32_t first, uint32_t n) {
                        drawN(n * 3, refTexFirst_[k] + first * 3);
                        ++layerPassDraws_, layerPassTris_ += n;
                    });
                else
                    drawN(refTexCount_[k], refTexFirst_[k]), ++layerPassDraws_, layerPassTris_ += refTexCount_[k] / 3;
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
    if (refVisible_ && !refInst_.empty() && !hdrFrame_) {  // instanced reference props (3.20)
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
    if (deferSceneBlended) drawSceneTex(firstBlendedScene, drawOrder_.size());  // the map's blended batches, last
    if (hdrFrame_) {
        // The lit scene is done: kept for the next frame's reflections, the post chain into the frame, then the unlit
        // pieces and the editor's overlays.
        keepSceneDepth(viewProj);
        keepFrameForSsr(viewProj);
        std::memcpy(lastViewProj_, viewProj, sizeof(lastViewProj_));
        drawPost();
        if (refVisible_ && !refInst_.empty()) {
            ctx_->RSSetState(rsTris_);
            ctx_->OMSetDepthStencilState(dsWrite_, 0);
            ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            drawReferenceInstances(in.capture || in.clean ? ReferencePass::Capture : ReferencePass::Screen);
        }
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
        // Antialiased lines cost about 6 ns each on the GPU: Verdansk's 1.2 million entity outline lines took 7 ms a
        // frame. Past half a million line vertices they are drawn plain.
        const size_t lineVerts = (fLines.ready() ? fLines.s->v.size() : 0) + (fLinesMisc.ready() ? fLinesMisc.s->v.size() : 0);
        ctx_->RSSetState(lineVerts > 500000 && rsLinesFast_ ? rsLinesFast_ : rsLines_);
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

void SceneRenderer::setSunShadowMapSize(unsigned size) {
    size = size < 1536 ? 1024u : (size < 3072 ? 2048u : 4096u);
    if (size == shadowSize_) return;
    shadowSize_ = size;
    // The maps of the old size go; createShadowResources makes the new ones (the sampler and the raster state stay).
    safeRelease(shadowSrv_), safeRelease(shadowDsv_), safeRelease(shadowDsvNear_), safeRelease(shadowTex_);
    shadowFailed_ = false;
    shadowDirty_ = nearDirty_ = true;
}

bool SceneRenderer::createShadowResources() {
    if (shadowSrv_ && shadowDsv_ && shadowDsvNear_ && cmpSampler_) return true;
    if (!dev_ || shadowFailed_) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = shadowSize_;
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
        FAILED(dev_->CreateShaderResourceView(shadowTex_, &sd, &shadowSrv_)) ||
        (!cmpSampler_ && FAILED(dev_->CreateSamplerState(&cs, &cmpSampler_)))) {
        shadowFailed_ = true;
        return false;
    }
    if (!rsShadow_) dev_->CreateRasterizerState(&rd, &rsShadow_);
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