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
rep('''static const float kSliverTilt = 2.5;     // a face tilted from the screen by more than this (tangent; about 68
                                          // degrees) and no wider than an outline is drawn as part of the line
''', '')
rep('''    float tilts[8];
''', '')
rep('''        tilts[w] = abs(tilt);
''', '')
rep('''    // A face seen nearly edge-on with an end close by on both sides.
''', '''    // A face no wider than a line, with an end on both sides, is part of the line (the side of a post, the top of a
    // frame: a pixel or two that would otherwise show between two lines).
''')
rep('''        ink = max(ink, saturate(width - span + 1.0) * smoothstep(kSliverTilt, 2.0 * kSliverTilt, tilts[kAcross[p].x]));''',
    '''        ink = max(ink, saturate(inner + 1.0 - span));''')
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
io.open(ROOT + 'out/agent/mp_cinderrail_toon/shaders/toon.iw8shader.hlsl', 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
