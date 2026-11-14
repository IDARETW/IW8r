import io

# x64-zt converter: the map surface shader runs outside flow control (it may use screen derivatives, e.g. for lines of a
# constant pixel width); its colour is taken where the surface is lit.

H = 'C:/zsw-iw8r/scriptables/src/convert/iw3_iw8/resources/map_surface_realtime.hlsl'
s = io.open(H, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '    if (!unlit && !sky && !layer) {' + nl + '        IW8RSurface iw8rSurfaceIn;'
assert s.count(a) == 1
s = s.replace(a, '    {' + nl + '        IW8RSurface iw8rSurfaceIn;')
b = '        color = iw8rSurface(iw8rSurfaceIn);' + nl + '        iw8rCustomLit = true;' + nl + '    }'
assert s.count(b) == 1
s = s.replace(b, '        const float3 iw8rColor = iw8rSurface(iw8rSurfaceIn);' + nl + '        if (!unlit && !sky && !layer) {' + nl +
              '            color = iw8rColor;' + nl + '            iw8rCustomLit = true;' + nl + '        }' + nl + '    }')
io.open(H, 'w', encoding='utf-8', newline='').write(s)
print('ok')
