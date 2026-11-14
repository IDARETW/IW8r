import io

# x64-zt converter: a map surface shader's colour is final for the lit part: the converter's sun / probe specular
# (iw8 style) and the IW3 gamma-space sun specular (iw3 style) are no longer added on top of it. The editor's viewport
# adds none either; in game the extra sheen washed toon surfaces out (owner test 2026-11-13).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'C:/zsw-iw8r/scriptables/src/convert/iw3_iw8/resources/map_surface_realtime.hlsl'
patch(H, '''    float3 color = texel.rgb * light;
#if defined(IW8R_CUSTOM) && !defined(MAP_COVERAGE)''', '''    float3 color = texel.rgb * light;
    // True where the map's surface shader gave the colour (no converter specular is added to it then).
    bool iw8rCustomLit = false;
#if defined(IW8R_CUSTOM) && !defined(MAP_COVERAGE)''')
patch(H, '''        color = iw8rSurface(iw8rSurfaceIn);
    }''', '''        color = iw8rSurface(iw8rSurfaceIn);
        iw8rCustomLit = true;
    }''')
patch(H, '''    if (!iw8Window)
        color += specular;''', '''    if (!iw8Window && !iw8rCustomLit)
        color += specular;''')
patch(H, '''    if (!unlit && !(kind == 2 && !layer && !decalLayer)) {
        int surfaceProbe = fixedProbe;''', '''    if (!unlit && !iw8rCustomLit && !(kind == 2 && !layer && !decalLayer)) {
        int surfaceProbe = fixedProbe;''')
print('ok')
