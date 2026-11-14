import io
ROOT = 'E:/IW8-Radiant/'
P = 'src/viewport/render.cpp'
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)
rep('''// a face on screen, so the face's step along an axis is the difference to a neighbour on the same face: the smaller of
// the two differences (the other one crosses an edge of the face).
float ps_scenez(V i) : SV_TARGET {''', '''// a face on screen, so the face's step along an axis is the difference to a neighbour on the same face: on the side
// where this pixel and the next two lie on one line (the other side crosses an edge of the face; at a corner where two
// faces meet the difference across the edge can be the smaller one, so the smaller difference alone is not enough).
// A face too narrow for that gets the smaller difference.
float faceStep(float d, float a1, float a2, float b1, float b2) {
    const float bendA = abs(a2 - 2.0 * a1 + d), bendB = abs(b2 - 2.0 * b1 + d);
    const float stepA = abs(a1 - d), stepB = abs(b1 - d);
    if (min(bendA, bendB) > 4e-7 + 0.1 * min(stepA, stepB)) return min(stepA, stepB);
    return bendA < bendB ? stepA : bendB < bendA ? stepB : min(stepA, stepB);
}
float ps_scenez(V i) : SV_TARGET {''')
rep('''        const float stepX = min(abs(r - d), abs(d - l)), stepY = min(abs(b - d), abs(d - u));
        d -= 1.5 * max(stepX, stepY);''', '''        const float stepX = faceStep(d, l, depthAt(q + int2(-2, 0)), r, depthAt(q + int2(2, 0)));
        const float stepY = faceStep(d, u, depthAt(q + int2(0, -2)), b, depthAt(q + int2(0, 2)));
        d -= 1.5 * max(stepX, stepY);''')
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
