# 69. Technique set export (ACTS JSON, Replay 1.20)

**Why.** Research 66 read technique sets only from their *names* and material flags. The roadmap 12.1 inspector needs the
body: the techniques, their shaders, arguments and state. This note records what the ACTS export of one real technique
set contains, with one real example per field, and what is still undecoded.

## How it was exported

Material `mo/me_traffic_cone_01_top` (zone `techsets_mp_emporium.ff`):

```
"/e/mw124/atian-cod-tools/build/bin/Release/acts.exe" --noUpdater fastfile -r mw19replay -g "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe" --oodle "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll" -a material -n "mo/me_traffic_cone_01_top" -l -o "E:\IW8-Radiant\out\agent_techset" "E:/IW8/Builds/1.20-replay/zone/techsets_mp_emporium.ff"
```

Its `fields.techniqueSet.name` is `,mo/lit_3_lit_rpl_ta1_802000_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0`. The
leading comma is ACTS's marker for a reference; the asset name has no comma.

The technique set is **not in the material's zone**. `-a techset -n` against `techsets_mp_emporium.ff` exports nothing
("0 tested"). It is in `techsets_common_mp.ff`, which exports it by name (comma removed):

```
"/e/mw124/atian-cod-tools/build/bin/Release/acts.exe" --noUpdater fastfile -r mw19replay -g "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe" --oodle "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll" -a techset -n "mo/lit_3_lit_rpl_ta1_802000_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0" -l -o "E:\IW8-Radiant\out\agent_techset" "E:/IW8/Builds/1.20-replay/zone/techsets_common_mp.ff"
```

Output: `out/agent_techset/mw19replay/techsets_common_mp/assets/techset/mo/lit_3_lit_rpl_ta1_802000_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0.0.asset.json`
(34,826 bytes, copied to `tests/data/techset_sample.asset.json`). The ACTS pool name is `techset` (pool id 18, root type
`MaterialTechniqueSet`). Shader bodies are not in this file (see below).

Where a name is not in the zone, ACTS reports `0 tested` and writes nothing. To find the zone, the whole emporium zone
was exported once (out/agent_techset/all, 774 techset entries in its `assets.jsonl`). Techset names in the journal have
no comma and carry the asset's own prefix (`mo/`, `mco/`, `tm2o/`, ...).

## Top level

| Key | Example | Meaning |
|---|---|---|
| `format` | `"mw19-asset-json"` | fixed |
| `format_version` | `1` | |
| `profile` | `"replay-1.20"` | |
| `pool` / `pool_id` | `"techset"` / `18` | |
| `root_type` | `"MaterialTechniqueSet"` | |
| `asset.type` | `"MaterialTechniqueSet"` | |
| `asset.fields` | (below) | the body |
| `asset.read_errors`, `unresolved_pointers`, `issues` | `0`, ... | ACTS read health; 0 for this sample |

## Fields of the technique set (`asset.fields`)

| Field | Example | Decoded? |
|---|---|---|
| `name` | `{"string": "mo/lit_3_lit_rpl_ta1_802000_..."}` | yes, the name (grammar in research 66) |
| `flags` | `67112992` | **not decoded**. Differs per family: `bad` set 67108864 (0x4000000), `rpl` 67112992 (+0x1020), `alt` 67109024 (+0xA0). Guess: per-set state bits |
| `materialType` | `{"value": 524304, "name": "MTL_TYPE_MODEL_SELFVIS"}` | yes (ACTS enum name) |
| `techniqueMask` | `{"mask": [17601696329, 0, 0, 0]}` | yes, see below |
| `maskedTechniques` | `{"count": 12, "stride": 8, "values": [...]}` | yes, one slot per set bit |

**techniqueMask to slot.** Bit `i` set means technique type `i` exists. Slot `k` of `maskedTechniques.values` is the
`k`-th set bit in ascending order, and the technique's `type.value` equals that bit index. Verified on this sample:
mask bits `[0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 28, 34]` and slot types `[0, 3, 6, 9, ..., 34]`. The `bad` set has a
single bit 34 (`TECHNIQUE_LIT_FORWARDPLUS_BITMASK`) and one slot.

**Technique types seen** (the enum value is the bit index): 0 `TECHNIQUE_FIRST`, 3 `..._DEPTH_PREPASS_VELRGD`,
6 `..._DEPTH_PREPASS_VELSKN`, 9 `..._DEPTH_PREPASS_STENCIL`, 12 `..._STENCIL_VELRGD`, 15 `..._STENCIL_VELSKN`,
18 `..._DEPTH_PREPASS_OBJECTID`, 21 `..._OBJECTID_VELRGD`, 24 `..._OBJECTID_VELSKN`, 27 `TECHNIQUE_BUILD_SHADOWMAP_DEPTH`,
28 `TECHNIQUE_BUILD_SHADOWMAP_DEPTH_BACK_FACE`, 34 `TECHNIQUE_LIT_FORWARDPLUS_BITMASK`. Names end in a hash suffix
(`_ed1fcfcfdcc7d56c`), which is not decoded. A `rpl` lit set has 12 techniques; a `bad` lit set has 1 (forward only).

## One technique (`maskedTechniques.values[k].values[0]`)

Each slot holds one `MaterialTechnique` record. Real example, the forward lit technique (type 34) of the `rpl` set:

| Key | Example | Notes |
|---|---|---|
| `name.string` | `"TECHNIQUE_LIT_FORWARDPLUS_BITMASK_ed1fcfcfdcc7d56c"` | type name plus hash |
| `type` | `{"value": 34, "name": "TECHNIQUE_LIT_FORWARDPLUS_BITMASK"}` | |
| `flags` | `0` | not decoded |
| `pipelineStateInputLayoutCount` | `1` | |
| `pipelineStateMap` | 25 bytes, `[255, 255, 255, 255, 255, 0, 0, 255, ...]` | guess: per-slot map into the input layouts; 255 = unused. Not decoded |
| `pipelineStates.values[]` | `[{"hash": 254860731661955731, "pso": null}]` | PSO hashes; `pso` is always null in the export |
| `customBufferFlags` | `[0, 0, 0, 0]` | not decoded |
| `rootParameterMap.bytes` | `"00ffffffffffffffffffffffffffffff"` | 16 bytes, probably root slot map; guess |
| `vertexShader`, `hullShader`, `domainShader`, `pixelShader` | `{"name": ",83cafbaec48e1ef140ed7c1895a5fed8", "asset_reference": true}` or `null` | **shader names are hashes**, not file names; `,null.hlsl` on the stencil and shadow techniques (no pixel shader) |
| `perPrimArgCount`, `perObjArgCount`, `stableArgCount`, `neverArgCount` | `0`, `0`, `13`, `4` | argument group sizes |
| `perPrimArgSize`, `perObjArgSize`, `stableArgSize` | `0`, `0`, `128` | bytes, guess |
| `colorRtCount`, `pipelineStateCount` | `2`, `1` | |
| `stateBits.values[]` | `[{"bits": [22, 32, 1, 0, 0]}]` | 5 bytes per PSO. Meaning not decoded |
| `renderTargetFormats` | `{"dsFormat": 4, "colorRtFormats": [0, 0, 0, 0]}` | enum values, names not exported |
| `customSamplerFlags` | `0` | |
| `precompiledIndex` | `{"value": 15, "name": "MTL_CONSTANT_LAYOUT_XMODEL_LIT_PROBE_OBJ"}` | constant layout name, readable |
| `stageConfig` | `{"value": 0, "name": "MTL_STANDARD_STAGES"}` | |
| `defaultStateBits` | `{"otherBits": {"value": 2049}, "blendBits": {"value": 0, "name": "GFXS_BLEND_STATE_BITS_EMPTY"}}` | the readable state word; numeric meaning not decoded |
| `args.values[]` | 17 entries, see below | |

**Blend and other bits by family (guess from the three sets compared):** the forward technique of an opaque `rpl` set
has `blendBits` 0 (`GFXS_BLEND_STATE_BITS_EMPTY`) and `otherBits` 2049. The alpha-test `alt` set has the same. The
alpha-blend `bad` set has `blendBits` 163920 and `otherBits` 3073. These match the blend class in research 66 but the bit
meanings are not decoded.

## Shader arguments (`args.values[]`)

Each entry: `{"type": {"value", "name"}, "shader": N, "dest": N, "index": N}`. Types seen in this sample:

| type name | Example | What it binds (verified or guess) |
|---|---|---|
| `MTL_ARG_CODE_CONST` | `{"shader": 2, "dest": 0, "index": 164}` | a constant from the engine (guess: the code constant id) |
| `MTL_ARG_CODE_TEXTURE` | `{"shader": 16, "dest": 3, "index": 79}` | engine texture id (guess) |
| `MTL_ARG_CODE_BUFFER` | `{"shader": 2, "dest": 0, "index": 27}` | engine buffer id (guess) |
| `MTL_ARG_CODE_CBUFFER` | `{"shader": 18, "dest": 7, "index": 0}` | constant buffer slot (guess) |
| `MTL_ARG_MATERIAL_TEXTURE` | `{"shader": 16, "dest": 1, "index": 9}` | **verified**: `index` is the material `textureTable` slot. The material has slots 0, 9, 59 and this technique reads 0, 9, 59 |
| `MTL_ARG_MATERIAL_CONST` | `{"shader": 16, "dest": 2, "index": 9}` | **probable**: `index` is the material `constantTable` slot. The material has constant slots 0, 9, 54, 72 and this technique reads 0, 9, 54, 72 |

`shader` (2, 16, 18) and `dest` are not decoded. The guess is that `shader` is a stage id and `dest` a register or
slot inside that stage. `argumentCount` in the parser is simply the length of `args.values`.

## What the material export adds

`out/agent_techset/.../material/mo/me_traffic_cone_01_top.0.asset.json` gives the material side: `surfaceFlags` (here
13107200), `materialType`, `sortKey`, `textureTable.values[].index` plus `image.name`, `constantTable.values[].index`.
The technique set side only names these by index, so the two files must be read together.

## What could not be decoded

- Shader names: only the hash strings in the export. The compiled `.cso` bodies are in the zone's shader pool, keyed by
  hash, and map to no file name here.
- `flags`, `customBufferFlags`, `pipelineStateMap`, `rootParameterMap`, `stateBits` (5 bytes), `defaultStateBits`
  numeric values, `renderTargetFormats` (as numbers only), `stageConfig` beyond the enum name.
- Meaning of argument `shader` and `dest` numbers.
- PSO objects: `pso` is always null, so the actual pipeline state (depth, cull, polygon offset) is not in the export.
- Techset names with the hash suffix (`_ed1fcfcfdcc7d56c`): the suffix is the same on every technique of one set.
- The `_` tokens after the state hex in the name (`1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0`): counts of
  arguments or vertex layouts are a guess and are not used.

## Guesses (not verified)

- Technique bit index equals technique type (verified), but what each type does in the renderer (depth prepass, shadow
  map, forward) is inferred from its name only.
- `otherBits` and `blendBits` correspond to the research 66 blend classes (verified only for the alpha-blend vs opaque
  blend values, not the individual bits).
- `flags` on the technique set holds per-set bits (differs between blend classes, meaning unknown).

## Editor use (core `techset_detail.*`)

`parseTechsetExport` reads the techniques, shader names, argument counts and a state summary built from the raw values
shown above. The state summary shows only decoded or raw values, so no meaning is claimed. `describeTechsetName` reuses
`parseTechset` from research 66.
