import io

# Docs for View > Game Lighting Options > Sun Shadow Detail (plan 1.10, the last open item).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **View > Game Lighting Options > Sun Shadow Detail.** The sun's shadow maps (the lit preview and Game Lighting) can
  be Low (1024 texels), Normal (2048, as before) or High (4096). High draws thin shadows such as cables and railings
  sharply; it costs 128 MB of video memory instead of 32 and no measurable frame time on Cinder Rail. The choice is
  saved.

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Plan 1.10 closed: Sun Shadow Detail (SceneRenderer::setSunShadowMapSize 1024 / 2048 / 4096, shadowSize_ replaces
  kShadowSize, shadowPcf reads the map's size with GetDimensions; CameraView::setSunShadowMapSize; command
  GameSunShadowDetail, setting game/sunShadowSize, bench override IW8R_SUN_SHADOW_SIZE). Cinder Rail A/B:
  out/agent/sh_cmp.png (2.05 to 2.12 ms at all three sizes). GUI check in "game lighting options".
''')
patch('docs/PLAN_REMAINING.md', '''compile panel's bake settings DONE 2026-11-13 (Compile > Bake Settings, core bake_options). Open: shadow quality.
''', '''compile panel's bake settings DONE 2026-11-13 (Compile > Bake Settings, core bake_options). Shadow quality DONE
2026-11-13 (Sun Shadow Detail: 1024 / 2048 / 4096). Nothing open in 1.10.
''')
s = io.open(ROOT + 'docs/USER_GUIDE.md', encoding='utf-8', newline='').read()
a = '**View > Game Lighting Options** turns its parts on and off and remembers them:'
assert s.count(a) == 1
i = s.index(a)
nl = '\r\n' if '\r\n' in s else '\n'
# The paragraph ends at the next blank line; the new sentence goes at its end.
j = s.index(nl + nl, i)
s = s[:j] + ' **Sun Shadow Detail** sets the size of the sun\'s shadow maps: Low, Normal or High (High draws thin' + nl + \
    'shadows such as cables sharply and uses 128 MB of video memory).' + s[j:]
io.open(ROOT + 'docs/USER_GUIDE.md', 'w', encoding='utf-8', newline='').write(s)
print('ok')
