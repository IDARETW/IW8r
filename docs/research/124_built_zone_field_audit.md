# 124. A built map's world and light assets against stock, field by field

**Why.** Two faults of built maps were found on 2026-11-14 by measuring a built zone against stock zones: the box of
the occlusion data (research 121) and the level of the reflection probes (research 123). Both had been in every build
and neither showed in a test, because a built zone was only ever checked against what the build meant to write. This
note does that comparison for every field of the two assets that decide what a map looks like, so the next fault of
this kind is not left to a playtest.

**Labels.** VERIFIED = read from the exports or the sources named. INFERRED = follows from verified facts. Nothing was
run in the game.

## 1. The tool

`tools/zone_field_audit.py <built asset .json> <gfx_map | com_map>` compares an ACTS export of a built zone with the
same asset of every stock multiplayer zone in the editor's world cache (11 zones on this machine). It prints a field
when the built zone does what no stock zone does: a value every stock zone agrees on and the built zone differs
from; a count or value that is 0 where stock never is; a pointer that is null where stock never is (or the reverse);
a number further outside the stock range than half that range. A flag is a question, not a fault.

Built zone audited: `samples/dev/iw8r_dev_showcase.map` (4 lamps, probes, script models), built with the converter of
2026-11-14 (`5ecf08a0`).

## 2. The light asset (`com_map`)

33 fields, 2 flags: the change-list number and its time (0 in a built zone). Nothing else differs in kind. VERIFIED.

## 3. The world asset (`gfx_map`)

713 fields, 59 flags, in these groups:

| Group (flags) | What stock has and the built zone lacks | Answer |
|---|---|---|
| `draw` lightmaps (7) | one lightmap set, type 1 or 2 | By design: a map is unbaked unless **Bake lightmaps** is ticked (research 55); the baked path is proven by `tools/lightmap_bake_build_test.py`. |
| `draw` decal volumes (9), `dpvs` decal bits (2) | 7 to 165 decal volume collections | By design: a built map's decals are geometry, not decal volumes. |
| `streamTree` (13) | a tree that says which images to stream in by position | By design: a built map's images are resident in its zone. |
| `smodels` splined models (5) | 1 to 649 splined model instances (cables, hoses) | Not built: the editor has no splined models. |
| `dpvsDyn`, `sceneDyn*`, `dynEntMotionBitsEntries` (8) | dynamic entity tables | Present when the map has dynamic entities (the showcase build had none carried in); the dynamic entity proofs cover them. |
| `dpvsPlanes` (2) | 10 to 463 planes | By design: one visibility cell, no planes. |
| `dpvs` sun shadow tables (3): `sunShadowOptCount`, `sunSurfVisDataCount`, `surfaceCastsSunShadowOpt` | which surfaces cast into which sun shadow slice | An optimisation table. The sun's shadows were seen on built maps in both playtests (ROADMAP F.4). INFERRED harmless. |
| `lightAABB` (2) | a box tree over some lights | See section 4: it lists the lamps that shadow moving objects. The converter writes it when a map has such a lamp; the showcase has none. |
| `voxelTree` (2) | one voxel tree | Known (research 81): with a count of 0 the game skips the heightfield lookup. |
| `gfxMapEntLookup` (2) | 1 to 20 records | Not determined (section 5). |
| `checksum` (1) | a non-zero number | Not determined (section 5). |
| `sun.sunFxPosition` (2), `modelCount` (1) | - | Not a difference in kind: the sun's direction and the number of brush models. |

No flag points at a fault of the kind of research 121 or 123. VERIFIED for the list; the "by design" answers are the
build's own documented choices.

## 4. What `lightAABB` lists (VERIFIED on three stock maps)

`GfxWorld.lightAABB` (a tree of boxes and a list of primary light rows) lists exactly the lamps whose
`needsDynamicShadows` is set in the light asset:

| Zone | Lamps | Listed | Listed lamps with `needsDynamicShadows` | Other lamps with it |
|---|---|---|---|---|
| mp_shipment | 22 | 5 | 5 | 0 |
| mp_rust | 80 | 23 | 23 | 0 |
| mp_hideout | 125 | 37 | 37 | 0 |

All of them are spot lamps with a shadow map. A map without such a lamp has one empty node (mp_m_speedball). The
converter builds the same list from the same rule (`replay_locallights.h`: a spot lamp with `canUseShadowMap` and
dynamic shadows; `EmitLightAABB`), so a built spot lamp with `iw8r_shadows 1` is in it. INFERRED: the game uses the
tree to find the lamps that must shadow a moving object near them.

## 5. Asset types

A stock map's own zone (mp_shipment, `out/m2-x64zt/dump/mp_shipment/mp_shipment.csv`) holds 51 asset types; a built
map's zone holds 24. The types only stock has are content a built map does not make (animations, weapons, vehicles,
scriptables, AI trees, decal volume materials, the super terrain), the map's sound bank and sound package, `fx_map`
and `streaminginfo`, and `keyvaluepairs` (the list of zones and packages the stock zone
depends on; a built map is loaded as a package). Nothing in this list was traced to a fault.

## 6. Not determined

- `checksum`: what reads it. A built map with 0 loads and plays (both playtests).
- `gfxMapEntLookup` (`lmapLookup`, `probesOffset`, `flags`, `lgvData`, `portalGISlot`; one record on the three maps
  read, all zero): what a missing table changes, if anything.
- The other assets of a map (collision, navigation, map entities, glass) were not audited this way; the stock exports
  for them are not in the cache.

## 7. Second pass: collision, map entities, navigation, paths, edges, glass (2026-11-14)

The stock exports for these were made with ACTS from mp_shipment, mp_m_speedball and mp_rust
(`-a col_map,map_ents,navmesh,glass_map,aipaths,tacgraph,edgelist`), and the tool takes them with `--stock <folder>`.
Built zones: the dev showcase, and a map with audio zones for the entity asset.

| Asset (flags of fields) | What differs | Answer |
|---|---|---|
| `col_map` (15 of 39) broadphase box | Stock small maps all store (-30720, -31744, -14336) .. (34816, 33792, 51200); the built map stored its geometry padded by 16384 / 8192 (z up to about 9,000) | **Fault, fixed.** The game sizes its physics world from this box and freezes a body that leaves it (the converter's own note at `map_zone.h`, from `q_HavokPhysics_OnBodyExitedBroadphase`). Killstreak aircraft fly higher and start further out. The converter now stores at least the stock box; proof `tools/physics_bounds_build_test.py`. |
| `col_map` `stageTrigger` | 18 to 65 trigger volumes in stock, none built | By design since 2026-11-11 (ROADMAP F.1): a built map's triggers are tested by its script. |
| `col_map` `collisionHeatmap`, `topDownMapData`, `checksum` | absent / 0 | Not determined. Maps load and play without them. |
| `map_ents` client triggers | present and of the stock shape when the map has audio zones | No difference in kind (research 72 compared the values). |
| `map_ents` `audioPropNodes` / `audioPropEdges`, `clientTriggerBlend` | 6 to 15 nodes, 14 to 36 edges, 4 to 11 blend nodes in stock; none built | Not built: the sound propagation graph and the blends between audio zones. A built map's zones switch without a blend. |
| `map_ents` `clientSideEffects`, `createFx*` | stock ambient emitters and effects | By design: a built map plays its effects and sounds from its script. |
| `map_ents` dynamic entity counts | 0 in a map without dynamic props | Content. |
| `navmesh` (5 of 24) `linkCreationData`, `obstacleBounds` | 12 to 83 links, 38 to 72 obstacle boxes in stock; none built | By design for the links: the converter writes traversals as Begin / End path nodes, which the game turns into links at load (`replay_navigation.cpp`). Obstacle boxes: not built. |
| `aipaths` (11 of 23) | one node tree and no `visDistData` where stock has 125 to 343 and 64 k to 515 k bytes | Not determined. Bots moved on built maps in both playtests. |
| `edgelist` (15 of 25) | 8 edges and query types 4 where stock has 578 to 862 and 7 | **A real gap, not yet built**: research 125. Mantling works without edges (the game falls back to a collision trace); weapon mounting does not. |
| `glass_map`, `tacgraph` | - | No flag. |
