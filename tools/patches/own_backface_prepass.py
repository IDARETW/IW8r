import io

# Research 132 G11, part 2: the depth pre-pass (AO, early depth) hides the same back faces, or a hidden one-sided face
# still writes its depth and the surfaces behind it fail the depth test.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


R = 'src/viewport/render.cpp'
patch(R, '''void ps_prepass(PSIn i) {
    if (tileFillHidden(i.col, i.w)) discard;''', '''void ps_prepass(PSIn i) {
    if (tileFillHidden(i.col, i.w)) discard;
    if (batchFlags2.z < -0.5 && dot(i.n, i.n) > 1e-6 && dot(i.n, eyeStart.xyz - i.w) < 0.0) discard;  // G11, as ps_tex''')
patch(R, '''            if (cbBatch_) {  // the same vertex inputs as the shading pass (sway), so the depths match exactly
                const float flags[8] = {0, b.blend == 0 && textureRawAlpha(b.texture) ? -2.0f : float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ, 0, 0, 0, 0};''',
      '''            if (cbBatch_) {  // the same vertex inputs as the shading pass (sway), so the depths match exactly
                // and the same hidden back faces of one-sided own materials (G11: flags[6] = -1).
                const TextureMaterial* ptm = textureMaterial(b.texture);
                const float flags[8] = {0, b.blend == 0 && textureRawAlpha(b.texture) ? -2.0f : float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ,
                                        0, 0, ptm && ptm->cullBack ? -1.0f : 0.0f, 0};''')
print('ok')
