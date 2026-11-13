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

- **Stock maps' own lamps light the scene in Game Lighting.** Every spot and point light of a stock map (124 on Hideout,
  267 on Aniyah Tac) now lights the world and the props as the game draws them at runtime (research 106): their
  falloff, cone, bulb size and tube shape, their IES beam profile (read once from your install) and their highlights
  on glossy surfaces. Rooms under lamps are lit as in game instead of only by the baked light; the game's lightmap does
  not contain this light. The nearest 256 in view are drawn, as the game does. Shadows from these lights come next.
- **Verdansk props lit per tile.** Props in Verdansk's streamed tiles take their light from their own tile's light grid
  (read with the tile's lightmap, in the background), so props in the town get the same treatment as on the 6v6 maps.
''')
edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- M1.6 part 1 (2026-11-13, research 106): core stock_lights (parseStockPrimaryLights, parseStockLightDef,
  packStockLight = Replay's 96-byte t49 record, evalStockLightDiffuse CPU reference; tests against research 106 5.6:
  lights 82 / 118 / 9). MainWindow::loadStockSun also packs the spots / omnis, exports the lightdefs and the *ieslookup
  strip once (world_cache/<zone>/lights, "done"), CameraView::setStockLocalLights. Renderer: nearest 256 in view
  (frustum + distance), StructuredBuffer t20, IES Texture1D t21, CPU-binned 32-px tile masks (8 words) t22, b11
  localInfo; ps_tex stockRef branch loops the tile's bits (gameLocalLight = research 106 5.3, no shadow yet).
  IW8R_NO_STOCK_LIGHTS for A/B; bench line stock_lights. Hideout floor under light 82 (camera "-128 392 268 0 -89
  100"): mean 37 -> 80. Hideout 13.2 ms; Aniyah 23.2 with lights, 23.5 without (lights cost nothing measurable).
- GPU profiler: IW8R_BENCH_PROFILE=1 IW8R_BENCH_GPU=1 adds gpu.<scope> timestamp times to the bench profile lines (a
  stall per frame, bench only). New scopes d.props / d.props_pre. Aniyah per frame: props 10.2 ms (30 M triangles: all
  props at LOD0), pre-pass 4.8, world 2.8, AO 0.7; SSR, probes, cut-out mode, AO and the prop light grid each change the
  props pass by < 0.3 ms. So Aniyah's 23.5 ms (budget 20; 18.1 at M0) is geometry: plan 1.13 (prop LODs).
- M1.5b part 1 (2026-11-13): Warzone tile light grids. WorldCache tile job also cuts the tile's LGV block
  (streamed/<tile>/lightgrid.iw8rlg + lightgrid.done "ok <transientZoneIndex> <volumes>"), re-run for tiles that lack
  it (MainWindow::updateStreamedTiles asks once per tile per session, requestTileLightmap); tileLightGrids(zone);
  MainWindow::loadStockTileLightGrids -> refPropTileLight_ by transient index, props pick their grid by worldTransient
  (debounced rebuild). Verdansk main zone: 17,737 of 255,682 placements from transient 0; raised camera 27.3 ms.
  Research 107 (shading terms) and 108 (Warzone probes) landed.
''')
edit('docs/PLAN_REMAINING.md', '''**1.6 Local lights.**''', '''**1.6 Local lights.** Part 1 DONE 2026-11-13 (research 106: every spot / omni at runtime, nearest 256, IES,
tile-binned). Open: spot shadow maps (research 106 section 4 / 5.4).''')
edit('docs/PLAN_REMAINING.md', '''**1.10 GUI and settings.**''', '''**1.13 Stock prop LODs (performance; found 2026-11-13).** Aniyah draws 30 M triangles a frame because every prop is
LOD0 (GPU profile: props 10.2 of 23.5 ms). The model detail already lists each LOD's surfaces and distance, and
ModelCache::requestLod fetches one LOD of one model per ACTS run. Fetch the LODs in batches, keep one mesh per LOD, and
let the instance cull pick the game's LOD by distance (and FOV). Done when Aniyah is within 20 ms and Verdansk
within 28 ms with no visible popping at the bench cameras.

**1.10 GUI and settings.**''')
print('ok')
