import io
import sys

# Docs for the placement's own LOD scale and the game's distance cull of every placed model (plan 1.13, research 119,
# 2026-11-14). Usage: docs_smodel_lod_scale.py "<Verdansk sentence>"

ROOT = 'E:/IW8-Radiant/'
VERDANSK = sys.argv[1]


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Stock maps: props disappear at the distance the game drops them.** The game stops drawing a placed model past
  its last level-of-detail distance, times a scale the level designer set on that one placement (0.25 to 4), and it
  measures to the nearest point of the model's box, not to its centre. The viewport dropped only grass and foliage,
  measured to the centre, and did not know the scale. It now reads the scale from the map (Hideout: 385 of 12,367
  placements have their own, mostly 2 to 4 on walls, drain pipes and manhole covers so they stay visible) and applies
  the game's rule to every placed model. Seen from inside Hideout the picture changes by 0.001 % (small far props);
  the nearer box distance moves 1.2 % of the picture to a more detailed model. From 6,000 units outside, 0.47 % of
  the picture changes (small props in the map's middle are gone, as the rule says) and the frame goes from 9.2 to
  8.3 ms. ''' + VERDANSK + ''' Read in the game-test source (research 119), not compared with the running game. The
  map's scripted objects (doors, lamps) are never dropped by distance.

''')
patch('docs/PROGRESS.md', '''  Plan 1.13: lodFov in SceneRenderer::cullTexInstances (research 119; IW8R_NO_LOD_FOV turns it off). The
  placement's own lodScale (instanceFlags & 0xF) is not in the stock placement data: taken as 1.''',
      '''  Plan 1.13: lodFov in SceneRenderer::cullTexInstances (research 119; IW8R_NO_LOD_FOV turns it off). The
  placement's own lodScale: StockModelInstance::lodCullScale (smodels.instanceFlags & 0xF, only for a collection
  of one instance without the clutter flag 0x20) -> InstancedTexMesh::cullScale (0 = never, the scripted objects).
  Every stock prop has lodOut = its last LOD distance now. The rule is core smodel_lod.h (box distance, cull);
  IW8R_LOD_CENTRE_DIST = the old centre distances, IW8R_NO_FOLIAGE_LOD_OUT = no distance cull (bench switches).''')
P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
a = '(research 119; the placement\'s own lodScale is taken as 1).'
assert s.count(a) == 1
s = s.replace(a, '(research 119). The placement\'s own lodScale and the game\'s cull of every placed model past its last LOD distance DONE 2026-11-14.')
start = s.index('| 1.13 the placement\'s own lodScale on the cull distance |')
end = s.index(nl, start) + len(nl)
s = s[:start] + s[end:]
write(P, s)

R = 'docs/research/119_lod_distance_rule.md'
patch(R, '''- `XModelGetDesiredLodForDistInternal` itself is not in the decompile (inlined); that it compares against the
  model's per-LOD distances is INFERRED from its arguments and from research 100's LOD tables.''',
      '''- `XModelGetDesiredLodForDistInternal` itself is not in the decompile (inlined); that it compares against the
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
  straps, column caps) and 239 of 1,147 foliage ones.''')
patch(R, '''- a LOD level is drawn where `dist * lodFov` lies in its range; the last level ends, and foliage without a group is
  culled, where `dist * min(lodFov, 1)` passes the cull distance;
- the placement's `lodScale` is taken as 1 (the editor's stock placement data does not carry the instance flags);
- `IW8R_NO_LOD_FOV` turns the factor off (a bench switch).''',
      '''- `dist` is the game's: to the nearest point of the placement's world box (the model's box through the placement's
  transform; core `smodel_lod.h`);
- a LOD level is drawn where `dist * lodFov` lies in its range;
- every stock prop is left out where `dist * min(lodFov, 1) / lodScale` reaches the model's last LOD distance;
  `lodScale` is read from `smodels.instanceFlags` by `readStockModelInstances` (`StockModelInstance::lodCullScale`),
  with the game's rule for collections of several instances and clutter (1);
- the map's scripted objects (doors, lamps; not static models) are never left out by distance;
- bench switches: `IW8R_NO_LOD_FOV` (factor off), `IW8R_LOD_CENTRE_DIST` (the distances of before: to the model's
  centre, scale 1), `IW8R_NO_FOLIAGE_LOD_OUT` (no distance cull).''')
patch(R, '''## 3. Not determined

- Whether Replay's constants equal the game-test build's (the 2.1186731 and the split-screen 2).
- The per-placement `lodScale` in the viewport (needs `smodels.instanceFlags` in the world export).''',
      '''Hideout with the placement scale and the cull of every prop: from the bench camera inside the map 0.001 % of the
pixels differ from "no distance cull" and 1.2 % from the centre distances (more detail on long objects); from 6,000
units outside 0.47 % differ and the frame goes from 9.2 to 8.3 ms. ''' + VERDANSK + '''

## 3. Not determined

- Whether Replay's constants equal the game-test build's (the 2.1186731 and the split-screen 2).
- How a collection of several instances picks each instance's level between the nearest and the farthest (the
  viewport measures every placement on its own).
- The clutter ramp (`r_st_clutterLodScale`, `r_st_clutterLodBias`): the viewport's ground clutter uses the plain rule.
- Sun shadows of left-out props: the viewport still draws them into the shadow map (`shadowCutoffLod` is not read).''')
patch('docs/TEST_CHECKLIST.md', '''## In game: your map's model light (2026-11-13), not seen in game''',
      '''## In game (optional, 2026-11-14): props at a distance
- [ ] On Hideout, stand at one end of the map and look along it in the game and in the editor (View > Game Lighting,
  the same place and view). Small props far away (conduit straps, bottles, small debris) should be missing in both,
  and the walls, pipes and manhole covers should be there in both. If the editor shows less than the game, say which
  objects.

## In game: your map's model light (2026-11-13), not seen in game''')
print('ok')
