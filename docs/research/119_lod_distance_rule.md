# 119. The game's LOD distance rule for placed models (field of view, lodScale)

**Why.** PLAN_REMAINING 1.13 left one point open: the viewport picked a stock prop's LOD by the plain distance to the
camera. The game scales that distance by the view's field of view first. This note reads the rule and says what the
editor does with it.

**Labels.** INFERRED = read in the IW8 game-test decompile ("GT",
`game-test-src-cleaned\code_source\src\`), which is not Replay; the arithmetic is as written there, and Replay was
not checked. Nothing was run in the game.

## 1. The rule (INFERRED, GT)

`R_SetLodParms` (`gfx_d3d\r_scene.cpp`, around lines 14600-14660):

```
fovScale    = view.tanHalfFovY * 2.1186731          // x lodOverride (1), / a memory-pressure modifier (1 when memory is fine)
invFovScale = (split screen ? 2 : 1) * fovScale
ramp.scale  = invFovScale * (dynResScale + r_lodScale)   // r_lodScale: dvar, default 1, range 0.5 .. 2
ramp.bias   = invFovScale * r_lodBias                    // default 0
cappedLodScale = min(invFovScale, 1)
```

- `2.1186731 = 1 / tan(25.27 deg)`: the factor is 1 at a vertical field of view of 50.5 degrees, which is the game's
  own 80 degree view at 16:9. A wider view makes the factor larger (objects are smaller on screen, so lower LODs
  come sooner); zooming in makes it smaller.

`R_GetStaticModelCollectionLODAndDists` (`gfx_d3d\r_add_staticmodel.cpp:3908-3936`):

```
lod = XModelGetDesiredLodForDist(model, dist * invFovScale, dist * cappedLodScale / lodCullScale)
```

- The level is picked by `dist * invFovScale` against the model's LOD distances.
- The second argument is the distance for the cull past the last level: `dist * min(invFovScale, 1) / lodCullScale`.
  So a wide view never removes a model sooner than its stored cull distance.
- `lodCullScale` is the placement's own `lodScale` (`GfxStaticModelLodDistIndexToScale(instanceFlags & 0xF)`, the 16
  values of research 54). It moves only the cull distance, not the levels.
- `XModelGetDesiredLodForDistInternal` itself is not in the decompile (inlined); that it compares against the
  model's per-LOD distances is INFERRED from its arguments and from research 100's LOD tables.

## 2. What the editor does (2026-11-14)

`SceneRenderer::cullTexInstances` (`src/viewport/render.cpp`):

- `lodFov = 2.1186731 / |second column of the view-projection|` (that column holds the up axis times
  `1 / tanHalfFovY`), kept inside 0.05 .. 8;
- a LOD level is drawn where `dist * lodFov` lies in its range; the last level ends, and foliage without a group is
  culled, where `dist * min(lodFov, 1)` passes the cull distance;
- the placement's `lodScale` is taken as 1 (the editor's stock placement data does not carry the instance flags);
- `IW8R_NO_LOD_FOV` turns the factor off (a bench switch).

Hideout, bench camera `ent node_pathnode 30`: 1.3 % of the pixels differ by more than 8 levels against the plain
distance, the frame goes from 8.31 to 8.06 ms.

## 3. Not determined

- Whether Replay's constants equal the game-test build's (the 2.1186731 and the split-screen 2).
- The per-placement `lodScale` in the viewport (needs `smodels.instanceFlags` in the world export).
