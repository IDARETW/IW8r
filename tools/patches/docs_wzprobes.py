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

- **Verdansk and Quarry reflections.** Their reflection probes now load (research 108): the editor reads all 5,620 of
  Verdansk's, takes the shared ones from your install's packages once (about 160 MB in the world cache) and each streamed
  tile's own with the tile, and gives the view the probes around the camera. Wet roads, glass and metal reflect the
  town around them instead of nothing.
''')
edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- M1.5b probes (2026-11-13, research 108; core by a helper: stock_probes v2, 1,259 tests). WorldCache: main job reads
  the streamed records + zone-0 images (readStockProbeTransientZone of <zone>.0 trzone, extractStockProbeZoneImages,
  merge, v2 main file); then requestSharedProbes: ACTS image export of the shared names (batches of 450, XPaks), the
  journal maps "*reflection_probe_octahedron_<i>" to its encoded DDS, readStockProbeDds, written into the main file,
  probes_shared.done. Tile job: the tile's exclusive images (tile trzone, the main file's records) into
  streamed/<tile>/probes.iw8rprb, probes.done ("ok N" / "none" / "error ..."); queueTileLightmap re-runs tiles that
  lack it once the main file exists. index.json probeRules 2 clears the old Warzone probe verdicts. MainWindow:
  loadStockProbes merges the tile files into refProbesFull_; updateProbeSubset (buildStockProbeSubset at the camera,
  128 boxes) on load and every 1,500 units (updateStreamedTiles); probe reloads debounced 3 s.
''')
edit('docs/PLAN_REMAINING.md', '''**1.5b Warzone baked light per tile.**''', '''**1.5b Warzone baked light per tile.** DONE 2026-11-13: tile light grids and the Warzone probes (research 108; the
game's tile activation rule is replaced by the nearest-boxes subset).''')
print('ok')
