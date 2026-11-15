import io

# Research 132 G11: one-sided own materials (not twoSided) are culled from behind in game (the build culls them;
# compile_iw8.cpp writes cull none only for twoSided). Game Lighting now hides their back faces: batchFlags2.z = -1
# marks an own one-sided batch (0 = own two-sided, 1 = the stock reference), and the pixel shader discards a fragment
# whose surface normal faces away from the eye (the stored normal, so the triangle winding does not matter).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


R = 'src/viewport/render.cpp'
patch(R, '''                const float flags[8] = {hasNormal ? 1.0f : 0.0f, b.blend == 0 && textureRawAlpha(b.texture) ? -2.0f : float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ,
                                        hasEmissive ? 1.0f : 0.0f, probeSrv_ ? 1.0f : 0.0f, 0, hasSpec ? 1.0f : 0.0f};''',
      '''                // flags[6] (batchFlags2.z): -1 = an own one-sided material in Game Lighting (back faces hidden, G11).
                const float flags[8] = {hasNormal ? 1.0f : 0.0f, b.blend == 0 && textureRawAlpha(b.texture) ? -2.0f : float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ,
                                        hasEmissive ? 1.0f : 0.0f, probeSrv_ ? 1.0f : 0.0f, tm && tm->cullBack ? -1.0f : 0.0f, hasSpec ? 1.0f : 0.0f};''')
patch(R, '''float4 ps_tex(PSIn i) : SV_TARGET {
    // UV animation''', '''float4 ps_tex(PSIn i) : SV_TARGET {
    // Research 132 G11: an own one-sided material is not drawn from behind (the build culls its back faces).
    if (batchFlags2.z < -0.5 && dot(i.n, i.n) > 1e-6 && dot(i.n, eyeStart.xyz - i.w) < 0.0) discard;
    // UV animation''')
patch('src/viewport/render.h', '''        bool cullBack = false;  // one-sided (not twoSided): back faces are culled''',
      '''        bool cullBack = false;  // one-sided (not twoSided): back faces are hidden in Game Lighting (ps_tex, batchFlags2.z = -1)''')
print('ok')
