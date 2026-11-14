import io
ROOT = 'E:/IW8-Radiant/'
P = 'data/shaders/toon.iw8shader.hlsl'
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')
def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)
rep('''                // The next sample out (for the last one, the one before): with a corner the rise holds or grows
                // with the distance, with something standing in front it falls as one over the distance.
                const int far = m < 3 ? m + 1 : m, near = m < 3 ? m : m - 1;
                const float share = float(near + 1) / float(far + 1);
                const float holds = saturate((rise[far] / min(rise[near], -1e-6) - share) / (1.0 - share));
                ink = max(ink, smoothstep(kFoldLow, kFoldHigh, -r) * holds * saturate(inner - pixels + 1.0));''',
    '''                // With a corner the rise holds or grows with the distance; with something standing in front it falls
                // as one over the distance. It has to hold over three samples: the side of something that stands
                // out by a pixel or two is not a corner worth a line of its own (it would come and go along the
                // edge as that side crosses the pixel rows).
                const int a = min(m, 1);
                const float holds = toonHolds(rise[a], rise[a + 1], float(a + 1) / float(a + 2)) *
                                    toonHolds(rise[a + 1], rise[a + 2], float(a + 2) / float(a + 3));
                ink = max(ink, smoothstep(kFoldLow, kFoldHigh, -r) * holds * saturate(inner - pixels + 1.0));''')
rep('''static const float2 kToonWays[8]''', '''// 1 where the rise in front of the plane holds from one sample to the next one out (`share`: their distances, the
// nearer over the farther), 0 where it falls as one over the distance.
float toonHolds(float nearer, float farther, float share)
{
    return saturate((farther / min(nearer, -1e-6) - share) / (1.0 - share));
}

static const float2 kToonWays[8]''')
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
io.open(ROOT + 'out/agent/mp_cinderrail_toon/shaders/toon.iw8shader.hlsl', 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
