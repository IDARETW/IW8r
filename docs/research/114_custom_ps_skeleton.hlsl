// Research 114: a hand-written replacement for Replay 1.20's world lit pixel shader.
//
// Interface: identical to stock PS 01631f08811f0f1923c11aaa9718d79b, the TECHNIQUE_LIT_FORWARDPLUS_BITMASK (type 34)
// pixel shader of tw/lit_3_lit_rpl_ta1_804000_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0 (the stock techset the
// x64-zt converter clones for every editor map's world surfaces). Same input signature (the stock world vertex shader
// d9ff726d... writes it), same single output, registers taken from the stock declarations. See
// docs/research/114_custom_pixel_shader_interface.md.
//
// It shades: albedo (t1, material UV transform and tint from cb3), the baked lightmap (t114 colour, t115 direction +
// ambient fraction, rectangle from t0[cb9[0].y]) and the sun (cb7[56] direction, cb7[57] radiance, t85 screen-space
// sun visibility). No normal map, specular, probes, GTAO, local lights or decal volumes.
//
// Compile (matches the stock blob's form: DXBC, ps_5_0, no reflection chunks):
//   fxc /nologo /T ps_5_0 /E main /O3 /Qstrip_reflect /Qstrip_debug /Fo out.cso 114_custom_ps_skeleton.hlsl
// For the static-model variant compile with /D STATIC_MODEL_OPAQUE (input signature of stock PS bce5a6cd...).

#ifndef STATIC_MODEL_OPAQUE
// ---- world (tw/ lit_3_lit_rpl..., MTL_CONSTANT_LAYOUT_WORLD_LIT_LMAP) ----
struct PSInput
{
    float4 position  : SV_POSITION; // v0: xy pixel centre, z depth, w linear view depth
    float4 texcoords : TEXCOORDS;   // v1: xy material UV (zw written by the VS, unused by the stock PS)
    float2 lmapcoord : LMAPCOORDS;  // v2: lightmap UV in the surface's 0..1 rectangle
    float3 normal    : NORMAL;      // v3: world normal (not normalised)
    float4 tangent   : TANGENT;     // v4: world tangent, w = bitangent sign
};
#else
// ---- opaque static model (tmo/ lit_3_lit_rpl..., MTL_CONSTANT_LAYOUT_SMODEL_LIT_PROBE), stock PS bce5a6cd ----
struct PSInput
{
    float4 position        : SV_POSITION;       // v0
    float4 texcoords       : TEXCOORDS;         // v1
    float3 normal          : NORMAL;            // v2
    float4 tangent         : TANGENT;           // v3
    float4 ambientColor    : AMBIENT_COLOR;     // v4: rgb baked light, a = ambient fraction
    float3 highlightDirVis : HIGHLIGHT_DIR_VIS; // v5: dominant baked light direction (world)
    float4 selfVis         : SELFVIS;           // v6: self-visibility cone (xyz) and width (w)
};
#endif

// cb2: the technique's "stable" rows (MTL_ARG_CODE_CONST args, dest = row). PS rows: 4 = camera origin
// (code const 0), 6 = screen size terms (code const 4; .zw = 1/size), 7 = .x pixel scale, .y view mip bias
// (code const 103).
cbuffer TechniqueRows : register(b2) { float4 cb2[8]; }
// cb3: the material's 64-byte psData (MTL_ARG_MATERIAL_CONST: rows 0..3 = constant slots 72, 0, 9, 54).
cbuffer MaterialRows : register(b3) { uint4 cb3[4]; }
// cb7: the per-view lighting constants (MTL_ARG_CODE_CBUFFER index 0). Stock world PS declares 213 rows.
cbuffer LightingRows : register(b7) { float4 cb7[213]; }
#ifndef STATIC_MODEL_OPAQUE
// cb9: per-draw rows set by the engine (no technique argument); .y = this surface's lightmap rectangle index.
cbuffer DrawRows : register(b9) { uint4 cb9[1]; }
#endif

SamplerState materialSampler : register(s3); // material images (stock: t1/t2 colour, normal)
SamplerState screenSampler   : register(s5); // stock: t85 sun visibility
SamplerState clampSampler    : register(s8); // stock: lightmaps t114/t115, GTAO, lookups

#ifndef STATIC_MODEL_OPAQUE
StructuredBuffer<uint2> lightmapRects  : register(t0);  // code buffer 40
Texture2D<float4>       albedoMap      : register(t1);  // material texture slot 0 (*_c), a = spec code
StructuredBuffer<uint2> lightingTiles  : register(t5);  // code buffer 44, 8x8-pixel tile records
Texture2DArray<float4>  lightmapColor  : register(t114);
Texture2DArray<float4>  lightmapDir    : register(t115);
#else
Texture2D<float4>       albedoMap      : register(t0);  // material texture slot 0
StructuredBuffer<uint2> lightingTiles  : register(t4);  // code buffer 44
#endif
Texture2D<float4>       sunVisibility  : register(t85); // persistent image 0 (cs_sunvis output)

float HalfAt(uint word, uint shift) { return f16tof32((word >> shift) & 0xffff); }

float3 Albedo(float2 uv)
{
    // UV transform (half floats): cb3[1].z = scale, cb3[1].y = (cos, sin), cb3[1].w = offset (doc 95).
    float2 s = float2(HalfAt(cb3[1].z, 0), HalfAt(cb3[1].z, 16));
    float2 r = float2(HalfAt(cb3[1].y, 0), HalfAt(cb3[1].y, 16));
    float2 o = float2(HalfAt(cb3[1].w, 0), HalfAt(cb3[1].w, 16));
    float2 a = uv * s;
    float2 t = float2(r.x * a.x + r.y * a.y, -r.y * a.x + r.x * a.y) + o;
    float3 albedo = albedoMap.SampleBias(materialSampler, t, cb2[7].y).rgb;
    // Tint bytes 0..2 of cb3[2].x (the tint mask t3 is not sampled here: unmasked tint).
    float3 tint = float3(cb3[2].x & 255, (cb3[2].x >> 8) & 255, (cb3[2].x >> 16) & 255) / 255.0;
    return albedo * tint;
}

float3 DecodeHemiOct(float2 e)
{
    float2 o = float2(e.x + e.y, e.x - e.y) * 0.5;
    return normalize(float3(o, 1.0 - abs(o.x) - abs(o.y)));
}

float SunVisibility(float2 pixel, float2 screenUv)
{
    // Tiles of class 11 skip the screen-space sun visibility when cb7[83].w > 0 (stock W:839-852).
    uint tilesX = (uint(cb7[54].x) + 7) >> 3;
    uint2 tile = uint2(pixel) >> 3;
    uint tileClass = lightingTiles[tile.y * tilesX + tile.x].y >> 28;
    float visibility = 0.0;
    if (!(cb7[83].w > 0.0 && tileClass == 11))
        visibility = abs(sunVisibility.SampleLevel(screenSampler, screenUv, 0).x);
    return visibility;
}

float4 main(PSInput input) : SV_TARGET
{
    float2 pixel = input.position.xy * cb2[7].x;
    float2 screenUv = pixel * cb2[6].zw;
    float3 n = normalize(input.normal);
    float3 albedo = Albedo(input.texcoords.xy);

#ifndef STATIC_MODEL_OPAQUE
    // Baked irradiance from the lightmap (stock W:483-530, geometric normal instead of the normal map).
    uint2 rect = lightmapRects[cb9[0].y];
    float2 size = float2(rect.y & 0xffff, rect.y >> 16) * cb7[212].y;
    float2 offset = float2(rect.x & 8191, (rect.x >> 13) & 8191) * cb7[212].y;
    float3 lmUv = float3(input.lmapcoord * size + offset, floor(float(rect.x >> 26) + 0.5));
    float3 color = lightmapColor.SampleLevel(clampSampler, lmUv, 0).rgb * cb7[43].z;
    float3 q = lightmapDir.SampleLevel(clampSampler, lmUv, 0).xyz;
    float3 dirT = DecodeHemiOct(q.xy * 2.0 - 1.0);
    float3 bitangent = cross(input.normal, input.tangent.xyz) * input.tangent.w;
    float3 dirW = normalize(dirT.x * input.tangent.xyz + dirT.y * bitangent + dirT.z * input.normal);
    float3 ambient = q.z * color;
    float3 directional = (1.0 - q.z) * color / max(dirT.z, 0.001);
    float3 baked = directional * saturate(dot(n, dirW)) + ambient;
#else
    // Static models: baked light per vertex, not scaled by cb7[43].z (stock bce5a6cd lines 478-491, doc 95).
    float3 ambient = input.ambientColor.rgb * input.ambientColor.a;
    float3 directional = input.ambientColor.rgb * (1.0 - input.ambientColor.a) /
                         max(dot(input.highlightDirVis, n), 0.05);
    float3 baked = directional * saturate(dot(n, input.highlightDirVis)) + ambient;
#endif

    // Sun: cb7[56].xyz towards the sun, .w != 0 = on; cb7[57].rgb radiance (stock W:837-908, diffuse only, no 1/pi).
    float3 sun = 0;
    if (cb7[56].w != 0.0)
        sun = cb7[57].rgb * SunVisibility(pixel, screenUv) * saturate(dot(n, cb7[56].xyz));

    float3 lit = albedo * (baked + sun);
    // Output as the stock shader writes it: HDR radiance x radiometricScale (cb7[42].x), alpha 1, clamped to 32255.
    return min(float4(lit * cb7[42].x, 1.0), 32255.0);
}
