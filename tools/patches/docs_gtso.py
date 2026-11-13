import io


def edit(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:
        return
    assert s.count(a) == 1, (p, a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


edit('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **The game's specular occlusion table.** Reflections in corners, creases and under props now fade exactly as the
  game's own table (gtso_lut, read once from your install like the BRDF table) says, from the ambient occlusion, the
  material's cavity, its gloss and the reflection angle. The change is small and mostly on glossy surfaces.
''')
edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- M1.4 gtso_lut (2026-11-13): game_look_assets cachedCodePostImage (shared ACTS export of a code_post_gfx image into
  AppLocalData/game_look), loadGtsoLut + decodeBc4VolumeDds (16x16x16 BC4 -> R8), SceneRenderer::setGtsoLut (Texture3D
  R8 at t18, gameSun.w = 1 when bound). ps_tex: so = gtsoLut(max(sqrt(1-ao), sqrt(1-cavity)), a2, sat(Nf . R)) with the
  lut sampler (raw coordinates, as W:796), else min(ao, cavity). GUI test: table read + a one-block decode. Speedball
  8.1 ms, 4 % of pixels change slightly (snowy tree tops, roofs).
''')
edit('docs/PLAN_REMAINING.md', '''  - read `gtso_lut` from the install (cached, like the env BRDF) and use the game's specular occlusion;''',
     '''  - read `gtso_lut` from the install (cached, like the env BRDF) and use the game's specular occlusion (DONE);''')
print('ok')
