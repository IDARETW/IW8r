# 73. Reflection probe path: why a probe volume shows no visible change

Status: research only. No source, map or build was changed. Built test map: `out/agent/mp_iw8r_fix1` (build log at 16:53 on 2026-11-11).
Converter: `E:\mw124\x64-zt`, branch `iw8radiant/build-extraction`, HEAD `062ec6b4`. Its `zonetool.exe` was built 16:48 on 2026-11-11.

Update 2026-11-14: research 123 found the larger cause (the stored level and the missing colour) and its fix.

Short answer: in this build every probe image is the same flat grey, and the converter assigns each surface one probe by its
own tag, not by the player's position. The authored volume never changes which probe a surface uses. A matte grey material
with no specular map is also unlikely to show any reflection even when the probe differs.

## 1. What the editor writes

Source entities (`out/agent/mp_iw8r_fix1/mp_iw8r_fix1.map`, lines 155-159):
`"origin" "0 0 64"`, `"classname" "reflection_probe"`. No mins, maxs, priority or feather. This is the only probe entity in the map.

Editor code (`src/core/compile_iw8.cpp`):
- Probe table: index 0 is the default probe at the bounds centre (`probes{bounds.center()}`, line 1825). Entity probes follow at index 1 and up, in map order, capped at 256 (line 562, lines 1770-1773).
- Volume: read from keys `mins` and `maxs` (relative to origin). Written only when both parse and every axis has extent (lines 1776-1789). Otherwise a warning says the converter derives the volume.
- Priority and feather: `priority` is passed through as a number (line 1798). `feather` takes one or three distances and writes their inverses, 1/d per axis (lines 1801-1806). The converter does not invert it again.
- Surface and model assignment: `probeFor(p)` returns `1 + nearest entity probe origin` (line 1828). It uses only the origin, never the volume. Surfaces use the centre of their vertex bounds (`surfaceCentre`, line 622). Models use their origin (line 2231).
- World JSON probe entry: `{"image", "origin", "volume"?, "priority"?, "feather"?}` (lines 2218-2224). Image name is `<id>_probe` for probe 0 and `<id>_probe<N>` for probe N (line 2218).
- Probe list in the DPVS cell: every probe index (lines 2317-2319).

Built map, `out/agent/mp_iw8r_fix1/iw8build/mp_iw8r_fix1/extraction/extracted/maps/mp/mp_iw8r_fix1.d3dbsp.replay-world.json`:
- `reflection_probes`: `[{"image":"mp_iw8r_fix1_probe","origin":[0,0,124]}, {"image":"mp_iw8r_fix1_probe1","origin":[0,0,64]}]`. Origin 124 is the bounds centre (bounds z -32..280). Probe 1 has no `volume`, `priority` or `feather`, matching the source.
- Surfaces: 10 surfaces, all `"reflection_probe":1`. Models: 5 models, all `"reflection_probe":1`. A grep found 15 such keys, all equal to 1.
- DPVS cell: `"reflection_probes":[0,1]`.
- Build log line 40: `world 10 surfaces, 100 triangles, 5 static models, 2 probes`.

The volume code path was not exercised by this map. The sample `samples/dev/iw8r_dev_showcase.map` (lines 981-992) has a probe with `mins -256 -192 -64` and `maxs 256 192 192`, but there is no extraction for it under `out/`.

## 2. What the converter reads, and what it ignores

Reads (`E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp` unless noted):
- Per surface `reflection_probe` index: `RawSurface` at line 2198, read at 2245, stored at 2414-2416 as `reflectionProbe`.
- Per static model instance `reflection_probe`: lines 3793-3797, with a range check against the source probe count.
- Per probe `volume`, `priority`, `feather`, `instances`: lines 6708-6712 (authored volume taken when it is an array of two, probe index not 0), lines 6754-6765.
- Image name: `iw3_render_assets.cpp` lines 5523-5544 read `exportRoot/images/<image>.dds` and throw if missing. They do not generate the image from the sky.

Uses of the authored volume:
- The authored volume (lines 6718-6725) goes only into the output `reflectionProbes[].volume` (line 6751). It is not used for any surface's tag.
- The surface-to-probe tag comes from `surface.reflectionProbe` alone (lines 6542-6563), so a volume cannot change it.
- The volume feeds the geometry bounds used for the fallback volume (`addProbePoint`, lines 6216-6233). Its only other use is the comment at lines 6207-6211 about dynamic models taking the volume that contains them. That is a converter-side bounds rule. How the game uses the volume at run time is not in this source.

Priority and feather: passed through unchanged (lines 6755-6758). Their meaning in the game is not in this source.

Is the volume code in the checked-out source? Yes.
- Commit `3395867e` "Read authored reflection probe volumes from world.json" (2026-11-09 18:30:12) adds it. It is an ancestor of HEAD `062ec6b4`.
- The editor comment at `compile_iw8.cpp:2219` says "an authored volume is read by the x64-zt fork (abf4bc6f+)". That citation is wrong. `abf4bc6f` is "Keep stock vfx/ effect names the map script loads" (2026-11-09 18:05), unrelated to probes. The correct commit is `3395867e`.
- The converter binary that ran contains the volume strings. `zonetool.exe` (16:48, 2026-11-11) matches "uses its authored volume" and "reflection probe volume has no extent". So the volume code was present at build time.
- The volume code was not exercised in `mp_iw8r_fix1` because the source probe had no mins or maxs. The build log contains no "uses its authored volume" line, which is expected.

Ignored or not checked:
- The `dpvs.cells[].reflection_probes` list (not checked).
- The editor's `volume` for probe 0 (ignored by design, line 6713).
- Any runtime use of the output `volume`, `priority` or `feather` (not in source).

Doc 29 (`docs/research/29_probes_models_sky.md`, lines 20-22) says the converter skips the first entity probe when entities exist. The code does not do that. `iw3_render_assets.cpp:5519-5520` sets `firstProbe = size > 1 ? 1 : 0`, so only source probe 0 (the default) is skipped. Entity probes start at source index 1 and are kept. The tags agree: `ReflectionProbeTag` (`iw3_fastfile.cpp:3947-3951`) returns the source index, and the converted slot is that minus 1, so tag = slot + 1 as the shader expects. Doc 29's concern is not supported by the code. It has not been checked on a real zone.

## 3. Where the probe images come from, and whether they differ

Editor side (`compile_iw8.cpp`):
- `buildProbeDds` (line 935) writes a 64x64 B8G8R8A8 cube with 7 mips. Without captured faces it is neutral grey (128) with alpha 255.
- `probeCube` (lines 2426-2442) uses an editor capture only when `opt.probeCaptureDir` is set and the capture position matches the probe origin within 1 unit (probe 0: 512 units). Otherwise it writes neutral grey. The capture folder is `<buildRoot>/probe_capture` (`compile_panel.cpp:656`). `buildRoot` is `<map dir>/iw8build/<mapId>` (`compile_panel.cpp:361-366`).
- Silent fallback: the warning "N of M reflection probe(s) use the editor capture" (lines 2446-2448) is printed only when a capture folder exists. With no folder, nothing is said.

Built map images, `out/agent/mp_iw8r_fix1/iw8build/mp_iw8r_fix1/extraction/extracted/images/`:
- `mp_iw8r_fix1_probe.dds` and `mp_iw8r_fix1_probe1.dds`, both 131192 bytes.
- Both have MD5 `593d94d4f3b0715cc185797776a5d053`, so they are identical.
- Pixel count: 98298 values of 128 and 32766 values of 255 (alpha). Neutral grey.
- The folder `out/agent/mp_iw8r_fix1/iw8build/mp_iw8r_fix1/probe_capture` does not exist. Build log lines 76-79 mention only the cached probe array.

Converter side: the converter reads each image (`iw3_render_assets.cpp:5538-5544`), projects SH from it (5546), and writes it to the BC6H array (5601-5623). It does not replace the image with its own render. So the game can only show what the editor wrote.

Sample for comparison: `out/gui-c101/iw8build/mp_iw8_sample_r/probe_capture/` has real captures. `probe0.rgba` and `probe1.rgba` have different MD5s and different colours. Their positions are `(0,0,56)` and `(0,0,64)`. That build has no extraction folder, so the converter's use of them was not checked.

## 4. Which objects read the probe, and whether a plain matte room could show it

Per-object use:
- World surfaces: each vertex carries the tag in the integer part of `lightmapUV.x` (`iw3_fastfile.cpp:6556-6563`, `65536 * ReflectionProbeTag(...)`). The comment at 6552-6555 says the shader reflects that probe alone. So the choice is fixed per surface, not picked by view position.
- Stock-path surfaces get no tag (line 6559). The stock path is chosen only for certain IW3 material kinds (lines 6429-6434; `stockWorldMaterialIndex` is set at `iw3_render_assets.cpp:7352` and by the other `addMaterial` calls). Whether editor materials (`iw8r/grey`, `iw8r_stock/*`) hit this path was not traced.
- Static model instances: `reflectionProbe` per instance (lines 3793-3797). Editor placements with no compiled instance pick the nearest probe (lines 4430-4452).
- View model and other dynamic models: the converter's comment at 6207-6211 says they take the volume containing them. The editor writes no view-model data. The runtime lookup is not in this source.

Matte material:
- The editor's default material is `buildMaterialJson` (`compile_iw8.cpp:995-998`). It has colorMap `$white`, normalMap `$identitynormalmap` and no specular map.
- `iw3_render_assets.cpp:5352-5356` says that materials without a specular map shade as matte (the shader's matte default). Reflections are controlled by the material's specular response.
- So with the default grey material, a probe change would probably be invisible, even if the image differed. This is inferred from converter comments. The game shader is not in the repo.

## 5. Ranked hypotheses for "no visible change"

1. Every probe image is the same flat grey (strongest evidence). The two `.dds` files are byte-identical with 128-grey pixels, and no `probe_capture` folder exists for this map. Against: when a capture exists, the converter uses it. Cheapest test: in the editor, run Compile > Capture Reflection Probes on a test map, then compare MD5s of `extracted/images/*_probe*.dds`. They should differ. Check that `probe_capture` exists under `<map>/iw8build/<mapId>`.
2. The surfaces are matte. The grey material has no specular map (`compile_iw8.cpp:995-998`), and the converter treats such materials as matte (`iw3_render_assets.cpp:5352-5356`). Evidence against: the game shader was not checked. Test: a test map with a glossy floor (a custom material with a specular or gloss value through the doc 21 library), two probes of very different colours, one under each half of the floor.
3. The probe is chosen by surface, not by player position. The editor assigns by origin only (`compile_iw8.cpp:1828`) and ignores the volume. The converter tags each vertex (`iw3_fastfile.cpp:6542-6563`) and uses the volume only in output metadata (6750-6751). So walking into a volume cannot change a world surface's probe. Evidence against: the game may use `reflectionProbes[].volume` for dynamic objects, which was not checked. Test: the same two-probe map as in 2. Stand far from the surface and check which colour it shows, then check the probe index in the built JSON.
4. Only one entity probe, so all surfaces use the same probe. The built map has 15 of 15 indices equal to 1. Even with different images, nothing could differ between surfaces. Test: at least two probes placed near different walls, so the indices split. Check the JSON `reflection_probe` values before walking.
5. A capture that no longer matches its probe is silently replaced by grey. Captures must match the origin within 1 unit (`compile_iw8.cpp:2433-2435`). A moved probe gives grey with no message (2446-2448). Test: move a probe after capture, build, and check the `.dds` hash and the build log.
6. Converter version or citation. The volume code is in the binary that ran (`zonetool.exe`, 16:48), so this is unlikely. The editor comment `abf4bc6f+` is wrong and should be `3395867e`. Doc 29's "first entity probe skipped" is also wrong. Neither explains the symptom.

## 6. What could not be determined

- The game shader: how it blends probes, whether it uses specular only, and whether it ever picks a probe by position. No shader or game code is in the repo.
- The game's use of `reflectionProbes[].volume`, `priority` and `feather` at run time. Units of `feather` are inverted in the editor (`compile_iw8.cpp:1806`) and passed through by the converter. The game's expected unit is not stated.
- Whether the owner's real build ran Capture Reflection Probes. `probe_capture` for `mp_iw8r_fix1` was not found under `out/agent`. The owner's build root may be elsewhere.
- Whether editor materials (`iw8r/grey`, `iw8r_stock/*`) take the stock path (no probe tag). Not traced.
- The `dpvs.cells[].reflection_probes` list in the game: not checked.
- Whether a zone built from this JSON was loaded in game, and what the owner saw. No zone was inspected.
- The sample `iw8r_dev_showcase` with a volume: no extraction under `out/` to check.
