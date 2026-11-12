# 44. Incremental builds (roadmap 7.10)

Status: research and core done (`src/core/build_cache.*`, tests in `tests/build_cache_tests.cpp`). The editor and
x64-zt are not changed yet; the integration steps in section 5 are the next work. Line numbers below are from the
files as read on 2026-11-10; `src/core/compile_iw8.cpp` is being edited concurrently, so cite by function if they drift.

Build pipeline in the editor (`src/qtapp/compile_panel.cpp`): Extraction (`runExtraction`, compiles the document into
`<build>/extraction` with `compileIw8Extraction`) -> Zones (`removeRecursively` of the zones folder, then
`zonetool -game iw8 buildextraction`, compile_panel.cpp:378-407) -> Validate (ACTS `--test` on every `.ff`,
compile_panel.cpp:410-420, 654-668). The user's "Extraction only" button runs the first stage alone.

## 1. Where build time goes

Source: `out/snippet-test/res/compile-log.txt` (run 16:40, mp_snip, 8 surfaces) and `out/compile-nm/compile-log.txt`
(run 15:55, mp_iw8r_dev_sho). All numbers are the `[info] timing:` laps x64-zt prints. Seconds, first run / second run.

| x64-zt lap | snippet 16:40 | compile-nm 15:55 | What it is |
|---|---|---|---|
| iw3 conversion (total) | 4.7 | 4.4 | Everything from reading the extraction to the converted world |
| iw3/render assets | 4.3 | 3.2 | Atlas, material images, probes, shadows (contains the rows below) |
| render/atlas mips | 1.9 | 1.5 | Atlas composition and mip chain, computed every build |
| render/shadow casters, sky and lightmaps | 1.7 | 0.5 | Sun-shadow scene and sky/lightmap setup |
| render/atlas tiles | 0.5 | 0.7 | Tile composition into the atlas, computed every build |
| iw3/baked sun depth | 0.4 | 0.3 | Sun depth bake (log line: "read the bake from the cache" in the second run) |
| render/material images | 0.1 | 0.5 | Material image decode |
| iw3/static models | 0.0 | 0.5 | Static model prep (none in the snippet map) |
| navigation (grid, pathnodes, tactical) | 0.4 | 0.4 | Navigation and footstep data |
| package/stock exports | 2.6 | 0.8 | Stock zone copies from `_stock_cache` (see 2.3: the key includes the names set) |
| package/main zone build | 0.3 | 0.5 | Assembling the main zone |
| package/zone write | 0.6 | 1.4 | Writing `.ff` files (the 338 MB main zone in the first run) |
| package/metadata and validation | 0.5 | 0.9 | Structural checks in x64-zt |
| package/asset registration, render load, other | 0.4 | 0.4 | Asset ledger, render load |

Top costs, in order: the atlas (mips + tiles, about 2.0 to 2.6 s, recomputed every build), sun/sky/lightmap setup
(0.5 to 1.7 s), stock exports (0.8 to 2.6 s, varies with the names set), zone write (0.6 to 1.4 s), metadata and
validation (0.5 to 0.9 s), and the sun depth and material images (0.3 to 0.5 s each). The x64-zt in-process total is
about 9 s per build.

Not in those logs, and so not measured yet: the editor's own Extraction stage (no timing line, and it writes every
file, section 3), and the five ACTS `--test` runs of Validate (external processes; their duration is not printed).
Measuring both is the first step of the build time report half of 7.10.

## 2. What x64-zt already caches

2.1 Converter caches, all in the shared output root (`x64zt_output\.iw3-render-cache`, passed through `--output`).
The root is `renderCache` by default beside the output (`convert.cpp:91-92`, `convert.cpp:2523-2526`).

- Reflection probe BC6H array: `iw3_render_assets.cpp:5618-5633`, key = SHA-256 of the probe inputs plus
  `bc6h-dxtex-v2`. The log line "reusing cached reflection-probe array" comes from here (both runs).
- Atlas BC7 encode: `iw3_render_assets.cpp:4373-4400` (`EncodeBc7Chain`), key = SHA-256 of the RGBA chain plus
  `bc7-mode6-v1`. Only the encode is cached: the atlas and its mips are composed first (`iw3_render_assets.cpp:6795-6797`),
  which is why atlas mips and tiles still cost 2 s.
- Resident image encodes: `iw3_render_assets.cpp:5119-5122` (`ResidentEncodeCache`), key = digest of the pixels and size.
  The digests are computed first, so the log's "0.0s digests" line is paid every build.
- Sun-shadow bake: `games/iw8/common/replay_sunshadow.cpp:1262-1400` (`ReadCache`, `WriteCache`), key = `CacheKey(scene, values)`.
  The scene is built before the lookup ("prepared sun-shadow scene" in both logs).
- Sound FLAC cache: `games/iw8/assets/replay_sound.cpp:669-760`, key per sample. Map builds with no sounds do not reach it.

2.2 The IW3 extraction cache (`convert/iw3_iw8/iw3_fastfile.cpp:10132-10160`, used at 10641-10656 and 10816-10830) copies a
finished extraction keyed on the Unlinker and fastfile stamps. It applies only when x64-zt reads an IW3 fastfile. The
editor's `buildextraction` route sets `options.extraction` ("external", `iw3_fastfile.cpp:10641-10650`) and reads the
folder in place, so the editor gets no extraction cache. The `IW8_ZONETOOL_NO_EXTRACT_CACHE` switch does not apply.

2.3 Stock export cache (`games/iw8/assets/replay_stock.cpp:296-330`). The directory key is
zone name + zone size + zone mtime + export types + the sorted list of requested asset names, and success is a
`complete.txt` marker. Any change to the set of stock names (one more stock material, one more stock model) makes a
new key: the whole zone export is removed and re-run through ACTS. This is why the stock step cost 2.6 s in one run and
0.8 s in the other.

2.4 Editor side: stock material PNGs are cached by mtime (`compile_panel.cpp:90-200`; stale test at 107 and 158-159), and
the stock model and sky images come from the ACTS caches (`stockModel_`, `skySource_`).

2.5 What no cache covers: zone assembly and write, metadata and validation, navigation, the world/collision JSON, and
the stock export when the names set changes.

2.6 Persistent mtime keys: the only persistent last-write-time keys in the converter and iw8 game code are the Unlinker
and fastfile stamps (`iw3_fastfile.cpp:10142`) and the stock zone stamp (`replay_stock.cpp:303`). The pixel-file cache
keyed on mtime (`games/iw8/common/replay_render.cpp:454`) is in-process only. So rewriting extraction files does not
invalidate any converter cache today.

## 3. What the editor redoes needlessly

All line numbers are in `src/core/compile_iw8.cpp` as read.

3.1 Every extraction file is rewritten on every build. `writeFile` (753) opens with `trunc` and writes unconditionally.
It is called for the nine fixed files (complete, skeleton/complete, the three `replay-*.json`, `.ents`, the grey
material, zone_source, impactfx) and for every entry in `customFiles` (the final loop at 1821). Unchanged bytes still get
a new mtime and a full write.

3.2 Image work with unchanged inputs:
- Custom material DDS: every colour, normal and specular PNG is decoded and re-encoded each build
  (`encodeDdsFromImage`, 1703; loop from 1690).
- Sky: the source sky DDS is read and converted to a cube map each build (`skyCubemapFromDds`, 1350), then written
  (a 24 MB file in the compile-nm report, "iw8r_sky_pano 24.0 MB").
- Reflection probes: `buildProbeDds` runs for every probe, including the default (`probeCube`, 1725-1732).

3.3 Model work with unchanged inputs: custom OBJ models are parsed each build (`importObj`, 1503) and rewritten
(`writeExtractionModel`, 1827).

3.4 Minimap: `renderMinimap(map, 512)` and `writeMinimapDds` run each build (1836). The minimap draws only worldspawn,
func_group and script_brushmodel (`src/core/minimap.cpp:48-50`, 86). A point entity move cannot change it, yet it is
rendered again.

3.5 Editor-side extras: `.iw8mat.json` for every stock material is rewritten (`compile_panel.cpp:192`), the metadata file
is rewritten (about line 390), the zones folder is deleted before every Zones run (`compile_panel.cpp:384`), and the
Zones stage runs x64-zt even when the extraction is byte-identical to the last one.

3.6 Validation: all five zones are run through ACTS `--test` on every build (`compile_panel.cpp:410-420`), even when
the zones are identical.

## 4. Design

### 4a. Skip unchanged extraction files

Step 1 (disk only): `writeFile` calls `writeIfChanged` (`src/core/build_cache.cpp`). An unchanged file keeps its mtime
and is not written. The change is a body edit in `writeFile`; it is not made here because only the three new files may
be touched in this change.

Effect on x64-zt: none of its caches key on extraction mtimes (section 2.6). The probe, atlas and resident-image caches
are keyed on SHA-256 of their content, and the stock cache reads the stock zone, not the extraction. So identical bytes
give identical keys whether or not the file was rewritten. The benefit is fewer bytes written
(the 24 MB sky file is the main one), and stable mtimes for any tool that inspects the folder.

Step 2 (CPU): reuse the output of an unchanged input. Store, beside the extraction, a manifest of
`output name -> input key -> output bytes digest`. Input keys: the source file's size and mtime (PNG, DDS, OBJ, LUT,
probe capture, sky source) and the encoder version. For images the key is the source bytes plus the converter's encoder
version. When the key matches and the output file exists with the recorded digest, skip the decode, encode or convert.
This is the part that saves the 3.2 and 3.3 CPU time; step 1 alone saves only writes.

Edge cases: `checkOutputFolder` (727) still guards the folder; the `complete` marker is written last, as now.
`writeIfChanged` returns false when another process holds the target open without share-delete (the unit test hit
this; the old `trunc` write fails the same way), so an open preview or a running x64-zt does not produce silent data
loss, only a reported write error.

### 4b. Entity-only fast path

What "entities-only" means here: the point-entity key/values changed and nothing else (no brush, material, model key,
worldspawn key or script key). Which outputs that changes:

| Entity change | Outputs that change | Source |
|---|---|---|
| any accepted point entity (spawns, script_origin, info_player_start, mp_* spawns, trigger keys) | `<id>.d3dbsp.ents` (the entity string), the spawn list in the zone | `compile_iw8.cpp` `appendEntityText` (552) and the entity loop around 1398-1420 |
| `light*` | comworld primary lights (`localPrimaryLights`, 591; call at 1555) | doc 24 |
| `reflection_probe` | `world.json` reflection_probes (probe block after 4b, near 1190-1250) | doc 29 |
| `misc_model` | `world.json` models[] placements, and an invisible collision box in the collision JSON (`modelCollisionBoxes`, 1032) | doc 29 |
| `fx_origin`, `sound_emitter` | the map `.gsc` and `<id>.stock_sounds.txt` (`readFxPlacements` / `readSoundPlacements`, `map_script.cpp:29, 55`) | doc 31 |
| none of these (minimap) | nothing (section 3.4) | `minimap.cpp:48-50` |

So "entities-only" is necessary but not sufficient for "ents file only". A later refinement, which the current
fingerprint does not support, is to hash point entities per class group: group A (only `.ents` and the zone change)
and group B (lights, probes, misc_model, fx, sound: world.json, comworld, collision or gsc change as well). Group B
changes must rebuild the JSON files they touch.

Can the zones be rebuilt with only the entity string? Not today:

- `buildextraction` always rebuilds all zones from the extraction (`cli/zone_commands.cpp:309-376`). There is no
  partial rebuild switch in the editor's route.
- Live refresh cannot carry it. `games/iw8/refresh_zone.cpp` (`is_refreshable`, around lines 37-55) covers materials,
  vertex and pixel and other shaders, techsets and images. `map_ents` (type 29) is not in the set. The comment there
  notes that refresh zones skip the dynent relink (`R_DynEntClient_RelinkAllFromMapEnts`), so moved entities would not
  update in a running match even if the type were allowed.
- The M2 path (`iw8r iw8d-set-ents`, `src/cli/main.cpp:436-480`, `src/core/iw8_dump.h`) rewrites only the entity string
  and `numEntityChars` inside an `IW8D` dump of a stock `map_ents` asset. It does not produce a `.ff`, and x64-zt has
  an `IW8D` writer (`games/iw8/zone/streams.hpp`) but no `IW8D`-to-zone loader in the code read. `docs/MILESTONES.md:37`
  lists the stock round trip as NOT VERIFIED.

Conclusion: a true entity-only zone patch needs x64-zt work, namely a `buildextraction --reuse <previous zone>` mode that
copies every asset of the previous build and regenerates only `map_ents` (and the spawn list, M2.2). That is a
converter feature, not an editor change. Until then, the entity-only path saves editor time (no minimap, probes,
models or image work, and no rewrite of unchanged files) and x64-zt time only through its warm caches. The zone is still
rebuilt, roughly 9 s plus validation.

Measure before promising a number: the editor's Extraction stage and the Validate stage are untimed (section 1).

### 4c. Script-only path

What changes: worldspawn `iw8r_script` changes the map `.gsc` (`extras.userScript` in `compile_iw8.cpp`, the block at
about 1765-1790, written as `maps/mp/<id>.gsc`). Nothing else in the extraction depends on it. Its warnings
(`has no user_main()`, `cannot be read`) are the only other effect.

Editor side: the classifier returns "script-only", so only the `.gsc` needs writing, plus the warnings.

x64-zt side: the `.gsc` is compiled by `gsc_tool` inside `buildextraction` (`compile_panel.cpp:403`; the warning text
"plays only when x64-zt compiles it" in `compile_iw8.cpp`). A script-only build still needs a `buildextraction` run
unless x64-zt gains a mode that recompiles the map's script into the existing map zone. That mode is not present in
the code read.

Gap that must be closed first: `fingerprintMap` hashes the script key (the path) only, as specified. Editing the
`.gsc` file on disk therefore produces "none" and a stale build. The caller must also hash the script file's bytes into
the `script` field (or keep a separate file key), and it must treat a missing or unreadable file as "full".

## 5. Integration (next steps, not done here)

1. `compileIw8Extraction`: make `writeFile` call `writeIfChanged` (section 4a step 1).
2. In `runExtraction`: `fingerprintMap(doc.map())`, compare with `loadFingerprint(<build>/fingerprint.txt)`. Classify.
   Start with "none" (skip the Zones stage only when the zones also match) and "full" (today's path). The fast paths
   wait for x64-zt support (4b) and for the script-file hash (4c).
3. Also key the stage on the options that change output: grey material name, custom material library, model roots and
   scale, sky file, probe capture folder, map folder, prefab roots, and stock model and texture names. The fingerprint
   covers only the `MapFile`, so these need their own key or the classifier can say "none" for a changed build.
4. Save the fingerprint after a successful Zones stage, using `saveFingerprint` (it writes through `writeIfChanged`).
5. Add timing laps in the editor (Extraction, Zones, Validate) to the build report.

## 6. Open items

- The entity-only zone patch needs x64-zt work (4b). Not verified: whether `patchzone` (`cli/commands.cpp`) can
  express it. It was not read.
- Not measured: the editor's Extraction and Validate durations.
- The fingerprint ignores the file inputs listed in step 3 and the script file's contents (4c).
- `localPrimaryLights` now takes IES profiles (`compile_iw8.cpp:1555`), which are read each build; they should join the
  image cache in 4a.

## 7. Files in this change

- `src/core/build_cache.h`, `src/core/build_cache.cpp`: `writeIfChanged`, `InputFingerprint`, `fingerprintMap`,
  `classifyChange`, `saveFingerprint`, `loadFingerprint`.
- `tests/build_cache_tests.cpp`: 11 tests (write-if-changed keeps the mtime when identical, rewrites when different,
  creates parents and leaves no temp file; fingerprint classes none, entities-only (moved point entity), script-only
  (changed script key), full (edited brush, model and material change, worldspawn key, entities plus script), and the
  save/load round trip with a damaged file).
- Not changed: `compile_iw8.cpp`, `compile_panel.cpp`, `docs/PROGRESS.md`, `docs/ROADMAP.md` (the file restriction for
  this change).
