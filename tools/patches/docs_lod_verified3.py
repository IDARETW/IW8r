import io

# Research 119: Replay's level function was read too (Hex-Rays, 0x1414E9E20 and 0x1418D6C80): the whole rule is
# verified in the Replay exe now.

p = 'E:/IW8-Radiant/docs/research/119_lod_distance_rule.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:60]
    s = s.replace(a, b)


rep('''the box distance, the two flags and "6 = not drawn" are VERIFIED in Replay. The inside of the level function
(`dist x invFovScale` against the LOD distances, `dist x cappedLodScale / lodCullScale` against the last one) was read
in GT only.''', '''the box distance, the two flags and "6 = not drawn" are VERIFIED in Replay.

Replay `0x1418D6C80` (the collection's level and distances) multiplies the distance by the factor and calls the
model's level function, Replay `0x1414E9E20`. That one walks the model's LODs (`numLods` at model + 10, each LOD's
distance at model + 240 + 64 x i): a level before the last is taken when the first distance is below its own LOD
distance; the last level is taken while the second distance (the cull distance) is below the last LOD distance, and
past it the answer is 6. So the whole rule of this note is VERIFIED in Replay: levels by `dist x invFovScale`, the
end by `dist x min(invFovScale, 1) / lodCullScale` against the last LOD distance.''')
rep('''- The inside of `R_GetStaticModelCollectionLODAndDists` in Replay (how the two scaled distances are compared): read
  in GT only.''', '''- The debug overrides inside the level function (test LODs, a minimum level while streaming) are not modelled.''')
rep('''## 1. The rule (the factor VERIFIED in Replay; the per-model use INFERRED from GT)''',
    '''## 1. The rule (VERIFIED in Replay; first read in GT)''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
