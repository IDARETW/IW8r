# 84. Stock map props: why a stock map opened almost empty, and what changed

Date: 2026-11-11. Modern Warfare 2019 (Replay 1.20) data only. The game was not started. The owner's editor, their
caches and their settings were not touched: every run used a private copy of the editor (`out\propfix_bin`) and
private model and material caches (`IW8R_MODEL_CACHE_ROOT`, `IW8R_MATERIAL_CACHE_ROOT`) under `out\propfix-*`. The
world cache and the stock entity cache were only read.

Owner report (their words, with a screenshot of entity markers floating in an almost empty scene, after File > Open
IW8 Stock Map > mp_rust): "im still not seeing all geometry in stock maps". Third report of this kind (roadmap F.17).

Terms, defined once:

- **Prop**: a static model the map places (a crate, a palm, a pipe). mp_rust places 6,938 props that use 514 different
  models; mp_backlot2 places 17,651 that use 939.
- **Stock reference**: the stock map's own world (ground, walls) and props, drawn read-only in the camera. Until now it
  was only loaded by View > Stock Map Reference.
- **Model cache**: the editor's folder of exported models (`model_cache`, one GLB file per model, `index.json` lists
  them). A model that is not in it is fetched from the game files through ACTS.
- **Batch**: the group of models the model cache fetches in one go (150).
- **Marker**: the small box the camera now draws where a prop's model is not there (amber: still being fetched, red:
  failed).

## 1. Short answer

1. **The main cause is not in the prop fetching at all. File > Open IW8 Stock Map loads only the map's entities. It
   never loads the stock reference**, so neither the world nor a single prop is drawn. What the owner saw (markers in
   an empty scene) is exactly the entity list on its own. This one is **not fixed by this work**: the fix is one line
   in `MainWindow::openStockZone`, a function outside the files this task was allowed to change. See section 3.A.
2. With the reference loaded, the existing fetch worked on mp_rust (511 of 514 models, about 34 seconds), but it had
   five real faults, all fixed here: nothing was drawn for a model that was not there yet and nothing said that work
   was going on; fallback boxes were never visible; four models in the owner's cache were marked failed for good
   although their files are in the cache; a few models whose geometry sits in another zone always failed; and the
   chain of batches depended on the camera redraw and could wait for ever when the model cache could not start.
3. After the change, with an empty model cache: **mp_rust 514 of 514 models, 19 to 21 seconds from the first pass until
   the last model is drawn (three runs); the old build got 511 and needed 33 seconds for its requests alone.
   mp_backlot2 939 of 939 in 26 to 32 seconds (three runs); the old build got 936 and needed 44 seconds for its
   requests alone.**

## 2. What was measured

| What | Where | Result |
|---|---|---|
| Owner's steps, old build: open the stock entity file of mp_rust | `out\propfix-a` | 1,079 entities, 7 model requests in total, no world, no props, 1,496 pixels drawn. The screenshot is the owner's screenshot |
| Owner's model cache at 20:47 | `...\model_cache` (read) | `index.json` last written 20:19; no batch folder `w`; the map was opened at 20:40. No prop was asked for |
| Old build, reference asked for (as View > Stock Map Reference does), empty cache | `out\propfix-before`, `out\propfix-old2` | 511 of 514 models, 3 failed; 33 s from the first model request to the last (26 s in the first run). The redraw comes after that (a 2.5 s timer and a pass of about 4 s); it was not timed |
| New build, same conditions | `out\propfix-new2`, `out\propfix-rust-final` | 514 of 514, 0 failed; 14 to 16 s from the first request to the last; 20.0, 18.9 and 20.9 s from the first pass until the last model is drawn |
| Old build, mp_backlot2, empty cache | `out\propfix-backlot-old` | 936 of 939, 3 failed; 44 s from the first request to the last |
| New build, mp_backlot2, empty cache | `out\propfix-backlot-new`, `out\propfix-backlot-final` | 939 of 939, 0 failed; 22 to 28 s from the first request to the last; 32.2, 26.1 and 25.5 s until the last model is drawn |
| New build, cache with 5 models marked failed and 19 removed | `out\propfix-failed` | 19 fetched in about 4 s, 108 red markers stay, label names the 5 and the reason; lit preview on: 0 markers, off again: 108 |
| New build, two models to fetch again while another program holds their old GLB open | `out\propfix-lock` | both end ready (the old files are kept); one of them was marked "cannot copy" in the index as in the owner's cache |
| Fetch chain against a cache that cannot deliver, no game | `out\propfix-chain` | both cases end as they should (section 6) |

The old and new runs of each map were made one after the other on the same machine, so the times can be compared. They
are for zones that Windows already had in its file cache; a first run after a restart is slower by the time it takes
to read the zone files (mp_rust.ff is 98 MB, mp_backlot2.ff more). F.17 quoted "about seven minutes" for mp_backlot2;
that was not seen again here with either build.

Every prop model of every cached stock map against the owner's cache (read only, 20:58): mp_rust 108 of 514 ready,
mp_backlot2 935 of 939, mp_shipment 50 of 277, mp_hideout 565 of 1,287, mp_emporium 223 of 1,328, mp_village2 206 of
945, mp_aniyah_tac 362 of 772, mp_m_fork 286 of 724, mp_m_king 53 of 191, mp_m_speedball 91 of 278, mp_donetsk 685 of
9,357. Only mp_backlot2 is complete, because it is the only map whose reference was ever left loaded long enough.

## 3. Causes, with evidence

### A. Opening a stock map never loads its world and props (NOT FIXED, one line, outside this task's files)

`MainWindow::openStockZone` (`src\qtapp\main_window.cpp`) exports the map's entities, calls `loadDocument`, sets
`stockZone_` and returns. `loadStockReference` is called from exactly two places: the menu command View > Stock Map
Reference and the smoke test variable `IW8R_SMOKE_REFERENCE`. Nothing connects the two.

Evidence:

- `out\propfix-a`: the old build opening the owner's file. The ACTS trace has 6 model requests for 7 models (the
  entities that name a model) and nothing else from the model cache. `screenshot.png` there is the owner's screenshot.
- The owner's cache: opening the map at 20:40 wrote `stock_cache\mp_rust\acts.log` (20:40:28) and nothing in
  `model_cache` (index 20:19, no `w` folder at 20:47). The two idle ACTS servers the main session saw were started for
  those 7 entity models and their textures, finished in seconds, and had nothing more to do.
- `out\propfix-before`: the same build, same file, plus `IW8R_SMOKE_REFERENCE=mp_rust`: the whole map is drawn.
- The F.17 check of mp_backlot2 was made with `IW8R_SMOKE_REFERENCE`, which is why it passed while the owner's steps
  did not.

The fix (for the main session, which owns that function): in `openStockZone`, after `stockZone_ = name;` add

```cpp
    loadStockReference(name);  // the map's own world and props (research 84)
```

Two things to decide with it: a stock entity file opened again from File > Open or the recent list goes through
`loadDocument` only (the zone name is in its path, `...\stock_cache\<zone>\...`), and opening an ordinary map after a
stock map leaves the old reference in place (View > Clear Stock Map Reference removes it).

`--stock-props-test` (section 6) opens the map with `openStockZone` and reports `reference_loaded_by_open=yes` or
`NO`. Today it says NO and then asks for the reference itself so that the rest can be measured.

### B. A model that was not there drew nothing, and nothing said so (fixed)

`rebuildReferenceProps` skipped a placement whose model was not in the cache. The only sign of work was a status-bar
message that the next message replaced after at most six seconds. A map with 405 of 514 models missing looked like a
map with a third of its props, with no hint that more would come.

### C. The fallback boxes were never visible (fixed)

A cached model that cannot be drawn as its mesh (unreadable file, or past the 4 million triangle budget) was drawn as
a box added to the flat reference triangle list. The camera does not draw that list while the textured reference is
set (`src\viewport\render.cpp`, the `else if` after the textured reference), and it is always set. The boxes only ever
reached the sun shadow pass. Roadmap 3.9 says "props as boxes"; that had not been true since the textured reference.

### D. "cannot copy the GLB into the cache" marked good models failed for good (fixed)

Four models in the owner's cache (`tm/decor_barbed_wire_bracket_01`, `tm/buildup_dirt_plaster_01_64`,
`tm/uk_awning_pole_01`, `tm/roof_vent_chimney_02`) are marked failed with this error, and ten of the eleven cached
maps use at least one of them. Their GLB files are in the cache (written 2026-11-10 21:33 and 22:58). The model was
fetched a second time later (its detail file is from 21:50 and 11:06 the next day), the old file could not be replaced
at that moment (something had it open: another editor process or a test run reading the same cache), and the model
was written to the index as failed. A failed model is never asked for again. In an empty private cache all four
export without trouble.

### E. Models whose LOD0 geometry is in another zone always failed (fixed)

Three models of mp_rust (`tm/uk_industrial_light_01_crash2_on`, `tm/ee_light_mounted_exterior_industrial_caged_02`
and its `_on` variant) and three of mp_backlot2 name their LOD0 surface buffer with a leading comma, for example
`,uk_industrial_light_01_lod000v011`. The comma is the game's mark for a reference: the buffer is not in this zone.
F.17 made such models fail alone (ACTS cannot be asked for a name with a comma). Checked here with ACTS: mp_rust has no
surface buffer of that name at all, and `common_base_mp` has it under the plain name, the zone where the sibling model
`uk_industrial_light_01` lives.

### F. The chain of batches depended on the camera redraw (fixed)

`rebuildReferenceProps` asked for 150 models, and the next 150 only when a `modelReady` signal arrived and a 2.5 second
timer ran out. Each batch therefore cost about four idle seconds. And when the model cache could not even start (a
tool path that does not exist: it drops the request and sends nothing), nothing ever asked again and nothing said why.
A batch in which every model failed did not stall: failed models are reported like others. That was checked, not
assumed, and it is now also covered by a test.

### G. Every redraw read every cached model file again (fixed)

Each pass read and converted every cached GLB on the UI thread: about four seconds for the 500 models of mp_rust by
the last pass. While it ran, no ACTS answer was handled, so the next round of requests waited (seen in the trace of
the first new build: a four second gap between the model tables arriving and the geometry being asked for).

### Checked and ruled out

- **Zones with a `.fp` patch file in server mode.** mp_rust has one. Its requests are answered in server mode like any
  other (trace: `mp_rust xmodel names=90 ... exit=0`, about half a second).
- **Requests dropped while another runs.** They are queued (`pending_`) and started when the batch ends.
- **Models only in the map's own zone.** `setFallbackZones` covers them. One gap closed: when it was called while a
  batch was running, older "not listed" failures were not forgotten; they are now, when that batch ends.

## 4. What changed

`src\qtapp\model_cache.h/.cpp`

- `startBatch`: a batch is at most `kBatchModels` (150) models; the rest stays queued and `finishBatch` starts the
  next batch at once. The chain is inside the cache and no longer needs a caller to ask again.
- New signal `batchEnded(int resolved)`, sent for every batch, also when nothing could start; `problem()` says why
  (a missing tool).
- `storeGlb` (new): when the old GLB cannot be replaced, it is kept and the model is good. `loadIndex` forgets the
  earlier "cannot copy" failures and the old comma failures, so the owner's four models heal on the next start.
- `buildReferenceRound`, `referenceZones` (new): a LOD0 named as a reference is asked for by its plain name in the
  zones of catalogue models whose name starts like the buffer's, one zone per round, eight at most.
- `setFallbackZones` during a batch: see above.
- `runModelCacheChainTest` (new): section 6.

`src\qtapp\main_window.h/.cpp` (only `rebuildReferenceProps`, `loadStockReference`, and two new functions they need:
`stockPropStatus`, `updateStockPropLabel`)

- Every missing model is asked for at once, most used first (`propFetchOrder`); the guard `PropFetchChain` says
  whether to ask, to wait, or that the cache has given up.
- Markers for placements without a model, boxes for models without a drawable mesh, all as instanced cubes handed to
  the camera with `setReferenceInstances` (no renderer or camera file was changed).
- A permanent label in the status bar.
- Models converted for drawing are kept between passes while models are still arriving, then dropped.
- The flat world list is handed to the camera once per reference, not copied and uploaded again on every pass.

`src\core\stock_prop_fetch.h/.cpp` (new, no Qt): the fetch order, the chain guard, failure reasons grouped, and the
label text. `tests\stock_prop_fetch_tests.cpp` (new): 8 tests.

`src\qtapp\main.cpp`: two headless modes, `--stock-props-test <zone>` and `--model-chain-test`.

## 5. How it looks

- **Label**, fixed at the right of the status bar while a reference is shown:
  - amber while fetching: `Stock props: 108 of 514 models ready, fetching 406 (a first open can take several
    minutes), 12 s` (the seconds count up);
  - plain when all are there: `Stock props: 514 of 514 models ready (fetched in 20 s)`;
  - red when some failed: `Stock props: 509 of 514 models ready, 5 failed (5: not in zone) (fetched in 4 s)`; the
    tooltip lists the failed models with their errors, and they are written to the Console once;
  - red when the fetch stopped: `Stock props: 108 of 514 models ready, 406 not fetched: Missing tool, model cache
    idle: <path>`.
- **Markers** in the camera: a 24 unit cube resting on the placement, amber for a model still being fetched, red for a
  failed one. `out\propfix-new2\screenshot-fetching.png` shows mp_rust one second after the world loaded (6,938 amber
  markers); `out\propfix-failed\screenshot.png` shows red ones.
- **Editor-only rule.** The camera draws every reference instance into the sun shadow map of the lit preview, and the
  renderer could not be changed in this task. So no marker is made while the lit preview is on (the label still says
  what is missing), and the props are redrawn when the lit preview is toggled (checked: 108 markers, 0 in the lit
  preview, 108 after it). Open point for the renderer's owner:
  markers are still part of the reference when reflection probes are captured with a reference shown; a "casts no
  shadow, not in captures" flag on `InstancedMesh` would make the rule hold in the renderer itself.

## 6. Tests

- Core: `tests\stock_prop_fetch_tests.cpp`, 8 tests (order, five chain cases including "nothing arrives" and "a
  whole batch fails", failure reasons, label text). All pass. The whole suite: 136,808 checks, 0 failures, 1,088
  tests at 21:07. At 21:28 the suite had 1,104 tests and 4 failures, all in `tests\prefab_bundle_tests.cpp`, a file
  another session was changing at that minute (it does not use anything changed here).
- `IW8Radiant.exe --model-chain-test --out <dir>`: needs no game. Case 1, no ACTS at all: the chain asks twice, then
  stops with the reason. Case 2, an "ACTS" that fails every run (a system program that rejects the arguments): 400
  models in three batches, every batch fails as a whole, the cache goes on by itself and all 400 end with an answer.
  Result in `out\propfix-chain\model-chain-test.txt`.
- `IW8Radiant.exe --stock-props-test <zone> --out <dir>`: opens the stock map the way the menu does, waits until no
  prop model is missing, writes counts, seconds, failed models with errors and two screenshots. With
  `IW8R_SMOKE_LIT=1` it also toggles the lit preview and reports the marker counts.

To run them again without touching the mapper's caches:

```
set IW8R_MODEL_CACHE_ROOT=E:\IW8-Radiant\out\propfix-x\mc
set IW8R_MATERIAL_CACHE_ROOT=E:\IW8-Radiant\out\propfix-x\mat
set IW8R_CAMERA=-1200 -1200 1400 -30 45
set IW8R_SMOKE_TEXTURED=1
out\propfix_bin\IW8Radiant.exe --stock-props-test mp_rust --out out\propfix-x
```

## 7. What is still not drawn, and why

- **Everything, when the map is opened with File > Open IW8 Stock Map**, until the line in 3.A is added.
- A model whose LOD0 is a reference to a zone that the name search does not find stays a red marker ("its LOD0
  surface buffer is in another zone, which was not found"). None on mp_rust or mp_backlot2.
- Models past the 4 million triangle budget of one reference draw as tan boxes of their size (now visible).
- mp_donetsk and other maps with streamed transient zones: their props in `_tr` zones are not covered (roadmap 3.9),
  and mp_donetsk was not run here (9,357 models).
- Shadow-only surfaces of models are left out on purpose, as before.

## 8. Not verified

- Nothing was checked in the owner's editor window; all checks are headless runs of a private copy.
- `--gui-test` was not run (not allowed while the owner's editor may be open). Its stock reference section expects
  prop meshes and at least as many GPU instances as prop meshes; both still hold by construction.
- In the screenshots many surfaces are still untextured: the runs used an empty private material cache and the
  textures were still streaming in when the picture was taken.
- Times after a cold start of the machine were not measured.
- Why exactly the old GLB could not be replaced in 3.D is inferred from the file times (a second fetch, an old file
  still in place); which program held the file is not known.
