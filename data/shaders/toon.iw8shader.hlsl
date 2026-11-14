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
//   3. Ink: lines of a set width on screen, found from the scene's depth around each pixel (iw8rSceneDepth). Where
//      what is drawn beside a pixel lies behind the pixel's own face, the face ends there: an outline, drawn on the
//      face. Where the neighbouring face meets this one, the two fold there: an inner line, half on each face,
//      thinner than an outline and thinning out as the fold gets softer. Faces in one plane have nothing between
//      them, so there is no line there, and lines run into each other without ends or gaps, whatever the geometry
//      is made of. Far away the lines thin out and fade. A host without scene depth gets the lines from the edge
//      inputs instead (toonInkEdges).
//
// Three functions: iw8rSurface lights a map's own surfaces (all of the above). iw8rFilter and iw8rFilterScreen, at the
// end, are the toon look for surfaces that keep the game's own lighting: a stock map's world and props (File >
// Rebuild Stock Zone builds them into the map's lit shaders). They see only the finished colour, so they give the
// vivid colour and the light in steps; iw8rFilterScreen also has the screen, and draws the same ink lines from the
// scene's depth (the build uses it in every stock shader that can take it, iw8rFilter in the rest).

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
static const float kLinePixels = 5.0;     // an outline's width on screen, near the eye
static const float kLineMaxUnits = 2.2;   // ... but never wider than this on the surface: from where that many pixels
                                          // would cover more, the line thins with distance (far windows and frames
                                          // stay drawings, not black blocks)
static const float kLineMinPixels = 1.0;  // ... and never thinner than this while it is drawn at all
static const float kLineFarFade = 0.55;   // a line at its thinnest is this strong (1 = as black as a near one)
static const float kCrease = 0.9;         // an inner line (both faces show) against an outline: its share of the width
static const float kFoldLow = 0.20;       // the rise where an inner line begins (a fold of about 11 degrees) ...
static const float kFoldHigh = 0.40;      // ... and where it is whole (about 22 degrees)
static const float kOutlineLow = 2.0;     // the rise from which a line widens from an inner line's reach ...
static const float kOutlineHigh = 5.0;    // ... to an outline's
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

// How much of this pixel a line covers that reaches `reach` pixels to each side of its centre, the centre `away`
// pixels from the pixel's (the overlap of the two, so a line thinner than a pixel is a lighter line).
float toonCover(float away, float reach)
{
    return saturate(min(away + reach, 0.5) - max(away - reach, -0.5));
}

// 1 where the rise in front of the plane holds from one sample to the next one out (`share`: their distances, the
// nearer over the farther), 0 where it falls as one over the distance.
float toonHolds(float nearer, float farther, float share)
{
    return saturate((farther / min(nearer, -1e-6) - share) / (1.0 - share));
}

static const float2 kToonWays[8] = {float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
                                    float2(1, 1), float2(-1, 1), float2(1, -1), float2(-1, -1)};

// The ink from the scene's depth, for an outline `width` pixels wide. `usable` is false when the host has no depth
// that fits this pixel (the caller then draws the lines from the edge inputs).
//
// Along a line on screen, one over the depth of a flat face changes by the same amount every pixel. So the depth this
// face's plane would have at a neighbouring pixel is known exactly, and what is drawn there is compared with it. The
// measure is the rise: how far what is drawn lies off the plane, over the distance to that pixel (both in pixel
// sizes; it is the tangent of the angle at which the scene leaves the plane, as seen from this pixel).
//   - no rise: the same face, or one in the same plane. Nothing, so faces in one plane have no line between them.
//   - behind the plane (the face ends there, or folds away): ink. A gentle rise is a fold: an inner line, thinner,
//     and thinner still as the fold gets softer. A steep one is the end of the face: an outline, the whole width.
//   - in front of the plane: ink only where the scene rises out of the plane from the pixel on (an inner corner: the
//     rise does not fall with the distance). Something that stands in front at a distance draws its own outline.
//
// `pixel`, `viewDepth`: this pixel on screen and its depth along the view; `pixelSize`: a pixel's size per unit of
// depth. Nothing else of the surface is needed, so a stock map's surfaces can be inked the same way (iw8rFilterScreen).
float toonInkScene(float2 pixel, float viewDepth, float pixelSize, float width, out bool usable)
{
    const float invMine = 1.0 / max(viewDepth, 1e-3);
    const float2 slope = float2(ddx(invMine), ddy(invMine));

    const float here = iw8rSceneDepth(pixel);
    usable = here > 1e-3 && here < 1e8 && abs(here * invMine - 1.0) < 0.05 && pixelSize > 1e-9;
    if (!usable) return 0.0;
    // The plane's depth is counted from the surface's own depth where the host's depth agrees with it as far as it
    // can (it is only as exact as iw8rSceneDepthStep), else from the host's: a host whose depth is a frame old (the
    // editor while the camera moves) then still compares like with like.
    const float invHere = abs(here * invMine - 1.0) <= iw8rSceneDepthStep ? invMine : 1.0 / here;
    // A depth read can be off by half a step: that much of a sample's rise is taken as nothing (with a margin).
    const float blur = 0.85 * iw8rSceneDepthStep / pixelSize;
    const float inner = 0.5 * kCrease * width;  // an inner line's reach to each side

    float ink = 0.0;
    float ends[8];   // per way: the pixels to the nearest sample that is clearly off the plane (1e6: none in reach)
    [unroll] for (int w = 0; w < 8; ++w) {
        const float2 way = kToonWays[w];
        const float unit = w < 4 ? 1.0 : 1.41421356;
        // This face's own tilt along the way (the tangent of its angle to the screen): a face seen nearly edge-on
        // has a large rise to everything beside it, which says nothing about how sharp the fold is.
        const float tilt = dot(slope, way) / (unit * invHere * pixelSize);
        const float level = 1.0 / sqrt(1.0 + tilt * tilt);
        float4 rise = 0.0;  // per sample: behind the plane > 0, in front < 0, 0 on it or unknown
        ends[w] = 1e6;
        [unroll] for (int n = 0; n < 4; ++n) {
            const float2 o = way * float(n + 1);
            const float depth = iw8rSceneDepth(pixel + o);
            const float planeInv = invHere + dot(slope, o);
            // No depth there (off the screen), or the plane has passed its horizon: taken as on the plane.
            if (depth > 1e-3 && planeInv > 1e-9) {
                const float off = planeInv * min(depth, 1e8) - 1.0;
                rise[n] = sign(off) * max(abs(off) / pixelSize - blur, 0.0) / (unit * float(n + 1)) * level;
            }
        }
        [unroll] for (int e = 3; e >= 0; --e)
            if (abs(rise[e]) > kFoldHigh * level) ends[w] = unit * float(e + 1);
        [unroll] for (int m = 0; m < 4; ++m) {
            const float pixels = unit * float(m + 1);
            const float r = rise[m];
            if (r > 0.0) {
                const float reach = lerp(inner, width, smoothstep(kOutlineLow, kOutlineHigh, r));
                ink = max(ink, smoothstep(kFoldLow, kFoldHigh, r) * saturate(reach - pixels + 1.0));
            } else if (r < 0.0) {
                // With a corner the rise holds or grows with the distance; with something standing in front it falls
                // as one over the distance. It has to hold over three samples: the side of something that stands
                // out by a pixel or two is not a corner worth a line of its own (it would come and go along the
                // edge as that side crosses the pixel rows).
                const int a = min(m, 1);
                const float holds = toonHolds(rise[a], rise[a + 1], float(a + 1) / float(a + 2)) *
                                    toonHolds(rise[a + 1], rise[a + 2], float(a + 2) / float(a + 3));
                ink = max(ink, smoothstep(kFoldLow, kFoldHigh, -r) * holds * saturate(inner - pixels + 1.0));
            }
        }
    }
    // A face no wider than a line, with an end on both sides, is part of the line (the side of a post, the top of a
    // frame: a pixel or two that would otherwise show between two lines).
    static const int2 kAcross[4] = {int2(0, 1), int2(2, 3), int2(4, 7), int2(5, 6)};
    [unroll] for (int p = 0; p < 4; ++p) {
        const float span = ends[kAcross[p].x] + ends[kAcross[p].y] - 1.0;  // the face's width on screen
        ink = max(ink, saturate(inner + 1.0 - span));
    }
    return ink;
}

// The same lines from the edge inputs (brush faces carry the distance to their nearest edge), for a host without
// scene depth.
float toonInkEdges(IW8RSurface s)
{
    float ink = 0.0;
    // Brush faces: the nearest edge of the face.
    if (s.edgePixels < 1e5) {
        // The face across the edge turned away from the eye: a silhouette, the whole line is on this face. Facing the
        // eye too: each face draws half. In between the width follows the turn, so nothing pops.
        const float across = dot(s.edgeNormal, s.view);
        const float seenToo = saturate(across * 12.0);
        const bool soft = dot(s.edgeNormal, s.faceNormal) > kSoftFold;
        // An outline is the whole width; an inner line is kCrease of it, half on each face.
        const float share = lerp(1.0, soft ? 0.0 : 0.5 * kCrease, seenToo);
        // The width near the eye, thinned from where that many pixels would be wider than kLineMaxUnits on the
        // surface, down to kLineMinPixels; a line that thin is also lighter.
        float full = kLinePixels;
        float strength = 1.0;
        if (s.edgeUnits > 1e-4) {
            const float pixelsPerUnit = s.edgePixels / s.edgeUnits;
            const float wanted = kLineMaxUnits * pixelsPerUnit;
            full = clamp(wanted, kLineMinPixels, kLinePixels);
            strength = lerp(kLineFarFade, 1.0, saturate((wanted - kLineMinPixels * 0.5) / kLineMinPixels));
        }
        ink = toonLine(s.edgePixels, full * share) * (share > 0.0 ? strength : 0.0);
    }
    // Rounded surfaces (models): the silhouette is where the surface turns edge-on; N.V over its change per pixel is
    // the distance to it in pixels. Flat faces do not turn, so they draw nothing here.
    const float facing = dot(s.faceNormal, s.view);
    const float turn = length(float2(ddx(facing), ddy(facing)));
    const float curved = length(fwidth(s.faceNormal));
    if (curved > 2e-3 && turn > 1e-5) ink = max(ink, toonLine(max(facing, 0.0) / turn, kLinePixels));
    return ink;
}

float toonInk(IW8RSurface s)
{
    // The width near the eye, thinned from where that many pixels would be wider than kLineMaxUnits on the surface,
    // down to kLineMinPixels; a line that thin is also lighter.
    const float3 ray = (s.position - s.eye) / max(s.viewDepth, 1e-3);
    const float unitsPerPixel = max(length(ddx(ray)), length(ddy(ray))) * s.viewDepth;
    const float wanted = kLineMaxUnits / max(unitsPerPixel, 1e-6);
    const float width = clamp(wanted, kLineMinPixels, kLinePixels);
    const float strength = lerp(kLineFarFade, 1.0, saturate((wanted - kLineMinPixels * 0.5) / kLineMinPixels));
    const float edges = toonInkEdges(s);
    bool usable;
    const float scene = toonInkScene(s.pixel, s.viewDepth, unitsPerPixel / max(s.viewDepth, 1e-3), width, usable);
    return usable ? scene * strength : edges;
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

// The same with the screen: the ink lines over the filtered colour.
float3 iw8rFilterScreen(float3 colour, float2 pixel, float viewDepth, float pixelSize)
{
    const float3 c = iw8rFilter(colour);
    const float wanted = kLineMaxUnits / max(pixelSize * viewDepth, 1e-6);
    const float width = clamp(wanted, kLineMinPixels, kLinePixels);
    const float strength = lerp(kLineFarFade, 1.0, saturate((wanted - kLineMinPixels * 0.5) / kLineMinPixels));
    bool usable;
    const float ink = toonInkScene(pixel, viewDepth, pixelSize, width, usable) * strength;
    return usable ? lerp(c, c * kInk * kInkTint, ink) : c;
}
