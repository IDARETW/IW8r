# 48. Stock map remix: what the editor and the x64-zt fork can change in a stock zone (roadmap L11)

Scope: roadmap 11.3, 11.4, 11.5, 11.6 and 3.19. Read-only research. Nothing was built, run, edited or committed. No tool
was run on game files; the only game-derived data read is the existing dump folder `out\m2-x64zt\dump\mp_shipment` and
its CSV, which were written earlier by x64-zt `dumpzone`. Nothing under `E:\IW8\Builds` was touched.

Paths: `E:\IW8-Radiant` (editor) and `E:\mw124\x64-zt\src` (fork). Short forms below: `fork:` = `E:\mw124\x64-zt\src\`.
Items marked **UNVERIFIED** are inferred from source or from a dump and were not confirmed in game or by running a tool.

---

## 0. Answers at a glance

| Item | Possible today (existing tools) | Possible with a fork or editor change | Not realistic now |
|---|---|---|---|
| 11.3 Textured stock world with props | Nothing in the editor shows props or textures. The camera reference is flat-shaded triangles only. | Props: a reader for `gfx_map` `smodels` plus the ModelCache (doc 23). Textures: decode the aux stream UVs and resolve images through the material cache (doc 20). | None of the items is blocked. Size L. |
| 11.4 Add to a stock map | Stock-model `script_model` entities added as text through `iw8r iw8d-set-ents` (UNVERIFIED in game). | Brushes: no overlay exists. The stock `GfxWorld` is one asset per zone, so new brushes mean merging a new `GfxWorld` into the stock one. Models with new meshes: new xmodel rows in the zone CSV (fork build path exists, UNVERIFIED for a stock zone). | Brush overlay as a separate `GfxWorld` in the same map: the engine loads one per map (section 3, 11.4). |
| 11.5 Hide or remove stock static models | None. No tool writes `GfxWorld`. | Hide: a fixed-size field patch in the dumped `gfx_map` record (zero a collection's `instanceCount`, or zero an instance's `scale`); UNVERIFIED in game. Remove: record splice of the static-model arrays (same method as doc 26). Hiding stock brush surfaces: patch the surface `tris` or swap the material by name (UNVERIFIED). | Removing collision: hidden stock geometry keeps its collision (`clipMap` / `col_map`). Removing it needs a collision edit, which is not evaluated here. |
| 11.6 Relight: sun, sky, fog, vision | Vision and fog: edit the plain-text `vision/mp_shipment.vision` raw file in the dump, then rebuild with the m2 path (UNVERIFIED in game; not a live change). Sun values: no route today. | Sun: patch `GfxWorld.sun` (fixed-size struct) and the primary light. Sky texture: a DDS to image writer for `sky_mp_shipment` (no IW8 writer found). Vision set: a generator exists in the converter. Baked sun shadows: re-bake needed (not realistic). | Sky dome shape (it is geometry in `GfxWorld`). Re-baking sun shadows and cascades. |
| Live swap by name (refresh) | Live set: MATERIAL, shaders, TECHNIQUE_SET, IMAGE (`fork:games\iw8\refresh_zone.cpp:41-55`). | None needed for the sky image or sky material. | GfxWorld, rawfile (vision), fogspline, map_ents, col_map, xmodel, grading CLUT: these apply on the next map load only (`refresh_zone.cpp:118, 214-220`). |

Recommendation: start 11.6 fog and vision (cheapest: text values in a rawfile) and 11.5 hide (one fixed-size patch), each
with an owner in-game test. Build the GfxWorld patch tool in `src\core` as a sibling of `spawnlist` (section 6).

---

## 1. What the editor already does with stock maps

### 1.1 World reference (read only)

- `src\core\stock_world.h:6` says it: "Static props (smodels) and the AUX vertex stream (normals, colours, UVs) are not
  read." The reader builds one mesh per material name, positions and triangle indices only (`stock_world.cpp:21, 113-137,
  176-179`). Normals are computed area-weighted (`stock_world.cpp:183-198`). UVs are zero (`stock_world.cpp:186`).
- Sky materials are skipped by name (`src\viewport\scene.cpp:896-905`, `skipSky` default true).
- Load path: `src\qtapp\main_window.cpp:3659` (`cache->load`) then `camera_->setReference` at `main_window.cpp:3668`.
  The status text says "read-only, camera view". It is a flat-shaded reference (`scene.cpp:896-920`), not a textured view.
- Data source: ACTS `gfx_map` and `gfx_map_trzone` exports cached by `WorldCache` (doc 30, sections "Route", "Cache").

### 1.2 Stock spawns and entity string

- Spawns are not in the entity string. They live in `MapEnts.spawnList` (doc 26 section 3). The editor turns them into
  point entities: `src\core\stock_spawns.h:19-29`, `stock_spawns.cpp:25-28` (`isSpawnListClass`: class starts `mp_` and
  contains `spawn`), and `stock_spawns.cpp:39-64` (`splitStockDocument`).
- Spawn list write-back: `src\core\spawnlist.{h,cpp}` (doc 26). The CLI is wired: `src\cli\main.cpp:555` dispatches
  `iw8d-spawns` to `src\cli\spawns_cmd.cpp:16`. (Doc 26 section 5 says wiring is pending. That is out of date.)
- Entity string: `iw8r iw8d-ents <map_ents .iw8> [-o out.ents]` prints record 2 (`src\cli\main.cpp:437-471`, via
  `mapEntsEntityString`, `src\core\iw8_dump.cpp:123-127`).
- `iw8r iw8d-set-ents <map_ents .iw8> <ents> -o <out .iw8>` replaces record 2 and the `numEntityChars` u32 at record 0 +16
  (`src\core\iw8_dump.cpp:129-140`, offset described in `src\core\iw8_dump.h:8-9`). Every other record is kept
  (`src\cli\main.cpp:470-471`). The check it runs is `checkMapEnts`.

### 1.3 Rebuild script

`tools\m2_rebuild_mapents.ps1` (read in full, 92 lines):

1. `zonetool -game iw8 -path <Game> dumpzone <Zone>` (line 49). Stock zone is read, not changed (line 90 checks the stamp).
2. Copy the zone CSV and the dump into `build\zonetool\<Zone>` (lines 50-51).
3. `iw8r iw8d-set-ents` (line 54) and optional `iw8r iw8d-spawns --set` (line 55).
4. `zonetool ... buildzone` into a scratch game folder (lines 56-57), then `verifyzone` (line 58).
5. ACTS `fastfile --test` (line 61) and a `map_ents` read-back diffed against the edit (lines 68-73), plus the spawn
   read-back (lines 74-86).
6. Result `out\<Zone>.ff`. Installing it is manual (line 92).

The script only writes the entity string and optionally the spawn list. It does not touch `gfx_map`, `vision`, `fogspline`
or any image. Note: `prepareStockRebuild` in the editor (`src\qtapp\main_window.cpp:3501`) calls this script.

---

## 2. x64-zt: dump, build and refresh (IW8 / Replay 1.20)

### 2.1 Dump

- `dump_zone` (`fork:games\iw8\dump.cpp:3783-3820`) loads the zone, dumps every non-referenced asset, writes the CSV
  (`write_asset_csv`). `dump_asset` (`dump.cpp:3827-3872`) does one asset.
- File format per asset (`fork:games\iw8\zone\asset_files.cpp:361-364, 408-419`): `<type>/<name>.iw8` binary records,
  except the shared formats (`asset_files.cpp:296-312`): `rawfile`, `scriptfile`, `stringtable`, `luafile`, `ttf` and
  `localizedstrings`, which are written as the plain file they came from. This is why the vision is editable text.
- GfxWorld is a binary stream (`fork:games\iw8\zone\asset_streams.cpp:3692-3730`, `stream_gfxworldstaticmodels`). Its arrays
  are written as data records with counts, and offsets point backwards (doc 26 section 2). The static-model arrays are
  `collections` (stream type 3, `:3706`), `instances` (`:3707`), `instanceFlags` (`:3708`), `smodelInstanceData` (`:3717`),
  `smodelExpansionData` (`:3720`), `smodelSurfMatIndirection` (`~:3721`) and others.
- The stock dump exists: `out\m2-x64zt\dump\mp_shipment\` (CSV `mp_shipment.csv`, 8,848 lines). Relevant rows:
  `gfx_map,maps/mp/mp_shipment.d3dbsp` (one row), `gfx_map_trzone,mp_shipment`, `map_ents,...`, `col_map,...`,
  `rawfile,vision/mp_shipment.vision` (CSV line 97), `fogspline,vision/mp_shipment.vision`, `gradingclut,mp_shipment_clut`,
  `image,sky_mp_shipment`, `image,sun`, `image,sun_flare`, `lightdef` (6 rows), `xmodel` (412 rows).
- Dump sizes: `gfx_map/maps/mp/mp_shipment.d3dbsp.iw8` 67,232,795 bytes; `gfx_map_trzone/mp_shipment.iw8` 132,187,989 bytes.

### 2.2 Verify

- `verifyzone` (`fork:games\iw8\dump.cpp:3874-3956`) reads all bytes.
- `IW8_REWRITE=dump verifyzone` (`dump.cpp:154`, `verify_dump_round_trip`) dumps, rebuilds and compares. A writer check, not a
  game check.
- `IW8_REWRITE=smodel verifyzone` (`dump.cpp:2419-2520`, `verify_static_models`) checks the static-model tables against
  the models: expansion first surface, `numLods`, surface indices, and material indirection. This is the check to run after
  any static-model edit.

### 2.3 Build

- `build_zone` (`fork:games\iw8\build.cpp:340-367`) reads `zone_source\<zone>.csv` (`build.cpp:86-89`), one row per asset.
  Each row calls `add_asset` (`build.cpp:195-224`), then `parse_asset` (`build.cpp:102-126`), which calls
  `parser.parse_file` on the dumped file (`fork:games\iw8\zone\asset_files.cpp:457-467`).
- So an asset is rebuilt from its dumped bytes. A changed dump gives a changed asset. An asset with no dump becomes a reference
  stub or is taken from a `require,<zone>` (`build.cpp:227-239`, `134-157`). Dependencies are parsed in a loop
  (`build.cpp:327-337`).
- The build does not regenerate `GfxWorld` from neutral data. Neutral to native adapters exist (`fork:games\iw8\neutral\gfxworld.cpp`,
  `from_neutral`), but they are called by the converter and the cross-game `dumpzone --target` path (`dump.cpp:690-725`), not
  by `buildzone` for a dumped IW8 zone.

### 2.4 Refresh (live map_refresh)

- `refresh_zone` (`fork:games\iw8\refresh_zone.cpp:161-267`) builds a `techsets_<id>_refresh.ff` from the rebuilt package and
  writes a manifest. The CLI is `refreshzone <deployed> <rebuilt> [out]` (`fork:cli\commands.cpp:35`).
- **Live set** (`refresh_zone.cpp:41-55`, `is_refreshable`): MATERIAL, VERTEXSHADER, HULLSHADER, DOMAINSHADER, PIXELSHADER,
  TECHNIQUE_SET, IMAGE. Compute and library shaders, XModel and everything else are excluded.
- Everything not in the live set goes to `package.others` (`refresh_zone.cpp:118`). A changed or new one gets the warning
  "outside the live-refresh set and apply only on a map load" (`refresh_zone.cpp:214-220`).
- Risk: doc 06 (lines 168-184) records a first live map_refresh that crashed the game (fixed later), and a hinged-clutter
  failure. Treat every refresh as an owner test.

**Answer to "which assets can be swapped by name in a refresh":** materials (including the sky material `tw/sky_mp_*`),
the shaders and techsets, and images (including `sky_mp_shipment`). Not: `gfx_map`, `rawfile` (the vision), `fogspline`,
`map_ents`, `col_map`, `xmodel`, `xmodelsurfs`, `gradingclut`, `lightdef`. Those need a full rebuild and a map load.

---

## 3. Per item

### 11.4 Add to a stock map: new brushes, models, lights and effects

- **Brushes.** Not realistic as an overlay. Evidence: a zone has one `gfx_map` (CSV: one row, section 2.1), and the stock
  `GfxWorld` owns the draw surfaces, cells, DPVS, lightmaps and light grid. Adding brushes means merging new surfaces, vertex
  buffers, cell data and collision into the stock `GfxWorld` (`stream_gfxworld`, `asset_streams.cpp:4500`). Nothing in the fork
  does a merge. The only producer of a whole `GfxWorld` from new geometry is the IW3 converter and the `buildextraction` route
  (docs 15, 17), which builds a separate map. R: needs a research report and a decision (merge project or separate map).
- **Models, placed as stock props.** Partly possible today as text. Add a `script_model` (or `misc_model`) entity with a
  `model` key that names a stock xmodel to the entity string through `iw8r iw8d-set-ents`. Stock maps already use script
  models (doc 34 section 1). UNVERIFIED in game: the model must be in the zone or a required zone, and the engine must spawn
  it. Caution from doc 22: the editor compile drops `model` from `script_model` (`src\core\compile_iw8.cpp:809-818`, per
  doc 22 section 0 item 4). That is the editor compile route. It does not affect a text edit made with `iw8d-set-ents`.
- **Models with new meshes.** Fork change. A new xmodel and xmodelsurfs need rows and dumps in the zone CSV (the custom
  model route of doc 22 writes these for `buildextraction`). Stock-zone use is UNVERIFIED. Static `misc_model` placement
  is not done (doc 22 item 10).
- **Lights.** Not evaluated here. See `docs\research\24_lights.md` (not read for this report).
- **Effects.** Not evaluated here (no read of the FX entity route).

### 11.5 Hide or remove stock static models and surfaces

Facts from the struct layout (`fork:games\iw8\structs.hpp`):

- `GfxWorld.smodels` is at offset 0x150 (`fork:games\iw8\iw8_map_structs.h:349, 358`). It holds `models[]` (`GfxStaticModel`:
  `XModel* model`, `flags`, `firstStaticModelSurfaceIndex`; `structs.hpp:12068-12074`), `collections[]`
  (`GfxStaticModelCollection`: `firstInstance`, `instanceCount`, `smodelIndex`, `flags`; `structs.hpp:12097-12106`),
  `instances[]` (`GfxStaticModelInstance`: one u32 `lgvIndex_transientIndex`; `structs.hpp:12108-12111`), `smodelInstanceData[]`
  (`GfxSModelInstanceData`: `int32 translation[3]`, `orientation`, `float scale`; `structs.hpp:12150-12155`), and
  `smodelExpansionData[]` (`structs.hpp:12169-12177`).
- The static-model flags (`structs.hpp:12057-12064`) have no hidden or visible bit. Lighting, opacity, spline, decal, tint.
- The `instanceFlags` byte is an LOD scale index when the collection flag says so, not a visibility flag (fork converter
  comment, `fork:convert\iw3_iw8\iw3_fastfile.cpp:2681-2684`).
- Stock counts (doc 30 section "What is missing"): 9,535 instances of 277 models in `mp_shipment`.

Options, cheapest first:

1. **Hide a model's instances: zero `collections[i].instanceCount`.** A fixed-size u32 inside an existing array record (no
   splice; the `collections` array is stream type 3, `asset_streams.cpp:3706`). The instances stay in place, so offsets and
   counts stay valid. UNVERIFIED: whether the renderer and the GPU expansion (`smodelSurfData`, `smodelInstanceData` buffer
   copy in `fork:games\iw8\neutral\gfxworld.cpp:773-774`) and the visibility arrays (`collectionVisData`, `asset_streams.cpp:3727-3728`)
   skip the collection. Test in game.
2. **Hide an instance: zero `smodelInstanceData[i].scale`.** Fixed-size float. UNVERIFIED: a zero scale draws nothing in the
   renderer, and the GPU buffer copy (`smodelInstanceDataBuffer`, `neutral/gfxworld.cpp:774`) does not keep the old value.
   Test in game. The translation encoding (int32) has no decode in the fork read path that this report found (UNVERIFIED).
3. **Remove a model.** Needs a record splice of every array that is indexed by instance or collection (the same method as
   doc 26 section 4): `collections`, `instances`, `instanceFlags`, `smodelInstanceData`, `modelStaticIndirection`,
   `collectionBounds`, `collectionVisData`, `collectionVisRefCount`, and the per-surface tables, with the offset records
   re-pointed. `verify_static_models` (`dump.cpp:2419`) then checks the result. Fork or iw8r change; R until option 1 or 2
   is tested.
4. **Brush surfaces.** `GfxWorld.surfaces` (`asset_streams.cpp:3674`) holds `tris` per surface (`posOffset`, `vertexCount`,
   `triCount`, `baseIndex`, doc 30 section "Layout"). Two options, both UNVERIFIED: set `triCount` to 0 (fixed-size), or swap the
   surface material to an existing nodraw material by name (the name is a record, so a size change needs a splice).
   Collision is not changed by either: the stock `clipMap` (col_map) keeps its havok shape (`iw8_map_structs.h:175`).
   Removing invisible walls therefore needs a collision edit (not evaluated; R).

Not realistic now: removing collision with a surface (needs a collision edit), and any change to the sky dome, which is
geometry.

### 11.6 Relight: sun, sky, fog and vision

- **Vision (and fog values).** Possible today, as text. The stock vision is a plain-text rawfile
  `vision/mp_shipment.vision` (CSV line 97; dump `out\m2-x64zt\dump\mp_shipment\vision\mp_shipment.vision`, 1,851 bytes,
  106 lines). Rawfiles are a shared format written as plain files (`asset_files.cpp:310`; `parse_rawfile` compresses on build,
  `asset_files.cpp:105-116`). The keys are hashed names. The converter's table names four fog fields
  (`fork:convert\iw3_iw8\iw3_vision.cpp:638, 764-772`):

  | Hashed key | Field (converter) | Stock `mp_shipment` value |
  |---|---|---|
  | `RPNOKKL` | fogStartDist | 600 |
  | `MSTOLQOTKN` | fogDensity | 25000 |
  | `NMLMMNQLSQ` | fogColor | 0.8509804 0.9254902 1 |
  | `TRPROKTLS` | fogMaxOpacity | 0.75 |

  Plan: change values only, never add or remove keys (the vision has 218 fields, doc 31 section 2), run the m2 path
  (`buildzone` picks up the dumped file: `asset_file_locations`, `asset_files.cpp:374-384`, UNVERIFIED for this path), then test
  in game. Not live: `rawfile` is outside the live set (`refresh_zone.cpp:41-55`).
- **Fog object.** `fogspline,vision/mp_shipment.vision` is also in the zone (CSV). Its dump is binary
  (`fogspline/vision/mp_shipment.vision.iw8`). Which of the two drives the fog the player sees is UNVERIFIED.
- **Vision set name.** `clipMap_t.visionSetName` at 0xE8 (`fork:games\iw8\iw8_map_structs.h:182`) names the vision set. Changing it
  is a string record change (splice). Not tested.
- **Vision from scratch.** The converter writes a complete 218-field vision from defaults plus overrides
  (doc 31 section 2, `iw3_vision.cpp:615-824`). A fork change could reuse that writer for a stock zone. UNVERIFIED.
- **Sun.** `GfxWorld.sun` is a fixed-size `sunflare_t` (`fork:games\iw8\structs.hpp:13154`: `hasValidData`, `spriteMaterial`,
  `flareMaterial`, sprite and flare sizes, fade times, blind and glare angles). A patch of the scalars is possible
  (UNVERIFIED layout offsets in the dump). Sun direction and colour live in the primary light list (`GfxWorld` primary lights,
  `structs.hpp:13334+`) and `Stage.sunPrimaryLightIndex` (`iw8_map_structs.h:105`). The field offsets for direction are not
  decoded here (UNVERIFIED).
- **Worldspawn sun keys.** Editing `sundirection`, `suncolor` or `csm_*` in the entity string with `iw8r iw8d-set-ents` changes the
  text only. Doc 39 section 2 shows that the editor's compile replaces the sun for its own maps and drops `csm_*`. Whether
  Replay reads worldspawn sun keys from a stock entity string is UNVERIFIED.
- **Sun shadows.** Baked (`gfx_compressed_sun_shadow` data, `asset_streams.cpp` GfxWorld stream). Re-baking is not realistic:
  no bake path for a stock zone was found in the fork. Doc 39 shows the cascade settings are fixed in the fork.
- **Sky.** The sky is a dome surface in `GfxWorld` with material `tw/sky_mp_shipment` (doc 30 line 81), and the dome image is
  `image,sky_mp_shipment` (CSV; dump `image\sky_mp_shipment.iw8`, a binary neutral image, doc 18 / doc 33 line 59: BC6H,
  2048x2048, one mip). Texture swap by name: IMAGE and MATERIAL are in the live set, so `refreshzone` can replace them. What is
  missing is a writer from a DDS to the IW8 image records. The only DDS reader under `fork:games\iw8` is
  `games\iw8\dump\read_image.cpp` (a dump reader); no DDS-to-`image_source` writer was found (UNVERIFIED that none exists in the
  IW8 build path). Sky rotation and sky fog are not supported (doc 33 section "limits").
- **Grading.** `gradingclut,mp_shipment_clut` is a stock asset (neutral `grading_clut_source`, `dump.cpp:283`). Swapping it needs
  a writer (UNVERIFIED, not evaluated further).

### 3.19 / 11.3 Textured stock world with props as the reference

See section 4.

---

## 4. What the editor's stock world reader has, and what is missing

Has (`src\core\stock_world.cpp`, `stock_world.h`):

- Positions (Z-up inches, no scale or axis change), triangle indices with the winding fixed to counter-clockwise, normals
  (area-weighted), one mesh per material name, bounds, warnings for missing transient zones (`stock_world.cpp:105-214`).
- Material names per surface (leading comma removed, `stock_world.cpp:139-142`).
- Cached ACTS export and parse (doc 30: 388 MB per mp_shipment cache, about 4 s parse).

Missing for a textured world with props:

1. **UVs and normals from the aux stream.** Aux is 15,129,844 bytes with no established per-vertex stride (doc 30 section
   "Layout", UNVERIFIED stride). Needed for textures and smooth normals.
2. **Images.** Material bodies are in `techsets_mp_shipment.ff` (doc 30 section "Commands"). The material cache (doc 20) resolves
   images. The world reader only has names. A textured view needs the material to image link for every mesh.
3. **Props (`smodels`).** Not read (`stock_world.h:6`). Needed: collections, instances, `smodelInstanceData` (translation, orientation,
   scale), and `models[]` to XModel names. The export has these as encoded bytes (doc 30 "What is missing" item 1). Missing: the
   decode of `translation` (int32 scale) and `orientation` (UNVERIFIED; not in the fork read path, which only copies the struct).
4. **Prop meshes.** ModelCache already resolves a stock model name to an LOD0 GLB via ACTS on demand (doc 23). That covers
   the xmodel mesh once the names are known.
5. **Sky.** Skipped by name (`scene.cpp:905`). Fine for the reference; a textured sky needs the image path (section 3).
6. **Transient zones.** Only zone 0 was seen in mp_shipment (doc 30 item 5).

Plan for the reader, in order of value: props (1 and 3) as an instanced marker layer, then aux UVs and normals (1), then
materials and images (2).

---

## 5. Items marked UNVERIFIED (consolidated)

- Whether zeroing `collections[i].instanceCount` hides the model in game (section 3, 11.5 option 1).
- Whether zeroing `smodelInstanceData[i].scale` hides the instance (option 2), and whether the GPU buffer copy matters.
- The int32 `translation` and `orientation` encodings for prop placement (section 4 item 3).
- Whether `buildzone` uses the dumped `vision/mp_shipment.vision` under the m2 layout without extra steps (section 3, 11.6).
- Whether `fogspline` or the vision controls the fog the player sees (section 3, 11.6).
- Sun direction and colour offsets in the primary light list (section 3, 11.6).
- Whether Replay reads worldspawn sun keys from a stock entity string (section 3, 11.6).
- Whether `image` records can be built from a DDS for IW8 (section 3, sky).
- Whether a `script_model` with a stock `model` key added through `iw8d-set-ents` spawns in game (section 3, 11.4).
- Whether a new xmodel can be added to a stock zone CSV (section 3, 11.4).

## 6. Ordered plan

1. **Editor, 3.19 / 11.3 (read only, no game change).** Add a `smodels` reader to `stock_world` (collections, instances,
   `smodelInstanceData`, `models[]`). Check the instance count on `mp_shipment` against 9,535 (doc 30). Draw as markers, then
   as ModelCache meshes. Decode the `translation` and `orientation` encodings first (write the decode in the report, with
   a test on one known prop).
2. **Editor, 11.3 (read only).** Decode the aux stream stride, then UVs and normals. Resolve material to image through doc 20.
3. **Fog and vision, 11.6 (cheapest relight).** A small editor tool that edits values of the vision rawfile by hashed key
   (table from `iw3_vision.cpp:638, 764-772`), refuses to add or remove keys, and writes a dump copy. Test through
   `tools\m2_rebuild_mapents.ps1`-style rebuild (a new script, or a flag). Owner test in game. Then decide `fogspline`.
4. **Hide, 11.5 option 1 and 2.** A GfxWorld patch tool in `src\core` (sibling of `spawnlist`): fixed-size field patches in
   the dumped `gfx_map` record, with `IW8_REWRITE=smodel verifyzone` (`dump.cpp:2419`) and `IW8_REWRITE=dump` round trip as
   checks. One collection, one scene, owner test in game. Only if both options fail, plan option 3 (splice) in a research
   report first.
5. **Sun, 11.6.** Patch `GfxWorld.sun` scalars and the primary light direction after decoding offsets (step 4 tool). Owner test.
6. **Sky texture, 11.6.** Fork research report on a DDS to `image_source` writer for `sky_mp_shipment` (format BC6H 2048x2048).
   Then a `refreshzone` test (live, owner-run, with the doc 06 crash risk in mind), or a full rebuild for a map load.
7. **Overlay, 11.4.** (a) Test a stock-model `script_model` added through `iw8d-set-ents` (owner test). (b) Brush overlay:
   research report choosing between a `GfxWorld` merge project in the fork and a separate map. No code before the report.
8. **Not planned here:** collision edits for hidden surfaces (11.5), sun shadow re-bake, sky dome geometry (11.6).

Owner gates (from ROADMAP L11 and the suite rules): every in-game step is run by the owner; Deploy and Launch are owner
triggered; nothing is written to `E:\IW8\Builds`.

## 7. Other notes

- Roadmap check: 11.4, 11.5, 11.6 are marked **R** (needs a research report). This report is the R report for those three,
  with the open items above. 11.3 (3.19) is L and has no research blocker.
- `docs\PROGRESS.md` (memory rule: keep it current) was not updated, because this task was limited to one new file.
