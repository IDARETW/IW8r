import io
# The scene depth custom shaders read has the game's precision in the viewport too: the game keeps it as depth / 6 in a
# 16-bit float (research 104: "Frontend FloatZ"), 11 significant bits. A shader that finds lines from it has to work
# with that, and the viewport has to show what the game will show.
ROOT = 'E:/IW8-Radiant/'
def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))
patch('src/viewport/render.cpp', '''        d -= 1.5 * max(stepX, stepY);
    }
    return mul(float4(worldAt((float2(q) + 0.5) / aoPrm.zw, d), 1.0), aoVp).w;
}''', '''        d -= 1.5 * max(stepX, stepY);
    }
    // Kept with the game's precision (its scene depth is depth / 6 in a 16-bit float, research 104), so that a shader
    // sees in the viewport what it will see in the game.
    const float z = mul(float4(worldAt((float2(q) + 0.5) / aoPrm.zw, d), 1.0), aoVp).w;
    return f16tof32(f32tof16(z / 6.0)) * 6.0;
}''')
patch('src/viewport/render.cpp', '''Texture2D<float> iw8rSceneZ : register(t31);
''', '''Texture2D<float> iw8rSceneZ : register(t31);
static const float iw8rSceneDepthStep = 1.0 / 1024.0;
''')
patch('src/core/custom_shader.h', '''bool customShaderUsesSceneDepth(const std::string& code);''', '''// The depth is as exact as the game keeps it: iw8rSceneDepthStep (a constant every host defines, 1 / 1024) is the
// largest step between two depths it can tell apart, as a share of the depth. A value read can be off by half of it.
bool customShaderUsesSceneDepth(const std::string& code);''')
print('ok')
