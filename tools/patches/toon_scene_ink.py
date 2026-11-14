import io

# Toon ink from the scene's depth (owner 2026-11-14: "the ends of lines are still visible, they should be seamless,
# this toon shader should look professionally and intricately made"). The lines are found on screen from what is
# actually drawn around a pixel, so faces in one plane have no line between them and every line joins the next; the
# edge inputs stay as the fallback for a host without scene depth.

ROOT = 'E:/IW8-Radiant/'
P = 'data/shaders/toon.iw8shader.hlsl'
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''//   3. Ink: outlines of a constant width on screen. On brush faces from the edge inputs (the distance to the nearest
//      edge of the face in pixels): a silhouette edge gets the full width on its face, an edge whose two faces both
//      show gets half on each, so every line is the same width; soft folds are inked only as silhouettes. On rounded
//      models from how fast the surface turns away from the eye.
''', '''//   3. Ink: lines of a set width on screen, found from the scene's depth around each pixel (iw8rSceneDepth). Where
//      what is drawn beside a pixel lies behind the pixel's own face, the face ends there: an outline, drawn on the
//      face. Where the neighbouring face meets this one, the two fold there: an inner line, half on each face,
//      thinner than an outline and thinning out as the fold gets softer. Faces in one plane have nothing between
//      them, so there is no line there, and lines run into each other without ends or gaps, whatever the geometry
//      is made of. Far away the lines thin out and fade. A host without scene depth gets the lines from the edge
//      inputs instead (toonInkEdges).
''')
rep('''// ---- ink
static const float kLinePixels = 5.0;     // line width on screen, near the eye
static const float kLineMaxUnits = 3.0;   // ... but never wider than this on the surface: from where 5 pixels would
                                          // cover more, the line thins with distance (far windows and frames stay
                                          // drawings, not black blocks)
static const float kLineMinPixels = 1.25; // ... and never thinner than this while it is drawn at all
static const float kLineFarFade = 0.55;   // a line at its thinnest is this strong (1 = as black as a near one)
static const float kCrease = 0.6;         // an inner line (both faces show) against an outline: its share of the width
''', '''// ---- ink
static const float kLinePixels = 4.0;     // an outline's width on screen, near the eye
static const float kLineMaxUnits = 3.0;   // ... but never wider than this on the surface: from where that many pixels
                                          // would cover more, the line thins with distance (far windows and frames
                                          // stay drawings, not black blocks)
static const float kLineMinPixels = 1.25; // ... and never thinner than this while it is drawn at all
static const float kLineFarFade = 0.55;   // a line at its thinnest is this strong (1 = as black as a near one)
static const float kCrease = 0.75;        // an inner line (both faces show) against an outline: its share of the width
static const float kFoldLow = 0.22;       // the sine of the fold where an inner line begins (about 13 degrees) ...
static const float kFoldHigh = 0.45;      // ... and where it has its whole width (about 27 degrees)
static const float kStepPixels = 1.5;     // a step in depth is an outline from this many pixel sizes of depth
''')
rep('''float toonInk(IW8RSurface s)
{
    float ink = 0.0;
    // Brush faces: the nearest edge of the face.
    if (s.edgePixels < 1e5) {''', '''// How much of this pixel a line covers that reaches `reach` pixels to each side of its centre, the centre `away`
// pixels from the pixel's (the overlap of the two, so a line thinner than a pixel is a lighter line).
float toonCover(float away, float reach)
{
    return saturate(min(away + reach, 0.5) - max(away - reach, -0.5));
}

static const float2 kToonWays[8] = {float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
                                    float2(1, 1), float2(-1, 1), float2(1, -1), float2(-1, -1)};

// The ink from the scene's depth, for an outline `width` pixels wide. `usable` is false when the host has no depth
// that fits this pixel (the caller then draws the lines from the edge inputs).
//
// Along a line on screen, one over the depth of a flat face changes by the same amount every pixel. So the depth this
// face's plane would have at a neighbouring pixel is known exactly, and what is drawn there is compared with it:
//   - on the plane: the same face, or one in the same plane. Nothing.
//   - off it, by more the farther out: another face that meets this one. The two samples give the pixel where they
//     meet (to a fraction of a pixel, so the line's side is smooth) and how sharp the fold is.
//   - behind it at once: this face ends there. An outline.
//   - in front of it at once: something stands in front. Its own pixels draw that outline, not these.
float toonInkScene(IW8RSurface s, float width, out bool usable)
{
    const float invMine = 1.0 / max(s.viewDepth, 1e-3);
    const float2 slope = float2(ddx(invMine), ddy(invMine));
    const float3 ray = (s.position - s.eye) * invMine;  // from the eye, per unit of depth: the same step every pixel
    const float3 rayX = ddx(ray), rayY = ddy(ray);
    const float3 turn = cross(ddx(s.position), ddy(s.position));
    const float3 plane = dot(turn, turn) > 1e-24 ? normalize(turn) : s.faceNormal;

    const float here = iw8rSceneDepth(s.pixel);
    usable = here > 1e-3 && here < 1e8 && abs(here * invMine - 1.0) < 0.05;
    if (!usable) return 0.0;
    // The plane's depth is counted from the depth the host has at this pixel, not from the surface's own: a host
    // whose depth is a frame old (the editor while the camera moves) then still compares like with like.
    const float invHere = 1.0 / here;
    const float pixelSize = max(length(rayX), length(rayY));  // a pixel's size, per unit of depth
    const float limit = kStepPixels * pixelSize;              // a step in depth, as a share of the depth
    const float flat = 0.2 * limit;                           // nearer the plane than this is on it
    const float creaseReach = 0.5 * kCrease * width;

    float ink = 0.0;
    [unroll] for (int w = 0; w < 8; ++w) {
        const float2 way = kToonWays[w];
        const float unit = w < 4 ? 1.0 : 1.41421356;
        // Four pixels along the way: two just past where an inner line can reach, two around an outline's width.
        const float first = max(1.0, ceil((creaseReach + 0.5) / unit));
        const float third = max(first + 2.0, round(width / unit));
        const float4 steps = float4(first, first + 1.0, third, third + 1.0);
        const float4 pixels = steps * unit;
        float4 off = 0.0;     // one over the depth there, less the plane's
        float4 behind = 0.0;  // how far behind the plane, as a share of the depth (below 0: in front)
        bool4 drawn = false;  // a surface is drawn there (not the sky)
        float3 at[4];         // what is drawn there, from the eye
        [unroll] for (int n = 0; n < 4; ++n) {
            const float2 o = way * steps[n];
            const float depth = iw8rSceneDepth(s.pixel + o);
            const float planeInv = invHere + dot(slope, o);
            at[n] = (ray + o.x * rayX + o.y * rayY) * min(depth, 1e8);
            // No depth there (off the screen), or the plane has passed its horizon: taken as on the plane.
            if (depth > 1e-3 && planeInv > 1e-9) {
                drawn[n] = depth < 1e8;
                off[n] = (drawn[n] ? 1.0 / depth : 0.0) - planeInv;
                behind[n] = planeInv * min(depth, 1e8) - 1.0;
            }
        }
        // The nearest of the first three that is off the plane (the fourth only helps to tell what the third is).
        const int f = abs(behind.x) > flat ? 0 : abs(behind.y) > flat ? 1 : abs(behind.z) > flat ? 2 : 3;
        if (f < 3) {
            const float offA = off[f], offB = off[f + 1];
            const float pixelsA = pixels[f], pixelsB = pixels[f + 1];
            // Two faces that meet: both samples on the same side of the plane, the farther one farther off, and the
            // pixel where they meet between here and the first of them.
            bool fold = false;
            if (drawn[f] && drawn[f + 1] && offA * offB > 0.0 && abs(offA) > 0.08 * abs(offB) && abs(offB) > 1.02 * abs(offA)) {
                const float meet = pixelsA - offA * (pixelsB - pixelsA) / (offB - offA);
                const float nearest = f == 0 ? -0.5 : pixels[f - 1] - 0.5;
                if (meet >= nearest) {
                    fold = true;
                    const float3 along = at[f + 1] - at[f];
                    const float sharp = abs(dot(plane, along)) / max(length(along), 1e-6);
                    const float weight = smoothstep(kFoldLow, kFoldHigh, sharp);
                    ink = max(ink, toonCover(max(meet, 0.0), creaseReach * weight));
                }
            }
            // Otherwise the nearest sample that is behind the plane by a step is past the end of this face.
            if (!fold) {
                float past = 1e6;
                if (behind.z > limit) past = pixels.z;
                if (behind.y > limit && f <= 1) past = pixels.y;
                if (behind.x > limit && f == 0) past = pixels.x;
                ink = max(ink, saturate(width - past + 1.0));
            }
        }
    }
    return ink;
}

// The same lines from the edge inputs (brush faces carry the distance to their nearest edge), for a host without
// scene depth.
float toonInkEdges(IW8RSurface s)
{
    float ink = 0.0;
    // Brush faces: the nearest edge of the face.
    if (s.edgePixels < 1e5) {''')
rep('''    if (curved > 2e-3 && turn > 1e-5) ink = max(ink, toonLine(max(facing, 0.0) / turn, kLinePixels));
    return ink;
}
''', '''    if (curved > 2e-3 && turn > 1e-5) ink = max(ink, toonLine(max(facing, 0.0) / turn, kLinePixels));
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
    const float scene = toonInkScene(s, width, usable);
    return usable ? scene * strength : edges;
}
''')
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))

# The host's helper: 0 for a pixel off the screen (unknown), not the sky.
P2 = 'src/viewport/render.cpp'
t = io.open(ROOT + P2, encoding='utf-8', newline='').read()
a = 'if (pixel.x < 0.0 || pixel.y < 0.0 || pixel.x >= float(zw) || pixel.y >= float(zh)) return 1e9;'
assert t.count(a) == 1
t = t.replace(a, 'if (pixel.x < 0.0 || pixel.y < 0.0 || pixel.x >= float(zw) || pixel.y >= float(zh)) return 0.0;')
a = '// size, as linear view depth (1e9: sky). Unbound (no frame kept yet): Load gives 0, which reads as "no depth".'
assert t.count(a) == 1
t = t.replace(a, '// size, as linear view depth (1e9: sky; 0: off the screen). Unbound (no frame kept yet): Load gives 0, "no depth".')
io.open(ROOT + P2, 'w', encoding='utf-8', newline='').write(t)
P3 = 'src/core/custom_shader.h'
t = io.open(ROOT + P3, encoding='utf-8', newline='').read()
a = '// the view depth (world units along the view; 1e9 for the sky or outside the screen) of what is drawn at a pixel of'
assert t.count(a) == 1, t.count(a)
t = t.replace(a, '// the view depth (world units along the view; 1e9 for the sky, 0 where there is none: off the screen) of what is drawn at a pixel of')
io.open(ROOT + P3, 'w', encoding='utf-8', newline='').write(t)
print('ok')
