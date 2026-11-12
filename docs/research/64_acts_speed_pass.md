# 64. ACTS and preview speed pass (2026-11-10)

Scope: why stock assets were slow to appear in the editor, what was changed in the ACTS fork and in the editor, and
what was measured. Machine: i7-12700F, 16 GB, zones and XPaks on NVMe. The game was running during most
measurements (about 3 to 8 GB of memory free), so single timings vary by tens of percent.

ACTS fork: `E:\mw124\atian-cod-tools`, branch `codex/mw2019-complete`. The work was done in a separate worktree
(`E:\mw124\acts-perf`, branch `perf/replay-speed`). Editor changes were built and tested in an isolated copy
(`C:\iw8r-perf`) so that the open editor was not touched.

## 1. Where the time went

One ACTS run per request, and every run loaded the whole zone before it exported anything:

| Cost in a run (before) | Cause |
| --- | --- |
| Most of the load time | Every stream hook of the native loader (Load_Stream, push, pop, align, resolve ...) built a JSON trace event and then dropped it, because no trace was requested. The hooks run millions of times per zone. |
| Hundreds of ms | `manifest.json` was replaced on disk every 256 assets while loading and every 128 records while exporting. |
| 100 to 850 ms | Fastfile blocks and the `.fp` patch were decoded on one thread, into buffers that were cleared first. |
| About 100 ms per start | The schema (2.7 MB of JSON) was parsed by the library's DOM parser, which searches each object linearly for every member it inserts. |
| Per asset | Schema types and rules were looked up by a linear search for every member of every node; scalars and string characters were read through `ReadProcessMemory` on the own process, one vector per read. |

In the editor:

| Cost (before) | Cause |
| --- | --- |
| 1 to 10 s per request | Every cache started its own `acts.exe` per request and zone. The Images browser asked for one image per run. |
| 100 to 400 ms per texture, on the GUI thread | The camera, the browsers and the icons decoded the full top mip of every DDS (16 MB of RGBA for 2048x2048) and shrank it to 512, 96 or 64 afterwards. |
| Tens of ms per scroll step | The thumbnail timer asked all rows of the list (46k materials) for their rectangle on every pass. |

## 2. ACTS changes (fork commits on `codex/mw2019-complete`)

- Loader hooks build a trace event only while `--trace-replay-streams` writes one.
- Periodic manifest writes at most every 400 ms; journal flushed every 200 ms and at the end.
- Loader memory read by bounds check and memcpy; names found with one scan.
- Blocks and patch windows decoded on several threads (`DecompressBlocks`, `bdiffParallel`); the fastfile is read in
  parallel ranges; stream reservations come from `VirtualAlloc` instead of `new[]` + `memset`.
- Schema: types, rules and pools indexed by name; the document is built by a SAX handler that appends members.
- Export: selected records are built and written on up to 8 threads, committed in record order (same journal, same
  file names). Structured JSON is written by a compact writer that copies strings which need no escaping. XPak
  payloads are read through a file mapping.
- **Serve mode**: `acts fastfile -r mw19replay ... --serve --serve-cache-mb N` stays alive, reads one JSON request per
  line on stdin and answers with one `@acts {json}` line. Loaded zones stay in memory (least recently used out first).
  Commands: `export`, `load`, `stats`, `trim`, `ping`, `quit`. Documented in the fork's `docs/mw19/fastfile.md`.
- `manifest.json` gains `timing_ms` and `stream_bytes`.

Exports are unchanged: the same folder layout, journal and manifest. Checked by:

- `--test --limit-per-pool 8` with 14 XPaks, old exe against new exe: journals and manifests identical on
  `mp_shipment`, `mp_hideout`, `mp_backlot2`, `common_mp`, `common_base_mp`, `code_post_gfx`, `code_pre_gfx` and
  `mp_frontend` (the `0xfda` legacy path). `mp_frontend7b` fails in both with the same unsupported-revision error.
- Full exports, old against new, file by file: `mp_shipment` world (5 files, 370 MB), `techsets_mp_shipment`
  materials and techsets (1,320 files), `mp_backlot2` images / models / surfaces (6,957 files, 4.7 GB): identical.
  `code_post_gfx` (6,013 files): 3 files differ, only in process addresses printed inside raw union bytes. Load
  addresses in `"address"` fields differ between any two runs, also of the old exe.
- The fork's `scripts/mw19/test_serve.py` (new): one-shot export against first and repeated serve requests.

### Measured (same commands, warm files)

| Run | Old ACTS | New, one-shot | New, serve (zone loaded) |
| --- | ---: | ---: | ---: |
| 3 material bodies, `techsets_mp_backlot2` | 0.85 s | 0.20 s | 4 ms |
| 6 streamed images, `mp_backlot2` | 3.2 s | 0.44 s | 60 ms |
| 3 xmodels, `common_base_mp` | 5.3 s | 0.40 s | 12 ms |
| Load only, `common_mp` (712 MB) | 9.4 s | 1.5 s | 4 ms |
| World geometry, `mp_shipment` (`gfx_map` + `gfx_map_trzone`) | 9.3 s | 2.0 to 2.8 s | |
| Every asset of `code_post_gfx` | 20.0 s | 4.2 s | |
| Materials + techsets, `techsets_mp_shipment` | 3.8 s | 0.43 s | |
| Images / models / surfaces of `mp_backlot2` (4.7 GB written) | 66.9 s | 37.4 s | |

## 3. Editor changes

- `src/qtapp/acts_runner.{h,cpp}` (new): one queue for every ACTS export. It keeps up to two `--serve` processes
  (Settings `acts/servers`, 1 to 4), sends a request to the server that already holds its zone, and loads different
  zones on different servers at the same time. Cache budget per server: Settings `acts/cacheMb`, or automatic (half
  of the free memory shared between the servers, 384 MiB to 2 GiB each). Idle servers release their zones after
  2 minutes and exit after 10. `acts/serve=false` forces one process per request.
  An `acts.exe` without `--serve` is detected at the first start (no greeting): requests then run as one process
  each, with the old command line. A request whose server dies is retried once that way.
- `material_cache`, `model_cache`, `stock_sky` (also the Images browser), `world_cache`: submit to the runner
  instead of starting `QProcess`. Material and model rounds submit all their zones at once.
- `src/viewport/texture_decode.{h,cpp}`: `decodeDdsPreview` / `loadDdsPreview(path, maxSide, ...)` read the DDS
  header and only the mip level a preview needs. Used by the camera (512), the browser thumbnails (96) and the
  icons (64). `texSizes_` still gets the size of the whole image.
- Camera texture loading per frame: at least 6 loads as before, more while under 12 ms.
- Asset browser: the thumbnail pass starts at the row under the viewport; rows that wait for a cache do not use up
  the pass budget, so every visible row is requested in the first pass.

### Measured

`IW8Radiant.exe --material-cache-test` / `--model-cache-test`, empty scratch cache
(`IW8R_MATERIAL_CACHE_ROOT`, `IW8R_MODEL_CACHE_ROOT`), new editor build:

| Test | Old ACTS (one process per request) | New ACTS (serve) |
| --- | ---: | ---: |
| 3 materials (2 requests) | 6.6 s | 0.66 to 1.2 s |
| 3 models (2 requests) | 15.0 s | 0.54 to 0.75 s |
| 36 materials from many zones (43 requests) | 110.9 s | 7.1 to 8.2 s |

The 36-material runs give identical results line by line and the same cache files (57 files, 177,436,452 bytes).

Tests: core `iw8r_tests` 128,135 checks, 0 failures (5 new preview tests); GUI test 427 checks, 0 failures with the
new ACTS and an empty scratch cache.

## 4. Structured export limits (fork `3c2198a1`)

Found by the editor session's world-cache job on ten maps: two world exports failed, and they fail the same way
with the exe from before this pass.

| Zone, asset | Error | Cause | Change |
| --- | --- | --- | --- |
| `mp_aniyah_tac`, `gfx_map_trzone` | Structured asset exceeds output budget | 173 MB read, inside the 256 MiB read budget; the same 256 MiB was applied to the JSON text, and a compact array is two hex digits per byte (337 MB of text) | Output budget = 3 x read budget (768 MiB) |
| `mp_donetsk`, `gfx_map` | Structured asset contains unreadable fields | 4,107,040 nodes against a cap of 4,000,000 ("Traversal limit exceeded" nodes) | Node cap 8,000,000 |

After the change: `mp_aniyah_tac` 2 of 2 records ok (425 MB, 3.5 s, ACTS peak memory 1.7 GB), `mp_donetsk` 283 of 283
ok (432 MB, 9.6 s, peak 2.6 GB). `IW8Radiant.exe --world-cache-test mp_aniyah_tac`: 4,430 meshes, 854,396 triangles,
ACTS 2.8 s, parse 5.4 s. `mp_donetsk` was not run through the editor's reader.

The editor's runner also ends its ACTS servers when `acts.exe` is replaced on disk (they finish the request they
are in), so an ACTS update takes effect without restarting the editor.

## 5. Not done / limits

- The preview decode still runs on the GUI thread (a 512x512 BC7 level costs 10 to 17 ms). Loading is bounded per
  frame; moving it to a worker thread is the next step if stutter remains while textures stream in.
- `compile_panel.cpp` (ACTS validation of built zones) and the `map_ents` export in `main_window.cpp` still start
  one process per run. They gain from the faster one-shot path only.
- The schema parse (about 60 ms) is the largest fixed cost of a one-shot run. A server pays it once.
- Reading a cold 712 MB zone is bound by the disk and by fresh memory; with little free memory the large zones take
  longer than the table says.
- `readStockWorld` parses 370 MB of JSON hex in the editor; that side was not profiled here.
- In-game behaviour is not affected by any of this and was not tested.
