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
rep('''    // The plane's depth is counted from the depth the host has at this pixel, not from the surface's own: a host
    // whose depth is a frame old (the editor while the camera moves) then still compares like with like.
    const float invHere = 1.0 / here;
''', '''    // The plane's depth is counted from the surface's own depth where the host's depth agrees with it as far as it
    // can (it is only as exact as iw8rSceneDepthStep), else from the host's: a host whose depth is a frame old (the
    // editor while the camera moves) then still compares like with like.
    const float invHere = abs(here * invMine - 1.0) <= iw8rSceneDepthStep ? invMine : 1.0 / here;
    // A depth read can be off by half a step: that much of a sample's rise is taken as nothing (with a margin).
    const float blur = 0.6 * iw8rSceneDepthStep / pixelSize;
''')
rep('''            if (depth > 1e-3 && planeInv > 1e-9)
                rise[n] = (planeInv * min(depth, 1e8) - 1.0) / (pixelSize * unit * float(n + 1)) * level;
        }''', '''            if (depth > 1e-3 && planeInv > 1e-9) {
                const float off = planeInv * min(depth, 1e8) - 1.0;
                rise[n] = sign(off) * max(abs(off) / pixelSize - blur, 0.0) / (unit * float(n + 1)) * level;
            }
        }''')
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
io.open(ROOT + 'out/agent/mp_cinderrail_toon/shaders/toon.iw8shader.hlsl', 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
