# 85. Viewport performance: the camera keeps its geometry

Date: 2026-11-11. Owner request (their words): "some extreme optimizations to the viewport". This note is the
measurement, the step log and the picture check of that work on the 3D camera view. The game and the owner's editor
were not touched; the editor's caches were only read.

Files changed: `src/viewport/render.h/.cpp`, `scene.h/.cpp`, new `camera_geometry.cpp` and `dirty_ranges.h`;
`src/qtapp/camera_view.h/.cpp`; `src/qtapp/main.cpp` (the benchmark mode only); `src/qtapp/qt_gui_test.cpp` (new
checks only); new `tests/viewport_stream_tests.cpp`; new tools `make_perf_maps.py`, `run_viewport_bench.py`,
`compare_bench_images.py`, `viewport_bench_table.py`.

## 1. What changed, in one paragraph

Before, the camera rebuilt every vertex of the whole map whenever the document or the selection changed, and on
every frame it copied all outline vertices and uploaded them again. Now the camera keeps its geometry between frames
as vertex streams with one GPU buffer each (`CameraGeometry` in `scene.h`). An item whose geometry or selection state
changed is made again alone and written over its old vertices; only those bytes go to the GPU. A still frame uploads
nothing and allocates nothing. The picture is the same: every screenshot of every scene and mode is pixel-identical
to the one of the old renderer (section 6).

Results are in section 5 (table) and the step log is section 4.

## 2. How it is measured

`IW8Radiant.exe --viewport-bench <map file> --out <dir> [--reference <stock world export folder>]` (new, in
`main.cpp`). It runs like the GUI test: Qt's offscreen platform, settings in memory only, no window is shown or
activated. It opens the map, renders at 1920 x 1080 and writes `<dir>/viewport-bench.txt` and screenshots.

- Every scenario runs 120 frames (`IW8R_BENCH_FRAMES`). Per frame it records: **cpu** = time inside
  `CameraView::render()`; **total** = render() plus the wait until the GPU has finished the frame (an event query);
  **sync** = `SceneCache::sync` after a document change (viewport code); **other** = the document command and the other
  panels' refresh (not viewport code, listed apart). Median and worst are reported.
- Scenarios: `still` (a camera that sees the whole map), `still_inside` (a camera inside it), `moving` (a circle around
  the map, then a flight through it), `select` (select one item / deselect, one change per frame), `edit` (move one
  brush by 16 units per frame, a real undoable edit), `toggle_modes` (textured, lit, wireframe in turn), and the same
  in the lit preview and wireframe (`still_lit`, `select_lit`, `edit_lit`, `still_wireframe`, `wireframe_inside`; the
  last three were added during the work, so the baseline has no `select_lit` and `wireframe_inside`).
- Counters per frame: triangles, lines, textured batches, draw calls, bytes uploaded; and the peak memory of the run.
- Screenshots: textured, lit, wireframe and flat from both fixed cameras, a selection from both, and a selected face
  (11 per scene). The wind and effect clocks are fixed so two runs draw the same frame.
- After every scenario the benchmark checks itself: the kept geometry must equal a fresh `buildCameraGeometry` of the
  same scene byte for byte, and the frame must equal the frame after a forced full rebuild (`check_<scenario>` lines).
- `IW8R_BENCH_PROFILE=1` adds section timers (`IW8R_PROF` in `render.h`); `IW8R_PERF_OFF=<names>` turns single
  optimisations off again on the same build (section 4).

Scenes (`python -I tools/make_perf_maps.py` writes them under `out/perf/`; `python -I tools/run_viewport_bench.py
--exe <exe> --out <dir>` runs them all):

| scene | what |
|---|---|
| sample_room | `tests/data/iw8_sample_room.map`: 17 items |
| fix1 | a copy of the owner's test map (`out/perf/fix1`, the original is only read): 50 items, 126,000 triangles of stock models, 106 textures |
| synth_2000 / 10000 / 50000 | 2,000 / 10,000 / 50,000 brushes as small buildings in no spatial document order, 200 / 300 / 500 point entities (40 lights among them) and 500 / 1,500 / 3,000 `misc_model`s of four stock models the fix1 map uses (90 percent traffic cones of 1,004 triangles, the rest 9,000 to 11,000 triangles each); 24 stock materials. 0.95 / 2.7 / 6.2 million triangles |
| reference | the sample room with the cached stock world of `mp_backlot2` under it (363,000 triangles, 1,579 materials) |

The editor's caches are not written. The benchmark reads copies of just the models and materials these maps use
(`out/perf/cache`, made by `make_perf_maps.py`; `IW8R_MODEL_CACHE_ROOT` / `IW8R_MATERIAL_CACHE_ROOT`), runs no
tool (the ACTS path is set to a file that does not exist) and no autosave. The stock world is read straight from the
cached export with core `readStockWorld`, without the world cache object. Its 1,579 materials are not in the scratch
copy, so the reference draws with the plain placeholder texture and its materials stay "waiting" for the whole run
(which is the state the editor is in while a reference's textures are still being exported).

Machine: Intel Core i7-12700F, 16 GB RAM, NVIDIA GeForce RTX 5060 (driver 32.0.16.1692), Windows 11. Other agents and
the owner's editor were running on the same machine during the measurements, so single numbers move by tens of percent
between runs; the changes below are far larger than that. The before / after table of section 5 was measured with
nothing else of this task running.

## 3. Baseline, before any optimisation

The baseline build has only the benchmark mode and the counters added (exe copy `out/perf_bin_base`).

Total time of a frame in ms, median / worst of 120 frames (render() plus the GPU; for `select` and `edit` plus the scene
cache sync):

| scenario | sample_room | fix1 | synth_2000 | synth_10000 | synth_50000 | reference |
|---|---|---|---|---|---|---|
| still | 0.62 / 7.20 | 8.07 / 13.2 | 95.7 / 138 | 67.0 / 127 | 114 / 167 | 1.94 / 2.74 |
| still_inside | 0.56 / 8.53 | 7.82 / 13.5 | 54.5 / 74.8 | 25.8 / 48.4 | 44.1 / 52.0 | 1.92 / 2.66 |
| moving | 0.57 / 7.22 | 8.26 / 13.1 | 67.6 / 116 | 46.3 / 80.6 | 83.9 / 135 | 1.91 / 2.74 |
| select | 0.93 / 8.03 | 55.6 / 66.6 | 593 / 1022 | 1073 / 1635 | 2434 / 4787 | 2.21 / 2.67 |
| edit | 1.34 / 8.57 | 124 / 265 | 379 / 574 | 1018 / 1817 | 2351 / 10655 | 2.26 / 4.26 |
| toggle_modes | 0.86 / 8.73 | 76.2 / 154 | 370 / 517 | 994 / 2798 | 2304 / 5948 | 2.03 / 3.69 |
| still_lit | 0.76 / 6.92 | 8.32 / 15.2 | 28.6 / 33.2 | 67.4 / 136 | 124 / 160 | 2.03 / 3.89 |
| edit_lit | 3.26 / 10.5 | 75.1 / 99.2 | 525 / 868 | 1474 / 3282 | 3595 / 8248 | 2.59 / 3.89 |
| still_wireframe | 0.42 / 8.50 | 7.91 / 17.1 | 59.6 / 102 | 170 / 201 | 366 / 374 | 1.74 / 2.62 |

CPU time inside `render()` alone, ms, median / worst:

| scenario | sample_room | fix1 | synth_2000 | synth_10000 | synth_50000 | reference |
|---|---|---|---|---|---|---|
| still | 0.13 / 0.33 | 0.63 / 1.22 | 80.0 / 124 | 57.7 / 117 | 80.9 / 134 | 1.53 / 1.88 |
| still_inside | 0.11 / 0.34 | 0.69 / 1.40 | 52.1 / 74.3 | 24.8 / 46.9 | 40.5 / 48.4 | 1.53 / 1.70 |
| moving | 0.11 / 0.29 | 0.71 / 1.33 | 64.9 / 114 | 40.6 / 73.1 | 65.3 / 108 | 1.53 / 1.89 |
| select | 0.20 / 0.56 | 50.0 / 62.8 | 578 / 1003 | 1054 / 1617 | 2381 / 4674 | 1.73 / 2.10 |
| edit | 0.21 / 0.43 | 118 / 237 | 375 / 566 | 1005 / 1792 | 2300 / 10538 | 1.73 / 3.54 |
| toggle_modes | 0.21 / 0.57 | 63.6 / 145 | 367 / 513 | 984 / 2789 | 2271 / 5915 | 1.59 / 1.86 |
| still_lit | 0.15 / 0.32 | 0.93 / 1.43 | 23.5 / 28.4 | 58.0 / 127 | 91.5 / 127 | 1.53 / 1.74 |
| edit_lit | 0.32 / 0.61 | 59.9 / 86.2 | 521 / 863 | 1457 / 3262 | 3545 / 8154 | 1.81 / 2.39 |
| still_wireframe | 0.02 / 0.08 | 2.70 / 4.39 | 13.1 / 44.8 | 36.8 / 53.8 | 76.0 / 84.6 | 1.44 / 1.63 |

The still scenarios again, measured later right before the new build (`out/perf/base_quiet`), total ms. The first
run shared the machine with builds of other agents, and its still rows of fix1 and synth_2000 (8 and 96 ms) are too
high; this table is the fair one for still frames:

| scenario | sample_room | fix1 | synth_2000 | synth_10000 | synth_50000 | reference |
|---|---|---|---|---|---|---|
| still | 0.34 / 0.63 | 1.44 / 1.68 | 10.0 / 17.1 | 66.5 / 73.0 | 165 / 176 | 1.96 / 2.46 |
| still_inside | 0.29 / 0.52 | 1.20 / 1.75 | 3.65 / 4.23 | 24.6 / 26.0 | 58.7 / 65.2 | 1.85 / 2.67 |
| moving | 0.30 / 0.63 | 1.31 / 1.87 | 7.18 / 15.0 | 46.6 / 56.1 | 124 / 144 | 1.89 / 2.30 |
| still_lit | 0.39 / 0.61 | 1.51 / 2.17 | 11.2 / 16.2 | 66.7 / 71.4 | 161 / 238 | 1.99 / 2.25 |
| still_wireframe | 0.26 / 0.42 | 3.72 / 3.97 | 58.2 / 61.9 | 164 / 185 | 370 / 418 | 1.67 / 2.07 |

Loading and memory:

| | sample_room | fix1 | synth_2000 | synth_10000 | synth_50000 | reference |
|---|---|---|---|---|---|---|
| items | 17 | 50 | 2704 | 11806 | 53531 | 17 |
| open, ms | 40 | 292 | 1654 | 2952 | 13087 | 33 |
| first frame, ms | 17 | 94 | 607 | 952 | 6470 | 170 |
| peak working set, MB | 266 | 621 | 1686 | 3747 | 8100 | 640 |
| peak committed, MB | 322 | 797 | 1938 | 4140 | 8762 | 756 |

What a frame draws (camera `a`, the whole map in view):

| scene | view | triangles | lines | textured batches | draw calls |
|---|---|---|---|---|---|
| sample_room | textured | 190 | 398 | 4 | 6 |
| sample_room | wireframe | 0 | 398 | 0 | 1 |
| sample_room | flat | 190 | 398 | 0 | 2 |
| fix1 | textured | 90,472 | 843 | 55 | 52 |
| fix1 | wireframe | 0 | 376,713 | 0 | 2 |
| fix1 | flat | 125,612 | 843 | 0 | 3 |
| synth_2000 | textured | 713,890 | 52,988 | 32 | 55 |
| synth_2000 | wireframe | 0 | 2,816,714 | 0 | 1 |
| synth_2000 | flat | 947,362 | 52,988 | 0 | 2 |
| synth_10000 | textured | 2,066,216 | 247,488 | 32 | 78 |
| synth_10000 | wireframe | 0 | 7,931,892 | 0 | 1 |
| synth_10000 | flat | 2,684,712 | 247,488 | 0 | 2 |
| synth_50000 | textured | 4,757,066 | 1,213,118 | 32 | 99 |
| synth_50000 | wireframe | 0 | 18,072,962 | 0 | 1 |
| synth_50000 | flat | 6,225,482 | 1,213,118 | 0 | 2 |
| reference | textured | 258,036 | 398 | 4 | 722 |
| reference | wireframe | 0 | 398 | 0 | 1 |
| reference | flat | 258,036 | 398 | 0 | 718 |

Where the time went (section timers on the baseline build, synth_10000, per frame; `out/perf/base_prof`):

- **Still frame, 56 ms of CPU.** 55 ms of it is one call: the 495,000 outline vertices (7.9 MB) are mapped with
  "discard" and copied to the GPU again on every frame. Copying the line vectors first costs 0.5 ms. Parsing the
  worldspawn atmosphere twice and the wind once, the LUT file time and listing the probe folder cost 0.24 ms. The four
  things the brief listed are all there; the upload is the one that matters.
- **Still frame in wireframe.** With models every triangle edge is a line: 7.9 million lines, 254 MB. 18 ms to copy
  them, 19 ms to upload them, every frame, and then the GPU needs 130 ms to draw them.
- **A selection change or an edit, about 1,000 ms.** 650 ms in `buildCameraGeometry` (every vertex of every item is
  made again, with five string-keyed map lookups per face), 312 ms in `draw()` (all batches copied into one buffer,
  the spatial order of every big batch sorted again, 400 MB uploaded), 57 ms for the lines. The scene cache sync adds
  4.5 ms: `Document::select` bumps the document revision, so the sync runs on selection changes too.
- **Lit preview edit, 1,460 ms.** As above, plus a collision world of the whole map built on every change and not
  used (the shadow map does the sun shadows), plus the sun shadow map drawn again.

## 4. Step log

Numbers are medians on synth_10000 unless a scene is named. "Switch" is the `IW8R_PERF_OFF` name that turns the step
off again on the final build (`out/perf/off_<name>`), so its share is measured on otherwise equal code.

**Step 0. The benchmark, counters and timers.** No change of behaviour. Baseline recorded (section 3).

**Step 1. The camera keeps its geometry; an item is written in place.** New `CameraGeometry` (`scene.h`,
`camera_geometry.cpp`) and `SceneRenderer::drawFrame` (`render.cpp`).

- The geometry is kept as vertex streams in the order `buildCameraGeometry` makes them: flat triangles (shadow-only,
  casters, tool faces and markers), outline lines, and one stream per textured batch. Each item has a slice in every
  stream it is in. Each stream has its own GPU buffer and remembers which vertex ranges changed; the renderer uploads
  only those (`UpdateSubresource` with a byte range).
- `SceneCache::sync` stamps every item it builds. An update compares stamp and selection state per item. A changed
  item is made again alone. Same vertex counts: its slices are overwritten (a moved brush, a selection change).
  Other counts, or items added or removed: only the streams those items are in are put together again, from the old
  slices and the new item (a new brush touches the lines and its material's batch, not the models). A change of the
  view settings (mode, lit preview, lights, sun, textures arriving, a capture) makes everything again.
- A selected item draws its outlines on top. Its place in the depth-tested lines is kept as zero-length lines, so
  selecting and deselecting move nothing.
- Small things that belong to no item (sun arrow, face selection, tags, probe volumes, navigation, spawns, axes) are
  small streams, made again on a change and replaced only when they differ. The gizmo is a stream of its own.
- No per-frame copy of the line and overlay vectors any more, and no upload in a frame where nothing changed.
- Big batches (over 4,096 triangles) keep their frustum culling: the spatial order is now an index buffer over
  vertices that stay in item order. Vertices written in place grow the boxes of their chunks; the chunks are made
  again when slices move or a quarter of the batch has grown boxes.
- The lit preview no longer builds a collision world that the shadow map makes unnecessary.
- Result: `select` 1,073 -> 11.1 ms, `edit` 1,018 -> 12.9 ms, `edit_lit` 1,474 -> 14.7 ms, `still` 66.5 -> 10.5 ms
  (CPU in render() 57 -> 0.06 ms). With switch `inplace` (the new renderer, but everything made again on each change):
  `select` 268 ms, `edit` 262 ms. So the new full build alone is 4 times faster than the old one, and writing in place
  is another 23 times. Uploads per change: 62 KB for a selection (a model is selected every fourth time), 3.7 KB for a
  moved brush, 0 bytes in a still frame.
- Picture: identical (section 6).

**Step 2. The scene cache is not synced again when nothing drawn changed.** `SceneCache::sync` first hashes the id and
rev of every item (no allocation). When that is what the last sync saw, it returns: a selection change costs 0.06 ms
instead of 1.94 ms (switch `syncfast`; at 50,000 brushes 0.34 instead of 20 ms). For real edits the sync tries the
previous list position before the hash lookup and skips the pass that drops dead items when there are none: 4.3 ->
2.0 ms (50,000 brushes: 18 -> 6.2 ms).

**Step 3. Worldspawn settings per document revision, files now and then.** Atmosphere and wind are parsed when the
document revision or path changes. The LUT file time, the probe folder and the sky source are looked at when the
document changes, when a frame follows a pause of 100 ms, after a capture or `invalidateGeometry()`, and at most once
a second while frames follow each other; the nearest probe is picked from the scanned list. Still-frame CPU 0.28 ->
0.06 ms (switch `settings`). On fix1, which has captured probes, the probe folder alone was 0.28 ms of a 0.29 ms frame.

**Step 4. The sun shadow map only when a caster or the sun changed.** The geometry counts caster changes (an item
whose stamp changed, a relayout, a full build); a selection change is not one. `select_lit`: 12.7 -> 11.7 ms and 120 ->
0 shadow passes in 120 selection changes (switch `shadow`). A change of the stock reference marks the map dirty too
(before, it stayed stale until the next rebuild).

**Step 5. Waiting reference materials are asked twice a second, not every frame.** While stock reference textures
are not cached yet, every frame asked for each of them again (1,579 lookups on mp_backlot2) and asked for another
frame, so the editor redrew at monitor rate. Now they are asked when an image arrived (`materialImagesChanged`), on
the next frame while the per-frame load budget is the limit, and otherwise every 500 ms. reference `still`: CPU 1.39 ->
0.04 ms, total 1.80 -> 0.44 ms (switch `refpoll`).

**Step 6. No heap allocation in a still frame.** The draw order, the blended sort (a small insertion sort, stable as
before), the visible chunk runs (`forVisibleRuns` instead of a vector per batch per frame), the pixel light list and
the blended batches' centres (computed when a batch changes, not per frame) use kept storage. Not measurable apart
from step 1; a still frame at 50,000 brushes is 0.15 ms of CPU in total.

**Step 7. Chunks survive a full rebuild that moved nothing.** The renderer hashes the positions of a big batch; when
everything was made again but the hash is the same (lit preview on or off, a texture arriving), the chunk order and
its index buffer are kept. This is part of why the full build in step 1 is 268 and not 1,073 ms (the old renderer
spent 312 ms per rebuild in concatenation, sorting and upload). Not measured apart.

**Step 8. Memory.** The benchmark found a leak of the first version: buffers of batches that were gone were released
only every 256 frames, and `toggle_modes` made new batches on every frame: 9.6 GB committed on synth_2000 (the old
renderer: 1.9 GB). Now a buffer that a frame did not use is released at the end of that frame, an empty stream gives
its buffer back, a buffer four times too big is replaced, and vectors of a bigger earlier build are shrunk. synth_2000
after the fix: 1.7 GB. Final numbers are in section 5.

**Step 9. Edits at 50,000 brushes.** After the steps above the largest CPU items of an edit were the box around all
items (1.6 ms, a pass over 53,000 boxes) and the sync (10.9 ms). The scene box is now the union of exact boxes of runs
of 16 items (a moved item costs its run), and the sync got the order hint of step 2. `edit` at 50,000 brushes: CPU in
render() 6.5 -> 2.9 ms, sync 11.5 -> 6.2 ms.

## 5. Before and after

"Before" is the baseline build, "after" the final build. The still scenarios of both were measured back to back
(`out/perf/base_quiet`, `out/perf/new_quiet`); `select`, `edit`, `edit_lit` and `toggle_modes` of the baseline are from
its first full run (`out/perf/base`), which took 45 minutes (fix1: measured again without other load,
`out/perf/base_quiet2`; in the first run its edit showed 124 ms). Total = render() + GPU + scene sync; CPU = render() +
scene sync.

| scene | scenario | before: total ms (median / worst) | after: total ms (median / worst) | faster | before: CPU ms | after: CPU ms |
|---|---|---|---|---|---|---|
| fix1 | still | 1.44 / 1.68 | 0.48 / 1.51 | 3x | 0.26 | 0.02 |
| fix1 | still_inside | 1.20 / 1.75 | 0.36 / 0.62 | 3x | 0.25 | 0.01 |
| fix1 | moving | 1.31 / 1.87 | 0.43 / 0.84 | 3x | 0.24 | 0.01 |
| fix1 | select | 45.0 / 48.0 | 0.59 / 3.48 | 77x | 42.0 | 0.06 |
| fix1 | edit | 44.6 / 47.5 | 0.78 / 1.21 | 57x | 41.7 | 0.23 |
| fix1 | edit_lit | 47.2 / 60.4 | 0.96 / 1.11 | 49x | 40.9 | 0.25 |
| fix1 | toggle_modes | 39.8 / 51.0 | 14.2 / 20.7 | 3x | 36.7 | 11.7 |
| fix1 | still_wireframe | 3.72 / 3.97 | 2.32 / 2.76 | 2x | 1.23 | 0.01 |
| synth_10000 | still | 66.5 / 73.0 | 10.5 / 10.9 | 6x | 57.2 | 0.05 |
| synth_10000 | still_inside | 24.6 / 26.0 | 1.55 / 1.75 | 16x | 23.7 | 0.03 |
| synth_10000 | moving | 46.6 / 56.1 | 7.00 / 7.94 | 7x | 41.7 | 0.04 |
| synth_10000 | select | 1073 / 1635 | 11.1 / 16.1 | 97x | 1058 | 0.49 |
| synth_10000 | edit | 1018 / 1817 | 12.9 / 14.0 | 79x | 1009 | 2.25 |
| synth_10000 | edit_lit | 1474 / 3282 | 14.7 / 15.4 | 100x | 1461 | 2.31 |
| synth_10000 | toggle_modes | 994 / 2798 | 447 / 776 | 2x | 984 | 363 |
| synth_10000 | still_wireframe | 164 / 185 | 131 / 132 | 1x | 33.0 | 0.14 |
| synth_50000 | still | 165 / 176 | 35.2 / 37.2 | 5x | 132 | 0.12 |
| synth_50000 | still_inside | 58.7 / 65.2 | 4.62 / 4.90 | 13x | 55.2 | 0.10 |
| synth_50000 | moving | 124 / 144 | 27.6 / 30.0 | 4x | 99.3 | 0.10 |
| synth_50000 | select | 2434 / 4787 | 38.5 / 48.8 | 63x | 2401 | 3.05 |
| synth_50000 | edit | 2351 / 10655 | 44.4 / 46.4 | 53x | 2318 | 9.01 |
| synth_50000 | edit_lit | 3595 / 8248 | 47.2 / 60.9 | 76x | 3563 | 8.34 |
| synth_50000 | toggle_modes | 2304 / 5948 | 1028 / 2673 | 2x | 2271 | 884 |
| synth_50000 | still_wireframe | 370 / 418 | 291 / 292 | 1x | 79.4 | 0.19 |

Every scene and scenario after the change, total ms, median / worst (`out/perf/new`):

| scenario | sample_room | fix1 | synth_2000 | synth_10000 | synth_50000 | reference |
|---|---|---|---|---|---|---|
| still | 0.29 / 0.56 | 0.53 / 0.76 | 3.17 / 3.41 | 10.6 / 10.8 | 35.3 / 36.5 | 0.42 / 0.71 |
| still_inside | 0.23 / 0.45 | 0.40 / 0.48 | 0.73 / 0.92 | 1.57 / 1.83 | 4.59 / 4.88 | 0.35 / 0.60 |
| moving | 0.26 / 0.51 | 0.45 / 0.67 | 2.23 / 2.63 | 7.11 / 7.99 | 27.9 / 46.6 | 0.39 / 0.68 |
| select | 0.35 / 0.55 | 0.59 / 3.48 | 3.41 / 31.7 | 11.1 / 16.1 | 38.5 / 48.8 | 0.49 / 0.70 |
| edit | 0.41 / 0.49 | 0.78 / 1.21 | 4.06 / 6.68 | 12.9 / 14.0 | 44.4 / 46.4 | 0.55 / 0.75 |
| toggle_modes | 0.36 / 0.74 | 14.2 / 20.7 | 164 / 441 | 447 / 776 | 1028 / 2673 | 0.51 / 1.83 |
| still_lit | 0.33 / 0.38 | 0.56 / 0.96 | 3.48 / 3.72 | 11.0 / 11.2 | 36.0 / 37.1 | 0.53 / 0.67 |
| select_lit | 0.41 / 0.48 | 0.67 / 2.53 | 3.64 / 7.69 | 11.5 / 14.6 | 38.8 / 43.0 | 0.61 / 0.77 |
| edit_lit | 0.52 / 0.60 | 0.96 / 1.11 | 4.92 / 7.76 | 14.7 / 15.4 | 47.2 / 60.9 | 0.85 / 2.49 |
| still_wireframe | 0.25 / 0.41 | 2.30 / 2.36 | 46.8 / 47.2 | 131 / 132 | 292 / 294 | 0.28 / 0.41 |
| wireframe_inside | 0.19 / 0.31 | 2.12 / 2.17 | 21.7 / 37.0 | 48.4 / 48.7 | 113 / 114 | 0.20 / 0.25 |

CPU time inside `render()` after the change, ms, median / worst:

| scenario | sample_room | fix1 | synth_2000 | synth_10000 | synth_50000 | reference |
|---|---|---|---|---|---|---|
| still | 0.01 / 0.14 | 0.02 / 0.16 | 0.03 / 0.18 | 0.04 / 0.31 | 0.06 / 1.28 | 0.04 / 0.20 |
| still_inside | 0.01 / 0.13 | 0.01 / 0.04 | 0.02 / 0.10 | 0.03 / 0.16 | 0.04 / 0.20 | 0.03 / 0.17 |
| moving | 0.00 / 0.14 | 0.01 / 0.17 | 0.02 / 0.20 | 0.03 / 0.22 | 0.14 / 2.51 | 0.04 / 0.20 |
| select | 0.04 / 0.15 | 0.06 / 1.95 | 0.17 / 28.1 | 0.43 / 2.45 | 2.63 / 9.89 | 0.07 / 0.23 |
| edit | 0.04 / 0.05 | 0.05 / 0.09 | 0.13 / 0.78 | 0.39 / 0.61 | 2.89 / 3.64 | 0.07 / 0.21 |
| toggle_modes | 0.06 / 0.10 | 11.7 / 19.3 | 142 / 420 | 363 / 737 | 884 / 2561 | 0.10 / 1.53 |
| still_lit | 0.00 / 0.02 | 0.01 / 0.04 | 0.02 / 0.15 | 0.03 / 0.25 | 0.07 / 0.98 | 0.03 / 0.06 |
| select_lit | 0.04 / 0.07 | 0.06 / 1.19 | 0.12 / 2.50 | 0.44 / 2.37 | 2.29 / 4.87 | 0.09 / 0.23 |
| edit_lit | 0.04 / 0.08 | 0.06 / 0.08 | 0.14 / 0.49 | 0.40 / 0.69 | 2.60 / 11.4 | 0.08 / 1.63 |
| still_wireframe | 0.00 / 0.01 | 0.01 / 0.02 | 0.03 / 0.18 | 0.14 / 0.23 | 0.14 / 0.34 | 0.01 / 0.02 |
| wireframe_inside | 0.00 / 0.01 | 0.01 / 0.02 | 0.03 / 0.16 | 0.02 / 0.14 | 0.14 / 0.28 | 0.00 / 0.03 |

Loading and memory after the change:

| | sample_room | fix1 | synth_2000 | synth_10000 | synth_50000 | reference |
|---|---|---|---|---|---|---|
| items | 17 | 50 | 2704 | 11806 | 53531 | 17 |
| open, ms | 29 | 163 | 753 | 1979 | 6209 | 31 |
| first frame, ms | 14 | 30 | 72 | 171 | 463 | 171 |
| peak working set, MB | 285 | 554 | 1283 | 2639 | 5027 | 639 |
| peak committed, MB | 340 | 783 | 1721 | 3520 | 6783 | 755 |

What is left in these numbers:

- A still frame is now GPU time. At 50,000 brushes the camera that sees the whole map draws 4.76 million triangles and
  1.2 million anti-aliased lines in 35 ms, of which the lines are about 20 ms (the wireframe view, 18 million lines,
  needs 291 ms: 16 ns per line). Inside the map it is 4.6 ms.
- `toggle_modes` still makes everything again: 1.0 s at 50,000 brushes (2.3 s before).
- The rest of a document change that is not viewport code (the document command and the other panels' refresh,
  "other" in the reports) is 13 to 15 ms at 50,000 brushes: now more than the viewport's own 9 ms of CPU.

## 6. The picture

- `tools/compare_bench_images.py out/perf/base out/perf/new`: all 66 screenshots (6 scenes x 11: textured, lit,
  wireframe and flat from two cameras, a selection from two cameras, a selected face) are **pixel-identical** between
  the baseline build and the final build. Largest difference: 0.
- Inside every benchmark scenario of every scene (60 checks): after 120 selections, edits or mode changes the kept
  geometry equals a fresh `buildCameraGeometry` byte for byte, and the frame equals the frame after a forced full
  rebuild. All "same".
- The GUI test repeats both checks in five view modes after: selecting, moving, deselecting, adding a brush, deleting
  it, undoing everything, and selecting a face.
- A difference that can exist and was not seen: inside one big textured batch (over 4,096 triangles) the draw order
  is the chunk order of the last time the chunks were made. After a brush was moved in place, a build from scratch
  sorts it into another chunk, so two faces of different items that lie exactly in the same plane and overlap could
  swap which one wins. The forced-rebuild comparisons above include such moves and were identical.
- Editor-only markers, tool faces and volumes are still a stream of their own that the sun shadow pass never draws and
  that clean captures leave out; the GUI checks of that (shadow caster counts, clean capture, probe capture) pass.

## 7. Tests

- Core: `bin\release\iw8r_tests.exe`: 139,134 checks, 0 failures, 1,112 tests, no "SKIPPED" line. New file
  `tests/viewport_stream_tests.cpp` (the changed-range set of a stream: merging, order, the cap of eight ranges; the
  renderer's visible-run loop against core `visibleDrawRanges`). The viewport library is not linked into the core
  tests, so the tested helpers are header only (`src/viewport/dirty_ranges.h`).
- GUI: `bin\release\IW8Radiant.exe --gui-test tests\data\iw8_sample_room.map --out out\gui-perf`: 845 checks, 0 failures.
  The game was not running (`tasklist | findstr game_dx12` empty), so the Live Link check passed as well. New section
  "camera geometry kept between frames": in flat, textured, lit, wireframe and contents mode it checks that a still
  frame uploads 0 bytes, that selecting an item writes one item in place with 0 rebuilds and without a new scene
  cache generation, that a selection does not ask for the shadow map again and a moved brush does, that moving a
  brush uploads a few KB, that a new brush makes no other item again, and after each step the two picture checks of
  section 6.
- Counters for tests: `CameraView::geometryBuilds()` (full builds), `geometryRelayouts()`, `itemsUpdatedInPlace()`,
  `geometryUpdates()`, `shadowCasterVersion()`, `frameStats()`, `geometrySelfCheck()`.

## 8. Tried and dropped, and what is left

Tried and taken out again:

- **Frustum culling of the map's lines, flat triangles and small batches** (boxes around runs of 16 items, drawn as
  runs in stream order so the picture stayed the same). Measured with a switch on three scenes: inside the 50,000
  brush map 4.89 ms with it and 4.70 ms without, flying through it 27.8 / 28.0 ms, wireframe from inside 112.5 / 112.7
  ms; the same on synth_10000 and fix1. The GPU throws away what is off screen cheaply, and 700 extra draw calls cost
  what the skipped vertices saved. Removed. (The existing chunk culling of big batches and of the stock reference is
  unchanged.)
- **Releasing unused buffers every 256 frames**: see step 8.
- **Looking at the files on every document change**: a selection change then cost 0.3 ms of folder listing on fix1.
  Narrowed to the rule of step 3.

Looked at and not done:

- **Instancing of the map's own models.** In synth_50000, 5.6 of 6.2 million triangles are 3,000 placements of four
  models, baked per placement: about 800 MB of vertices in memory and again on the GPU, and most of the 1.0 s of a
  mode change. Instancing would transform the vertices on the GPU in single precision and could not reproduce the
  per-face lit colours, so the picture would change by more than nothing. It needs the owner's decision on what
  difference is acceptable.
- **Index buffers for shared vertices, sorting by texture.** Brush and model vertices carry a per-face colour, normal
  and tangent, so nothing is shared; batches are already one per texture (32 to 55 batches, 0.06 ms per frame).
- **The present path** (off-screen target copied to the swap chain, `Present(1, 0)`): not measurable on the offscreen
  platform, and captures need the off-screen target.

Next, in the order of what the numbers say:

1. Outline lines are the largest cost of a still frame on big maps (20 of 35 ms; wireframe 291 ms). Anti-aliased lines
   are slow on the GPU. Dropping or thinning outlines far from the camera would be the fix, and it changes the
   picture: a decision for the owner.
2. Instancing of repeated models (above), together with a compact model form in `PrimGeom` (now one small vector and
   one string per triangle: 6.5 s to open the 50,000 brush map, 5 GB).
3. Mode changes: keep the vertex positions and write only the colours.
4. The non-viewport share of a document change (13 to 15 ms at 50,000 brushes) is now the largest part of an edit's
   CPU time; it is in files this task did not own.

## 9. How to run it again

```
python -I tools/make_perf_maps.py
python -I tools/run_viewport_bench.py --exe bin/release/IW8Radiant.exe --out out/perf/new [--profile] [--scenes ...] [--only still,select]
python -I tools/compare_bench_images.py out/perf/base out/perf/new --diff-dir out/perf/diff
python -I tools/viewport_bench_table.py out/perf/base out/perf/new
```

Run one benchmark at a time: the 50,000 brush scene needs several GB of memory (8.8 GB committed in the old
renderer).

## 10. Prefab models, marker flag, scriptable model (2026-11-12)

Three renderer changes on top of the kept geometry; nothing of sections 1 to 9 was undone, and every new vertex goes
through the same per-item path (`genItem` in `camera_geometry.cpp` and `buildCameraGeometry` in `scene.cpp` make the
same vertices; the GUI and benchmark self-checks pass).

- **Models inside placed prefabs** (`scene.cpp`: `addPrefabContents`, `buildPrefab`, `prefabSourcesChanged`,
  `appendModelPolys` split out of `buildModelGeom`). A prefab's `misc_model`, `script_model` and `dynamic_prop`
  entities are drawn with the mesh and materials a model placed in the map gets, at the origin and angles
  `instantiatePrefab` gives them and their own `modelscale` (the build keeps an inner `modelscale` as written: the
  placement's `modelscale` moves inner models but does not resize them, so the picture follows the build). Prefabs
  inside prefabs are expanded to 8 levels as `flattenPrefabs` does (scales multiply, the outer material swaps and key
  overrides apply after the inner ones). The prefab item keeps its polys in three runs (`PrimGeom::meshFrom`,
  `markerFrom`): brush and patch faces with outlines, model triangles without outlines unless selected or in
  wireframe (as `mesh`), and boxes of the other inner point entities. A model that is not loaded yet is such a box;
  the item records what each inner model and prefab resolved to (`PrimGeom::deps`), and a full sync (the
  `invalidate()` the shell already does on `modelReady`) makes the prefab again only when one of them resolves
  differently. The prefab is still one item, selected and picked by its bounds.
- **Inner entity boxes are editor markers**: on screen only, no sun shadow (`noCast` stream), not in probe captures.
  Before, they were shadow casters and appeared in captures.
- **`dynamic_prop`** placed directly now draws its mesh too (it was a box; the build places it as a model). In fix1
  this is one traffic cone: 992 more triangles, 24 fewer outline lines.
- **Reference marker flag** (`render.h` `InstancedMesh::editorMarker`, `reference_markers.h`
  `drawsReferencePiece` / `referenceVertices`, `FrameInput::capture`). The amber and red cubes of stock props whose
  model is missing or failed are marked in `MainWindow::rebuildReferenceProps`; the renderer draws marked meshes on
  screen only, never in the sun shadow pass, a probe capture (`SceneCache::captureMode`) or a clean capture. The
  existing rule that no marker is made while the lit preview is on is unchanged.
- **Scriptable model** (`scriptable_model.h` `scriptableModelName` / `withModelKey`, `SceneCache::scriptableModelFor`,
  set by `CameraView` from `ScriptableCatalog::shared`). A `scriptable` without a model key whose definition has a
  catalogue model is drawn exactly as a `misc_model` with that model key; otherwise it stays a box. Inside prefabs too.

Checks: core `tests/viewport_scene_keys_tests.cpp` (`scriptable_model_name_from_catalogue`,
`reference_markers_left_out_of_captures_and_shadows`): 139,154 checks, 0 failures, 1,114 tests. GUI
`src/qtapp/qt_gui_test_prefab_models.cpp` (three sections: a scene cache of its own with in-memory models, the window
with a custom OBJ in a prefab file, the reference marker meshes in a probe capture): 879 checks, 0 failures.

Benchmark fix1, `--only still,select` (out/perf/prefabmodels and prefabmodels2 against out/perf/new), ms median:
still cpu 0.020 / 0.021 against 0.017, total 0.545 / 0.549 against 0.532; select cpu 0.064 / 0.060 against 0.062,
total 0.588 / 0.574 against 0.586. Same builds, relayouts, items in place and uploads; `check_still` and
`check_select` same. The differences are run-to-run noise with other work on the machine.

Not done: a nested prefab's `model` is resolved like a top-level one (next to the open map and in the prefab
folders), not next to the outer prefab's file as the build resolves it. Scriptables inside a bundle are drawn even
when the build leaves them out (it does for bundles without scriptable support). Brush faces inside prefabs stay
untextured, as before.
