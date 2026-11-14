import io

# x64-zt converter: a map surface shader's edge inputs (IW8 Radiant core face_edges). With IW8R_EDGES the world
# surfaces' vertex colour holds the brush faces' edge data: it is not multiplied into the colour, and the hook fills
# faceNormal / edgePixels / edgeUnits / edgeNormal with the editor's helper (iw8rEdgeFields, part of the shader text
# the editor hands over). Models keep their vertex colour and get "no edge".


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'C:/zsw-iw8r/scriptables/src/convert/iw3_iw8/resources/map_surface_realtime.hlsl'
patch(H, '''#else
    texel *= input.color;
#endif
    uint alphaTest = (flags >> 3) & 3;''', '''#elif defined(IW8R_EDGES) && !defined(STATIC_MODEL)
    // IW8 Radiant: the world's vertex colour holds the brush faces' edge data for the map's surface shader.
#else
    texel *= input.color;
#endif
    uint alphaTest = (flags >> 3) & 3;''')
patch(H, '''        iw8rSurfaceIn.gloss = .3;
#endif
''', '''        iw8rSurfaceIn.gloss = .3;
#endif
        iw8rSurfaceIn.faceNormal = normalize(input.normal);
#if defined(IW8R_EDGES) && !defined(STATIC_MODEL)
        iw8rEdgeFields(iw8rSurfaceIn, input.color.rgb, ddx(input.relativePosition), ddy(input.relativePosition));
#else
        iw8rEdgeFields(iw8rSurfaceIn, float3(1, 0, 0), 0, 0);
#endif
''')
print('ok')
