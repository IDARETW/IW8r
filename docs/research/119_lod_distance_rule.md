# 119. The game's LOD distance rule for placed models (field of view, lodScale)

**Why.** PLAN_REMAINING 1.13 left one point open: the viewport picked a stock prop's LOD by the plain distance to the
camera. The game scales that distance by the view's field of view first. This note reads the rule and says what the
editor does with it.

**Labels.** INFERRED = read in the IW8 game-test decompile ("GT",
`game-test-src-cleaned\code_source\src\`), which is not Replay. VERIFIED = read in the Replay 1.20 exe: its copy of
`R_SetLodParms` is at `0x14197F880` (found by the constant 2.1186731, which the exe holds once, at `0x1424CE830`).
Nothing was run in the game.

## 1. The rule (the factor VERIFIED in Replay; the per-model use INFERRED from GT)

Replay `0x14197F880`: `tanHalfFovY x 2.118673 x lodOverride`, divided by the memory-pressure modifier, is stored as
`invFovScale` (+64); `min(invFovScale, 1)` as `cappedLodScale` (+68); the ramp's scale and bias are that factor times
`(dynResScale + r_lodScale)` and `r_lodBias`; `allowLODOutStatic` is `r_lodOutStaticRamp`. Replay's function has no
split-screen factor (the game-test build doubles the value in split screen). The functions that use the two values
per collection were read in GT only.

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
- The result 6 means "not drawn": `R_GetStaticModelCollectionLODMaskInternal` returns an empty LOD mask for it
  (`r_add_staticmodel.cpp:4149-4153`). This holds for every static model, not only foliage. The dvar
  `r_lodOutStaticRamp` (default off) only decides whether `r_lodScale` / `r_lodBias` move that distance too.
- `dist` (`R_GetStaticModelInstanceLODDistWithBounds`, `:4327-4376`): from the camera to the nearest point of the
  collection's axis-aligned bounds, `length(max(|eye - mid| - half, 0))`; 0 inside the box.
- `lodCullScale` is the placement's scale only for a collection of one instance without the clutter flag (collection
  flag 0x20): `:4074`, `:4143-4145`, `:4210-4211`, `:4258-4267`. A collection of several instances, or clutter, uses
  1, and takes its levels from the nearest and the farthest point of its bounds.

### 1.1 The data (VERIFIED on the exports)

- `GfxWorld.smodels.instanceFlags`: one byte per instance row, in the ACTS gfx_map export as hex bytes. Only the low
  four bits are used on the maps looked at (the high four are 0).
- `GfxStaticModelCollection` is u32 firstInstance, u32 instanceCount, u16 smodelIndex, u16 transientGfxWorldPlaced,
  u16 clutterIndex, u8 flags, u8 pad (x64-zt `iw8/structs.hpp`; the exports' bytes agree: flags 0x80 / 0x81 / 0x84
  at byte 14).
- Hideout (12,367 placements, every collection holds one instance): 11,982 at index 6 (scale 1), 208 at 13 (3),
  132 at 11 (2), 32 at 15 (4), 10 at 9 (1.5), 3 at 2 (0.5). The raised ones are walls
  (`me_plaster_barrier_fence_wall_256_hideout` x153, last LOD distance 10,000), manhole covers, drain pipes, bricks,
  conduit joints: objects whose absence would show. The three at 0.5 are palm trees. This fits the rule above: the
  designers raise the scale where the model's own last distance would drop a visible object too soon.
- With the rule, a camera in the middle of Hideout drops 831 of 11,043 non-foliage placements (DVD cases, conduit
  straps, column caps) and 239 of 1,147 foliage ones.

## 2. What the editor does (2026-11-14)

`SceneRenderer::cullTexInstances` (`src/viewport/render.cpp`):

- `lodFov = 2.1186731 / |second column of the view-projection|` (that column holds the up axis times
  `1 / tanHalfFovY`), kept inside 0.05 .. 8;
- `dist` is the game's: to the nearest point of the placement's world box (the model's box through the placement's
  transform; core `smodel_lod.h`);
- a LOD level is drawn where `dist * lodFov` lies in its range;
- every stock prop is left out where `dist * min(lodFov, 1) / lodScale` reaches the model's last LOD distance;
  `lodScale` is read from `smodels.instanceFlags` by `readStockModelInstances` (`StockModelInstance::lodCullScale`),
  with the game's rule for collections of several instances and clutter (1);
- the map's scripted objects (doors, lamps; not static models) are never left out by distance;
- bench switches: `IW8R_NO_LOD_FOV` (factor off), `IW8R_LOD_CENTRE_DIST` (the distances of before: to the model's
  centre, scale 1), `IW8R_NO_FOLIAGE_LOD_OUT` (no distance cull).

Hideout, bench camera `ent node_pathnode 30`: 1.3 % of the pixels differ by more than 8 levels against the plain
distance, the frame goes from 8.31 to 8.06 ms.

Hideout with the placement scale and the cull of every prop: from the bench camera inside the map 0.001 % of the
pixels differ from "no distance cull" and 1.2 % from the centre distances (more detail on long objects); from 6,000
units outside 0.47 % differ and the frame goes from 9.2 to 8.3 ms. Verdansk, raised bench camera: 2,684 more of the placements in view are left out than without the distance cull, and the frame is 13.4 to 16.9 ms over two runs (budget 28; Verdansk's picture differs from run to run with the streamed terrain, so its pictures were not compared).

## 3. Not determined

- The per-collection functions (`R_GetStaticModelCollectionLODAndDists` and its callers) in Replay: read in GT only.
- How a collection of several instances picks each instance's level between the nearest and the farthest (the
  viewport measures every placement on its own).
- The clutter ramp (`r_st_clutterLodScale`, `r_st_clutterLodBias`): the viewport's ground clutter uses the plain rule.
- Sun shadows of left-out props: the viewport still draws them into the shadow map (`shadowCutoffLod` is not read).
