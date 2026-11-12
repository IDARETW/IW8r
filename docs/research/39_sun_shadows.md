# 39. Sun depth: cascade keys, sun flare, per-model shadow flags (roadmap 5.16)

Read-only research. Scope: worldspawn `csm_*` and sun keys, sun flare, and per-model sun shadow flags, checked
against the editor (`E:\IW8-Radiant`), the x64-zt fork (`E:\mw124\x64-zt`) and the stock dump in the cache.

## Sources and how the keys were named

- **Stock dump:** `C:\Users\user\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\stock_cache\mp_shipment\mw19replay\mp_shipment\assets\map_ents\maps\mp\mp_shipment.d3dbsp.0.asset.json`
  (MapEnts `entityString`, 62,892 bytes, hex-encoded). It is the only stock dump in the cache. `mp_backlot2` is named in
  the inventory doc but has no dump here, so every "stock value" below is from `mp_shipment` only.
- **Key names:** the entity string writes keys as numeric canonical ids. Names were resolved with
  `data/generated/iw8_canonical_keys.tsv` (generated from `gsc-tool` `iw8_token.cpp`; the same table the editor's
  `KeyTable` loads, `src/core/map_text.h:95`). `script_strings.json` does NOT resolve these ids (it maps `212` to
  `iw8_lm_mgolf34`, not `classname`), so the TSV is the source of names.
- **Stock worldspawn:** one `worldspawn` block in the dump (the inventory's "2" counts both maps). Its keys, decoded:

| id | key | value |
|---|---|---|
| 2703 | `_primarylightcount` | `1 0 0 0` |
| 2498 | `__smuid` | `_e0` |
| 212 | `classname` | `worldspawn` |
| 13774 | `csm_enable` | `1` |
| 13775 | `csm_max_resolution` | `256` |
| 13776 | `csm_sample_size` | `2` |
| 20376 | `forwardplus` | `1` |
| 25872 | `highresgpulightgrid` | `1` |
| 36888 | `outdoor_only_maxs` | `1984 4736 1064` |
| 36889 | `outdoor_only_mins` | `-3016 8 -104` |
| 35999 | `objectgroupcost` | `0` |
| 692 | `northyaw` | `90` |
| 39993 | `precomputed_sky_illumination_bounce_count` | `1` |
| 39994 | `precomputed_sky_illumination_diffuse_reflectance` | `0.5` |
| 41035 | `rayguidemode` | `1` |
| 47364 | `skycolor` | `1 1 1` |
| 47369 | `skydomelighting` | `1` |
| 47371 | `skylight` | `0` |
| 47372 | `skyrotation` | `0 -15.4 0` |
| 47416 | `smallesthole` | `4` |
| 47417 | `smallestoccluder` | `24` |
| 967 | `suncolor` | `1 0.9764 0.945` |
| 968 | `sundirection` | `-20.261 -15.4 0` |
| 969 | `sunlight` | `27.118` |
| 54237 | `uselmapcompression` | `0` |
| 54239 | `usemightconnectflag` | `1` |

- `_csm_bound_center` (2561) and `_csm_bound_halfsize` (2562) are NOT on the `mp_shipment` worldspawn. They appear only
  as examples in `docs\ENTITY_REFERENCE.md:1396-1397` (from another stock map, not in the cache). Those example values
  are UNVERIFIED for any map here.
- The stock dump has no entity with `caststaticshadow` (10695), `casting_shadows` (10690), `no_static_shadows` (35292)
  or `enableshadows` (354) across its 611 entity blocks. So per-entity shadow keys are not used on stock `mp_shipment`.

## 1. Key table

Converter = the x64-zt fork's IW3-to-IW8 converter (`E:\mw124\x64-zt\src\convert\iw3_iw8\`; short names such as
`convert.cpp` and `iw3_fastfile.cpp` refer to that folder); the editor feeds it through
`extracted/` (world.json, comworld.json, .ents). "Editor passes" = the key is written into the built entity string or
world/comworld files by `src\core\compile_iw8.cpp`.

| key | stock values seen (mp_shipment) | converter reads it? | editor passes it? | effect | evidence |
|---|---|---|---|---|---|
| `sundirection` | `-20.261 -15.4 0` | yes: `SunDirection` -> `EntityValue(entities, "sundirection")` | yes, regenerated from the sun dialog (`sunPitch/Yaw/Roll`, default `-50 45 0` when absent or unparsed) | sun direction in lighting and the sun primary light | fork `iw3_fastfile.cpp:8140-8153` (EntityValue), `:8184-8191` (SunDirection); editor read `compile_iw8.cpp:1134-1142`, write `:1333-1334`; sun direction for the comworld light `:1633`; negative pitch = above horizon (`sky_panorama.h:9-10`) |
| `suncolor` | `1 0.9764 0.945` | yes: `EntityValue(entities, "suncolor")` at `iw3_fastfile.cpp:8207`; then REPLACED by the compiled sun light's colour and luminance when a type-1 primary light exists (`:8220-8231`) | yes when set (`compile_iw8.cpp:1145-1157` read, `:1335-1336` write); the compiled sun light colour is the sun dialog colour (`:1633`) | sun colour; effectively carried by the compiled light | `iw3_fastfile.cpp:8207`, `:8222-8231`; editor `compile_iw8.cpp:1145-1157`, `:1335-1336`, `:1633` |
| `sunlight` | `27.118` | read: `ParseEntityFloat(EntityValue(entities, "sunlight"))` at `iw3_fastfile.cpp:8212-8213` | NO. The editor's worldspawn entity string writes only `classname`, `sundirection`, `suncolor` (`compile_iw8.cpp:1333-1336`). | intended intensity; in practice replaced by the compiled sun light's luminance (`:8226-8229`) | `iw3_fastfile.cpp:8212-8231`; `compile_iw8.cpp:1333-1337` |
| `csm_enable` | `1` | NO. No `csm_` reference in `src\convert\iw3_iw8\` or `src\games\iw8\` except the GSC token `csm_alg` (`src\games\iw8\assets\gsc_tool_iw8_tokens.inc:2745`). | NO. Worldspawn is written from a fixed list (`compile_iw8.cpp:1333-1337`), not passthrough. | none found. The sun shadow bake is not gated by it: `convert.cpp:1601-1647` runs `bakeSunShadow` unconditionally. | `convert.cpp:1601-1647`; `compile_iw8.cpp:1333-1337` |
| `csm_max_resolution` | `256` | NO | NO | none found. Shadow resolution comes from the bake (`GfxCompressedSunShadowParams.resolution`, `games\iw8\structs.hpp:24031`), not this key. | `convert.cpp:1601-1647`; `games\iw8\structs.hpp:24030-24040` |
| `csm_sample_size` | `2` (ENTITY_REFERENCE also lists `1`, another map: UNVERIFIED here) | NO | NO | none found. The converter bakes at a fixed `2.0` units per texel (`convert.cpp:1631`, `:1647`) and a fixed margin ladder. | `convert.cpp:1631-1647` |
| `_csm_bound_center` | not on mp_shipment (ENTITY_REFERENCE example `-605.625 44.5 -110.5`, UNVERIFIED for this map) | NO | NO | none found. The bake box comes from `renderMesh.sceneBounds` and a play-space focus crop (`convert.cpp:1608`, `:1640-1647`). | `convert.cpp:1608`, `:1640-1647` |
| `_csm_bound_halfsize` | not on mp_shipment (ENTITY_REFERENCE example `10668.125 9251.25 1513.25`, UNVERIFIED) | NO | NO | as above | as above |

Other keys seen on the stock worldspawn but outside 5.16 scope (not in the code table): `_primarylightcount`,
`forwardplus` (read only as technique names, `iw3_render_assets.cpp:3582`), `highresgpulightgrid`,
`outdoor_only_*`, `skycolor`, `skylight`, `skyrotation`, `skydomelighting`, `northyaw` (dropped by the IW3 converter per
ENTITY_REFERENCE). None of them is read by the converter or passed by the editor except where listed above.

## 2. What passes through today

- **Passed:** `sundirection` (always), `suncolor` (only when set), plus the editor's own `skyname`/`iw8r_*` keys,
  which have dedicated code paths (`compile_iw8.cpp:1299-1331`, `:1553-1571`, `:1696-1748`).
- **Dropped silently:** every other worldspawn key, including all `csm_*`, `_csm_bound_*`, `sunlight`. The
  editor's loop over `points` skips worldspawn (`compile_iw8.cpp:933`, `:941`), and the worldspawn line is hand-written
  (`:1333-1337`). A second worldspawn only warns that its keys are not written (`:920-925`).
- **Read but overridden:** `sunlight` and `suncolor` are read by the fork but replaced by the compiled sun light
  (`iw3_fastfile.cpp:8220-8231`). For editor maps the lighting comes from the editor's sun light (`compile_iw8.cpp:1628-1635`).
- **Consequence:** setting `csm_enable 0` or `csm_max_resolution` in the editor changes nothing in the built map.
  The shadow resolution and bake are fixed in the fork.

## 3. Sun flare

- **Stock worldspawn:** no flare key. The flare lives in the map's `GfxWorld.sun` (`sunflare_t`, 112 bytes,
  `src\games\iw8\structs.hpp:13154-13172`; `sun` member at `:13388`). Stock values for `mp_shipment` are UNVERIFIED
  because the GfxWorld asset is not in the cache dump.
- **Fork source:** `ReadSunFlare` reads `sun/<map>.sun` (IW3 `r_sun*` dvars, `iw3_fastfile.cpp:10484-10498`,
  wired at `:12986`). `ReadMapSun` reads the same file for the sprite and flare alpha (`iw3_vision.cpp:336-372`).
  Without the file, the fork uses "no sprite or flare, stock blind and glare" (`iw3_fastfile.cpp:10481-10483`).
- **Editor:** nothing writes `sun/<map>.sun` (no `sunflare`, `sun_flare` or `"sun"` writer in `E:\IW8-Radiant\src`).
  So editor-built maps get no sun sprite or flare from the fork. The editor's `iw8r_vision` overrides go to the
  Replay vision overrides (`atmosphere.h:45-47`, `compile_iw8.cpp:1748`), not to the `.sun` values, and no
  override name for the flare is verified.
- **Verdict:** sun flare is not a worldspawn key. It needs an editor-side `sun/<map>.sun` writer (or a new key with
  a fork change in `ReadSunFlare`). UNVERIFIED in game.

## 4. Per-model sun shadow flags

- **Fork:** static model shadow is material-driven. `material.castsShadow` comes from the material's `gameFlags`
  containing `CASTS_SHADOW` (`iw3_render_assets.cpp:2567-2576`). For non-entity static models the fork sets
  `noCastShadow = 0x40000` when it does not cast (`iw3_render_assets.cpp:8266-8270`). The per-surface sun mask is
  derived from `material.castsShadow` (`iw3_fastfile.cpp:6400`, `:6474`) and read by Replay's render path as
  `castsSunShadow = 0x01` (`games\iw8\common\replay_render.cpp:2038-2043`).
- **Editor:** a `misc_model` placement writes `model`, `origin`, `axis`, `scale`, `reflection_probe` (`compile_iw8.cpp:1290`,
  `:1546-1549`). There is no shadow field. The fork reads `model` per instance (`iw3_fastfile.cpp:3743-3760`); no
  per-instance shadow flag was found in the fork.
- **Stock:** no shadow key on any mp_shipment entity (scan above). `caststaticshadow`, `casting_shadows` and
  `no_static_shadows` exist in the TSV but are not documented as entity keys in `data\iw8_entity_defs.json` or
  `docs\ENTITY_REFERENCE.md`. UNVERIFIED as entity keys.
- **Verdict:** a per-placement shadow flag would need a fork change in the static model import
  (`iw3_render_assets.cpp:8266-8270` taking an instance override) and an editor field at `compile_iw8.cpp:1546-1549`.

## 5. Recommendations

1. **Expose (already exposed):** `sundirection` and `suncolor` through the Sun and Sky dialog. Their defaults are the
   stock values (`-20.261 -15.4 0`, `1 0.9764 0.945`). The compiler default when absent stays `-50 45 0`.
2. **Show as read-only with a warning, do not write:** `sunlight`, `csm_enable`, `csm_max_resolution`,
   `csm_sample_size`, `_csm_bound_center`, `_csm_bound_halfsize`. Mappers who copy stock worldspawns should see the
   "not written" warning from `checkSunKeys`. This is what `src\core\sun_keys.cpp` does.
3. **Needs a fork change to matter:**
   - `csm_enable`: gate `bakeSunShadow` in `convert.cpp:1601` and pass the key from `compile_iw8.cpp:1333-1337`.
   - `csm_sample_size`: replace the fixed `2.0` at `convert.cpp:1631` and `:1647` with the worldspawn value.
   - `_csm_bound_*`: replace `renderMesh.sceneBounds` at `convert.cpp:1608` when set.
   - Each needs the key added to the editor's worldspawn passthrough list (`compile_iw8.cpp:1333`). Not done here.
4. **Sun flare:** an editor `sun/<map>.sun` writer (for `ReadSunFlare`, `iw3_fastfile.cpp:10484`), or a new key and a
   fork change. Not recommended until verified in game (UNVERIFIED).
5. **Per-model shadow flags:** defer. Requires the fork change in item 4 of section 4 and an editor field. Stock maps
   do not use per-entity shadow keys, so the material `CASTS_SHADOW` flag is the current mechanism.

## Unverified

- Every "in game" effect: none of this was built or run. The game was not run.
- `mp_backlot2` stock values (no dump in the cache).
- Stock `sunflare_t` values for `mp_shipment` (no GfxWorld dump).
- Upper bounds for `sunlight`, `csm_max_resolution`, `csm_sample_size`, and the `_csm_bound_*` values for any map here.
- `caststaticshadow` / `casting_shadows` / `no_static_shadows` as entity keys.
