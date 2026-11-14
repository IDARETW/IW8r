import io

# Research 119: one of Replay's per-collection LOD functions was read too (Hex-Rays, 0x1418D71A0).

p = 'E:/IW8-Radiant/docs/research/119_lod_distance_rule.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:60]
    s = s.replace(a, b)


rep('''split-screen factor (the game-test build doubles the value in split screen). The functions that use the two values
per collection were read in GT only.''', '''split-screen factor (the game-test build doubles the value in split screen).

Replay `0x1418D71A0` (one of the seven functions that read the scale table, which Replay holds at `0x142578690` with
the same 16 values): the clutter test `flags & 0x20`, `instanceCount > 1 || clutter` for the many-instance path, the
distance `sqrt(sum(max(|eye - mid| - half, 0)^2))` to the collection's bounds (and `|eye - mid| + half` for the far
end of a many-instance collection), the call that returns the level, and `level == 6` returning nothing to draw. So
the box distance, the two flags and "6 = not drawn" are VERIFIED in Replay. The inside of the level function
(`dist x invFovScale` against the LOD distances, `dist x cappedLodScale / lodCullScale` against the last one) was read
in GT only.''')
rep('''- The per-collection functions (`R_GetStaticModelCollectionLODAndDists` and its callers) in Replay: read in GT only.''',
    '''- The inside of `R_GetStaticModelCollectionLODAndDists` in Replay (how the two scaled distances are compared): read
  in GT only.''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
