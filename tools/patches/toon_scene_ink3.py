import io

# Toon ink, third step: a face seen nearly edge-on that is only a few pixels wide (the top of a window frame, the side
# of a post) was inked on some pixels and not on others, a comb along the line. Such a face is now part of the line.
# Line widths back up (the owner wants thick lines): 5 pixels near, thinning from 2.2 units on the surface.

ROOT = 'E:/IW8-Radiant/'
P = 'data/shaders/toon.iw8shader.hlsl'
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('static const float kLinePixels = 4.0; ', 'static const float kLinePixels = 5.0; ')
rep('static const float kLineMaxUnits = 1.5; ', 'static const float kLineMaxUnits = 2.2; ')
rep('''static const float kOutlineHigh = 5.0;    // ... to an outline's
''', '''static const float kOutlineHigh = 5.0;    // ... to an outline's
static const float kSliverTilt = 2.5;     // a face tilted from the screen by more than this (tangent; about 68
                                          // degrees) and no wider than an outline is drawn as part of the line
''')
rep('''    float ink = 0.0;
    [unroll] for (int w = 0; w < 8; ++w) {
        const float2 way = kToonWays[w];
        const float unit = w < 4 ? 1.0 : 1.41421356;
''', '''    float ink = 0.0;
    float ends[8];   // per way: the pixels to the nearest sample that is clearly off the plane (1e6: none in reach)
    float tilts[8];
    [unroll] for (int w = 0; w < 8; ++w) {
        const float2 way = kToonWays[w];
        const float unit = w < 4 ? 1.0 : 1.41421356;
''')
rep('''        float4 rise = 0.0;  // per sample: behind the plane > 0, in front < 0, 0 on it or unknown
''', '''        float4 rise = 0.0;  // per sample: behind the plane > 0, in front < 0, 0 on it or unknown
        ends[w] = 1e6;
        tilts[w] = abs(tilt);
''')
rep('''            if (depth > 1e-3 && planeInv > 1e-9)
                rise[n] = (planeInv * min(depth, 1e8) - 1.0) / (pixelSize * unit * float(n + 1)) * level;
        }
''', '''            if (depth > 1e-3 && planeInv > 1e-9)
                rise[n] = (planeInv * min(depth, 1e8) - 1.0) / (pixelSize * unit * float(n + 1)) * level;
        }
        [unroll] for (int e = 3; e >= 0; --e)
            if (abs(rise[e]) > kFoldHigh * level) ends[w] = unit * float(e + 1);
''')
rep('''    return ink;
}

// The same lines from the edge inputs''', '''    // A face seen nearly edge-on with an end close by on both sides.
    static const int2 kAcross[4] = {int2(0, 1), int2(2, 3), int2(4, 7), int2(5, 6)};
    [unroll] for (int p = 0; p < 4; ++p) {
        const float span = ends[kAcross[p].x] + ends[kAcross[p].y] - 1.0;  // the face's width on screen
        ink = max(ink, saturate(width - span + 1.0) * smoothstep(kSliverTilt, 2.0 * kSliverTilt, tilts[kAcross[p].x]));
    }
    return ink;
}

// The same lines from the edge inputs''')
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
io.open(ROOT + 'out/agent/mp_cinderrail_toon/shaders/toon.iw8shader.hlsl', 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
