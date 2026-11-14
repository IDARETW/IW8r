import io
import re

# Toon ink from the scene's depth, second form: no fitting of two samples (pixels beside a face only a pixel or two
# wide changed class from one pixel to the next, which made ragged double lines). Every sample is now judged by one
# continuous measure, so a line's side is a smooth curve.

ROOT = 'E:/IW8-Radiant/'
P = 'data/shaders/toon.iw8shader.hlsl'
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')

a = s.index('static const float2 kToonWays[8]')
b = s.index('// The same lines from the edge inputs')
s = s[:a] + '''static const float2 kToonWays[8] = {float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
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
float toonInkScene(IW8RSurface s, float width, out bool usable)
{
    const float invMine = 1.0 / max(s.viewDepth, 1e-3);
    const float2 slope = float2(ddx(invMine), ddy(invMine));
    const float3 ray = (s.position - s.eye) * invMine;  // from the eye, per unit of depth: the same step every pixel
    const float pixelSize = max(length(ddx(ray)), length(ddy(ray)));  // a pixel's size, per unit of depth

    const float here = iw8rSceneDepth(s.pixel);
    usable = here > 1e-3 && here < 1e8 && abs(here * invMine - 1.0) < 0.05 && pixelSize > 1e-9;
    if (!usable) return 0.0;
    // The plane's depth is counted from the depth the host has at this pixel, not from the surface's own: a host
    // whose depth is a frame old (the editor while the camera moves) then still compares like with like.
    const float invHere = 1.0 / here;
    const float inner = 0.5 * kCrease * width;  // an inner line's reach to each side

    float ink = 0.0;
    [unroll] for (int w = 0; w < 8; ++w) {
        const float2 way = kToonWays[w];
        const float unit = w < 4 ? 1.0 : 1.41421356;
        // This face's own tilt along the way (the tangent of its angle to the screen): a face seen nearly edge-on
        // has a large rise to everything beside it, which says nothing about how sharp the fold is.
        const float tilt = dot(slope, way) / (unit * invHere * pixelSize);
        const float level = 1.0 / sqrt(1.0 + tilt * tilt);
        float4 rise = 0.0;  // per sample: behind the plane > 0, in front < 0, 0 on it or unknown
        [unroll] for (int n = 0; n < 4; ++n) {
            const float2 o = way * float(n + 1);
            const float depth = iw8rSceneDepth(s.pixel + o);
            const float planeInv = invHere + dot(slope, o);
            // No depth there (off the screen), or the plane has passed its horizon: taken as on the plane.
            if (depth > 1e-3 && planeInv > 1e-9)
                rise[n] = (planeInv * min(depth, 1e8) - 1.0) / (pixelSize * unit * float(n + 1)) * level;
        }
        [unroll] for (int m = 0; m < 4; ++m) {
            const float pixels = unit * float(m + 1);
            const float r = rise[m];
            if (r > 0.0) {
                const float reach = lerp(inner, width, smoothstep(kOutlineLow, kOutlineHigh, r));
                ink = max(ink, smoothstep(kFoldLow, kFoldHigh, r) * saturate(reach - pixels + 1.0));
            } else if (r < 0.0) {
                // The next sample out (for the last one, the one before): with a corner the rise holds or grows
                // with the distance, with something standing in front it falls as one over the distance.
                const int far = m < 3 ? m + 1 : m, near = m < 3 ? m : m - 1;
                const float share = float(near + 1) / float(far + 1);
                const float holds = saturate((rise[far] / min(rise[near], -1e-6) - share) / (1.0 - share));
                ink = max(ink, smoothstep(kFoldLow, kFoldHigh, -r) * holds * saturate(inner - pixels + 1.0));
            }
        }
    }
    return ink;
}

''' + s[b:]


def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


m = re.search(r'// ---- ink\n(.*?)static const float kInk = ', s, re.S)
assert m
s = s[:m.start()] + '''// ---- ink
static const float kLinePixels = 4.0;     // an outline's width on screen, near the eye
static const float kLineMaxUnits = 1.5;   // ... but never wider than this on the surface: from where that many pixels
                                          // would cover more, the line thins with distance (far windows and frames
                                          // stay drawings, not black blocks)
static const float kLineMinPixels = 1.0;  // ... and never thinner than this while it is drawn at all
static const float kLineFarFade = 0.55;   // a line at its thinnest is this strong (1 = as black as a near one)
static const float kCrease = 0.9;         // an inner line (both faces show) against an outline: its share of the width
static const float kFoldLow = 0.20;       // the rise where an inner line begins (a fold of about 11 degrees) ...
static const float kFoldHigh = 0.40;      // ... and where it is whole (about 22 degrees)
static const float kOutlineLow = 2.0;     // the rise from which a line widens from an inner line's reach ...
static const float kOutlineHigh = 5.0;    // ... to an outline's
''' + s[m.end() - len('static const float kInk = '):]
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
io.open(ROOT + 'out/agent/mp_cinderrail_toon/shaders/toon.iw8shader.hlsl', 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
