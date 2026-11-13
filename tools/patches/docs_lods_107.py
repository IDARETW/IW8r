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

- **Stock props use their level-of-detail models, as in game.** Far props switch to the game's lower-detail models at
  the game's own distances, fetched in the background the first time a map is opened. Aniyah Tac went from 23.5 to
  10.3 ms a frame (30 to 6.8 million triangles), Speedball from 8 to 6 ms.
- **Lamps, signs and screens glow at their real brightness.** Stock emissive surfaces now use their material's
  brightness constant (research 107): a wall lantern or a care-package light is as bright as in game instead of a dim
  texture.
- **Grass, leaves, tarps and flags let light through.** Back-lit foliage and cloth glow with the sun behind them, and
  pick up the baked light from their far side (research 107 transmission). Wrap-lit materials (rocks, snow, some
  ground) soften their shading past the shadow line as in game.
- **Stock material constants read correctly.** Some materials (mostly foliage and sandbags) had their gloss and
  specular settings read from the wrong buffer or row; the material cache now reads the lit technique's own constants.
  Materials cached before are read again once, in the background.
- **The editor no longer closes when Verdansk runs out of memory while redrawing props**: it keeps the props as they
  are and says so in the status bar.
''')
edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- M1.13 prop LODs DONE (2026-11-13): ModelCache::requestLods (48 xmodelsurfs per ACTS run, by zone; lodsPending()),
  RefPropMesh::lods (cached LODs, game distances: LOD i up to lods[i].distance, a missing level's range stays with
  the one before; the last runs to the foliage LOD-out, else no limit), InstancedTexMesh lodGroup / lodNear / lodFar,
  renderer texLodGroups_: one cull decision per placement and group (group bounding sphere), each LOD mesh keeps the
  placements in its window; sun shadow and unculled draws use LOD0 only. lodReady drops the kept mesh and rebuilds the
  props once no LOD fetch is pending. IW8R_NO_PROP_LODS. stockCacheIdle (bench) also waits for LOD fetches. No FOV /
  lodScale factor yet (raw distance). Aniyah 23.5 -> 10.3 ms (6.8 M triangles), Speedball 8.1 -> 6.1 ms.
- Research 107 terms (2026-11-13): MaterialCache picks the lit buffer (constantBufferIndex[34]), reads constants by
  index (literal floats, {"bits"} for non-finite words), rewrites single-layer psData to canonical rows (c72, c0, c9,
  c54; 48 bytes without c54), keeps consts 18 / 36 / 103 / 104 and the slot-68 image (constRules 1: older entries
  resolve again). StockMaterialFacts: emissiveFile / transmissionFile / emissiveScale / transTint / wrap.
  CameraView::applyMaterialExtras -> SceneRenderer::setReferenceBaseExtra / setReferenceInstExtra (16 floats after
  the params in each 256-byte b7 slot: matE, matTr, matW; images t3 and t19 via "\\x06<path>" textures). ps_tex
  stockRef: emissive x c36 x (1 - max Fenv); transmission (T = slot 68 x c18; ambient T (1 - max Fenv) (1 - 10^-a2)
  E(-N) aoMB with E(-N) from the lightmap or the prop's grid (vs_tex_inst lgvBack); sun lobe through the blade); wrap
  diffuse on the lightmap's directional part and the sun (per-channel shadow power). Not yet: wrap on local lights,
  the transmission lobe for local lights, layer modes 2/3 rule (research 107 section 3), AO strength from the vision.
  Found while doing it: the old `lgv` PSIn interpolant had never been removed (the patch helper skips an edit whose
  replacement text already exists; ` };` did); now lgvBack.
- Verdansk crash (2026-11-13): std::bad_alloc in rebuildReferenceProps (the debounced rebuild after tile light grids)
  at ~16 GB commit on this 15.8 GB machine. rebuildReferenceProps now catches it (props stay, status message); the
  crash handler also records abort() (SIGABRT -> structured exception -> dump + stack). Open: Verdansk's memory use.
''')
edit('docs/PLAN_REMAINING.md', '''**1.13 Stock prop LODs (performance; found 2026-11-13).**''', '''**1.13 Stock prop LODs (performance; found 2026-11-13).** DONE 2026-11-13 (Aniyah 10.3 ms, Speedball 6.1 ms).
Open: the game's FOV / lodScale factor on the distances.''')
edit('docs/PLAN_REMAINING.md', '''- **Material and occlusion:**''', '''- **Research 107 (2026-11-13) DONE:** lit-buffer constants, emissive x c36, transmission (ambient + sun), wrap diffuse
  (lightmap + sun), probe rough fallback. Open: wrap and transmission for local lights, layer modes 2/3, AO strength.
- **Material and occlusion:**''')
print('ok')
