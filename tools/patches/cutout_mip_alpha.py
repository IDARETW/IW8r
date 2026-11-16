import io

# Alpha-tested surfaces vanishing at a distance (owner re-check 2026-11-15: "objects still vanish, like the palm
# trees, in hideout"; the instance culling drops no palm: benches with IW8R_POP_LOG show no pops of them): a
# texture's smaller mips average a thin frond's alpha below the cut-out threshold, so the fronds fade out while the
# trunk stays. The cut now scales alpha up with the mip level (Ben Golus, "Anti-aliased Alpha Test": a x (1 + mip x
# 0.25)), in the shading pass, the depth pre-pass and the sun shadow pass alike, so their depths still match.

P = 'E:/IW8-Radiant/src/viewport/render.cpp'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')


def sub(a, b):
    global s
    if b in s and a not in s:
        return
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)


sub('''Texture2D tex0 : register(t0);
Texture2D nrm0 : register(t1);
SamplerState samp0 : register(s0);
''', '''Texture2D tex0 : register(t0);
Texture2D nrm0 : register(t1);
SamplerState samp0 : register(s0);
// A cut-out's alpha for the cut-out test: scaled up with the mip level, so a thin frond's coverage holds at a distance
// (the smaller mips average its alpha below the threshold: palm trees faded out; Golus, "Anti-aliased Alpha Test").
// The shading pass, the depth pre-pass and the shadow pass use it alike.
float cutAlpha(float a, float2 uv) { return a * (1.0 + max(0.0, tex0.CalculateLevelOfDetail(samp0, uv)) * 0.25); }
''')
sub('''void ps_shadow_clip(PSIn i) {
    if (!rawSpecAlpha()) clip(tex0.Sample(samp0, i.uv).a - cutoutAt());
}''', '''void ps_shadow_clip(PSIn i) {
    if (!rawSpecAlpha()) clip(cutAlpha(tex0.Sample(samp0, i.uv).a, i.uv) - cutoutAt());
}''')
sub('''    if (!rawSpecAlpha()) clip(tex0.Sample(samp0, i.uv).a - cutoutAt());
    if (gameTerrainCut.w > 0.5''', '''    if (!rawSpecAlpha()) clip(cutAlpha(tex0.Sample(samp0, i.uv).a, i.uv) - cutoutAt());
    if (gameTerrainCut.w > 0.5''')
sub('''        const float cutAt = cutoutAt();
        if (batchFlags.y < -0.5) {''', '''        const float cutAt = cutoutAt();
        const float ta = cutAlpha(t.a, uvS);
        if (batchFlags.y < -0.5) {''')
sub('''            const float cov = saturate((t.a - cutAt) / max(fwidth(t.a), 1e-4) + 0.5);''',
    '''            const float cov = saturate((ta - cutAt) / max(fwidth(ta), 1e-4) + 0.5);''')
sub('''        } else {
            clip(t.a - cutAt);
        }''', '''        } else {
            clip(ta - cutAt);
        }''')
io.open(P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
