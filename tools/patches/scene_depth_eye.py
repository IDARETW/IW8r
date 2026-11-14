import io

# The eye position as a custom surface shader input (with pixel / viewDepth / iw8rSceneDepth a shader rebuilds the
# world position of what is drawn at a neighbouring pixel: outlines and creases from the scene depth).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/custom_shader.h', '''    float  viewDepth;  // this pixel's depth along the view, world units (compare with iw8rSceneDepth)
''', '''    float  viewDepth;  // this pixel's depth along the view, world units (compare with iw8rSceneDepth)
    float3 eye;        // the camera's position, world space
''')
patch('src/core/custom_shader.h', '''// built map from the game's scene depth. A shader finds outlines with it by comparing a pixel's neighbours
// (data/shaders/toon.iw8shader.hlsl); a value at `s.pixel` that is not near `s.viewDepth` means the host has no
// usable depth right now.''', '''// built map from the game's scene depth. A shader finds outlines with it by comparing a pixel's neighbours
// (data/shaders/toon.iw8shader.hlsl); a value at `s.pixel` that is not near `s.viewDepth` means the host has no
// usable depth right now. The world position of what is drawn at a pixel `o` pixels away is
//   s.eye + (ray + o.x * ddx(ray) + o.y * ddy(ray)) * iw8rSceneDepth(s.pixel + o),  ray = (s.position - s.eye) / s.viewDepth
// (the ray per unit of view depth changes linearly across the screen).''')
patch('src/viewport/render.cpp', '''            s.viewDepth = mul(float4(i.w, 1.0), viewProj).w;
''', '''            s.viewDepth = mul(float4(i.w, 1.0), viewProj).w;
            s.eye = eyeStart.xyz;
''')
print('ok')
