import io

# The scene depth for custom shaders, without the editor's depth offset. Faces are drawn with a slope-scaled depth
# bias (rsTris_: 40 + 1.5 x the face's steepest depth step per pixel) so that editor lines lie in front of them. The
# kept scene depth carried that offset: a face seen nearly edge-on was stored far behind where it is (a face a pixel
# wide even at the far plane, read as sky), and a shader comparing neighbouring depths saw a step at every fold
# between a tilted and an upright face. The game's depth has no such offset; ps_scenez takes it off again.

ROOT = 'E:/IW8-Radiant/'
P = 'src/viewport/render.cpp'
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '''float ps_scenez(V i) : SV_TARGET {
    const int2 q = int2(i.pos.xy);
    const float d = depthTex.Load(int3(q, 0));
    if (d >= 0.999999) return 1e9;
    return mul(float4(worldAt((float2(q) + 0.5) / aoPrm.zw, d), 1.0), aoVp).w;
}'''.replace('\n', nl)
b = '''// The faces were drawn with a depth offset of 1.5 x their steepest depth step per pixel (rsTris_, so that editor
// lines lie in front of them); the game's depth has none, so it is taken off here. The buffer's depth is linear across
// a face on screen, so the face's step along an axis is the difference to a neighbour on the same face: the smaller of
// the two differences (the other one crosses an edge of the face).
float ps_scenez(V i) : SV_TARGET {
    const int2 q = int2(i.pos.xy);
    float d = depthTex.Load(int3(q, 0));
    const float l = depthAt(q + int2(-1, 0)), r = depthAt(q + int2(1, 0));
    const float u = depthAt(q + int2(0, -1)), b = depthAt(q + int2(0, 1));
    if (d >= 0.999999) {
        // A face a pixel wide and nearly edge-on is pushed to the far plane: with a surface on both sides it is
        // taken as the farther of the two, not as a pixel of sky.
        const float acrossX = max(l, r), acrossY = max(u, b);
        d = min(acrossX, acrossY);
        if (d >= 0.999999) return 1e9;
    } else {
        const float stepX = min(abs(r - d), abs(d - l)), stepY = min(abs(b - d), abs(d - u));
        d -= 1.5 * max(stepX, stepY);
    }
    return mul(float4(worldAt((float2(q) + 0.5) / aoPrm.zw, d), 1.0), aoVp).w;
}'''.replace('\n', nl)
assert s.count(a) == 1
s = s.replace(a, b)
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
