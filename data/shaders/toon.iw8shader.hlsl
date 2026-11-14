// Toon (anime cel) shading for a map's own surfaces. Used by worldspawn "iw8r_surface_shader" "toon" or a material's
// "shader": "toon.iw8shader.hlsl"; the inputs are IW8RSurface (IW8 Radiant, src/core/custom_shader.h).
//
// The model is the common cel model of anime toon shaders (see lilToon, MIT, github.com/lilxyzw/lilToon, and Unity's
// Toon Shader / UTS2): the sun's light comes in a few flat tones with sharp, slightly feathered borders instead of a
// smooth falloff; shade takes a darker, more saturated and slightly cool colour; the baked light stays as it is.
// Written for IW8 Radiant (not copied from those shaders). Outlines are geometry (Tools > Toon Ink Outlines): flat
// brush faces give no silhouette for a shader to find.
//
// The tones keep the stock lighting's brightness on average (the sun's N.L on a 40-degree sun is .5-.8 on ground and
// sun-facing walls), so the map is not brighter or darker than with the map's normal shading.

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);
// Sun tones: N.L x shadow above kHigh gets kHighTone of the sun, above kLow kLowTone, below it none (shade).
static const float kLow = 0.10;
static const float kHigh = 0.38;
static const float kLowTone = 0.42;
static const float kHighTone = 0.78;
static const float kFeather = 0.025;      // half width of each tone border
static const float kBaseSaturate = 1.15;  // lit colour a little more vivid than the texture
static const float kShadeSaturate = 1.3;
static const float3 kShadeTint = float3(0.90, 0.94, 1.08);  // anime shade leans cool

float3 saturateColour(float3 c, float amount)
{
    const float g = dot(c, kLuma);
    return max(lerp(g.xxx, c, amount), 0.0);
}

float iw8rTone(float x)
{
    const float low = smoothstep(kLow - kFeather, kLow + kFeather, x);
    const float high = smoothstep(kHigh - kFeather, kHigh + kFeather, x);
    return lerp(low * kLowTone, kHighTone, high);
}

float3 iw8rSurface(IW8RSurface s)
{
    // The sun's shadow folded in: a shadowed point is in shade whatever its angle.
    const float direct = saturate(dot(s.normal, s.sunDir)) * s.sunShadow;
    const float tone = iw8rTone(direct);
    const float lit = tone / kHighTone;  // 0 in shade, 1 in the brightest tone

    const float3 base = saturateColour(s.albedo, kBaseSaturate);
    const float3 shade = saturateColour(s.albedo, kShadeSaturate) * kShadeTint;
    const float3 albedo = lerp(shade, base, saturate(lit * 2.0));
    return albedo * (s.ambient + s.sunColor * tone);
}
