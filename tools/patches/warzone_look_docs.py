import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('docs/HANDOVER.md', [
 ('''- Game Lighting on Warzone maps: in progress at handover time (their lightmaps are streamed); see `docs\\PROGRESS.md`.
''', ''''''),
 ('''own baked lightmap, reflection probes, sun, auto exposure and the stock world shader's specular. Measured cost: about''',
  '''own baked lightmap, reflection probes, sun, auto exposure and the stock world shader's specular; on Warzone maps the
lightmaps of the streamed tiles around the camera (Verdansk, Quarry, the tutorial map). Measured cost: about'''),
])
patch('CHANGELOG.md', [
 ('''- **Stock reflections and specular in Game Lighting.**''', '''- **Game Lighting on Warzone maps.** Verdansk, Quarry and the tutorial map draw with their own lightmaps: the
  editor reads the lightmaps of the streamed tiles it loads around the camera (one tile at a time, the scratch deleted
  at once) and packs them into one atlas. Verdansk uses Quarry's sun and exposure, as the game does.
- **Stock reflections and specular in Game Lighting.**'''),
])
patch('docs/PROGRESS.md', [
 ('''  Local tile editing needs the Umbra portal encoding decoded (not done).''', '''  Local tile editing needs the Umbra portal encoding decoded (not done).
- Then Warzone lightmaps: type 0 (BC6H C), one lightmap per transient zone (research 94 section 8); stock_lightmap
  reads many, stock_world keeps lightmapIndex per vertex (and fixed: Verdansk surfData as hex bytes, vertex sharing
  across tiles); WorldCache lightmapFile(zone, index) / lightmapIndices, tile jobs after streamed exports;
  game_lightmap stockLightmapAtlas packs them; reference vertices carry index + u; render.cpp LightmapRects (b6).
  Checked: mp_br_tut (1 lightmap, 125,825 vertices), mp_donetsk with 12 tiles (13 lightmaps, all 391,888 lightmapped
  vertices have their file; atlas 2048 x 1282; 0.44 ms per frame). mp_donetsk uses the mp_br_quarry look.'''),
])
print('ok')
