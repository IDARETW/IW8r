import io
p = 'src/viewport/render.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:80]
    s = s.replace(a, b)


rep('''        if (dot(i.n, i.n) <= 0.5 && dot(Nf, eyeStart.xyz - i.w) < 0) Nf = -Nf;''',
    '''        // A face seen from behind is lit with its normal turned toward the viewer: the stock two-sided foliage shaders
        // negate the tangent frame on back faces (research 100), and back-lit grass cards were dark speckles.
        if (dot(Nf, eyeStart.xyz - i.w) < 0) Nf = -Nf;''')
rep('''    if (batchFlags.y < 0.5) clip(t.a - 0.3);''',
    '''    // Cut-outs: the stock alpha-tested shaders keep opacity above 128 / 255 (research 100); the editor's own materials
    // keep the editor's 0.3.
    if (batchFlags.y < 0.5) clip(t.a - (game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3));''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
