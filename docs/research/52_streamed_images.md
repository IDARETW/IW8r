# 52. Streamed images: which stock images have mips only in .xpak

Roadmap 4.25. Goal: mark stock images whose full mip chain is streamed (payload in an `.xpak`, not in the zone `.ff`),
and show which quality the editor's material cache and a build get.

Scope and rules: read-only on `E:\IW8\Builds\1.20-replay` and on the `E:\mw124` repos. ACTS ran once on one zone
(`mp_backlot2.ff`) with the 14 populated XPaks. Output went to `E:\IW8-Radiant\out\streamed_probe\` and was deleted
afterwards (129 MB). The x64-zt paths are from the checked-out tree `E:\mw124\x64-zt` (branch `iw8radiant/build-extraction`);
doc 18 cites the same paths on branch `iw8-conversion`. Line numbers are from the trees as read. Claims marked UNVERIFIED
were not reproduced.

## 1. Fields that say a mip chain is streamed

ACTS decodes `GfxImage` in `src/core/acts/tools/mw19/mw19_image.cpp` (repo `E:\mw124\atian-cod-tools`).

| Field (byte offset) | Meaning | Source |
| --- | --- | --- |
| `flags` (+24, u32), bit `0x40` | Streamed. Without XPak packages the decoder throws `streamed_image` | `mw19_image.cpp:31-33` |
| `flags` bit `0x40` as union selector | `GfxImagePixels` is `streamedDataHandle` when set, `residentData` (pointer) when clear | `scripts/mw19/union_rules.py:46-47` |
| `totalSize` (+28, u32) | Bytes of the full mip chain (all mips, all faces) | `mw19_image.cpp:26`, `:75`, `:93` |
| `width`, `height` (+36, +38) | Full top mip | `mw19_image.cpp:27` |
| `levelCount` (+48, u8) | Mip count of the full chain | `mw19_image.cpp:29`, `:52` |
| `streamedPartCount` (+50, u8) | Number of stream parts (1 to 4). Replay reads it at +50. The source schema puts it at +49 | `mw19_image.cpp:92`; `scripts/mw19/replay_layouts.py:34-38`, `:292`; `config/data/mw19/schema.json:8341` |
| `streams[4]` (+56, 40 bytes each) | Part table. Per part: `xpakEntry.key` (+0, u64), `levelCountAndSize` (+32, u32), `width` (+36), `height` (+38) | `mw19_image.cpp:101-104`, `:110-111`; `schema.json:8246` (GfxImage) |
| `levelCountAndSize` | Low 4 bits = cumulative mip count of that part. Bits above, via `>> 4`, = cumulative byte size. Counted from the smallest mip | `mw19_image.cpp:103-104`, `:116` |

How a streamed image is rebuilt (`mw19_image.cpp:86-128`): each part holds the smallest N mips. Part `i` covers the
suffix of the chain that starts at `totalSize - cumulativeSize`. The parts must cover the whole chain (`:120-121`).
Each part is read from its XPak key (`:123-128`). The `pixels` field is a handle, and the decoder never reads pixel bytes
from the `.ff` for a streamed image. Output copies only from the streamed buffer (`:154-157`).

Consequence for "resident in the `.ff`": by this decoder a streamed image has **no pixel bytes in the `.ff`**. The `.ff`
keeps the header and the part table. Every part has an XPak key. Whether the game loads the smallest part from the `.ff`
or from an XPak at zone load is **UNVERIFIED**.

### Probe (`mp_backlot2.ff`, the 6 material images from doc 20)

Structured export (section 5 explains the env var):

```
ACTS_REPLAY_STRUCTURED=1 acts.exe --noUpdater fastfile -r mw19replay -g "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe"
  --oodle "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll" --xpak <14 populated XPaks> -a image -n "<6 names>" -l -o <out>
  "E:/IW8/Builds/1.20-replay/zone/mp_backlot2.ff"
```

Exit 0 in 3.6 s: 12,784 assets loaded, 6 tested, 0 failed, 0 unavailable.

| Image | Full size, format | flags | levels | parts | Part top sizes, smallest first | Part cumulative bytes (levels 9, 10, 11, 12) |
| --- | --- | --- | --- | --- | --- | --- |
| `me_curb_stone_01_c&defaultspec~7205550087674971592` | 1024x2048 BC1 sRGB | 0x2140 | 12 | 4 | 128x256, 256x512, 512x1024, 1024x2048 | 21,888; 87,424; 349,568; 1,398,144 |
| `me_curb_stone_01_n&me_curb_stone_01_g~6416585005236763635` | 1024x2048 BC7 | 0x48 | 12 | 4 | 128x256, 256x512, 512x1024, 1024x2048 | 43,728; 174,800; 699,088; 2,796,240 |
| `plaster_painted_white_02_c&defaultspec~12589240972894416292` | 2048x2048 BC1 sRGB | 0x2140 | 12 | 4 | 256, 512, 1024, 2048 | 43,728; 174,800; 699,088; 2,796,240 |
| `plaster_painted_white_02_n&plaster_painted_white_02_g~15597937019187775082` | 2048x2048 BC7 | 0x48 | 12 | 4 | 256, 512, 1024, 2048 | 87,408; 349,552; 1,398,128; 5,592,432 |
| `plaster_tan_cracked_wall_painted_01_c&defaultspec~8039078974570857293` | 2048x2048 BC1 sRGB | 0x2140 | 12 | 4 | 256, 512, 1024, 2048 | total 2,796,240 (part sizes not listed) |
| `plaster_tan_cracked_wall_painted_01_n&plaster_tan_cracked_wall_painted_01_g~6988346727713066890` | 2048x2048 BC7 | 0x48 | 12 | 4 | 256, 512, 1024, 2048 | total 5,592,432 (part sizes not listed) |

Sizes are `levelCountAndSize >> 4`. The low 4 bits give 9, 10, 11 and 12 cumulative mips per part. For a 2048 image the
smallest part (256x256, 9 of 12 levels) is 43,728 of 2,796,240 bytes, about 1.6% of the full chain.

Every image has the streamed flag, and the four keys in a row are all non-zero, so all four parts come from XPaks.

## 2. What the editor's material cache gets

- `MaterialCache` always passes every populated XPak. `src/qtapp/material_cache.cpp:253-258` (`xpaks()`, minimum size
  `kXpakMinBytes` = 64 KiB at `:29`). The argument list is at `:394-395` (`--xpak` for each, then `-a image`). `startRun` is at `:390`.
- ACTS has no resident-only image output. Without packages a streamed image throws `PayloadUnavailable`
  (`mw19_image.cpp:32-33`). With packages but a missing key the handler reports `package_key_missing`
  (`handler_game_mw19_replay.cpp:615`). Doc 20 measured it: no XPak gives 6 `unavailable` and exit 127; all 14 XPaks give 6 ok.
- The DDS is the full chain. For streamed images `mw19_image.cpp:152-157` copies all `mipCount` levels, and `:130-149`
  writes a header with that mip count. The on-disk cache agrees. The three doc 20 materials are 2.8 MB (2048 BC1) and
  5.6 MB (2048 BC7), which matches `totalSize` 2,796,240 and 5,592,432, not the 43 KB smallest part.
- Editor cache on disk, `%USERPROFILE%\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\material_cache\images`:
  606 DDS files. 595 have the full mip count for their size. 11 are short: 4 cube sky BC6H files with one mip (expected for
  cubes, UNVERIFIED), plus 7 `_sdf` and `_alpha` files at 512 to 1024 with one or two levels missing. Those 7 were not traced.

So the editor gets the **full** chain for a streamed image, when the XPaks are passed. Without them it gets nothing.

## 3. What a build gets

- The converter reads `images\<name>.dds` from the extraction root first, then from the source paths
  (`src/convert/iw3_iw8/iw3_render_assets.cpp:1286-1299`, in `LoadImage` at `:1272`). Whether the editor's cached DDS is
  copied into the build's `images\` folder is **UNVERIFIED**: the copy step was not read. Doc 18 says a stock image named
  by a world material has "no stock path" (`iw3_render_assets.cpp:1274-1310`), so the copy is the route that matters.
- `DecodeDds` keeps every mip. The DX10 BC7 path takes `metadata.mipLevels` (`:786-788`, `image.mips.push_back(level(index))`)
  and keeps the native BC7 bytes (`:789-790`).
- Re-encode versus pass-through. A source with `nativeBc7` is passed through unchanged only when the texture is not a colour
  map (`colour = (channel == 0 || colourImage) && !linearData`, `:6420`), has no alpha coverage or alpha channel, and has
  no tint (`:6421-6423`; the pass-through is used at `:6441`). Colour and tinted maps are re-encoded from RGBA.
- Missing mips are generated when the source has fewer levels than the target needs (`:3872`,
  `image.mips.push_back(Downsample(...))`). That branch is in the alpha-coverage path (`:3860-3875`). Other paths UNVERIFIED.

**Open problem.** `DecodeDds` accepts a DX10 file only when the format is `DXGI_FORMAT_BC7_UNORM` (`:766-771`, throw
"unsupported DDS DX10 image"). In the editor cache, 179 of 606 DDS are DX10 `BC1_UNORM_SRGB` (DXGI 72), 10 are
`BC4_UNORM` (DXGI 80), and 4 are `BC6H` (DXGI 95). All the probe colour maps are DXGI 72 (the header was read directly).
If the build reads those files unchanged, the converter rejects them. This is UNVERIFIED end to end, because the build copy
path was not traced. The DXGI value follows `scripts/mw19/replay_image.py` (`DXGI[34]` = 72 for `GFX_PF_BC1_SRGB`).

## 4. Catalogue

`data/asset_catalog/iw8_streamed_images.tsv`, built by `tools/build_streamed_images.py` from the structured export of
`mp_backlot2.ff`'s image pool (`-a image` with no `-n`: 2671 assets, 0 failed, 0 unavailable):

- 2671 rows, 2452 streamed (91.8%), 219 not streamed.
- Streamed rule: `flags & 0x40` and `streamedPartCount > 0`. Both agreed on every row, so nothing was skipped.
- Resident size: top mip of the smallest part (fewest mips) with a non-zero key. Most common full to resident pairs:
  1024x1024 to 128x128 (678), 2048x2048 to 256x256 (639), 512x512 to 64x64 (340).

Coverage is one zone only. Other zones (for example `code_post_gfx`, whose 305 images are all non-streamed per doc 16) are
not catalogued.

## 5. How the structured export is produced

Without the env var, ACTS writes the image pool as DDS: `handler_game_mw19_replay.cpp:626` (`HasPayloadExporter` includes
`image`, `mw19_payload.cpp:134-141`). The `.asset.json` with the `GfxImage` fields is written only when
`ACTS_REPLAY_STRUCTURED=1` (`handler_game_mw19_replay.cpp:620-623`, export at `:659`, and the check in `mw19_payload.cpp:111`).
The `.asset.json` files already on disk are only 3 real image JSONs (`out\` and `ww_mp_shipment`, flags 0x131 and 0x133,
none streamed). The catalogue therefore needed a fresh run.

## 6. Notes for the UI and build owners

- `streamedQualityText` in `src/core/streamed_images.cpp` prints the catalogue's resident size. The catalogue calls that
  level "resident" (UNVERIFIED, section 1). The editor's cache gets the full size when XPaks are passed (section 2). The UI
  should decide which number to show.
- Doc 16 says every `code_post_gfx` image is resident in the `.ff`. That is right for that zone and does not hold for
  `mp_backlot2`.

## Open questions

1. Is the smallest part resident in the `.ff`, or loaded at zone load from an XPak? Only a game-side or streamer read settles it.
2. Does the build copy the cached DDS? If it does, BC1 DX10 is rejected at `iw3_render_assets.cpp:766-771`. Check with a build
   of a stock material that uses a streamed colour map.
3. The schema puts `streamedPartCount` at +49, and the replay native read uses +50. `replay_layouts.py` settles it in favour
   of +50. The schema is not corrected here.
4. Seven short-chain cache DDS (`_sdf`, `_alpha`): cause not traced.
