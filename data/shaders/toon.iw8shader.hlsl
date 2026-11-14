// Toon (anime cel) look for a map's own surfaces. Everything about the look is in this file: edit it in
// Tools > Shader Editor (make a copy with New), nothing of it is built into the editor.
//
// Used by worldspawn "iw8r_surface_shader" "toon" or a material's "shader": "toon.iw8shader.hlsl". The inputs are
// IW8RSurface (IW8 Radiant, src/core/custom_shader.h); the same code runs in the viewport and in the built map.
//
// The model is the common one of anime toon shaders (lilToon, MIT, github.com/lilxyzw/lilToon; Unity Toon Shader /
// UTS2; Arc System Works' GDC 2015 talk on Guilty Gear Xrd), written for this interface, not copied:
//   1. Colour: the texture made more vivid (dull colours gain the most), with more contrast and its fine noise
//      gathered into a few painted steps.
//   2. Light: the sun in flat tones with crisp borders, on the surface's own normal (normal-map bumps would break the
//      borders up); cast shadows get the same crisp border. Shade is a cooler, more saturated colour, not just darker.
//   3. Ink: outlines of a constant width on screen. On brush faces from the edge inputs (the distance to the nearest
//      edge of the face in pixels): a silhouette edge gets the full width on its face, an edge whose two faces both
//      show gets half on each, so every line is the same width; soft folds are inked only as silhouettes. On rounded
//      models from how fast the surface turns away from the eye.
//
// Two functions: iw8rSurface lights a map's own surfaces (all of the above). iw8rFilter, at the end, is the toon look
// for surfaces that keep the game's own lighting: a stock map's world and props (File > Rebuild Stock Zone builds it
// into the map's lit shaders). It sees only the finished colour, so it gives the vivid colour and the light in steps;
// the ink lines need the edge inputs, which stock geometry does not have.

// ---- colour
static const float kVibrance = 1.10;      // extra saturation for dull colours (0 = the texture as it is)
static const float kSaturation = 1.18;    // saturation of every colour
static const float kBrightness = 1.12;    // the texture's brightness (cel colours are clean and light)
static const float kContrast = 0.30;      // 0 none .. 1 strong, around kContrastPivot
static const float kContrastPivot = 0.42; // the brightness that contrast leaves alone (a mid-grey texture, as seen)
static const float kPaint = 0.45;         // 0 the texture's own shading .. 1 flat painted steps
static const float kPaintSteps = 7.0;     // brightness steps of the painted look
// ---- light
static const float kToneLow = 0.08;       // sun angle (N.L x shadow) where shade turns to the middle tone
static const float kToneHigh = 0.36;      // ... and the middle tone to full light
static const float kToneMiddle = 0.46;    // the sun's share in the middle tone
static const float kToneFull = 0.80;      // ... and in full light (below 1: flat tones light more of a face than N.L)
static const float kToneFeather = 0.02;   // half width of a tone border
static const float kShadowLow = 0.42;     // the cast shadow's soft edge is tightened to this range
static const float kShadowHigh = 0.58;
static const float kShadeLevel = 0.40;    // shade is never darker than this share of full light (0 = as dark as the
                                          // map's baked light makes it; cel shade is a tone, not blackness)
static const float3 kLitTint = float3(1.04, 1.00, 0.94);    // light leans warm
static const float3 kShadeTint = float3(0.86, 0.92, 1.12);  // shade leans cool
static const float kShadeSaturation = 1.25;
// ---- ink
static const float kLinePixels = 3.0;     // line width on screen
static const float kLineMaxUnits = 3.4;   // ... but never wider than this on the surface (far lines thin out;
                                          // the edge inputs reach 4 units from an edge)
static const float kInk = 0.07;           // brightness left under a line (0 = black)
static const float3 kInkTint = float3(0.85, 0.80, 1.0);  // ink leans to a dark violet rather than grey
static const float kSoftFold = 0.90;      // cos of the fold below which an edge is hard (about 26 degrees)

// ---- stock surfaces (iw8rFilter)
static const float kFilterVibrance = 0.70; // as kVibrance, on the finished colour (the light's own tint gains too)
static const float kFilterStops = 0.75;    // the light in steps this many stops high (a stop is twice the light)
static const float kFilterBands = 0.75;    // 0 the game's smooth light .. 1 flat steps
static const float kFilterEdge = 0.10;     // softness of a step's border, as a share of a step

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

float3 toonSaturate(float3 c, float amount)
{
    return max(lerp(dot(c, kLuma).xxx, c, amount), 0.0);
}

// 1. The texture as a painter would lay it down.
float3 toonColour(float3 albedo)
{
    const float hi = max(albedo.r, max(albedo.g, albedo.b));
    const float lo = min(albedo.r, min(albedo.g, albedo.b));
    const float saturation = hi > 1e-5 ? (hi - lo) / hi : 0.0;
    float3 c = toonSaturate(albedo, kSaturation + kVibrance * (1.0 - saturation));

    // Contrast and the painted steps work on the brightness as the eye sees it (the square root of linear light).
    const float light = max(dot(c, kLuma), 1e-5);
    float seen = saturate(sqrt(light * kBrightness));
    // An S curve through the pivot: darker below it, lighter above, the ends kept.
    const float curve = 1.0 + kContrast;
    seen = seen < kContrastPivot ? kContrastPivot * pow(seen / kContrastPivot, curve)
                                 : 1.0 - (1.0 - kContrastPivot) * pow((1.0 - seen) / (1.0 - kContrastPivot), curve);
    const float stepped = seen * kPaintSteps;
    const float painted = (floor(stepped) + smoothstep(0.25, 0.75, frac(stepped))) / kPaintSteps;
    seen = lerp(seen, painted, kPaint);
    return c * (seen * seen / light);
}

// 2. The sun's share of light in flat tones.
float toonTone(float direct)
{
    const float middle = smoothstep(kToneLow - kToneFeather, kToneLow + kToneFeather, direct);
    const float full = smoothstep(kToneHigh - kToneFeather, kToneHigh + kToneFeather, direct);
    return lerp(middle * kToneMiddle, kToneFull, full);
}

// 3. How much of this pixel a line covers (0 none .. 1 all), for a line `width` pixels wide whose centre is `pixels`
// away, with one pixel of soft edge.
float toonLine(float pixels, float width)
{
    return saturate(width - pixels + 0.5);
}

float toonInk(IW8RSurface s)
{
    float ink = 0.0;
    // Brush faces: the nearest edge of the face.
    if (s.edgePixels < 1e5) {
        // The face across the edge turned away from the eye: a silhouette, the whole line is on this face. Facing the
        // eye too: each face draws half. In between the width follows the turn, so nothing pops.
        const float across = dot(s.edgeNormal, s.view);
        const float seenToo = saturate(across * 12.0);
        const bool soft = dot(s.edgeNormal, s.faceNormal) > kSoftFold;
        float width = kLinePixels * lerp(1.0, soft ? 0.0 : 0.5, seenToo);
        // Far away a line of that many pixels would be wider than kLineMaxUnits on the surface: thin it instead.
        if (s.edgeUnits > 1e-4) width = min(width, kLineMaxUnits * s.edgePixels / s.edgeUnits);
        ink = toonLine(s.edgePixels, width);
    }
    // Rounded surfaces (models): the silhouette is where the surface turns edge-on; N.V over its change per pixel is
    // the distance to it in pixels. Flat faces do not turn, so they draw nothing here.
    const float facing = dot(s.faceNormal, s.view);
    const float turn = length(float2(ddx(facing), ddy(facing)));
    const float curved = length(fwidth(s.faceNormal));
    if (curved > 2e-3 && turn > 1e-5) ink = max(ink, toonLine(max(facing, 0.0) / turn, kLinePixels));
    return ink;
}

float3 iw8rSurface(IW8RSurface s)
{
    const float3 base = toonColour(s.albedo);

    // The sun on the face's own normal, with the cast shadow's edge tightened: a point is in shade or it is not.
    const float shadow = smoothstep(kShadowLow, kShadowHigh, s.sunShadow);
    const float tone = toonTone(saturate(dot(s.faceNormal, s.sunDir)) * shadow);
    const float lit = saturate(tone / kToneMiddle);  // 0 in shade, 1 from the middle tone up

    const float3 shadeColour = toonSaturate(base, kShadeSaturation) * kShadeTint;
    const float3 litColour = base * kLitTint;
    // The light: the baked light plus the sun's tone, lifted to the shade level with light of the baked light's colour.
    float3 light = s.ambient + s.sunColor * tone;
    const float lightNow = dot(light, kLuma);
    const float lightFloor = kShadeLevel * dot(s.ambient + s.sunColor * kToneFull, kLuma);
    if (lightNow < lightFloor) {
        const float ambientLuma = dot(s.ambient, kLuma);
        const float3 fill = ambientLuma > 1e-6 ? s.ambient / ambientLuma : float3(1, 1, 1);
        light += fill * (lightFloor - lightNow);
    }
    float3 c = lerp(shadeColour, litColour, lit) * light;

    // Ink over it.
    const float ink = toonInk(s);
    return lerp(c, c * kInk * kInkTint, ink);
}

// The finished colour of a surface the game lights itself (lit, fogged, exposure applied): vivid, and its brightness
// gathered into steps, the border in the middle of each step so that the picture is as bright as before on average.
float3 iw8rFilter(float3 colour)
{
    const float hi = max(colour.r, max(colour.g, colour.b));
    const float lo = min(colour.r, min(colour.g, colour.b));
    const float saturation = hi > 1e-6 ? (hi - lo) / hi : 0.0;
    const float3 c = toonSaturate(colour, kSaturation + kFilterVibrance * (1.0 - saturation));

    const float light = max(dot(c, kLuma), 1e-6);
    const float stops = log2(light) / kFilterStops;
    const float stepped = floor(stops) + smoothstep(0.5 - kFilterEdge, 0.5 + kFilterEdge, frac(stops));
    const float banded = lerp(stops, stepped, kFilterBands);
    return c * (exp2(banded * kFilterStops) / light);
}
