import io

# x64-zt converter: the map surface shader hook read `gloss`, which only exists under MAP_IW8_LIGHTING
# (fxc X3004 on the toon Cinderrail build). Outside it the hook passes the rough default .3.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'C:/zsw-iw8r/scriptables/src/convert/iw3_iw8/resources/map_surface_realtime.hlsl'
patch(H, '''        iw8rSurfaceIn.gloss = gloss;
''', '''#ifdef MAP_IW8_LIGHTING
        iw8rSurfaceIn.gloss = gloss;
#else
        iw8rSurfaceIn.gloss = .3;
#endif
''')
print('ok')
