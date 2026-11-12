# 30. Stock Replay world geometry (ACTS gfx_map + gfx_map_trzone)

Status: working for `mp_shipment` (MW2019 Replay 1.20, profile `replay-1.20`). Triangles with positions and per-surface
material names are read. Props, normals, UVs and lightmaps are not. Task: roadmap 3.9 (read-only reference layer).

## Route

The world render geometry is in two ACTS asset types, both JSON exports with no extra flags:

| Asset | What it holds | Used for |
| --- | --- | --- |
| `gfx_map` (GfxWorld) | `surfaces.surfaces.values[]`: one GfxSurface per draw surface. `tris` = `{posOffset, vertexCount, triCount, baseIndex}`, `material.name`, `transientZone`. Also bounds, cells, props (`smodels`) and lighting. 138 MB JSON for mp_shipment. | surface index ranges and material names |
| `gfx_map_trzone` (GfxWorldTransientZone) | `drawVerts.posData.bytes` (hex, float3 per vertex, 12-byte stride), `drawVerts.indices.bytes` (hex, uint16 LE), `drawVerts.auxData.bytes` (hex, not read). 249 MB JSON for mp_shipment. | positions and triangle indices |

Considered and rejected:

- `--geometry` only exports xmodelsurfs GLBs (docs/research/16, 23). It does not cover GfxWorld.
- `--dumpBinaryAssets` writes the same JSON (the same file, byte for byte, in a second folder).
- No ACTS writer produces raw buffers for these types. The hex JSON is the only exporter output, so the reader decodes it.
- The clipmap types (`col_map`, `com_map`) carry collision, not render triangles. Not evaluated further.
- `gfx_map_trzone` alone has no surface table, so `gfx_map` is needed for material names and index ranges.

The two assets come from one `fastfile` run. The loader cost is paid once.

## Commands

Check the assets (cheap, report only):

```
E:\mw124\atian-cod-tools\build\bin\Release\acts.exe --noUpdater fastfile -r mw19replay -g "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe" --oodle "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll" -a gfx_map,gfx_map_trzone --test "E:/IW8/Builds/1.20-replay/zone/mp_shipment.ff"
```

The export the editor uses (the world cache runs this with `-l`, plus `--xpak <pak>` for every zone XPak of 64 KB or more):

```
acts.exe --noUpdater fastfile -r mw19replay -g "<replay exe>" --oodle "<oo2core dll>" -a gfx_map,gfx_map_trzone -l -o <scratch> "<zone>.ff"
```

For mp_shipment no XPak is needed (`mp_shipment.xpak` is a 32 KB header, under the 64 KB threshold), and the buffers came
from the `.ff` itself. Larger zones may stream the buffers from their XPaks; the world cache passes every XPak of 64 KB or
more, as the model cache does. This was not tested on a second zone.

Material bodies are not in `mp_shipment.ff`. They are in `techsets_mp_shipment.ff`: `-a material` there gives 945 material
assets, and `tw/cement_ground_patchwork_cracked_wet` is among them. The world's surface names match those asset names once
the leading comma is removed (see Axis and names).

## Layout (verified on mp_shipment)

- GfxSurface `tris.posOffset` is a byte offset into the zone's `posData`. Surface vertex `k` is the float3 at
  `posOffset + 12*k`. The 12-byte stride is confirmed by the offsets themselves: for 292 consecutive surface pairs the gap
  divided by the earlier surface's `vertexCount` is exactly 12.0.
- The first surface's vertices start at byte 4 (`posOffset` 4). Bytes 0..3 are padding.
- Index `i` of a surface triangle is surface-relative: vertex `posOffset + 12*i`. Every index is below `vertexCount`
  (0 out of range for all 1,039 surfaces).
- `baseIndex` is an index into the `indices` array (uint16 elements). `triCount*3` indices follow it. The index array has
  1,256,001 entries and every surface range fits inside it.
- Surfaces share vertices: 5,385,833 surface vertices map onto 644,164 stream vertices, because surfaces reference the
  same posOffset ranges (for example surfaces with posOffset 7216 share 1,887 vertices).
- 1,039 surfaces, 418,667 triangles, 266 distinct material names. All surfaces have `transientZone` 0; `transientZoneCount`
  is 1 for this zone.
- The aux stream is 15,129,844 bytes. Its per-vertex stride is not established (it is not a whole divisor of 644,164).
  Normals, colours and UVs live there. Not decoded.

## Axis and scale

- Z-up inches, the same as Radiant and as ACTS xmodelsurfs (docs/research/16 note about the 0.0254 node scale does not
  apply here: GfxWorld positions are already in inches).
- Evidence: the floor sits at z 1744..1856 (height), and the header bounds `midPoint +- halfSize` match the decoded
  extents on each axis (x -73113..81993, y -75825..82337, z -6401..52961). The header and the decoded data agree.
- The world reader outputs the raw values, with no scale or axis change.

Winding: ACTS data is clockwise seen from outside, as for the xmodel sidecars (docs/research/22). The reader swaps two
corners per triangle so the output matches `ImportedModel` (counter-clockwise). Check on mp_shipment: the triangles of
`tw/cement_ground_patchwork_cracked_wet` (the floor) have a raw geometric normal pointing down in 971 cases and up in
122 cases. The dominant sign is clockwise from above. The 122 up-facing triangles were not inspected.

Bounds of the whole world, 155k inches across, are dominated by backdrop geometry, not by the playable area:

| Set | x | y | z |
| --- | --- | --- | --- |
| all triangles (`tw/sky_mp_shipment` dome, ±82k) | -73112 .. 81992 | -75824 .. 82336 | -6400 .. 52960 |
| percentiles 0.1 % .. 99.9 % | -23934 .. 13943 | -22429 .. 8658 | -25 .. 3107 |

`tw/uk_periph_city_card_01` is a 150k backdrop plane. The reader's `bounds` covers everything. A reference layer should
clip or filter by material if it needs the playable box.

Material names: ACTS prints the surface material with a leading comma (`,tw/floor`). The reader removes it, so the mesh
name is the material asset name (`tw/floor`), as in the techsets export.

## Result (mp_shipment)

From `E:\IW8-Radiant\out\world-cache-test\world-cache-test.txt` (editor `--world-cache-test mp_shipment`):

| Item | Value |
| --- | --- |
| Surfaces read | 1,039 (all; none skipped) |
| Meshes (one per material) | 266 |
| Unique positions | 644,164 |
| Triangles | 418,667 |
| Bounds (all, Z-up inches) | min (-73112, -75824, -6400), max (81992, 82336, 52960) |
| ACTS run (gfx_map + gfx_map_trzone, one call) | 9.4 s wall, `fastfile` load included |
| ACTS alone, gfx_map | 6.8 s, JSON 138,293,258 bytes |
| ACTS alone, gfx_map_trzone | 6.7 s, JSON 249,146,374 bytes |
| Export on disk (assets folder) | 387,439,632 bytes (about 2.1 x the zone .ff, which is 187,760,776 bytes) |
| Reader parse (json_lite DOM, both files) | 3.8 s to 4.3 s |
| Cache hit (no ACTS) | 0.11 s to the request, then the parse |

Measured on this machine, Windows, release build, with the zone read from E:. Peak memory was not measured.

## Cache and limits

- `WorldCache` (src/qtapp/world_cache.*) keeps the ACTS export folder under
  `AppLocalDataLocation/world_cache/<zone>/`. The index is `world_cache/index.json`. A zone over 2 GB is refused: the
  estimate is 2.5 x the zone `.ff` before the run, and the measured size is checked after it.
- C: had about 14 GB free and was 98 % full when this was written. One mp_shipment cache takes 388 MB.
- The reader parses the whole DOM. Each load takes about 4 s for mp_shipment. A binary cache would cut that; not built.

## What is missing

1. Static props (`smodels`): 9,535 instances of 277 models (`smodels.collections`, `instanceFlags`, `collectionBounds`, all
   encoded bytes). These are the placed xmodels. They need the instance decoder and the xmodel cache (docs/research/23).
2. The aux vertex stream: normals, colours, UVs. The reader computes area-weighted normals and writes zero UVs.
3. Lightmaps, sun shadows, decals and the GfxCell/dpvs data. Not needed for geometry.
4. Materials are names only. Images are resolved by the material cache (docs/research/20), not here.
5. Only transient zone 0 was seen on mp_shipment. Multi-zone maps are handled (the reader matches `transientZone` to
   `transientZoneIndex`) but this was not tested.
6. The reader does not check that the export is complete: a zone whose trzone is missing from the export gets a warning
   per surface (see `WorldCache` and `readStockWorld`).
