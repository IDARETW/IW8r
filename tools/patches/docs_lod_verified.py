import io

# Research 119: Replay's own LOD parameter function was read (Hex-Rays, 0x14197F880) and agrees with the game-test
# source, without the split-screen factor.

p = 'E:/IW8-Radiant/docs/research/119_lod_distance_rule.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:60]
    s = s.replace(a, b)


rep('''which is not Replay; the arithmetic is as written there, and Replay was
not checked. Nothing was run in the game.''', '''which is not Replay. VERIFIED = read in the Replay 1.20 exe: its copy of
`R_SetLodParms` is at `0x14197F880` (found by the constant 2.1186731, which the exe holds once, at `0x1424CE830`).
Nothing was run in the game.''')
rep('''## 1. The rule (INFERRED, GT)
''', '''## 1. The rule (the factor VERIFIED in Replay; the per-model use INFERRED from GT)

Replay `0x14197F880`: `tanHalfFovY x 2.118673 x lodOverride`, divided by the memory-pressure modifier, is stored as
`invFovScale` (+64); `min(invFovScale, 1)` as `cappedLodScale` (+68); the ramp's scale and bias are that factor times
`(dynResScale + r_lodScale)` and `r_lodBias`; `allowLODOutStatic` is `r_lodOutStaticRamp`. Replay's function has no
split-screen factor (the game-test build doubles the value in split screen). The functions that use the two values
per collection were read in GT only.
''')
rep('''- Whether Replay's constants equal the game-test build's (the 2.1186731 and the split-screen 2).''',
    '''- The per-collection functions (`R_GetStaticModelCollectionLODAndDists` and its callers) in Replay: read in GT only.''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
