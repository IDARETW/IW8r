# 20. Stock material texture cache (colour and normal DDS, Replay 1.20)

Status: working for the three sample materials. `MaterialCache` (`src/qtapp/material_cache.{h,cpp}`) resolves a stock
material to its colour (slot 0) and normal (slot 9) DDS with ACTS on demand, caches the result under
`AppLocalDataLocation\material_cache`, and answers later lookups without ACTS. Test mode:
`IW8Radiant.exe --material-cache-test <m,m,...> --out <dir>` (`src/qtapp/main.cpp`). The editor's 3D view is not wired to
it yet; the API is in place.

The game folder was only read. ACTS wrote only to its `-o` folder under the cache root (C:). ACTS was not rebuilt and the
game was not started.

## Results (test: `tm2co/me_curb_stone_01`, `tm2co/me_plaster_barrier_fence_wall`, `tm2co/ee_trim_beam_airport_concrete`)

Output: `E:\IW8-Radiant\out\material-cache-test\material-cache-test.txt` (cold run), `...\material-cache-test-run2\` (warm run).

| Material | Body zone (techset file) | Colour image (decoded mip 0) | Normal image (decoded mip 0) | XPak serving the payloads |
| --- | --- | --- | --- | --- |
| `tm2co/me_curb_stone_01` | `techsets_mp_backlot2.ff` | `me_curb_stone_01_c&defaultspec~7205550087674971592` (1024x2048) | `me_curb_stone_01_n&me_curb_stone_01_g~6416585005236763635` (1024x2048) | `pak_tu_remaster_2.xpak` |
| `tm2co/me_plaster_barrier_fence_wall` | `techsets_mp_backlot2.ff` | `plaster_tan_cracked_wall_painted_01_c&defaultspec~8039078974570857293` (2048x2048) | `plaster_tan_cracked_wall_painted_01_n&plaster_tan_cracked_wall_painted_01_g~6988346727713066890` (2048x2048) | `pak_magma1.xpak` |
| `tm2co/ee_trim_beam_airport_concrete` | `techsets_mp_backlot2.ff` | `plaster_painted_white_02_c&defaultspec~12589240972894416292` (2048x2048) | `plaster_painted_white_02_n&plaster_painted_white_02_g~15597937019187775082` (2048x2048) | `pak_launch.xpak` |

All three resolved with no failures. The image files live in `mp_backlot2.ff`; only the material bodies live in `techsets_mp_backlot2.ff`.

Timings (wall clock, from `request()` to idle, as printed by the test):

| Run | Wall time | ACTS runs | Notes |
| --- | --- | --- | --- |
| Cold (cache folder deleted), final build | 9,512 ms (process 9.9 s) | 2 | 1 body run (3 names) + 1 image run (6 names) |
| Warm (same cache), final build | 104 ms (process 0.36 s) | 0 | everything from `index.json` and the image files |
| Cold, earlier builds (2 runs) | 10,270 and 10,563 ms | 2 | before the handle and scratch fixes below |
| Warm, earlier build | 98 ms | 0 | |

Single ACTS runs measured by hand (same commands the cache issues; E: is shared and timings vary between runs):

| Command | Wall time |
| --- | --- |
| Body run, `techsets_mp_backlot2.ff`, 3 names, 14 XPaks | 2.6 s (4.5 s on another run without XPaks) |
| Image run, `mp_backlot2.ff`, 6 names, 14 XPaks | 17.5 s (the same 2-image run took 5.8 s earlier) |
| Image run, `mp_backlot2.ff`, 6 names, one XPak | 8 to 11 s (4 of 6 unavailable) |

Most of the cold cost is loading the zone, not the exports (about 5 to 10 s per zone load for the 163 MB `mp_backlot2.ff`).

Cache size after the cold run: 6 DDS files totalling 21.0 MB (1.4, 2.8, 2.8, 5.6, 2.8 and 5.6 MB), plus `index.json`
(1.4 KB). Total 20,973,975 bytes in 7 files.

## Commands that worked

Body (material JSON, one run per zone per batch):

```
acts.exe --noUpdater fastfile -r mw19replay -g "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe"
  --oodle "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll"
  -a material -n "tm2co/me_curb_stone_01,tm2co/me_plaster_barrier_fence_wall,tm2co/ee_trim_beam_airport_concrete"
  -l -o <out> "E:/IW8/Builds/1.20-replay/zone/techsets_mp_backlot2.ff"
```

Images (`--xpak` repeated, then the names of the slot-0 and slot-9 images, with their `~hash`):

```
acts.exe --noUpdater fastfile -r mw19replay -g ... --oodle ... --xpak <each populated XPak> ...
  -a image -n "me_curb_stone_01_c&defaultspec~7205550087674971592,..." -l -o <out>
  "E:/IW8/Builds/1.20-replay/zone/mp_backlot2.ff"
```

The cache passes every populated XPak in the zone folder (size above 64 KiB), 14 files here:
`pak_magma_parts, pak_magma1, pak_magma_base, pak_magma2, pak_tu_remaster_2, pak_tu20, pak_tu21, pak_tu22,
pak_launch, pak_launch_remaster_1, pak_launch_remaster_2, pak_launch_tu20, pak_launch_tu21, pak_launch_tu22`.

Output layout under `<out>\mw19replay\<zone>\`: `assets.jsonl` (one line per asset), `manifest.json`, `assets\material\...json`,
`assets\image\<name>.<n>.dds`. The journal's `file` field is relative to `<out>\mw19replay\<zone>`.

## XPak needs (images)

Image payloads are streamed. For these three materials the stream keys are spread over three different archives:

| Image | XPak that served it (`package_reads`) |
| --- | --- |
| `me_curb_stone_01_c` and `_n` | `pak_tu_remaster_2.xpak` |
| `plaster_painted_white_02_c` and `_n` | `pak_launch.xpak` |
| `plaster_tan_cracked_wall_painted_01_c` and `_n` | `pak_magma1.xpak` |

What was tried for `mp_backlot2` images:

- No XPak: 6 names, `unavailable`, `package_key_missing`, exit 127.
- `mp_backlot2.xpak` (the zone's own, 32 KB, no payload): 6 unavailable.
- `pak_launch.xpak` alone: 2 ok, 4 unavailable.
- `pak_tu_remaster_2.xpak` alone: 2 ok, 4 unavailable.
- All 14 populated XPaks: 6 ok, exit 0.

So the zone's own XPak is empty for images, and each image needs the right archive among the 14 populated ones. Passing all 14
did not add a measurable cost in the runs timed (each archive's index is read on open; the 128 MB index budget in ACTS was not reached).

Body runs needed no XPaks (the body JSON is resident in the `.ff`), but the cache passes the same list anyway.

## Findings that changed the design

1. **Catalog zone is not the body zone.** `mp_backlot2.ff` contains 2,982 materials as name-only references (`references`
   in the manifest, `tested` 0). `-a material` on it exports nothing. The bodies are in `techsets_mp_backlot2.ff`, so
   the cache tries `techsets_<zone>` before `<zone>` for bodies, and `<zone>` before `techsets_<zone>` for images. The task's
   "first zone in the catalog whose .ff exists" would have failed all three materials.
2. **MAX_PATH.** An image written under a long output folder (scratch under the Temp folder) failed with
   `Cannot write Replay asset`; the 5.6 MB normal map of the plaster material. The same export into a short folder worked.
   The batch work folder is therefore `<cache>\w\<n>\`. A name longer than about 115 characters could still fail the write.
3. **Name-only references.** Material and image names in the JSON carry a leading `,`. The cache strips it. `-n` needs the full
   name, including `~hash`, to match an image.
4. **Open handles block cleanup on Windows.** A journal or log file still open in `finishRun` left a file behind in the work folder.
   The journal is now closed first, and ACTS output is captured in memory instead of a log file.
5. **Exit code 127 is not a failure signal.** ACTS returns 127 when any payload is unavailable or failed, while the rest of the
   batch is written. The cache reads the journal per name instead.
6. **Environment variables.** The cache sets neither `ACTS_NATIVE_BYTES` nor `ACTS_REPLAY_STRUCTURED`. The structured flag makes
   `image` skip the DDS payload exporter (`handler_game_mw19_replay.cpp`: `HasPayloadExporter(type) && !structuredOnly`), so images
   would not come out as DDS. Materials exported fine without them (`mw19-asset-json-v1`).
7. **`-n` does not skip the zone load.** The zone is loaded in full. The name filter only limits what is exported. Loading
   `mp_backlot2.ff` costs about 5 to 10 s whichever names are given.

## Seeds

`data\stock_assets\code_post_gfx\index.json` (371 materials) is loaded at startup as seeds: colour from `color_image_file`,
normal from the slot-9 image. That covers 18 colour and 7 normal images with files. The other materials are seeded as entries
with no image. Seeds are never written back, and a cache entry for the same material takes precedence.

Checked: `ecv/gfx_missing_fx` is answered from the seed (64x64 colour, no normal), with no ACTS run.

## Other tested cases (cache folder: `%USERPROFILE%\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\material_cache`)

- `tm2co/does_not_exist_xyz`: not in `iw8_materials.tsv`. Cached as failed with that error, and not retried.
  It stays failed until `index.json` is deleted.
- `tm2co/me_curb_stone_01_crack`: body from `techsets_mp_backlot2.ff`, colour and normal reused from the cache (no image run).
- The cache resolves materials in batches, one ACTS run per zone per round.

## Limits and not done

- Only the three sample materials were run through ACTS. The full catalog (46,164 names, 76 zones) was not run.
- The editor's texture view does not call `MaterialCache` yet; `main_window.cpp` and the asset browser are unchanged.
- `MaterialCache` runs on the GUI thread via `QProcess` (asynchronous, no nested event loop). A batch is one request at a time; requests made while busy are queued and run after.
- A tool-path problem (missing `acts.exe`, Replay exe or Oodle) leaves materials uncached, so they are retried later.

## Files

- `E:\IW8-Radiant\src\qtapp\material_cache.h`, `E:\IW8-Radiant\src\qtapp\material_cache.cpp`
- `E:\IW8-Radiant\src\qtapp\main.cpp`: `--material-cache-test` (arguments and one early branch)
- Cache: `%USERPROFILE%\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\material_cache\` (`index.json`, `images\`)
- Test output: `E:\IW8-Radiant\out\material-cache-test\material-cache-test.txt`, `...\material-cache-test-run2\`
