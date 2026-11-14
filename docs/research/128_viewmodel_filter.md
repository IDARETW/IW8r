# 128. The toon filter on the first-person weapon and hands (the viewmodel)

**Why.** The owner wants the toon look on the viewmodel. The map filter (research 115) reaches only a map's own
technique sets. This note finds where the viewmodel's shaders live, what a filter there must do differently, and
what a scratch build showed. Nothing was installed.

**Labels.** VERIFIED = measured here (ACTS exports, the converter run, file listings, code read). INFERRED = from
usage or reasoning.

## 1. Where the viewmodel's materials and shaders are

- The viewmodel's models are in streamed zones: `mp_wv_weapon_vm_*_tr` (117 files), `mp_av_attachment_vm_*_tr`
  (1,526), `mp_vh_*_vm_arms_*_tr` (104). They hold only models. Their models name materials by name, for example
  `,mo/weapon_vm_ar_kilo433_mag`, `,m2o/weapon_vm_ar_kilo433_mag`, `,mco/vm_wyatt_arms`,
  `,mo/wyatt_arm_l_skintone_03`. VERIFIED (ACTS export of three of them).
- Those materials, and the technique sets and pixel shaders they use, are in **`techsets_global_stream_mp.ff`**
  (7,995,528 bytes, signed `IWffa100`, no `.fp`). VERIFIED (ACTS: 40,650 assets, 0 failed).
- Counts per shared techset zone (ACTS exports; lit = technique types 34, 35, 36, 151):

| Zone | Sets | Lit techniques | Lit pixel shaders (program in the zone) | Viewmodel sets |
|---|---|---|---|---|
| techsets_global_stream_mp | 1,405 | 1,406 | 736 (735) | 623 |
| techsets_common_base_mp | 153 | 119 | 95 (82) | 24 |
| techsets_common_br_mp | 76 | 66 | 63 (46) | 3 |
| techsets_common_core_mp | 78 | 58 | 55 (41) | 2 |
| techsets_common_mp | 69 | 19 | 15 (10) | 2 |
| techsets_common | 70 | 13 | 12 (12) | 1 |
| techsets_global_mp | 1 | 1 | 1 (0) | 0 |
| techsets_global, _global_core_mp, _common_stream_mp | 0 | 0 | 0 | 0 |

- `techsets_global_stream_mp` holds 13,936 materials: every weapon (first and third person), attachment, operator
  body, head and arms. 6,770 have `_vm_` or `vm_` in the name. Their 623 sets use 622 lit techniques and 396 lit
  pixel shaders. VERIFIED.
- **Telling viewmodel from world.** Only by material name, and not fully. `cameraRegion` is 0 (lit opaque) for both
  (6,581 of 6,770 viewmodel materials). Set prefixes are the same as world models (`mo` 281, `m2o` 280, `mco` 48,
  `m2co` 14). Some arm materials have no marker (`mo/wyatt_arm_l_skintone_03`). 187 of the 623 viewmodel sets are
  also used by other materials (mostly `weapon_wm_*`, `attachment_wm_*`, operator bodies). VERIFIED. The sure way is
  to walk from the models of the `mp_wv_`, `mp_av_` and `mp_vh_` zones to the materials they name. INFERRED.

## 2. Names across zones, and what the filter must do differently

- Inside `techsets_global_stream_mp` a material points at its set by pointer. Other zones point at its sets and
  shaders **by name** (`,name`). VERIFIED counts (from the five other shared zones and `techsets_mp_rust`): 171
  materials name one of its sets, and about 56 lit techniques name one of its pixel shaders. Rust alone: 17 sets and
  11 lit shaders. The Shipment toon zone names 182 `mo/` sets of another zone (research 115).
- The map filter renames three things (converter `convert.cpp`, `lit_filter`):
  1. the technique set, `<prefix>/iw8rf_<24 hex>` (lines 6117-6126);
  2. the pixel shader, `iw8rf_<26 hex>`, changed in place when the zone owns it (lines 6056-6073);
  3. the technique, its 16-hex suffix (lines 6093-6103), and its pipeline-state hashes (lines 6104-6113).
- In a shared zone renames 1 and 2 orphan the references from other zones. VERIFIED on the scratch build (section 3):
  all 171 set references and 57 shader references no longer find their asset. What the game does with a missing
  `,name` asset was not checked.
- A viewmodel filter must therefore:
  - **keep the set names** (skip rename 1). The sets' techniques change in place, so every zone that names the set
    gets the filtered technique with its own scene-depth binding. INFERRED: consistent.
  - **keep the stock shader under its stock name** and add the filtered one as a new asset (`iw8rf_...`), as the
    filter already does for a shader that comes from a shared zone (lines 6061-6066). A technique of another zone that
    names the stock shader then still gets the stock program, which matches its stock hash and its stock thermal-mask
    argument. Changing the shader in place would give those techniques a spliced shader whose depth register still
    holds code image 79.
  - **keep rename 3.** Techniques are inside their set; nothing names them. The new hashes are needed: research 114
    section 7 says pipeline states are found by hash (`main\techsets_global_stream_mp.psob`, 116 MB, holds the stock
    hashes). That the game keys them on the hash, not the name, is INFERRED there; that it builds states for new
    hashes at load is VERIFIED in game for Shipment (research 116 section 7).

## 3. The scratch build (VERIFIED)

- The command works on a shared zone as it is: the zone name is the part after `techsets_`.
  `zonetool -game iw8 -path <game> editzone global_stream_mp edits.json <scratch>` with `"techsets_only": true`,
  `"patch": false` and the `lit_filter` of `edits3.json` (shared list without the zone itself).
- It reads `zone\global_stream_mp.ff` (13 MB, with its stock `.fp`) because `edit_zone` always loads `<zone>.ff`
  first; with `techsets_only` that zone is not written. It reads and writes `techsets_global_stream_mp.ff`. The six
  shared zones were read; none had to supply a program.
- Report: technique sets 1,400; techniques 1,404; shaders 735; shaders with screen 734; techniques with screen
  1,398, unbound 0; shaders refused 0; screen refused 1 (`90c840d181a825826f3fd32fdbd7f1cc`: its screen position
  input lacks a part the filter reads); 1 shader without a program (`6aa6c9de...`, as on Shipment); materials renamed 0.
- Output 14,411,684 bytes, `IWffc100`, version 11, 0xFF7, flags 0, both decoded sizes 107,535,645. It passes the
  client's unsigned rule as `unsignedReplayZoneHeader` reads it. ACTS: 40,650 assets loaded, 16,167 tested, 0 failed.
- Read back: 5 of 1,405 set names kept; 735 of 826 pixel shaders renamed. So this build is **not installable**:
  it is the orphaning case of section 2.

## 4. Risks of installing a shared techset zone

- Every map loads it (it is in every recipe's shared list; INFERRED that the game loads it with every MP map,
  front end included). While installed, every map gets the look on every weapon and character it draws from that
  zone, first and third person, unless the filter is limited to the viewmodel sets (section 1).
- The game folder has no stock `.fp` for any shared techset zone (only the client's own `dw_` and `mw120r` ones).
  VERIFIED (listing). No size check is known; the signature check does not apply to an unsigned zone (research 116).
- Load cost: about 1,400 new pipeline states per map load, against 369 on Shipment. INFERRED.
- The memory the whole zone declares always loaded: 107 MB decoded. INFERRED harmless.
- The install code (`core/stock_install.cpp`) takes any zone name: it checks the unsigned header, moves the stock
  file to `iw8r_stock`, copies ours, writes a marker. VERIFIED (code read). But
  `installStockZonesQuiet` names the backup folder after the first file with `techsets_` cut off, so it would be
  `global_stream_mp`; `--restore-stock global_stream_mp` would undo it. File > Restore Stock Map restores the open
  map only, so it cannot reach this zone from the menu; and the install sets `lastStockRebuildZone_` to
  `global_stream_mp`. A viewmodel install needs its own menu entries (install / restore "viewmodel look"). VERIFIED
  (code read).
- The game must be closed for the install (it holds the file). The installer fails cleanly in that case.

## 5. The viewmodel in the scene depth

- The scene depth stores a viewmodel pixel as minus its depth / 6 (research 104). The screen filter's host reads
  `abs(...) * 6`, so a viewmodel pixel reads as its positive view depth. VERIFIED (`custom_shader.cpp`).
- What the ink test does with that (INFERRED, from `toonInkScene`): on the weapon, its own depth (`position.w`)
  should match the read depth, so `usable` is true and lines are drawn as on the world. Around its silhouette the
  world behind reads much deeper: an outline. On a world pixel next to the weapon, the weapon reads as something far
  in front: no line on the world side. If the viewmodel's `position.w` differs from its stored depth by more than
  5% (a different projection), `usable` is false: the colour filter only, no lines. The line width clamps to
  `kLinePixels` at viewmodel distances. The pixel size uses `2 / width`, not the viewmodel's field of view.
- Inputs (VERIFIED, disassembly): all 622 viewmodel lit techniques have the thermal-mask slot (code image 79); 395
  of their 396 lit pixel shaders have the `SV_Position` input. Whole zone: 1,406 of 1,406 and 735 of 736.

## 6. Recommended steps

1. Converter: `lit_filter` option `"keep_names": true`. With it, skip the set rename, never change an owned shader in
   place (copy it, name the copy `iw8rf_...`, add it like a shared-zone shader), keep the technique rename and the
   new hashes. Optional `"materials": "<regex>"`: filter only sets used by matching materials, and clone a set that
   other materials also use (new name; the matching materials point at the clone by pointer).
2. Build into scratch, check with ACTS, and check that every `,name` set and shader of the other shared zones and
   of a map techset zone still resolves.
3. Editor: "Install / Restore Viewmodel Look" using the same core functions with the zone `global_stream_mp`.
4. Ask the owner before installing: it changes every map while installed.
