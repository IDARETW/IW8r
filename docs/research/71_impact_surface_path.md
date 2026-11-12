# 71. Bullet impact surface path: why every impact looks like stone

Status: research only. No code or data was changed. Sources read: editor (`E:\IW8-Radiant`), converter
(`E:\mw124\x64-zt`, read only), and the editor's output for `mp_iw8r_fix1` under
`E:\IW8-Radiant\out\agent\mp_iw8r_fix1\`.

## Summary

- The editor's surface numbers are correct for what the converter reads: `surface_type` uses the same
  numbering as the converter's `NativeSurfaceMaterial`, row for row (checked 1 to 28).
- For brush hulls, the converter puts the type in the shape tag's `userData` bits 19 to 31 (`material << 19`).
  `surface_type` alone is enough for that. `surface_flags` is not needed for the type.
- Every non-glass shape tag also carries the same physics-material CRC, `0x1AB7BC33` (named PM_Concrete in the
  converter). Nothing in the tag ties that CRC to the surface type. This is the strongest candidate for "all stone".
- In `mp_iw8r_fix1` 29 of 38 collision brushes carry no `surface_type` at all. Those fall back to material 5
  (concrete), which is stone-like. No brush in this map carries type 18 (rock).
- The stock impact table is keyed by the same surface number. For big calibres (xxl) rock (18) and concrete (5)
  share one effect. For small and medium bullets they differ.
- The game-side read of the trace surface is not in the repo. Every claim about what the game reads is inferred
  from converter comments and data layout.

## Data path (editor to game), brush hull

1. Editor, `E:\IW8-Radiant\src\core\compile_iw8.cpp`
   - `:318-326` brush surface type = most common non-tool face material, mapped by
     `replaySurfaceTypeForIw3(surfaceTypeOf(material, library))`.
   - `:2317` writes `"surface_type"` only when the value is greater than 0. Never writes `"surface_flags"`,
     `"axial_materials"`, `"side_materials"`, or a top-level `"materials"` array.
   - `:2338` writes `"triangles":[]` and `"vertices":[]`. Collision is hulls only.
2. Converter reads JSON, `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp`, `ReadCollision` (`:7128`)
   - `:7229-7230` `surface_type` becomes `hull.materialOverride` (raw value, no remap).
   - `:7231-7232` `surface_flags` (if present) is OR-ed into `hull.surfaceFlags & 0x7FFFF`.
   - `:7144-7162` the `collisionMaterials` list is built only if a top-level `"materials"` array exists. The
     editor writes none, so the axial/side branch (`:7236-7280`) never runs.
3. Converter writes the MWCOLL12 intermediate, `WriteCollision` (`:8071`, called at `:13091`):
   `:8095` `surfaceFlags` word, then `:8098` `materialOverride` word (separate fields, not packed).
4. Converter bake, `E:\mw124\x64-zt\src\games\iw8\common\replay_havok.cpp`
   - `:416-419` MWCOLL12 reader; `:525-527` reads `materialOverride`; `:537` rejects `materialOverride > 63`
     and `surfaceFlags` with bits above 18.
   - `BakeCollision` (`:2356`) reads the MWCOLL12 file (`ReadCollision(input.collision)`, `:2362`) and
     the footstep file (`:2363`).
   - Native branch (`BuildShapeListNative`, starts `:1895`). Brush material choice at `:1995-1998`:
     `glassId ? glass : materialOverride ? materialOverride : useFloorMaterials ? floors.At(...) : 5`.
     Tag lookup `findTag(contents, material, surfaceFlags, glassId)` at `:2024`.
   - `findTag` (`:1912-1933`, same logic as the Replay-exe copy at `:1392-1418`):
     `userData = (tagClass << 48) | (glassId << 32) | (material << 19) | surfaceFlags` (`:1929-1931`,
     `:1408-1410`), and `materialNameCrc = (material == 9 || glass) ? 0xF728E572 : 0x1AB7BC33` (`:1932`,
     `:1411-1414`).
   - Tag table written by `physics.cpp:145-157` as 24-byte records: contents, material_crc, material_id
     (0xFFFF), 6 pad bytes, user_data. Struct: `physics.hpp:8-14`.
5. Game reads the trace surface from the hit shape's tag. The converter's comments say `trace surfaceFlags >> 19`
   (`iw3_fastfile.cpp:9806-9808`). The game code that does this is not in the repo.

So the surface number is in the tag's `userData` bits 19 to 31. It is not written as a separate tag, and the
hull's `surfaceFlags` word does not carry it. Many hulls share a tag: the tag key is
(contents, material, surfaceFlags, glassId), so identical hulls reuse one entry (`:1382-1418`).

### Log lines

- `collision: built %zu hulls and %zu mesh triangles natively in %zu shape lists and %zu tags` (`:2099-2101`).
  "shape lists" = `groups.size()` for that call: one entry per group (the world group, and one per brush model
  in the entities call). Each entry is one compound or compressed-mesh shape holding its hulls as convex instances.
  "tags" = `tags.size()` for that call, which includes seed tags copied from the world call for the entities call.
- "untagged" does not appear as a string in the converter (searched). The closest is `iw3: tagged %zu brush hulls
  from source materials` (`:7376`), which counts the axial-material path and is 0 for the editor's output.
- The "collision: built native PhysicsAsset ... surface 16" line is at `replay_havok.cpp:2185-2186`
  (`BakePhysicsAssetNative`). Its surface is `source.surfaceType`.
- The converter log for `mp_iw8r_fix1` was not found. `out\agent\mp_iw8r_fix1\build\compile-log.txt` is the
  editor log and has no "collision:" lines. Searches for "built native PhysicsAsset" and "shape lists" under
  `out\agent\mp_iw8r_fix1` returned nothing.

## Question 1: what the hull holds, and where the game reads the type

- For a brush with only `surface_type`: `hull.materialOverride = surface_type`, `hull.surfaceFlags = 0`
  (`iw3_fastfile.cpp:7229-7232`). Tag userData gets `surface_type << 19` (`replay_havok.cpp:1410`).
- `surface_flags` is not required for the type. The reader's comment (`iw3_fastfile.cpp:7227-7228`) says
  IW7-style extractions give "its type and the low 19 bits of its surface flags". That is the IW7 exporter
  (`E:\mw124\x64-zt\src\convert\iw7_iw8\export_collision.cpp:246-247`, `:271`). The IW3 path from the editor
  writes no flags, so the low 19 bits are 0. Nothing here shows the game needs those bits for impacts.
- Where it is stored, in the Havok output:
  - Brush hulls: in the shape-list `shapeTagData` (user_data, bits 19 to 31).
  - Static-model physics assets: in the compressed-mesh shape's `userData` (see Q4).
  - Static-model detail collision: in `XModelCollisionTagData.surface_flags` (see Q4).
- Surface 0 is not written (`compile_iw8.cpp:2317`), so `materialOverride` is 0. The native path then uses
  `floors.At(...)` for world hulls (`:1997`) and 5 for brush-model hulls (`:1998`). See Q2.
- Is the type also in `surface_flags`? No. The type is `material << 19`, which is the same bit range
  (19 and up) that `surface_flags` would occupy. The two fields are separate in the tag. `surface_flags`
  (low 19 bits) is OR-ed in separately (`:1410`).

## Question 2: brushes without axial or side materials

- The editor never writes `axial_materials` or `side_materials`, and never writes a top-level `"materials"`
  array. So `collisionMaterials` is empty (`iw3_fastfile.cpp:7236`) and the axial branch never runs. Those
  brushes take the plain path (`:7369-7370`): one hull, one `materialOverride`.
- The "mixed" split paths (`:7324-7368`) need `mixedMaterials`, which needs the axial branch. They never run for
  editor output.
- Hulls with no `surface_type` have `materialOverride = 0`. There is no separate "untagged" hull kind. Their
  material is decided later in the native bake (`:1995-1998`):
  - world hulls (`useFloorMaterials = true`, `:1943` in the native builder, called from `:2453`): the footstep
    triangle under the hull center, `FloorMaterials::At`, or 5 if no footstep triangle is found;
  - brush-model hulls: 5 (concrete).
- Footstep file: `WriteFootsteps` (`iw3_fastfile.cpp:8345`, called `:13000`). Only upward faces
  (normal z / length at least 0.55, `:8364`) of the first model's render surfaces are written. Their material is
  `FootstepMaterial(...)`, which returns 5 when no name rule matches (`:8342`).
- Concretely in `mp_iw8r_fix1` (JSON `replay-collision.json`): 38 brushes. `surface_type` counts: 17 (plaster) x4,
  5 (concrete) x4, 9 (glass) x1. So 29 brushes are untagged and default to 5 or to the floor material.
  No brush has 18 (rock). The "stone" look most likely comes from 5 (concrete) and the 5 default.
- Merged lists and tags: all hulls of one shape list are convex instances of one compound (world), and the tag is
  per instance (`:2024`). Many hulls share one tag. "4 tags" in the log would mean four distinct
  (contents, material, flags, glass) keys in that call.

## Question 3: which surface numbers have their own impact family

Source: `E:\mw124\x64-zt\src\games\iw8\assets\replay_impact_data.h` (`kEffectNames` `:15-296`, `kEffectIndices`
`:298`, 42 impact types x 2 directions x 2 packs x 87 effects). Lookup: `replay_impact.cpp:32-36`, `:54-79`.
Surface slot = effect index 0 to 63 (flesh is 64 and up). Impact types from the converter comment
`iw3_fastfile.cpp:9721-9727`: sml = 1, med = 2, lrg = 3, 12g = 6, xxl = 15. Direction 0, pack 0.

| Surface (Replay no.) | sml (1) | med (2) | lrg (3) | 12g (6) | xxl (15) |
|---|---|---|---|---|---|
| 1 bark | bark_solid_med | bark_solid_med | bark_solid_med | wood_solid_12g (fallback) | bark_solid_med |
| 4 cloth | cloth_med | cloth_med | cloth_med | cloth_12g | cloth_med |
| 5 concrete | concrete_sml | concrete_med | concrete_lrg | concrete_12g | concrete_xxl |
| 9 glass | glass_sml | glass_med | glass_lrg | glass_12g | glass_lrg |
| 16 paper | paper_med | paper_med | paper_med | paper_12g | generic_xxl (fallback) |
| 17 plaster | plaster_sml | plaster_med | plaster_lrg | plaster_12g | plaster_xxl |
| 18 rock | rock_med | rock_med | rock_med | rock_12g | concrete_xxl (fallback) |
| 35 cushion | cushion_med | cushion_med | cushion_med | cushion_12g | generic_xxl |

Full sml (type 1) row, index to family: 1 bark_solid_med, 2 concrete_sml, 3 cloth_med, 4 cloth_med, 5 concrete_sml,
6 dirt_sml, 7 flesh_sml, 8 foliage_med, 9 glass_sml, 10 grass_med, 11 gravel_med, 12 ice_med, 13 metal_nonpen_sml,
14 metal_nonpen_sml, 15 mud_sml, 16 paper_med, 17 plaster_sml, 18 rock_med, 19 sand_sml, 20 snow_med, 21 water_med,
22 wood_solid_sml, 23 asphalt_sml, 24 tile_med, 25 plastic_med, 26 rubber_med, 27 splash_med, 28 metal_thick_sml,
35 cushion_med, 36 generic_sml. Index 0 is empty ("-") for sml, med, lrg and 12g, so surface 0 gets no impact effect from the table there. For xxl,
index 0 is generic_xxl.

Answer for types 4, 5, 16, 17, 18: they have distinct families (cloth, concrete, paper, plaster, rock) for sml, med
and 12g. For xxl, 18 (rock) and 5 (concrete) use the same effect (concrete_xxl), 16 (paper) falls back to
generic_xxl, and 4 (cloth) uses cloth_med.

Notes:
- Index 2 (brick) and index 3 (carpet) have no family of their own. In the table they hold concrete and cloth.
  Index 3 = cloth contradicts the comment at `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp:6986-6987`
  ("3 carpet_solid"). The table is the data the game uses, so the comment is probably stale.
- The editor's own surface table `E:\IW8-Radiant\src\core\iw8_contents.cpp:318-346` (footstep sounds) uses the
  same fallbacks, which confirms the families.
- The editor writes `_default.iw3.json` with all-null entries (`iw3_fastfile.cpp:9796-9797` skips nulls), so no
  effect override is applied. Impacts use the stock table, keyed by the surface number from the hit. This is not
  the cause of the stone look.

## Question 4: static model physics assets

- Where the number is stored, two places:
  - PhysicsAsset (static model): `BakePhysicsAssetNative` (`replay_havok.cpp:2107`). Surface number
    `shapeUserData = surfaceType << 19` (`:2114`), written to the compressed mesh `user_data` (`:2138`). The
    triangles use tag 0xFFFF (`:2137`), so the hit reads the shape's user data (comment `:2546-2549`, which cites
    the game function at 16172E0; that address was not verified). Body `material_crc = 0x4B02BC9D` (PM_Plastic,
    `:2154`) and `material_id = 0xFFFF` (`:2166`).
  - Detail collision (`XModelDetailCollision`, `BakeXModelDetailCollision` at `:2922-2950`): the body's
    `surface_flags = surfaceType << 19` (`:2944`) is written as `XModelCollisionTagData.surface_flags`
    (`physics.hpp:122-135`, `physics.cpp:370-377`). The comment says bullets and melee hit this shape.
    Detail collision is on by default (`convert.cpp:2644-2650`, `replay_havok.cpp:2910-2919`), with a native
    compendium.
- Which surface: `physics.surfaceType` = IW3 surface type covering the most collision area
  (`iw3_fastfile.cpp:5113-5171`). Each triangle's material is `NativeSurfaceMaterial(sourceSurfaceTypeBits)`
  (`:5140-5143`), so one number per model.
- Numbering: the same as brush `surface_type`. `replaySurfaceTypeForIw3`
  (`E:\IW8-Radiant\src\core\iw8_contents.cpp:251-284`) matches `NativeSurfaceMaterial`
  (`iw3_fastfile.cpp:6984-7020`) row by row (bit 1 to type 1 ... bit 2^27 to 28, with brick 2, carpet 3, mud 14 to 15,
  cushion 26 to 35, fruit 27). Custom materials get `surfaceTypeBits = 1 << (type - 1)` from
  `iw3SurfaceTypeBits` (`compile_iw8.cpp:1023`, `iw8_contents.cpp:246-249`).
- Cardboard box: material `ee_food_fruit_cardboard_box` has `surfaceTypeBits` 16384 (bit 14, IW3 type 15 paper),
  which becomes Replay 16 (paper). That matches the "surface 16" in the user's log.
- Consequence: a box's detail collision says paper (16), and its impacts should say paper_med. The stone look
  therefore points away from the static-model surface number, or to a hull that was hit instead (see H4).

## Question 5: ranked hypotheses

1. **The impact lookup uses the physics material CRC, which is PM_Concrete on every brush tag.**
   - For: `replay_havok.cpp:1411-1414` and `:1931-1932` write `0x1AB7BC33` for every non-glass tag, regardless of
     material. `iw3_fastfile.cpp:3425` names that CRC PM_Concrete. Any lookup by CRC gives concrete for every brush
     hit, and concrete reads as stone.
   - Against: the converter's comments say the trace surface is `surfaceFlags >> 19` (`:9806-9808`,
     `replay_havok.cpp:2546-2549`). Static-prop collision has no CRC on the detail shape, so this cannot explain
     the box via detail collision. The game side is not in the repo.
   - Cheapest test: not possible from editor output (the editor writes no CRC). A converter experiment in a private
     worktree (not the shared x64-zt checkout) that sets `materialNameCrc` from the PM table (`iw3_fastfile.cpp:3405-3429`)
     per material, then compares one brush of each type in game. An editor-only proxy: compare two brushes, one
     tagged paper (cardboard-named face) and one tagged rock, in one room. If they look identical, this or H2 holds.

2. **Most brushes have no type, so they resolve to material 5 (concrete) or the footstep default.**
   - For: 29 of 38 brushes in `mp_iw8r_fix1` have no `surface_type` (`compile_iw8.cpp:2317` writes only > 0).
     Their material is 5 (`replay_havok.cpp:1998`, `:2054`) or `FootstepMaterial` default 5
     (`iw3_fastfile.cpp:8342`). For sml, med, lrg and 12g, surface 0 gives no effect at all
     (`replay_impact_data.h` index 0 is empty), so untagged hits go through the 5 default.
   - Against: the 9 tagged brushes (plaster, concrete, glass) should then be right. The box's placed-model hull
     carries its face-material type if its faces are named (cardboard = paper), so it would not be stone unless its
     hull is untagged.
   - Cheapest editor-only test: a test room with three brushes: one whose face material is named "cardboard" (paper),
     one named "stone" (rock), and one with no type (plain or tool material). Fire bullets at each. If the untagged and
     rock brushes look the same, this holds. This needs no converter change.

3. **Calibre collapse in the stock table (xxl, and 12g for paper).**
   - For: for xxl, 18 (rock) and 5 (concrete) share `concrete_xxl`. For 12g and xxl, paper (16) falls back. The weapon
     picks its impact type (`iw3_fastfile.cpp:9721-9727`).
   - Against: sml and med bullets get distinct families for 4, 5, 16, 17, 18, so a normal rifle should not collapse.
     Only the xxl or shotgun-style calibres collapse.
   - Cheapest test: fire the sml and xxl weapons at one brush of each type. This is in game and needs no editor change.

4. **Static prop detail collision: the game reads a different field than `XModelCollisionTagData.surface_flags`.**
   - For: the number is written in both detail collision and the PhysicsAsset shape, but the box's material is plastic in
     the PhysicsAsset body (`:2154`). A game that reads the body material, or the xmodel surface bits, would show a
     different type. Neither matches "stone" directly, which is why this ranks low.
   - Against: nothing in the repo shows the game reading anything other than the tag or shape user data. The
     `surface_flags` path is the one the comment names for bullets.
   - Cheapest editor-side test: compare a stock paper prop with a custom material whose library type is paper
     (`compile_iw8.cpp:1023` writes its bits). If the custom one differs, the material bits matter.

5. **Numbering mismatch (IW3 numbers reach the game).** Low. The editor and converter tables match row for row
   (Q4). The footstep and impact tables use the same families. Cheapest check: none needed beyond the test in H2.

Also considered and not supported: a missing ladder or flags bit (the editor writes no `surface_flags`, but the type
does not depend on it). Writing `surface_flags` from the editor would change only the low 19 bits and is not expected
to fix the type. Writing `axial_materials` and `side_materials` is a no-op unless a top-level `"materials"` array is
also written. With it, uniform brushes get the same `materialOverride` as `surface_type`, so the result would not
change for single-type brushes.

## Question 6: what could not be determined

- The game-side read of the trace surface: which field (tag `userData`, shape `userData`, body surface flags, or
  material CRC), and the exact index. Only converter comments say `surfaceFlags >> 19`. The address 16172E0 is cited
  in a comment and was not checked. The Replay binary was not opened.
- Whether `surface_flags` low bits (ladder, etc.) affect impact selection.
- Whether surface 0 falls back to some default effect in the game (the table has none).
- Whether the bullet hits the detail collision or the brush hull for a placed prop. This decides H1 against H4.
- The converter log for `mp_iw8r_fix1` (the "surface 16" and "1 shape list, 4 tags" lines) was not found on disk.
  The quoted lines were used as given.
- The `mp_iw8r_fix1` build's `surface_type` values are as read from the JSON. Which brush the user fired at was not
  recorded.
- Whether "stone" is concrete (5) or rock (18). They share the xxl effect and differ by name only at sml and med.
- The footstep input used during this build: the file path was not confirmed in `extracted/`.
