import io
p = 'src/viewport/render.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:80]
    s = s.replace(a, b)


rep('''        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));
        if (gameSky.w > 0.5 && probeCount.x > 0.5)
            indirect = stockProbeLight(i.w, N, 0.0) * dot(gameSky.rgb, float3(0.2126, 0.7152, 0.0722));''',
    '''        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));  // the probes' light, below, where no lightmap''')
rep('''        // The sun: the baked sun depth where it covers the point (here the sun shadow map), else the lightmap's mask.''',
    '''        // Only where no lightmap lit the point: the probes' fallback light (it cost every lightmapped pixel two probe
        // lookups that were thrown away).
        if (!lmOk && gameSky.w > 0.5 && probeCount.x > 0.5)
            indirect = stockProbeLight(i.w, N, 0.0) * dot(gameSky.rgb, float3(0.2126, 0.7152, 0.0722));
        // The sun: the baked sun depth where it covers the point (here the sun shadow map), else the lightmap's mask.''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
