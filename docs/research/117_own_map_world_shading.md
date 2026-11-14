# 117. What the built map's world shader does on an editor map, and what Game Lighting showed instead

**Why.** View > Game Lighting should show the user's own brushes as the built map looks in the game (plan 1.9). The
owner compares the viewport with in-game shots and found the game flatter than the editor. This note reads the
converter's world shader for an editor build and compares it with the viewport's shader for own brushes.

**Answer in one paragraph.** An editor build uses the converter's default style (`iw3`), so the newer lighting
block (`MAP_IW8_LIGHTING`: probe reflections, GGX sun specular, ambient from the probes) is not compiled in. A map
built **without** "Bake lightmaps" gets no lightmap at all: every surface takes a constant two-colour sky fill by its
normal, plus the live sun with its shadow. The viewport always baked a preview lightmap (sky occlusion and bounce)
and showed that, so unbaked maps looked richer in the editor than in the game. Game Lighting now shows the constant
fill for such maps, and the indirect light of own brushes takes the converter's multi-bounce occlusion form.

**Labels.** VERIFIED = the line was read. INFERRED = follows from the lines, not run in game. Converter paths are
under `src/convert/iw3_iw8/` of the x64-zt worktree; `hlsl` is `resources/map_surface_realtime.hlsl`.

## 1. Which variant an editor build compiles (VERIFIED)

- The Compile panel passes no `--style`; the converter's default is `"iw3"` (`convert.cpp:99`).
  `MAP_IW8_LIGHTING` is defined only for the iw8 style (`iw3_vision.cpp:471`).
- A world surface is a "native world" surface only when the map has lightmaps
  (`iw3_render_assets.cpp:6113`: `nativeWorld = !plan.lightmaps.empty() && !atlasWorld`). Without a bake the surfaces
  use the atlas world technique set and their lightmap flag is clear.
- `MAP_UNBAKED_GAIN` and `MAP_INDIRECT_GAIN` are never set by the converter: both are 1 (hlsl:6-18).

## 2. The colour of a world surface (VERIFIED, hlsl)

```
// no lightmap (hlsl:1383-1386)
float hemi = saturate(normalize(input.normal).z * .5 + .5);
float3 indirect = lerp(float3(.22, .23, .24), float3(.38, .41, .44), hemi) * MAP_UNBAKED_GAIN;
// with a lightmap: indirect = the lightmap's ambient and directional parts (SourceIndirect, hlsl:291-314)

float3 bounce = 1 - min(texel.rgb, .85) * (1 - gtao);            // hlsl:1468
indirect *= replayLighting[43].z * gtao / max(bounce, 1e-3);     // hlsl:1573
float3 light = indirect + visibility * (ndotl + transmitted) * sun;   // hlsl:1574
color = texel * light;                                           // then fog, x exposure (cb7[42].x), clamp
```

- `visibility` is the game's screen sun shadow, limited by the baked sun depth where there is one (hlsl:1364-1382).
- The fill uses the vertex normal, not the normal map.
- A surface gets specular light only when its material names a specular map (`SourceSunSpecular` returns 0
  otherwise, hlsl:762-763). A plain brush material has none.
- A custom surface shader (research 114) receives `ambient = indirect` after the occlusion line (hlsl:1620).

## 3. What the viewport did, and does now (`src/viewport/render.cpp`, `ps_tex`)

| | Built map | Viewport before | Viewport now |
|---|---|---|---|
| Map without "Bake lightmaps" | the constant sky fill | the preview bake: sky with occlusion, bounce | the constant sky fill (`gameSky.w = 2`), no preview bake |
| Sun shadow, no bake | screen sun shadow | the bake's sun mask, or the shadow map | the shadow map |
| Occlusion of the indirect light | multi-bounce form | plain `indirect x ao` | multi-bounce form (`aoOwn`) |
| Map with "Bake lightmaps" | its lightmap | the preview bake | unchanged (same bake code, same settings: core bake_options) |

- `CameraView::gameLookUnbaked()`: an own map (not brushes on a stock map) whose worldspawn `iw8r_bake_lightmaps` is
  not 1. `maybeBakeGameLook` then bakes nothing and drops an earlier lightmap; the status line says so once.
- Brushes added to a stock map: section 5.

Checked: Cinder Rail with and without the key at the same camera (`out/agent/unb_cr_cmp.png`): the unbaked view has
no corner darkening and brighter shadowed walls; 87 % of the pixels differ by more than 8 of 255. GUI test "game
lighting": the map without the key draws with no preview lightmap and a different picture.

## 4. Open

- `replayLighting[43].z` (a scale on the indirect light) is taken as 1 in the viewport (INFERRED: the converter's own
  maps; not read from a built zone).
- The exposure of an own map in game (the converter's fixed exposure) against the viewport's: unchanged here.
- Fog on own maps: the converter blends its fog in gamma space when the map has fog; not compared here.
- Materials with a specular map: the converter's sun and probe specular (hlsl:761-781) is not in the viewport.
- Not seen in game.

## 5. Brushes added to a stock map (2026-11-14)

Brushes added to a stock map are not world surfaces: the rebuild carries them as a model (`iw8r_ovl_...`, a script
model). Research 105 section 2.1 found the map's tetrahedral light grid (`gpuLightGrid.gpuLightGrid` of the
`gfx_map_trzone` export), which the static-model shader does not read and which is INFERRED to light dynamic models.

- **Decode (VERIFIED on Shipment's export).** 223,432 probes (32-byte records, positions as three floats) and
  1,330,096 tetrahedra (four probe indices, bit 31 a flag). A probe: the DC as R11G11B10F, then eight coefficients per
  channel as bytes, `n = (byte - 127.5) / 127`, coefficient = `n x DC x sqrt3` (i < 4) or `sqrt5`, read literally. Of
  2,013 probes sampled at their own position, all were found in a tetrahedron and gave their own light back; the mean
  light on an upward face is 1.98 (research 105 measured 0.7 under cover and 3.4 in the open on another map).
- **Sampling.** The tetrahedron holding the point (a uniform cell index over the tetrahedra's boxes), the four probes
  blended by barycentric weights, then the static-model shader's irradiance formula (`evalStockLgv`) for the face's
  normal, times the map's bakedLightScale.
- **Viewport.** The preview bake of such brushes takes this as each texel's ambient light (`BakeScene::ambientAt`);
  the sun and its shadow stay live. Where no tetrahedron holds the texel, the sky bake stays.
- **Check.** A box added in a shaded alley of Shipment: its faces average 25.6 of 255 with the grid and 44.3 with
  the sky bake, beside containers of the same darkness (`out/agent/gg_cmp.png`).
- **Open.** Whether the game lights a script model per pixel or once per model from this grid (INFERRED per point
  here); the probes' visibility byte and the tetrahedron visibility records are not used.
